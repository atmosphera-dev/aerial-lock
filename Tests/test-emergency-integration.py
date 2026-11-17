#!/usr/bin/env python3
"""Harness failure retirement must preserve the actual live-lock blocker."""
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import threading
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
from contextlib import nullcontext

spec = importlib.util.spec_from_file_location('emergency_acceptance', Path(__file__).with_name('emergency-integration.py'))
acceptance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(acceptance)


class Retention(unittest.TestCase):
    def test_external_observer_uses_the_same_default_and_opt_in_filter(self):
        trace = (b'[12:34:56.123456] wl_display#1.sync(new id wl_callback#7)\n'
                 b'[12:34:56.123457] wl_keyboard#4.key(1, 2, PRIVATE_SENTINEL, 1)\n'
                 b'[12:34:56.123458] wl_callback#7.done(9)\n'
                 b'[12:34:56.123459] ext_session_lock_v1#9.locked()\n')
        for external in (False, True):
            for sync in (False, True):
                with self.subTest(external=external, sync=sync), tempfile.TemporaryDirectory() as directory:
                    log = Path(directory) / 'filtered.log'
                    if external:
                        # The FIFO adapter publishes its process interface before
                        # opening the stream and starting its custom reader.
                        peer = SimpleNamespace(stdout=None)
                        process = acceptance.driver.ObservedProcess.from_process(peer, log, sync, start_reader=False)
                        peer.stdout = io.BytesIO(trace)
                        process.start_reader()
                        process.thread.join(5)
                        self.assertFalse(process.thread.is_alive())
                    else:
                        process = acceptance.driver.ObservedProcess(
                            [sys.executable, '-c', 'import sys; sys.stdout.buffer.write(' + repr(trace) + ')'],
                            os.environ, log, sync)
                        self.assertEqual(process.finish(5), 0)
                    text = log.read_text()
                    self.assertNotIn('PRIVATE_SENTINEL', text)
                    self.assertIn('.locked()', text)
                    self.assertEqual('.sync(' in text, sync)
                    self.assertEqual('.done(' in text, sync)
                    process.process.stdout.close()

    def test_sync_trace_allowlist_never_persists_keyboard_payloads(self):
        lines = ['[12:34:56.123456] wl_display#1.sync(new id wl_callback#7)',
                 '[12:34:56.123457] wl_keyboard#4.key(1, 2, TRACE_PRIVATE_SENTINEL, 1)',
                 '[12:34:56.123458] wl_callback#7.done(9)']
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'filtered.log'
            process = acceptance.driver.ObservedProcess([sys.executable, '-c', 'print(' + repr('\n'.join(lines)) + ')'],
                                                       os.environ, log, protocol_sync=True)
            self.assertEqual(process.finish(5), 0)
            process.process.stdout.close()
            text = log.read_text()
            self.assertIn('.sync(', text)
            self.assertIn('.done(', text)
            self.assertNotIn('TRACE_PRIVATE_SENTINEL', text)

    def test_sampling_error_cannot_bypass_live_locker_retention(self):
        with tempfile.TemporaryDirectory() as directory:
            frames = acceptance.FrameWitness({}, 'owned', directory)
            frames.error = 'observed capture failure'
            with self.assertRaisesRegex(RuntimeError, 'capture failure'):
                frames.finish()
            probe = object.__new__(acceptance.RecoveryProbe)
            probe.stop, probe.flood = threading.Event(), None
            probe.frames, probe.frame_summary = frames, None
            probe.locker = SimpleNamespace(process=SimpleNamespace(poll=lambda: None))
            self.assertFalse(probe.close())
            value = json.loads((Path(directory) / 'frames.json').read_text())
            self.assertEqual(value['error'], 'observed capture failure')
            self.assertEqual(value['samples'], 0)


