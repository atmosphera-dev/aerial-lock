#!/usr/bin/env python3
"""Harness failure retirement must preserve the actual live-lock blocker."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import threading
import sys
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location('emergency_acceptance', Path(__file__).with_name('emergency-integration.py'))
acceptance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(acceptance)


class Retention(unittest.TestCase):
    def test_sync_trace_allowlist_never_persists_keyboard_payloads(self):
        lines = ['[12:34:56.123456] wl_display#1.sync(new id wl_callback#7)',
                 '[12:34:56.123457] wl_keyboard#4.key(1, 2, TRACE_PRIVATE_SENTINEL, 1)',
                 '[12:34:56.123458] wl_callback#7.done(9)']
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'filtered.log'
            process = acceptance.driver.ObservedProcess([sys.executable, '-c', 'print(' + repr('\n'.join(lines)) + ')'],
                                                       os.environ, log, protocol_sync=True)
            self.assertEqual(process.finish(5), 0)
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


if __name__ == '__main__':
    unittest.main()
