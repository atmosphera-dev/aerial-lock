#!/usr/bin/env python3
"""VM-platform contracts. Execute only inside a disposable project VM."""
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import tarfile
import time
import unittest
import subprocess
import sys
from unittest.mock import Mock, patch
from types import SimpleNamespace

spec = importlib.util.spec_from_file_location('aerial_vm_platform', Path(__file__).with_name('vm_platform.py'))
platform = importlib.util.module_from_spec(spec)
spec.loader.exec_module(platform)
guest_spec = importlib.util.spec_from_file_location('aerial_vm_guest', Path(__file__).with_name('guest.py'))
guest = importlib.util.module_from_spec(guest_spec)
guest_spec.loader.exec_module(guest)


class Channel:
    def __init__(self, *chunks):
        self.chunks = iter(chunks)

    def settimeout(self, value):
        self.timeout = value

    def recv(self, size):
        return next(self.chunks, b'')


class Synchronization(unittest.TestCase):
    def test_stale_errors_and_wrong_nonces_are_ignored_until_our_nonce(self):
        channel = Channel(b'junk\n{"error":{"desc":"stray sentinel"}}\n',
                          b'{"return":5}\n', b'\xff', b'{"ret', b'urn":42}\n')
        self.assertEqual(platform.response(channel, time.monotonic() + 2, nonce=42), 42)

    def test_sentinel_discards_partial_stale_json(self):
        channel = Channel(b'{"unclosed":', b'\xff{"return":42}\n')
        self.assertEqual(platform.response(channel, time.monotonic() + 2, nonce=42), 42)

    def test_closed_channel_before_matching_nonce_fails(self):
        with self.assertRaises(ConnectionError):
            platform.response(Channel(b'{"return":5}\n'), time.monotonic() + 2, nonce=42)

    def test_synchronized_command_error_is_not_ignored(self):
        with self.assertRaisesRegex(RuntimeError, 'command failed'):
            platform.response(Channel(b'{"error":{"desc":"denied"}}\n'), time.monotonic() + 2)

    def test_synchronized_malformed_response_fails(self):
        with self.assertRaisesRegex(RuntimeError, 'malformed synchronized'):
            platform.response(Channel(b'not-json\n'), time.monotonic() + 2)

    def test_absolute_deadline_is_not_reset_by_stale_responses(self):
        with self.assertRaises(TimeoutError):
            platform.response(Channel(), time.monotonic() - 1, nonce=42)


class SerialReadiness(unittest.TestCase):
    def identity(self):
        return dict(uuid='expected-uuid', macs=['52:54:00:00:00:01'], key='ssh-ed25519 public-key',
                    boot_id='fresh-boot', machine_id='fresh-machine', user='ci-user')

    def test_marker_from_exact_instance_not_a_network_keyscan(self):
        line = platform.BOOT_MARKER + json.dumps(self.identity()) + '\n'
        value = platform.serial_identity('boot noise\n' + line, 'expected-uuid', '52:54:00:00:00:01')
        self.assertEqual(value['user'], 'ci-user')

    def test_partial_marker_is_not_a_ready_receipt(self):
        line = platform.BOOT_MARKER + json.dumps(self.identity())
        self.assertIsNone(platform.serial_identity(line, 'expected-uuid', '52:54:00:00:00:01'))

    def test_wrong_uuid_or_mac_is_refused(self):
        line = platform.BOOT_MARKER + json.dumps(self.identity()) + '\n'
        for uuid, mac in [('foreign', '52:54:00:00:00:01'), ('expected-uuid', '52:54:00:00:00:02')]:
            with self.assertRaisesRegex(RuntimeError, 'mismatch'):
                platform.serial_identity(line, uuid, mac)

    def test_missing_host_key_is_refused(self):
        value = self.identity()
        value['key'] = ''
        with self.assertRaisesRegex(RuntimeError, 'incomplete'):
            platform.serial_identity(platform.BOOT_MARKER + json.dumps(value) + '\n', 'expected-uuid', '52:54:00:00:00:01')


