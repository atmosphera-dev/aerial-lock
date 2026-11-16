#!/usr/bin/env python3
"""VM-platform contracts. Execute only inside a disposable project VM."""
import importlib.util
import io
import json
import os
from pathlib import Path
import re
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


class ProvisioningObservation(unittest.TestCase):
    def test_exit_status_is_strictly_parsed_on_the_host(self):
        for text, expected in [('running\n', None), ('0\n', 0), ('1\n', 1), ('255\n', 255)]:
            self.assertEqual(platform.parse_provision_status(text), expected)
        for text in ('', '0', '00\n', '-1\n', '256\n', ' 0\n', '0\n\n',
                     '0\r\n', 'running', 'running\n0\n', 'unexpected\n'):
            with self.subTest(text=text), self.assertRaisesRegex(RuntimeError, 'invalid provisioning status'):
                platform.parse_provision_status(text)

    def test_observers_use_only_sh_and_coreutils_without_python_or_sudo(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            observed = root / 'observed file'
            vm = platform.VM.__new__(platform.VM)
            vm.execute = Mock(side_effect=AssertionError('no sudo/env wrapper during provisioning'))
            calls = []
            def ssh(argv, **kwargs):
                calls.append(argv)
                self.assertEqual(argv[:2], ['/bin/sh', '-c'])
                # Real shell probe, but with no Python/sudo/env in PATH.
                return subprocess.run([*argv[:-1], str(observed)], check=True,
                    env=dict(os.environ, PATH=str(root / 'no-tools')), **kwargs)
            vm.ssh = ssh
            self.assertIsNone(vm.provision_status())
            observed.write_text('0\n')
            self.assertEqual(vm.provision_status(), 0)
            observed.write_text('27\n')
            self.assertEqual(vm.provision_status(), 27)
            observed.write_text('progress while packages are replaced\n')
            self.assertEqual(vm.provision_log(), observed.read_text())
            for argv in calls:
                self.assertNotRegex(argv[2], r'python|sudo|\benv\b')
            vm.execute.assert_not_called()

    def test_builder_waits_for_status_before_using_upgraded_guest_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = root / 'state'
            state.mkdir()
            vm = Mock()
            vm.directory = state
            vm.process.poll.return_value = 0
            vm.provision_status.side_effect = [None, 0]
            boundary = RuntimeError('stop at completed provisioning boundary')
            def execute(argv, **kwargs):
                self.assertEqual(vm.provision_status.call_count, 2)
                self.assertEqual(argv[:2], ['/usr/bin/cloud-init', 'status'])
                raise boundary
            vm.execute.side_effect = execute
            args = SimpleNamespace(lock=str(Path(__file__).parent / 'image.lock.json'),
                                   resolve_lock=False, output_dir=str(root / 'output'))
            with patch.object(platform, 'VM', return_value=vm), \
                    patch.object(platform, 'download'), patch.object(platform.time, 'sleep'), \
                    self.assertRaises(RuntimeError) as error:
                platform.build_image(args, dict(storage=str(state), build_seconds=10))
            self.assertIs(error.exception, boundary)
            vm.stop.assert_called_once_with(force=True)

    def test_exit_marker_is_published_by_rename_after_complete_write(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'bin').mkdir()
            rename = root / 'bin/mv'
            rename.write_text('#!/bin/sh\n'
                'test -s "$2" && test ! -e "$3" || exit 90\n'
                'printf renamed > "$TRACE_FILE"\n'
                'exec /usr/bin/mv "$@"\n')
            rename.chmod(0o755)
            script = (Path(__file__).parent / 'provision.sh').read_text().split('cd "$root/provision"', 1)[0]
            script = script.replace('root=/var/lib/aerial-ci', 'root=$FIXTURE_ROOT')
            result = subprocess.run(['/bin/bash', '-c', script + '\nexit 23\n'],
                env=dict(os.environ, PATH=str(root / 'bin') + ':/usr/bin',
                         TRACE_FILE=str(root / 'trace'), FIXTURE_ROOT=str(root)))
            self.assertEqual(result.returncode, 23)
            self.assertEqual((root / 'provision.exit').read_text(), '23\n')
            self.assertEqual((root / 'trace').read_text(), 'renamed')


class FailureCustody(unittest.TestCase):
    def test_remote_failure_keeps_stderr_and_local_boot_diagnostics(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state, output = root / 'state', root / 'output'
            state.mkdir()
            output.mkdir()
            for name in ('serial.log', 'qemu.log', 'ssh-errors.log', 'instance.json'):
                (state / name).write_text('diagnostic ' + name)
            vm = SimpleNamespace(directory=state, process=Mock(), stop=Mock())
            vm.process.poll.return_value = 0
            primary = subprocess.CalledProcessError(1, ['ssh'], stderr='guest runtime failed')
            result = platform.preserve_failure(vm, output, primary)
            self.assertEqual(result['returncode'], 1)
            self.assertEqual(result['stderr'], 'guest runtime failed')
            for name in ('serial.log', 'qemu.log', 'ssh-errors.log', 'instance.json'):
                self.assertEqual((output / name).read_bytes(), (state / name).read_bytes())

    def test_ssh_failure_prints_and_retains_captured_stderr_without_masking_error(self):
        with tempfile.TemporaryDirectory() as directory:
            vm = platform.VM.__new__(platform.VM)
            vm.directory = Path(directory)
            vm.ssh_args = Mock(return_value=['ssh'])
            primary = subprocess.CalledProcessError(1, ['ssh'], stderr='guest runtime failed')
            with patch.object(platform, 'command', side_effect=primary), \
                    patch.object(platform.sys, 'stderr', new_callable=io.StringIO) as stderr:
                with self.assertRaises(subprocess.CalledProcessError) as error:
                    vm.ssh(['probe'], capture_output=True, text=True)
            self.assertIs(error.exception, primary)
            self.assertIn('guest runtime failed', stderr.getvalue())
            self.assertIn('guest runtime failed', (vm.directory / 'ssh-errors.log').read_text())

    def test_secondary_collection_and_stop_errors_do_not_replace_primary(self):
        with tempfile.TemporaryDirectory() as directory:
            vm = Mock()
            vm.process.poll.return_value = None
            vm.directory = Path(directory)
            vm.provision_log.side_effect = TimeoutError('secondary collection timeout')
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


class ReviewRegressions(unittest.TestCase):
    def test_each_kvm_step_acquires_runner_device_access(self):
        workflow = (Path(__file__).parents[2] / '.github/workflows/vm-smoke.yml').read_text()
        for name in ('Rebuild the locked Arch image', 'One nested niri unlock cycle'):
            step = workflow.split('- name: ' + name, 1)[1].split('- name:', 1)[0]
            grant = 'sudo setfacl -m "u:$(id -un):rw" /dev/kvm'
            self.assertIn(grant, step)
            self.assertLess(step.index(grant), step.index('python3 Tests/vm/vm_platform.py'))
            self.assertIn('getfacl /dev/kvm', step)

    def test_workflow_deadlines_cover_workload_and_cleanup(self):
        workflow = (Path(__file__).parents[2] / '.github/workflows/vm-smoke.yml').read_text()
        steps = dict(re.findall(r'- name: ([^\n]+)\n(?:[ \t]+#[^\n]*\n)*\s+timeout-minutes: (\d+)', workflow))
        deadlines = {key: int(re.search(key + r'=(\d+)', workflow)[1])
                     for key in ('boot_seconds', 'build_seconds', 'group_seconds', 'export_seconds')}
        # Include cloud-init/account/transfer/summary SSH calls and shutdown,
        # not just the guest workload. Leave additional setup margin in the step.
        chain = sum(deadlines.values()) + 60 + 450 + 150 + 60
        self.assertGreaterEqual(int(steps['One nested niri unlock cycle']) * 60, chain)
        job = int(re.search(r'    timeout-minutes: (\d+)', workflow)[1])
        self.assertGreaterEqual(job, sum(map(int, steps.values())))
        self.assertIn('Compact diagnostic results', steps)
        self.assertIn('Report measured result', steps)

    def test_initial_receipt_failure_reaps_qemu_and_preserves_primary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            key = root / 'client'
            key.write_text('test-only placeholder')
            key.chmod(0o600)
            key.with_suffix('.pub').write_text('ssh-ed25519 placeholder')
            cfg = dict(instance='owned', host='test', user='tester', ssh_port=2200,
                       ssh_key=str(key), qemu='qemu-system-x86_64', cpus=2, memory_mib=2048)
            process = Mock(pid=123)
            primary = OSError('initial receipt full')
            with patch.object(platform.os, 'access', return_value=True), \
                    patch.object(platform, 'command'), \
                    patch.object(platform.subprocess, 'Popen', return_value=process) as launch, \
                    patch.object(platform.VM, 'save', side_effect=primary):
                with self.assertRaises(OSError) as error:
                    platform.VM(cfg, root / 'image', root / 'instance')
            self.assertIs(error.exception, primary)
            process.kill.assert_called_once()
            process.wait.assert_called_once_with(timeout=5)
            self.assertTrue(launch.call_args.kwargs['stdout'].closed)

    def group_fixture(self, root):
        source = root / 'source'
        source.mkdir()
        (source / '.gitignore').touch()
        (source / 'file').write_text('source')
        image = root / 'image'
        image.write_bytes(b'image')
        instance = root / 'instance'
        instance.mkdir()
        cfg = dict(guest_root=str(root / 'inner'), storage=str(root), user='tester',
                   host='test', build_seconds=30, group_seconds=30, export_seconds=10)
        args = SimpleNamespace(image=str(image), sha256=platform.digest(image), source=str(source),
                               results=str(root / 'results'), source_revision=None)
        vm = Mock(spec=['name', 'uuid', 'mac', 'receipt', 'directory', 'wait_ready', 'ssh', 'stop', 'execute'])
        vm.name, vm.uuid, vm.mac = 'owned', 'owned-uuid', '52:54:00:00:00:01'
        vm.directory, vm.receipt = instance, dict(phase='starting', qemu_exit=0)
        return source, cfg, args, vm

    def test_receipt_failures_cannot_skip_disposal_or_replace_primary(self):
        with tempfile.TemporaryDirectory() as directory:
            _, cfg, args, vm = self.group_fixture(Path(directory))
            primary = RuntimeError('original boot failure')
            vm.wait_ready.side_effect = primary
            vm.ssh.side_effect = TimeoutError('secondary export failure')
            with patch.object(platform, 'VM', return_value=vm), \
                    patch.object(platform, 'write_json', side_effect=OSError('results full')), \
                    patch.object(platform.sys, 'stderr'):
                with self.assertRaises(Exception) as error:
                    platform.run_group(args, cfg)
            vm.stop.assert_called_once_with(force=True)
            self.assertIs(error.exception, primary)

    def test_source_transfer_preserves_modes_under_restrictive_guest_umask(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, cfg, args, vm = self.group_fixture(root)
            (source / 'file').chmod(0o664)
            vm.execute.return_value = '{"status":"done"}'
            reached_transfer = []
            def ssh(argv, **kwargs):
                if argv[0] == 'python3':
                    return SimpleNamespace(stdout=json.dumps(dict(user='tester', home=str(root))))
                if argv[:3] == ['sudo', '-n', 'cat']:
                    return SimpleNamespace(stdout=vm.uuid)
                if argv[0] == 'mkdir':
                    for path in (argv[2:] if argv[1] == '-p' else argv[-1:]):
                        Path(path).mkdir(parents=True, exist_ok=True)
                    return
                if argv[0] == 'tar':
                    old = os.umask(0o077)
                    try:
                        subprocess.run(argv, stdin=kwargs['stdin'], check=True)
                    finally:
                        os.umask(old)
                    reached_transfer.append(True)
                    raise RuntimeError('stop after transfer probe')
                raise RuntimeError('no guest workload in this contract')
            vm.ssh.side_effect = ssh
            with patch.object(platform, 'VM', return_value=vm), self.assertRaises(RuntimeError):
                platform.run_group(args, cfg)
            self.assertTrue(reached_transfer)
            transferred = Path(cfg['guest_root']) / 'src/aerial-lock/file'
            self.assertEqual(transferred.stat().st_mode, (source / 'file').stat().st_mode)


class ShadowBackupRegression(unittest.TestCase):
    def test_partial_or_unsecured_backup_is_never_restored(self):
        for phase in ('early', 'copy', 'permissions', 'after-backup'):
            with self.subTest(phase=phase), tempfile.TemporaryDirectory() as directory:
                root = Path(directory) / 'guest'
                source = root / 'src/aerial-lock'
                source.mkdir(parents=True)
                backup = root / 'run/shadow-backup'
                restored = []
                driver = Mock()
                driver.ObservedProcess.return_value.process.pid = 123
                def compositor(*_):
                    (root / 'runtime/wayland-0').touch()
                    return driver.ObservedProcess.return_value
                driver.ObservedProcess.side_effect = compositor
                def run(argv, **_):
                    if phase == 'early' and argv[0] == 'make':
                        raise RuntimeError('early build failure')
                    if argv[:3] == ['sudo', '-n', 'cp']:
                        if argv[-1] == str(backup):
                            backup.write_text('simulated backup')
                            if phase == 'copy':
                                raise OSError('partial backup')
                        else:
                            restored.append(argv)
                    if argv[:3] == ['sudo', '-n', 'chmod'] and phase == 'permissions':
                        raise OSError('unsecured backup')
                    if argv[:3] == ['sudo', '-n', 'chpasswd']:
                        raise RuntimeError('after backup, before fixture')
                    if argv[:4] == ['sudo', '-n', 'rm', '-f']:
                        backup.unlink(missing_ok=True)
                    return SimpleNamespace(returncode=0)
                def output(argv, **_):
                    if argv[0] == 'getcap':
                        return ''
                    if argv[0] == 'swaymsg':
                        return '[{"active":true,"name":"HEADLESS-1"}]'
                    return 'niri pinned-version\n'
                request = root / 'request.json'
                request.write_text(json.dumps(dict(root=str(root), source_files={}, uuid='owned',
                    image_sha256='image', source_freeze_identity='source', build_seconds=10)))
                owned = SimpleNamespace(OwnedInstance=Mock(), digest=Mock(), session_identity=Mock())
                with patch.dict(sys.modules, owned_instance=owned), \
                        patch.object(guest.Path, 'home', return_value=root.parent), \
                        patch.object(guest.sys, 'path', list(sys.path)), \
                        patch.object(guest.importlib.util, 'spec_from_file_location', return_value=Mock()), \
                        patch.object(guest.importlib.util, 'module_from_spec', return_value=driver), \
                        patch.object(guest, 'digest', return_value='same-helper'), \
                        patch.object(guest.Path, 'is_socket', return_value=True), \
                        patch.object(guest.subprocess, 'run', side_effect=run), \
                        patch.object(guest.subprocess, 'check_output', side_effect=output):
                    self.assertEqual(guest.run_group(request), 1)
                self.assertEqual(len(restored), int(phase == 'after-backup'))
                self.assertFalse(backup.exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