class FrameStorage(unittest.TestCase):
    def test_save_interval_and_marker_priority_do_not_change_capture_rate(self):
        with tempfile.TemporaryDirectory() as directory:
            frames = acceptance.FrameWitness({}, 'owned', directory, save_interval=1, frame_limit=10, byte_limit=100)
            observations = iter([(b'abc', False), (b'abc', False), (b'abc', True), (b'abc', False)])
            def sample():
                value = next(observations)
                if value == (b'abc', False) and len(frames.frames) == 3:
                    frames.stop.set()
                return value
            frames.sample = sample
            with patch.object(acceptance.time, 'monotonic_ns', side_effect=[0, 100, 200, 300]), \
                    patch.object(acceptance.time, 'sleep', side_effect=AssertionError('sampling must not sleep')):
                frames.start()
                frames.thread.join(5)
                summary = frames.finish()
            self.assertEqual(summary['samples'], 4)
            self.assertEqual(summary['saved_frames'], 2)
            self.assertEqual(summary['saved_bytes'], 6)
            self.assertEqual(summary['samples_with_desktop_marker'], 1)
            self.assertEqual([f['saved'] for f in frames.frames], [True, False, True, False])

    def test_frame_and_byte_caps_include_control_and_do_not_stop_observation(self):
        for frame_limit, byte_limit in ((2, 100), (100, 6), (100, 2)):
            with self.subTest(frames=frame_limit, bytes=byte_limit), tempfile.TemporaryDirectory() as directory:
                frames = acceptance.FrameWitness({}, 'owned', directory, save_interval=0,
                                                 frame_limit=frame_limit, byte_limit=byte_limit)
                frames.sample = lambda: (b'abc', True)
                frames.control()
                def sample():
                    if len(frames.frames) == 4: frames.stop.set()
                    return b'abc', len(frames.frames) == 4
                frames.sample = sample
                frames.start()
                frames.thread.join(5)
                summary = frames.finish()
                files = list(Path(directory).glob('*.ppm'))
                self.assertEqual(summary['samples'], 5)
                self.assertEqual(summary['samples_with_desktop_marker'], 1)
                self.assertLessEqual(len(files), frame_limit)
                self.assertLessEqual(sum(f.stat().st_size for f in files), byte_limit)
                self.assertEqual(summary['saved_frames'], len(files))
                self.assertEqual(summary['saved_bytes'], sum(f.stat().st_size for f in files))
                self.assertFalse(frames.frames[-1]['saved'])


class InjectorValidation(unittest.TestCase):
    def test_actual_injector_error_is_rejected_after_join(self):
        with tempfile.TemporaryDirectory() as directory:
            probe = acceptance.RecoveryProbe(directory, {}, '', '', '', '', '')
            with patch.object(acceptance, 'key', side_effect=RuntimeError('injected wtype failure')):
                probe.start_flood()
                probe.flood.join(5)
            self.assertFalse(probe.flood.is_alive())
            with self.assertRaisesRegex(RuntimeError, 'injected wtype failure'):
                probe.finish_flood()

    def test_unretired_injector_is_not_success(self):
        with tempfile.TemporaryDirectory() as directory:
            probe = acceptance.RecoveryProbe(directory, {}, '', '', '', '', '')
            probe.flood = Mock()
            probe.flood.is_alive.return_value = True
            with self.assertRaisesRegex(RuntimeError, 'did not stop'):
                probe.finish_flood()

    def test_failure_cases_cannot_publish_pass_after_injector_failure(self):
        cases = ('missing-qs', 'missing-plugin', 'emergency-retry', 'helper-missing',
                 'external-release', 'emergency-missing', 'emergency-exhausted', 'compositor-death')
        for failing in cases:
            with self.subTest(case=failing), tempfile.TemporaryDirectory() as directory:
                session = object.__new__(acceptance.EmergencySession)
                session.args = SimpleNamespace(backend='niri')
                session.env = {}
                session.output = Path(directory)
                session.owned = SimpleNamespace(record=dict(uuid='fixture', source_freeze_identity='fixture'))
                session.prepare = session.cleanup = lambda: None
                session.password = lambda: b'fixture'
                session.absent = lambda name: nullcontext()
                session.compositor = Mock()
                def probe_for(name):
                    probe = Mock(spec=acceptance.RecoveryProbe)
                    probe.locker = Mock()
                    probe.locker.process.pid = 12345
                    probe.locker.log = Path(directory) / (name + '.log')
                    probe.locker.log.write_text('launching locker, attempt 1/3\n')
                    probe.locker.lines = []
                    probe.child.return_value = dict(pid=12346)
                    probe.locker.finish.return_value = 0 if name == 'compositor-death' else 3
                    probe.witness = Mock()
                    probe.witness.process.poll.return_value = None
                    probe.authenticate.return_value = probe.complete.return_value = dict(status='PASS')
                    probe.close.return_value = True
                    if name == failing:
                        probe.finish_flood.side_effect = RuntimeError('injected wtype failure')
                    return probe
                session.probe = probe_for
                with patch.object(acceptance, 'key'), patch.object(acceptance.os, 'kill'):
                    with self.assertRaisesRegex(RuntimeError, 'injected wtype failure'):
                        session.failures()
                outcomes = session.output / 'outcomes.json'
                if outcomes.exists():
                    self.assertNotIn(failing, [row['case'] for row in json.loads(outcomes.read_text())])


if __name__ == '__main__':
    unittest.main()
