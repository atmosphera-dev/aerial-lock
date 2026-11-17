#!/usr/bin/env python3
"""Actual private-bus ownership, zero-cap status and owner retirement."""
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time


def wait(predicate, label):
    until = time.monotonic() + 10
    while time.monotonic() < until:
        value = predicate()
        if value:
            return value
        time.sleep(0.025)
    raise AssertionError(label)


binary, source = map(Path, sys.argv[1:])
cli = source / "cli/aerialctl"
processes = []
with tempfile.TemporaryDirectory(prefix="companion-owner-") as temporary:
    root = Path(temporary)

    def bus(label):
        process = subprocess.Popen(["dbus-daemon", "--session", "--nofork", "--nopidfile", "--print-address=1",
                                    "--address=unix:path=" + str(root / label)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        processes.append(process)
        assert select.select([process.stdout], [], [], 5)[0], "bus address not published"
        return process, process.stdout.readline().decode().strip()

    def environment(address, label):
        profile = root / label
        profile.mkdir()
        result = dict(os.environ, DBUS_SESSION_BUS_ADDRESS=address, DBUS_SYSTEM_BUS_ADDRESS=address,
                      AERIAL_LOCK_SHELL=str(source), QT_LOGGING_TO_CONSOLE="1")
        for name in ("CONFIG", "CACHE", "STATE", "RUNTIME"):
            path = profile / name.lower()
            path.mkdir(mode=0o700)
            result["XDG_" + name + "_HOME" if name != "RUNTIME" else "XDG_RUNTIME_DIR"] = str(path)
        config = profile / "config/aerial-lock/config.json"
        config.parent.mkdir()
        config.write_text(json.dumps({"sources": [], "cache": {"maxBytes": 0},
                                      "acquisition": {"automatic": {"enabled": False}}}))
        return result, profile

    def command(env, *args):
        return subprocess.run([str(cli), *args], env=env, capture_output=True, text=True, timeout=10)

    def start(env):
        process = subprocess.Popen([str(binary)], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        processes.append(process)
        return process

    try:
        first_bus, address = bus("bus1")
        env, profile = environment(address, "profile")
        no_bus = dict(env, DBUS_SESSION_BUS_ADDRESS="unix:path=" + str(root / "absent"))
        assert command(no_bus, "--help").returncode == 0
        assert command(no_bus, "--version").returncode == 0
        assert command(no_bus, "fetch", "--limit", "oops").returncode == 2
        owner = start(env)

        def status():
            result = command(env, "status", "--json")
            return json.loads(result.stdout) if result.returncode == 0 else None

        snapshot = wait(status, "service did not export zero-cap status")
        assert snapshot["MaxBytes"] == snapshot["ManagedBytes"] == 0
        assert snapshot["State"] == "deferred" and snapshot["DeferReason"] == "CacheFull", snapshot
        assert not (profile / "cache/aerial-lock/managed-v1").exists()
        assert command(env, "fetch", "--wait").returncode == 1
        second_bus, second_address = bus("bus2")
        duplicate = start(dict(env, DBUS_SESSION_BUS_ADDRESS=second_address))
        assert duplicate.wait(timeout=10) == 1, "independent bus acquired the same profile"
        diagnostic = duplicate.stderr.read()
        assert b"profile already owned" in diagnostic, diagnostic
        assert command(env, "config", "set", "cache.maxBytes", "1000000").returncode == 0
        wait(lambda: (status() or {}).get("State") == "paused", "positive cap did not initialize queue")
        first_bus.terminate()
        first_bus.wait(timeout=5)
        assert owner.wait(timeout=10) == 0, "service continued after bus loss"

        # Refusing unowned storage must be visible, not a fictional idle/zero
        # usage success. The foreign file itself remains byte-identical.
        env, profile = environment(second_address, "unowned")
        config = profile / "config/aerial-lock/config.json"
        document = json.loads(config.read_text())
        document["cache"]["maxBytes"] = 1000000
        config.write_text(json.dumps(document))
        foreign = profile / "cache/aerial-lock/managed-v1/personal.txt"
        foreign.parent.mkdir(parents=True)
        foreign.write_bytes(b"not adopted")
        owner = start(env)
        snapshot = wait(status, "storage failure did not export status")
        assert snapshot["State"] == "error" and snapshot["DeferReason"] == "StorageError", snapshot
        assert snapshot["ManagedBytes"] == 0 and not snapshot["UsageKnown"]
        assert command(env, "fetch", "--wait").returncode == 1
        assert foreign.read_bytes() == b"not adopted"
        print("COMPANION_OWNER_PASS zero_cap status independent_bus_conflict bus_loss storage_error no_unowned_deletion help_without_bus")
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