class Configuration(unittest.TestCase):
    def test_relative_host_file_paths_use_the_configuration_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            value = dict(instance='portable', host='builder', user='alice', storage='state',
                         guest_root='/home/alice/work', ssh_key='keys/client', firmware='bios.fd', qemu='./bin/qemu')
            path = root / 'config.json'
            path.write_text(json.dumps(value))
            observed = platform.config(path)
            for key, relative in [('ssh_key', 'keys/client'), ('firmware', 'bios.fd'), ('qemu', 'bin/qemu')]:
                self.assertEqual(observed[key], str(root / relative))

    def test_distinct_non_fleet_accounts_paths_and_ports(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for user, port in [('alice', 2201), ('ci-worker', 45123)]:
                value = dict(instance='ci-' + user, host='github-runner', user=user,
                    storage=str(root / ('storage ' + user)), guest_root='/home/' + user + '/test root', ssh_port=port)
                path = root / (user + '.json')
                path.write_text(json.dumps(value))
                observed = platform.config(path)
                self.assertEqual(observed['ssh_port'], port)
                self.assertEqual(observed['user'], user)
                self.assertEqual(observed['storage'], value['storage'])

    def test_ambient_path_symlinks_and_traversal_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'actual').mkdir()
            (root / 'alias').symlink_to(root / 'actual', target_is_directory=True)
            path = root / 'config.json'
            value = dict(instance='ci-one', host='runner', user='alice',
                         storage=str(root / 'alias'), guest_root='/home/alice/test')
            path.write_text(json.dumps(value))
            with self.assertRaisesRegex(ValueError, 'symlink'):
                platform.config(path)
            value.update(storage=str(root / 'safe'), guest_root='/home/alice/../escape')
            path.write_text(json.dumps(value))
            with self.assertRaisesRegex(ValueError, 'traversal'):
                platform.config(path)


