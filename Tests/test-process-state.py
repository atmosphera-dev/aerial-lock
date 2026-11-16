#!/usr/bin/env python3
"""Deterministic process-exit races at the procfs observation boundary."""
from pathlib import Path
import unittest
from unittest.mock import patch

from process_state import descendants, process_entries, process_exited, process_text, require_process, thread_children
from owned_instance import process_identity, socket_identity


class ProcessState(unittest.TestCase):
    def test_missing_reads_mean_exited_without_an_existence_check(self):
        for error in (FileNotFoundError, ProcessLookupError):
            with self.subTest(error=error), patch.object(Path, "exists", side_effect=AssertionError("TOCTOU")), \
                    patch.object(Path, "read_text", side_effect=error):
                self.assertTrue(process_exited(123))
                self.assertEqual(process_text(123, "comm"), "")
                self.assertEqual(descendants(123), [])

    def test_running_and_zombie_states_with_parentheses_in_name(self):
        for state, exited in (("S", False), ("Z", True)):
            with patch.object(Path, "read_text", return_value=f"123 (name ) with parens) {state} 1 2"):
                self.assertEqual(process_exited(123), exited)

    def test_process_disappears_during_directory_iteration(self):
        for error in (FileNotFoundError, ProcessLookupError):
            def vanishing():
                yield Path("/proc/123/task/123")
                raise error()
            with self.subTest(error=error), patch.object(Path, "iterdir", return_value=vanishing()):
                self.assertEqual(process_entries(123, "task"), [])

    def test_finished_thread_is_ignored_but_live_thread_children_are_observed(self):
        with patch.object(Path, "iterdir", return_value=iter([Path("/proc/123/task/123"), Path("/proc/123/task/124")])), \
                patch.object(Path, "read_text", side_effect=[FileNotFoundError(), "456 789"]):
            self.assertEqual(thread_children(123), [456, 789])

    def test_child_exits_between_parent_scan_and_recursive_read(self):
        with patch.object(Path, "read_text", side_effect=["456 789", ProcessLookupError(), "321", ""]):
            self.assertEqual(descendants(123), [456, 789, 321])

    def test_unexpected_io_errors_are_not_hidden(self):
        for error in (PermissionError, OSError):
            with self.subTest(error=error), patch.object(Path, "read_text", side_effect=error):
                with self.assertRaises(error):
                    process_exited(123)
            with patch.object(Path, "iterdir", side_effect=error):
                with self.assertRaises(error):
                    process_entries(123, "task")

    def test_required_process_exit_is_an_explicit_failure(self):
        for error in (FileNotFoundError, ProcessLookupError):
            with self.subTest(error=error), self.assertRaisesRegex(RuntimeError, "process 123 exited"):
                with require_process(123):
                    raise error()
            with patch.object(Path, "stat", side_effect=error):
                with self.assertRaisesRegex(RuntimeError, "process 123 exited"):
                    process_identity(123)

    def test_descriptor_scan_of_exited_process_fails_ownership(self):
        from types import SimpleNamespace
        import os
        import stat
        for error in (FileNotFoundError, ProcessLookupError):
            with self.subTest(error=error), \
                    patch.object(Path, "stat", return_value=SimpleNamespace(st_mode=stat.S_IFSOCK, st_uid=os.getuid())), \
                    patch.object(Path, "read_text", return_value="header\n0 0 0 00010000 0001 01 123 /fake\n"), \
                    patch.object(Path, "iterdir", side_effect=error):
                with self.assertRaisesRegex(RuntimeError, "not owned"):
                    socket_identity("/fake", 123)


if __name__ == "__main__":
    unittest.main(verbosity=2)
