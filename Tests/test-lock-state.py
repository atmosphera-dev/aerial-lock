#!/usr/bin/env python3
"""Real Quickshell controller logging fixture; run inside a test VM session."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


class LockStateLog(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = Path(__file__).resolve().parent.parent
        cls.scratch = tempfile.TemporaryDirectory(prefix="lock-state-")
        shell = Path(cls.scratch.name) / "shell"
        shutil.copytree(source, shell, ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
        (shell / "shell.qml").write_text(
            'import QtQuick\nimport Quickshell\nShellRoot { settings.watchFiles: false; '
            'Loader { source: "root:/Tests/lock-state-shell.qml" } }\n')
        cls.logs = []
        for verbose in ("0", "1"):
            env = dict(os.environ, AERIAL_LOCK_VERBOSE=verbose,
                       XDG_CONFIG_HOME=str(Path(cls.scratch.name) / "config"),
                       XDG_CACHE_HOME=str(Path(cls.scratch.name) / "cache"))
            result = subprocess.run([
                "bash", str(source / "Scripts/aerial-lock-run"), "--shell-dir", str(shell),
                "--plugin-root", str(source / "native/qt-plugins")],
                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=30)
            if result.returncode or "LOCK_STATE_EXERCISED" not in result.stdout:
                raise AssertionError(result.stdout)
            cls.logs.append(re.sub(r"\x1b\[[0-9;]*m", "", result.stdout))

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def test_exact_prompt_fail_success_sequence(self):
        expected = [
            "auth idle -> prompting reason=initial-prompt",
            "auth prompting -> authenticating reason=submit",
            "auth authenticating -> prompting reason=pam-prompt",
            "auth prompting -> authenticating reason=submit",
            "auth authenticating -> failed reason=pam-failed",
            "auth failed -> authenticating reason=submit",
            "auth authenticating -> success reason=pam-success",
        ]
        for log in self.logs:
            lines = [line.split("lock-state: ", 1)[1] for line in log.splitlines()
                     if "lock-state: " in line]
            self.assertEqual(lines, expected)

    def test_password_material_never_logged(self):
        for log in self.logs:
            for secret in ("wrong-secret-7e83", "right-secret-91ab"):
                self.assertNotIn(secret, log)
            self.assertIn("lock-state: auth authenticating -> failed reason=pam-failed", log)
            self.assertIn("lock-state: auth authenticating -> success reason=pam-success", log)


if __name__ == "__main__":
    unittest.main()