class SourceProvenance(unittest.TestCase):
    def test_source_build_scripts_are_kept_but_artifacts_and_credentials_are_not(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / '.gitignore').write_text('generated.bin\n')
            (root / 'build-wtype.sh').write_text('source')
            (root / 'generated.bin').write_text('artifact')
            (root / '.envrc').write_text('never copy')
            (root / 'build').mkdir()
            (root / 'build/object.o').write_text('artifact')
            observed = platform.source_files(root)
            self.assertEqual(set(observed), {'.gitignore', 'build-wtype.sh'})

    def test_source_symlinks_cannot_copy_an_external_secret(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            source.mkdir()
            (source / '.gitignore').touch()
            (root / 'secret').write_text('secret')
            (source / 'external').symlink_to(root / 'secret')
            with self.assertRaisesRegex(ValueError, 'symlink escape'):
                platform.source_files(source)


class BeforeBoot(unittest.TestCase):
    def test_launcher_directory_does_not_shadow_the_standard_platform_module(self):
        subprocess.run([sys.executable, '-c', 'import platform; assert platform.system()'],
                       cwd=Path(__file__).parent, check=True, timeout=10, capture_output=True)

    def test_missing_kvm_fails_before_any_emulator_or_tcg_command(self):
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(platform.os, 'access', return_value=False), \
                patch.object(platform, 'command') as command:
            with self.assertRaisesRegex(RuntimeError, 'TCG fallback'):
                platform.VM({}, 'unused-image', Path(directory) / 'fresh')
            command.assert_not_called()

    def test_corrupt_image_is_refused_before_vm_construction(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / 'bad.qcow2'
            image.write_bytes(b'corrupt input')
            args = SimpleNamespace(image=str(image), sha256='0' * 64)
            with patch.object(platform, 'VM') as vm, self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                platform.run_group(args, {})
            vm.assert_not_called()

    def test_changed_recipe_pin_is_refused_before_provisioning(self):
        with tempfile.TemporaryDirectory() as directory:
            value = json.loads((Path(__file__).parent / 'image.lock.json').read_text())
            value['aur']['qt6-mpvqml']['recipe_sha256'] = '0' * 64
            path = Path(directory) / 'lock.json'
            path.write_text(json.dumps(value))
            with self.assertRaisesRegex(ValueError, 'recipe hash mismatch'):
                platform.validate_lock(path)


class DiskObservation(unittest.TestCase):
    def test_reports_sampled_growth_without_inventing_a_quota(self):
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(platform.shutil, 'disk_usage', side_effect=[SimpleNamespace(free=n) for n in (1000, 600, 800)]):
            meter = platform.DiskMeter(directory)
            meter.sample()
            meter.sample()
            self.assertEqual(meter.result()['sampled_peak_growth_bytes'], 400)
            self.assertEqual(meter.result()['minimum_free_bytes'], 600)


class FailureCustody(unittest.TestCase):
    def test_secondary_collection_and_stop_errors_do_not_replace_primary(self):
        with tempfile.TemporaryDirectory() as directory:
            vm = Mock()
            vm.process.poll.return_value = None
            vm.execute.side_effect = TimeoutError('secondary collection timeout')
            vm.stop.side_effect = RuntimeError('secondary stop error')
            primary = ValueError('primary failure')
            result = platform.preserve_failure(vm, Path(directory), primary)
            retained = json.loads((Path(directory) / 'failure.json').read_text())
            self.assertEqual(retained, result)
            self.assertEqual(retained['reason'], 'primary failure')
            self.assertEqual(len(retained['cleanup_errors']), 2)

    def test_receipt_write_error_does_not_raise_over_primary(self):
        vm = Mock()
        vm.process.poll.return_value = 0
        with patch.object(platform, 'write_json', side_effect=OSError('evidence full')), \
                patch('sys.stderr'):
            result = platform.preserve_failure(vm, Path('unused'), ValueError('primary'))
        self.assertEqual(result['reason'], 'primary')

    def test_successful_shutdown_observes_qemu_exit_not_guest_exec_status(self):
        vm = platform.VM.__new__(platform.VM)
        vm.process = Mock()
        vm.process.poll.return_value = None
        vm.process.wait.return_value = 0
        vm.process.returncode = 0
        vm.receipt, vm.log, vm.qmp = {}, Mock(), Path('owned-qmp.sock')
        vm.save = Mock()
        vm.execute = Mock(side_effect=AssertionError('shutdown must not use guest-exec'))
        with patch.object(platform, 'rpc') as rpc:
            vm.stop()
        rpc.assert_called_once_with(vm.qmp, 'system_powerdown', qmp=True)
        self.assertEqual(vm.receipt['qemu_exit'], 0)
        vm.execute.assert_not_called()


class ConsumerResults(unittest.TestCase):
    def request(self):
        return dict(uuid='owned', image_sha256='image', source_freeze_identity='source')

    def summary(self):
        return dict(instance_uuid='owned', image_sha256='image', source_identity='source',
                    status='PASS', group='niri-smoke', planned=1, exercised=1,
                    no_surviving_locker=True,
                    cycle=dict(exit=0, unlock_and_destroy=True, desktop_input=True))

    def test_requires_the_end_of_the_unlock_and_desktop_input_chain(self):
        platform.validate_summary(self.summary(), self.request())
        for field in ('unlock_and_destroy', 'desktop_input'):
            value = self.summary()
            value['cycle'][field] = False
            with self.assertRaises(RuntimeError):
                platform.validate_summary(value, self.request())

    def test_missing_blocked_and_other_source_results_are_never_passes(self):
        for change in ({'status': 'BLOCKED'}, {'exercised': 0}, {'source_identity': 'foreign'}, {'cycle': {}}, {'no_surviving_locker': False}):
            with self.assertRaises(RuntimeError):
                platform.validate_summary(self.summary() | change, self.request())
        with self.assertRaises(RuntimeError):
            platform.validate_summary({}, self.request())


class EvidenceExport(unittest.TestCase):
    def test_exports_only_allowed_results_not_seed_keys_or_media(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'results').mkdir()
            (root / 'run').mkdir()
            (root / 'results/group.log').write_text('filtered outcome')
            (root / 'results/frame.png').write_bytes(b'image')
            (root / 'run/client-key').write_text('private, never exported')
            output = io.BytesIO()
            with patch.object(guest.sys, 'stdout', SimpleNamespace(buffer=output)):
                guest.export_results(root)
            with tarfile.open(fileobj=io.BytesIO(output.getvalue())) as archive:
                self.assertEqual(set(archive.getnames()), {'group.log', 'evidence-sha256.json'})

    def test_planted_fixture_secret_blocks_the_whole_export(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'results').mkdir()
            (root / 'run').mkdir()
            (root / 'run/pam-fixture-secret').write_text('generated-secret\n')
            (root / 'results/group.log').write_text('accidentally generated-secret')
            output = io.BytesIO()
            with patch.object(guest.sys, 'stdout', SimpleNamespace(buffer=output)), \
                    self.assertRaisesRegex(RuntimeError, 'credential'):
                guest.export_results(root)
            self.assertEqual(output.getvalue(), b'')

    def test_previously_detected_leak_cannot_be_exported_after_secret_removal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'results').mkdir()
            (root / 'results/summary.json').write_text('{"export_forbidden":true}')
            with self.assertRaisesRegex(RuntimeError, 'credential'):
                guest.export_results(root)


if __name__ == '__main__':
    unittest.main(verbosity=2)
