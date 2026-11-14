#!/usr/bin/env python3
"""Real shell exit policy with zero/one/duplicate native notifications."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ReleaseState(unittest.TestCase):
    def test_release_observations_and_unconfirmed_backstop(self):
        source = Path(__file__).resolve().parent.parent
        with tempfile.TemporaryDirectory(prefix="release-state-") as directory:
            root = Path(directory)
            shell = root / "shell"
            shutil.copytree(source, shell, ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
            (shell / "ProductionShell.qml").write_bytes((source / "shell.qml").read_bytes())
            shutil.copyfile(source / "Tests/lock-release-state-shell.qml", shell / "shell.qml")
            for mode in ("none", "once", "twice", "late", "held", "secure-held"):
                with self.subTest(mode=mode):
                    config = root / mode / "aerial-lock"
                    config.mkdir(parents=True)
                    (config / "config.json").write_text(json.dumps(dict(sources=[], fallbackQuitMs=100, preferredResolution=1080)))
                    env = dict(os.environ, RELEASE_FIXTURE_MODE=mode, AERIAL_LOCK_PAM_SERVICE="login",
                               XDG_CONFIG_HOME=str(root / mode), XDG_CACHE_HOME=str(root / "cache"))
                    env.pop("AERIAL_LOCK_ENGAGEMENT_SOCKET", None)
                    result = subprocess.run(["bash", str(source / "Scripts/aerial-lock-run"),
                        "--shell-dir", str(shell), "--plugin-root", str(source / "native/qt-plugins")],
                        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=15)
                    unconfirmed = mode in ("held", "secure-held")
                    self.assertNotIn("Binding loop", result.stdout)
                    self.assertNotIn("TypeError:", result.stdout)
                    self.assertEqual(result.returncode, 1 if unconfirmed else 0, result.stdout)
                    self.assertIn("exit-backstop idle -> armed reason=unlock-requested", result.stdout)
                    if unconfirmed:
                        self.assertNotIn("RELEASE_FIXTURE unlocked=", result.stdout)
                        self.assertIn("reason=release-unconfirmed exit-code=1", result.stdout)
                    else:
                        self.assertEqual(result.stdout.count("RELEASE_FIXTURE unlocked=1"), 1, result.stdout)
                        self.assertNotIn("RELEASE_FIXTURE unlocked=2", result.stdout)
                        self.assertIn("-> released reason=compositor-unlocked", result.stdout)
                        self.assertIn("locker running -> exiting reason=" + (
                            "release-confirmed-backstop exit-code=0" if mode == "late" else "exit-code-0"), result.stdout)


if __name__ == "__main__":
    unittest.main()
