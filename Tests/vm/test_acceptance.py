#!/usr/bin/env python3
"""Acceptance contract regressions. Run only inside an authorized execution VM."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import uuid

import acceptance as a
import guest_acceptance as guest
import vm_platform


def outcome(backend, group, case):
    value = dict(status='PASS', exit=0, unlock_and_destroy=True, desktop_input=True,
                 sync_confirmed=True, no_lock_time_desktop_input=True, real_pam=True,
                  wrong_rejected=True, lock_retained=True, rejected=True)
    if case == 'main-crash':
        value.update(main_pids=[11, 12, 13], sampled_frames=dict(samples=10, error=None, samples_with_desktop_marker=0))
    if case in ('emergency-missing', 'emergency-exhausted'):
        value.update(exit=3, no_automatic_unlock=True, protection_previously_confirmed=True)
    if case == 'compositor-death': value['compositor_dead'] = True
    return value


class Contract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.inputs = self.root / 'slices'
        self.output = self.root / 'merged'
        self.plan = dict(schema=1, source_revision='revision', source_identity='source', image_specification='image-spec',
                         image_tools={'tool': 'digest'}, image_lock_sha256=a.identity({}),
                         run='100', attempt='1', matrix=a.matrix())

    def populate(self):
        for row in self.plan['matrix']:
            root = self.inputs / row['slice']
            root.mkdir(parents=True)
            instance = str(uuid.uuid4())
            cells = [dict(id=cell, outcome=outcome(row['backend'], row['group'], cell.split('/')[-1])) for cell in row['cells']]
            for cell in cells:
                cell['outcome'].update(instance_uuid=instance, source_identity='source',
                    backend=row['backend'])
            summary = dict(schema=1, status='PASS', backend=row['backend'], group=row['group'],
                cells=cells, planned=len(cells), exercised=len(cells), no_surviving_locker=True,
                source_revision=self.plan['source_revision'], source_identity=self.plan['source_identity'],
                instance_uuid=instance, image_sha256='a' * 64, acceptance_plan=a.identity(self.plan), elapsed_seconds=10)
            request = dict(uuid=instance, backend=row['backend'], group=row['group'], image_sha256='a' * 64,
                           source_revision='revision', source_freeze_identity='source', acceptance_plan=a.identity(self.plan))
            a.write(root / 'summary.json', summary)
            a.write(root / 'request.json', request)
            a.write(root / 'instance.json', dict(uuid=instance, image_sha256='a' * 64, qemu_exit=0))
            (root / 'diagnostics.tar.gz').write_bytes(b'compact evidence')
            (root / 'resolved-lock.json').write_text('{}')
            a.write(root / 'image-manifest.json', dict(image_sha256='a' * 64,
                lock_sha256=a.digest(root / 'resolved-lock.json'), tools=self.plan['image_tools']))
            self.assertEqual(a.result(self.plan, row['slice'], root, root / 'slice.json')['status'], 'PASS')

    def merge(self, **kwargs):
        return a.merge(self.plan, self.inputs, self.output, **kwargs)

    def change(self, slice_id, filename, function, reseal=False):
        root = self.inputs / slice_id
        path = root / filename
        value = json.loads(path.read_text())
        function(value)
        a.write(path, value)
        if reseal:
            record = json.loads((root / 'slice.json').read_text())
            record['evidence'][filename] = a.digest(path)
            a.write(root / 'slice.json', record)

    def test_matrix_is_deterministic_disjoint_and_complete(self):
        self.assertEqual(a.matrix(), a.matrix())
        self.assertEqual(len(a.matrix()), 4)
        cells = [cell for row in a.matrix() for cell in row['cells']]
        self.assertEqual(len(cells), 12)
        self.assertEqual(len(cells), len(set(cells)))
        self.assertEqual({row['backend'] for row in a.matrix()}, set(a.COMPOSITORS))
        self.assertEqual({row['group'] for row in a.matrix()}, set(a.GROUPS))
        self.assertNotIn('M1', str(cells))
        self.assertEqual({row['capability'] for row in a.matrix()}, {'nested-one-output-software'})

    def test_exact_matrix_produces_one_summary_and_junit(self):
        self.populate()
        summary = self.merge()
        self.assertEqual(summary['status'], 'PASS')
        self.assertEqual(summary['planned'], summary['exercised'])
        self.assertTrue((self.output / 'junit.xml').is_file())
        self.assertTrue((self.output / 'summary.md').is_file())

    def test_missing_slice_is_not_a_skip_or_pass(self):
        self.populate()
        shutil.rmtree(self.inputs / 'niri-smoke')
        self.assertEqual(self.merge()['status'], 'FAIL')

    def test_duplicate_slice_is_rejected(self):
        self.populate()
        shutil.copytree(self.inputs / 'niri-smoke', self.inputs / 'duplicate')
        self.assertEqual(self.merge()['status'], 'FAIL')

    def test_corrupt_or_missing_evidence_is_rejected(self):
        for mode in ('corrupt', 'missing'):
            with self.subTest(mode=mode):
                if self.inputs.exists(): shutil.rmtree(self.inputs)
                self.populate()
                path = self.inputs / 'niri-smoke/diagnostics.tar.gz'
                path.write_bytes(b'changed') if mode == 'corrupt' else path.unlink()
                self.assertEqual(self.merge()['status'], 'FAIL')

    def test_removed_required_evidence_entry_is_rejected(self):
        self.populate()
        self.change('niri-smoke', 'slice.json', lambda value: value['evidence'].pop('diagnostics.tar.gz'))
        self.assertEqual(self.merge()['status'], 'FAIL')

    def test_stale_source_tool_image_spec_run_and_attempt_plans_are_rejected(self):
        self.populate()
        for key in ('source_revision', 'source_identity', 'image_specification', 'run', 'attempt'):
            with self.subTest(key=key):
                stale = copy.deepcopy(self.plan)
                stale[key] = 'stale'
                self.assertEqual(a.merge(stale, self.inputs, self.output)['status'], 'FAIL')

    def test_failed_blocked_cancelled_or_infra_slices_are_not_passes(self):
        self.populate()
        for status in ('FAIL', 'BLOCKED', 'CANCELLED', 'INFRA_ERROR', 'UNAVAILABLE'):
            with self.subTest(status=status):
                self.change('niri-smoke', 'slice.json', lambda value: value.update(status=status))
                self.assertEqual(self.merge()['status'], 'FAIL')

    def test_matrix_failure_cannot_be_hidden_by_valid_artifacts(self):
        self.populate()
        self.assertEqual(self.merge(matrix_status='failure')['status'], 'FAIL')

    def test_changed_and_duplicated_cell_sets_are_rejected(self):
        self.populate()
        self.change('niri-emergency-recovery', 'summary.json', lambda value: value['cells'].__setitem__(1, value['cells'][0]), reseal=True)
        self.assertEqual(self.merge()['status'], 'FAIL')

    def test_summary_pass_is_not_enough_without_consumer_outcome(self):
        self.populate()
        self.change('niri-smoke', 'summary.json', lambda value: value['cells'][0]['outcome'].update(desktop_input=False), reseal=True)
        self.assertEqual(self.merge()['status'], 'FAIL')

    def test_instance_image_and_request_identity_must_agree(self):
        for filename, key in (('instance.json', 'image_sha256'), ('instance.json', 'uuid'), ('request.json', 'source_freeze_identity')):
            with self.subTest(filename=filename, key=key):
                if self.inputs.exists(): shutil.rmtree(self.inputs)
                self.populate()
                self.change('niri-smoke', filename, lambda value: value.update({key: 'foreign'}), reseal=True)
                self.assertEqual(self.merge()['status'], 'FAIL')

    def test_image_provenance_is_revalidated_after_download(self):
        self.populate()
        self.change('niri-smoke', 'image-manifest.json', lambda value: value.update(tools={'foreign': 'tool'}), reseal=True)
        self.assertEqual(self.merge()['status'], 'FAIL')

    def test_malformed_slice_still_writes_failure_summary(self):
        self.populate()
        (self.inputs / 'niri-smoke/slice.json').write_text('{')
        self.assertEqual(self.merge()['status'], 'FAIL')
        self.assertTrue((self.output / 'junit.xml').is_file())

    def test_malformed_slice_identity_never_breaks_failure_reporting(self):
        self.populate()
        for name in ([], {}, None, 17):
            with self.subTest(name=name):
                a.write(self.inputs / 'niri-smoke/slice.json', {'slice': name})
                summary = self.merge()
                self.assertEqual(summary['status'], 'FAIL')
                self.assertTrue(summary['errors'])
                for filename in ('summary.json', 'junit.xml', 'summary.md'):
                    self.assertTrue((self.output / filename).is_file())

    def test_image_plan_binds_lock_content_and_manifest_binds_exact_bytes(self):
        source = Path(a.__file__).parents[2]
        plan = a.freeze(source, 'revision', 'run', '1')
        content = json.loads((source / 'Tests/vm/image.lock.json').read_text())
        image = self.root / 'image'
        image.mkdir()
        lock = image / 'resolved-lock.json'
        lock.write_text(json.dumps(content, separators=(',', ':')))
        a.write(image / 'image-manifest.json', dict(image_sha256='a' * 64,
            lock_sha256=a.digest(lock), tools=plan['image_tools']))
        a.validate_image(plan, image, 'a' * 64)
        lock.write_text(lock.read_text() + '\n')
        with self.assertRaises(ValueError): a.validate_image(plan, image, 'a' * 64)
        content['snapshot'] = '2000/01/01'
        lock.write_text(json.dumps(content))
        a.write(image / 'image-manifest.json', dict(image_sha256='a' * 64,
            lock_sha256=a.digest(lock), tools=plan['image_tools']))
        with self.assertRaises(ValueError): a.validate_image(plan, image, 'a' * 64)

    def test_local_matrix_preserves_original_configuration_path_base(self):
        origin = self.root / 'original'
        origin.mkdir()
        config = dict(instance='original', host='test-vm', user='tester', storage='storage',
                      guest_root='/home/tester/acceptance', ssh_key='keys/id', firmware='bios.fd', qemu='bin/qemu')
        a.write(origin / 'config.json', config)
        image = self.root / 'image.qcow2'
        image.write_bytes(b'not booted: matrix transport is mocked')
        checksums = self.root / 'SHA256SUMS'
        checksums.write_text(a.digest(image) + '  ' + image.name + '\n')
        observed = []
        def capture(args, plan):
            observed.append(json.loads(Path(args.config).read_text()))
        args = SimpleNamespace(workspace=str(self.root), source=str(Path(a.__file__).parents[2]), revision='revision',
            image=str(image), checksums=str(checksums), config=str(origin / 'config.json'))
        with patch.object(a, 'run_slice', side_effect=capture), patch.object(a, 'result'), \
                patch.object(a, 'merge', return_value=dict(status='PASS', exercised=0, planned=12)):
            a.local(args)
        self.assertEqual(len(observed), len(a.GROUPS))
        for cfg in observed:
            for name in ('ssh_key', 'firmware', 'qemu'):
                self.assertEqual(cfg[name], str(origin / config[name]))
            self.assertTrue(cfg['instance'].startswith('acceptance-niri-'))
            self.assertTrue(Path(cfg['storage']).is_relative_to(self.root / 'v'))

    def test_adapter_deadline_bounds_blocked_cleanup_and_retains_evidence(self):
        root = self.root / 'guest'
        for name in ('run', 'results'): (root / name).mkdir(parents=True)
        worker = root / 'fixture-worker.py'
        worker.write_text('import json, pathlib, signal, sys, time\n'
            'job=json.loads(pathlib.Path(sys.argv[1]).read_text())\n'
            'root=pathlib.Path(job["request"]["root"])\n'
            '(root/"results/acceptance-progress.json").write_text("retained progress")\n'
            '(root/"run/BLOCKED.json").write_text("retained live-lock latch")\n'
            'print("cleanup blocked", flush=True)\n'
            'signal.signal(signal.SIGTERM, signal.SIG_IGN)\n'
            'while True: time.sleep(60)\n')
        request = dict(root=str(root), backend='niri', group='smoke', group_seconds=1, acceptance_plan='plan')
        started = time.monotonic()
        with patch.object(guest, '__file__', str(worker)), self.assertRaises(subprocess.TimeoutExpired):
            guest.run({}, self.root, root, dict(run=root / 'run', results=root / 'results'),
                      root / 'run/owned-vm.json', dict(os.environ), request)
        self.assertLess(time.monotonic() - started, 5)
        self.assertEqual((root / 'results/acceptance-progress.json').read_text(), 'retained progress')
        self.assertTrue((root / 'run/BLOCKED.json').is_file())
        self.assertIn('cleanup blocked', (root / 'results/acceptance-adapter.log').read_text())

    def test_acceptance_junit_names_do_not_require_a_plan_token(self):
        names = vm_platform.group_testcases('emergency-failures', 'niri')
        self.assertEqual(names, [f'niri/emergency-failures/{case}' for case in a.FAULTS])
        self.assertEqual(vm_platform.group_testcases('niri-smoke', 'niri'), ['niri/unlock/1'])
        self.assertEqual(vm_platform.group_testcases('niri-emergency', 'niri'),
                         ['niri/emergency/missing-runner', 'niri/emergency/main-crash'])

    def test_adapter_consumes_only_complete_identity_bound_worker_result(self):
        root = self.root / 'adapter-result'
        for name in ('run', 'results'): (root / name).mkdir(parents=True)
        worker = root / 'fixture-worker.py'
        worker.write_text('import json, pathlib, sys\n'
            'job=json.loads(pathlib.Path(sys.argv[1]).read_text())\n'
            'root=pathlib.Path(job["request"]["root"])\n'
            '(root/"results/acceptance-worker-result.json").write_text(json.dumps(job["summary"]))\n')
        request = dict(root=str(root), backend='niri', group='smoke', group_seconds=5,
                       acceptance_plan='plan', uuid='instance', source_freeze_identity='source')
        value = dict(instance_uuid='instance', source_identity='source', source_revision='rev', image_sha256='image',
            backend='niri', group='smoke', acceptance_plan='plan', planned=1, exercised=1,
            no_surviving_locker=True, status='PASS', cells=[dict(id='niri/smoke/unlock', outcome=outcome('niri', 'smoke', 'unlock'))])
        args = (self.root, root, dict(run=root / 'run', results=root / 'results'), root / 'run/owned-vm.json',
                dict(os.environ), request)
        with patch.object(guest, '__file__', str(worker)):
            observed = copy.deepcopy(value)
            guest.run(observed, *args)
            self.assertEqual(observed, value)
        # A worker's PASS cannot replace the actual caller identity or contract.
        original = worker.read_text()
        for mutation in ('job["summary"]["instance_uuid"]="other"',
                         'job["summary"]["cells"][0]["outcome"]["desktop_input"]=False'):
            worker.write_text(original.replace('root=pathlib.Path', mutation + '\nroot=pathlib.Path'))
            with patch.object(guest, '__file__', str(worker)), self.assertRaises((ValueError, RuntimeError)):
                guest.run(copy.deepcopy(value), *args)

    def test_evidence_path_escape_is_rejected(self):
        self.populate()
        outside = self.root / 'external'
        outside.write_text('not owned evidence')
        self.change('niri-smoke', 'slice.json', lambda value: value['evidence'].update({'../../external': a.digest(outside)}))
        self.assertEqual(self.merge()['status'], 'FAIL')

    def test_result_preserves_missing_and_failed_driver_as_failure(self):
        root = self.root / 'empty'
        root.mkdir()
        self.assertEqual(a.result(self.plan, 'niri-smoke', root, root / 'slice.json')['status'], 'FAIL')

    def test_image_failure_collection_keeps_diagnostics_not_private_storage(self):
        image = self.root / 'image'
        image.mkdir()
        for name in ('provision.log', 'failure.json', 'serial.log', 'aerial-ci-arch-x86_64.qcow2', 'seed.iso', 'id_ed25519'):
            (image / name).write_text('fixture')
        (image / 'qemu.log').symlink_to(image / 'id_ed25519')
        a.collect_image(image, self.output)
        self.assertEqual({path.name for path in (self.output / 'image-build').iterdir()},
                         {'provision.log', 'failure.json', 'serial.log'})

    def test_absent_image_still_allows_combined_failure_collection(self):
        a.collect_image(self.root / 'missing-image', self.output)
        self.assertTrue((self.output / 'image-build').is_dir())

    def test_niri_recovery_rejects_desktop_frame_exposure(self):
        value = outcome('niri', 'emergency-recovery', 'main-crash')
        value['sampled_frames']['samples_with_desktop_marker'] = 2
        with self.assertRaises(ValueError): a.validate_outcome('niri', 'emergency-recovery', 'main-crash', value)

    def test_manual_groups_and_hyprland_are_not_claimed_as_ci_coverage(self):
        for backend, group in (('hyprland', 'smoke'), ('niri', 'writer-handoff'), ('niri', 'panel'), ('niri', 'power')):
            with self.subTest(backend=backend, group=group), self.assertRaises((ValueError, KeyError)):
                a.cases(backend, group)

    def test_wrong_right_requires_both_rejection_and_retained_lock(self):
        for key in ('wrong_rejected', 'lock_retained', 'unlock_and_destroy', 'desktop_input'):
            value = outcome('niri', 'pam', 'wrong-right')
            value[key] = False
            with self.subTest(key=key), self.assertRaises(ValueError):
                a.validate_outcome('niri', 'pam', 'wrong-right', value)

    def test_recovery_requires_real_pam_and_complete_frame_observations(self):
        for key in ('real_pam', 'rejected', 'sync_confirmed', 'no_lock_time_desktop_input'):
            value = outcome('niri', 'emergency-recovery', 'main-crash')
            value[key] = False
            with self.subTest(key=key), self.assertRaises(ValueError):
                a.validate_outcome('niri', 'emergency-recovery', 'main-crash', value)
        for frames in ({}, dict(samples=0, error=None), dict(samples=1, error='capture failed')):
            value = outcome('niri', 'emergency-recovery', 'main-crash')
            value['sampled_frames'] = frames
            with self.subTest(frames=frames), self.assertRaises(ValueError):
                a.validate_outcome('niri', 'emergency-recovery', 'main-crash', value)

    def test_exhaustion_and_compositor_death_require_observed_exit_class(self):
        for case in ('emergency-missing', 'emergency-exhausted', 'compositor-death'):
            value = outcome('niri', 'emergency-failures', case)
            value['exit'] = 1
            with self.subTest(case=case), self.assertRaises(ValueError):
                a.validate_outcome('niri', 'emergency-failures', case, value)

    def test_resealed_failure_marker_cannot_become_a_success(self):
        self.populate()
        path = self.inputs / 'niri-smoke/stop-error.json'
        a.write(path, dict(error='abnormal disposal'))
        self.change('niri-smoke', 'slice.json', lambda value: value['evidence'].update({'stop-error.json': a.digest(path)}))
        self.assertEqual(self.merge()['status'], 'FAIL')

    def test_missing_or_malformed_plan_emits_combined_failure(self):
        for content in (None, '{', '[]', 'null'):
            with self.subTest(content=content):
                path = self.root / 'bad-plan.json'
                if content is not None: path.write_text(content)
                result = subprocess.run([sys.executable, str(Path(a.__file__)), 'merge', '--plan', str(path),
                    '--inputs', str(self.inputs), '--output', str(self.output)],
                    cwd=Path(a.__file__).parents[2], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(json.loads((self.output / 'summary.json').read_text())['status'], 'FAIL')
                self.assertTrue((self.output / 'junit.xml').is_file())


if __name__ == '__main__':
    unittest.main()
