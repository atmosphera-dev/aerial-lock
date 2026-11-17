#!/usr/bin/env python3
"""Account restoration contracts on a synthetic disk-backed database."""
from contextlib import contextmanager
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import patch

import shadow_fixture as fixture

ORIGINAL = b'tester:!original:19000:1:90:5:2:20000:\n'
CURRENT = b'root:!:1::::::\ntester:!temporary:20000:0:99999:7:::\npeer:!old:2::::::\n'


class ShadowRestoration(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='shadow-fixture-')
        self.addCleanup(self.directory.cleanup)
        self.database = Path(self.directory.name) / 'shadow'
        self.database.write_bytes(CURRENT)
        self.database.chmod(0o640)
        self.held = False

        @contextmanager
        def lock():
            self.assertFalse(self.held)
            self.held = True
            try:
                yield
            finally:
                self.held = False
        self.lock = patch.object(fixture, 'account_lock', side_effect=lock).start()
        self.addCleanup(patch.stopall)

    def test_lock_precedes_read_and_covers_atomic_replacement(self):
        read = Path.read_bytes
        replace = os.replace

        def guarded_read(path):
            if path == self.database:
                self.assertTrue(self.held, 'live database read without account lock')
            return read(path)

        def guarded_replace(source, destination):
            self.assertTrue(self.held, 'account lock released before replacement')
            self.assertEqual(Path(source).parent, self.database.parent)
            self.assertEqual(Path(destination), self.database)
            return replace(source, destination)

        with patch.object(Path, 'read_bytes', guarded_read), patch.object(fixture.os, 'replace', side_effect=guarded_replace) as swap:
            fixture.restore_account('tester', ORIGINAL, self.database)
        self.lock.assert_called_once()
        swap.assert_called_once()
        self.assertFalse(self.held)

    def test_other_account_update_before_lock_acquisition_survives(self):
        @contextmanager
        def competing_writer_finishes():
            self.database.write_bytes(CURRENT.replace(b'peer:!old:', b'peer:!new:'))
            self.held = True
            try:
                yield
            finally:
                self.held = False
        self.lock.side_effect = competing_writer_finishes
        fixture.restore_account('tester', ORIGINAL, self.database)
        self.assertEqual(fixture.account_line(self.database.read_bytes(), 'peer'), b'peer:!new:2::::::\n')
        self.assertEqual(fixture.account_line(self.database.read_bytes(), 'tester'), ORIGINAL)

    def test_sync_failure_leaves_live_database_intact(self):
        with patch.object(fixture.os, 'fsync', side_effect=OSError('injected sync failure')):
            with self.assertRaises(OSError):
                fixture.restore_account('tester', ORIGINAL, self.database)
        self.assertEqual(self.database.read_bytes(), CURRENT)
        self.assertEqual(list(self.database.parent.iterdir()), [self.database])

    def test_replace_failure_leaves_live_database_intact(self):
        with patch.object(fixture.os, 'replace', side_effect=OSError('injected rename failure')):
            with self.assertRaises(OSError):
                fixture.restore_account('tester', ORIGINAL, self.database)
        self.assertEqual(self.database.read_bytes(), CURRENT)
        self.assertEqual(list(self.database.parent.iterdir()), [self.database])

    def test_preserves_other_records_ownership_and_permissions(self):
        before = self.database.stat()
        fixture.restore_account('tester', ORIGINAL, self.database)
        after = self.database.stat()
        self.assertEqual(self.database.read_bytes(), CURRENT.replace(fixture.account_line(CURRENT, 'tester'), ORIGINAL))
        self.assertEqual((after.st_uid, after.st_gid, stat.S_IMODE(after.st_mode)),
                         (before.st_uid, before.st_gid, stat.S_IMODE(before.st_mode)))
        self.assertNotEqual(before.st_ino, after.st_ino, 'never truncate the live inode')

    def test_missing_or_duplicate_user_is_not_rewritten(self):
        for content in (b'peer:!:1::::::\n', CURRENT + ORIGINAL):
            self.database.write_bytes(content)
            with self.assertRaises(ValueError):
                fixture.restore_account('tester', ORIGINAL, self.database)
            self.assertEqual(self.database.read_bytes(), content)


if __name__ == '__main__':
    unittest.main(verbosity=2)
