#!/usr/bin/env python3
"""Independent process org.freedesktop.Application contract on a private bus."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import re
import select

binary, source, peer_binary = map(Path, sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="settings-instance-") as temporary:
    root = Path(temporary)
    env = dict(os.environ, AERIAL_LOCK_SHELL=str(source), QT_QPA_PLATFORM="offscreen")
    for key in ("XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"):
        env[key] = str(root / key)
    def call(method, *arguments, success=True):
        result = subprocess.run(["gdbus", "call", "--session", "--dest", "org.aeriallock.AerialSettings", "--object-path", "/org/aeriallock/AerialSettings", "--method", "org.freedesktop.Application." + method, *arguments], env=env, capture_output=True, text=True, timeout=10)
        assert (result.returncode == 0) == success, result.stderr
        return result.stdout
    def owner():
        result = subprocess.run(["gdbus", "call", "--session", "--dest", "org.freedesktop.DBus", "--object-path", "/org/freedesktop/DBus", "--method", "org.freedesktop.DBus.GetNameOwner", "org.aeriallock.AerialSettings"], env=env, capture_output=True, text=True, timeout=5)
        return result.stdout if result.returncode == 0 else ""
    profile = None
    for label, launches in (("window-first", [[]]), ("tray-first", [["--tray"]]), ("simultaneous", [["--tray"], [], ["--tray"], [], [], ["--tray"]])):
      with (root / (label + ".log")).open("w+") as log:
        apps = [subprocess.Popen([str(binary), *args], env=env, stdout=log, stderr=log) for args in launches]
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                first_owner = owner()
                if first_owner:
                    break
                assert any(app.poll() is None for app in apps)
                time.sleep(.03)
            else:
                raise AssertionError("settings owner did not appear")
            while time.monotonic() < deadline:
                assert any(app.poll() is None for app in apps), "settings exited before exporting its profile"
                try:
                    reply = subprocess.run(["gdbus", "call", "--session", "--dest", "org.aeriallock.AerialSettings", "--object-path", "/org/aeriallock/AerialSettings", "--method", "org.freedesktop.DBus.Properties.Get", "org.freedesktop.Application", "Profile"], env=env, capture_output=True, text=True, timeout=min(1, max(.01, deadline - time.monotonic())))
                except subprocess.TimeoutExpired:
                    continue
                if reply.returncode == 0:
                    properties = reply.stdout; break
                time.sleep(.03)
            else:
                raise AssertionError("settings profile export did not become ready")
            profile = re.search(r"'([^']+)'", properties)[1]
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
            assert owner() == first_owner
            deadline = time.monotonic() + 10
            while sum(app.poll() is None for app in apps) != 1 and time.monotonic() < deadline:
                time.sleep(.03)
            assert sum(app.poll() is None for app in apps) == 1
            assert all(app.poll() in (None, 0) for app in apps)
        finally:
            for app in apps:
                if app.poll() is None:
                    app.terminate()
                app.wait(timeout=10)
        log.seek(0)
        text = log.read()
        print(text, end="")
        assert not text, "unexpected construction/teardown log"
    for mode in ("reject", "exit"):
        peer = subprocess.Popen([str(peer_binary), "--forward-peer", profile, mode], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            assert select.select([peer.stdout], [], [], 10)[0] and peer.stdout.readline().strip() == "READY"
            failed = subprocess.run([str(binary), "--tray"], env=env, capture_output=True, text=True, timeout=10)
            assert failed.returncode == 1 and not failed.stdout + failed.stderr, (mode, failed.returncode, failed.stderr)
        finally:
            if peer.poll() is None:
                peer.terminate()
            _, error = peer.communicate(timeout=10)
            assert not error, error
print("SETTINGS_SINGLE_INSTANCE_PASS")
