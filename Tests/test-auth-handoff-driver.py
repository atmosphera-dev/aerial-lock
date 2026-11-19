#!/usr/bin/env python3
"""Synthetic-only regression of native acceptance case finalization."""
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock

spec = importlib.util.spec_from_file_location("handoff", Path(__file__).with_name("auth-handoff-vm.py"))
handoff = importlib.util.module_from_spec(spec)
spec.loader.exec_module(handoff)


class Finalization(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="handoff-finalize-")
        self.addCleanup(self.scratch.cleanup)
        self.output = Path(self.scratch.name)
        self.session = SimpleNamespace(output=self.output, cleanup=Mock())
        self.result = {"status": "PASS", "case": "synthetic"}

    def persisted(self):
        return json.loads((self.output / "handoff.json").read_text())

    def test_leak_records_failure_and_still_cleans_up(self):
        (self.output / "locker.log").write_text("synthetic-only-secret")
        handoff.finalize_cycle(self.session, self.result, b"synthetic-only-secret")
        self.session.cleanup.assert_called_once_with()
        self.assertEqual(self.persisted()["status"], "FAIL")
        self.assertNotIn("synthetic-only-secret", json.dumps(self.persisted()))

    def test_log_check_failure_still_cleans_up_and_persists(self):
        (self.output / "locker.log").mkdir()
        handoff.finalize_cycle(self.session, self.result, b"synthetic-only-secret")
        self.session.cleanup.assert_called_once_with()
        self.assertEqual(self.persisted()["status"], "FAIL")

    def test_cleanup_failure_still_persists_failed_result(self):
        self.session.cleanup.side_effect = RuntimeError("synthetic cleanup failure")
        with self.assertRaisesRegex(RuntimeError, "synthetic cleanup failure"):
            handoff.finalize_cycle(self.session, self.result, None)
        self.assertEqual(self.persisted()["status"], "FAIL")
        self.assertIn("cleanup_error", self.persisted())

    def test_success_is_preserved(self):
        (self.output / "locker.log").write_text("no credential material")
        handoff.finalize_cycle(self.session, self.result, b"synthetic-only-secret")
        self.assertEqual(self.persisted()["status"], "PASS")
        self.session.cleanup.assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
