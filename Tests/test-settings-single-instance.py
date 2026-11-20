#!/usr/bin/env python3
"""Independent process org.freedesktop.Application contract on a private bus."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

binary, source = map(Path, sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="settings-instance-") as temporary:
    root = Path(temporary)
    env = dict(os.environ, AERIAL_LOCK_SHELL=str(source), QT_QPA_PLATFORM="offscreen")
    for key in ("XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"):
        env[key] = str(root / key)
    def call(method, *arguments, success=True):
        result = subprocess.run(["gdbus", "call", "--session", "--dest", "org.aeriallock.AerialSettings", "--object-path", "/org/aeriallock/AerialSettings", "--method", "org.freedesktop.Application." + method, *arguments], env=env, capture_output=True, text=True, timeout=10)
        assert (result.returncode == 0) == success, result.stderr
        return result.stdout
    with (root / "app.log").open("w+") as log:
        app = subprocess.Popen([str(binary), "--tray"], env=env, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                result = subprocess.run(["gdbus", "call", "--session", "--dest", "org.freedesktop.DBus", "--object-path", "/org/freedesktop/DBus", "--method", "org.freedesktop.DBus.GetNameOwner", "org.aeriallock.AerialSettings"], env=env, capture_output=True, text=True, timeout=5)
                if not result.returncode:
                    owner = result.stdout
                    break
                assert app.poll() is None
                time.sleep(.03)
            else:
                raise AssertionError("settings owner did not appear")
            call("ActivateAction", "unknown", "[]", "{}", success=False)
            call("ActivateAction", "tray", "[]", "{}")
            other_env = dict(env, XDG_CONFIG_HOME=str(root / "other-profile"))
            wrong = subprocess.run([str(binary), "--tray"], env=other_env, capture_output=True, text=True, timeout=10)
            assert wrong.returncode == 1 and not wrong.stderr, "different profile must not activate the existing application"
            call("Open", "['file:///not-supported']", "{}", success=False)
            for args in ([], ["--tray"], []):
                forwarded = subprocess.run([str(binary), *args], env=env, capture_output=True, text=True, timeout=10)
                assert forwarded.returncode == 0, forwarded.stderr
                assert not forwarded.stderr, forwarded.stderr
            call("Open", "[]", "{}")
            call("Activate", "{}")
            current = subprocess.check_output(["gdbus", "call", "--session", "--dest", "org.freedesktop.DBus", "--object-path", "/org/freedesktop/DBus", "--method", "org.freedesktop.DBus.GetNameOwner", "org.aeriallock.AerialSettings"], env=env, text=True, timeout=5)
            assert current == owner
        finally:
            app.terminate()
            app.wait(timeout=10)
        log.seek(0)
        text = log.read()
        print(text, end="")
        assert not text, "unexpected construction/teardown log"
print("SETTINGS_SINGLE_INSTANCE_PASS")
