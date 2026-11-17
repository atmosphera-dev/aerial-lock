#!/usr/bin/env python3
"""Run the native service against a private-session-bus login1 peer, never the OS."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="power-actions-") as directory:
    env = dict(os.environ, TMPDIR=directory)
    result = subprocess.run(["dbus-run-session", "--", *sys.argv[1:]], env=env, timeout=90)
    raise SystemExit(result.returncode)
