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
    services = root / "services"
    services.mkdir()
    marker = root / "unexpected-activation"
    (services / "org.aeriallock.Aerial1.service").write_text(
        "[D-BUS Service]\nName=org.aeriallock.Aerial1\nExec=/usr/bin/touch " + str(marker) + "\n")

    def bus(label):
        config = root / (label + ".conf")
        config.write_text('<busconfig><type>session</type><auth>EXTERNAL</auth><listen>unix:path=' + str(root / label)
                          + '</listen><servicedir>' + str(services)
                          + '</servicedir><policy context="default"><allow user="*"/><allow send_destination="*"/>'
                            '<allow receive_sender="*"/><allow own="*"/></policy></busconfig>')
        process = subprocess.Popen(["dbus-daemon", "--config-file=" + str(config), "--nofork", "--nopidfile", "--print-address=1"],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        processes.append(process)
        assert select.select([process.stdout], [], [], 5)[0], "bus address not published"
        address = process.stdout.readline().decode().strip()
        assert address.startswith("unix:"), "bus did not publish a usable address: " + repr(address)
        return process, address

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
        assert command(no_bus, "status").returncode == 1
        missing = command(env, "status")
        assert missing.returncode == 1 and "NameHasNoOwner" in missing.stderr, missing.stderr
        assert not marker.exists(), "custom-profile CLI activated the production name before comparing profiles"
        owner = start(env)

        def status():
            if owner.poll() is not None:
                raise AssertionError("service exited before status: " + owner.stderr.read().decode(errors="replace"))
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
        owner.terminate()
        assert owner.wait(timeout=5) == 0 or owner.returncode == -15

        env, profile = environment(second_address, "planner-recovery")
        config = profile / "config/aerial-lock/config.json"
        document = json.loads(config.read_text())
        document["cache"]["maxBytes"] = 1000000
        config.write_text(json.dumps(document))
        obstruction = profile / "state/aerial-lock"
        obstruction.write_bytes(b"temporary storage obstruction")
        owner = start(env)
        snapshot = wait(status, "planner failure did not export status")
        assert snapshot["DeferReason"] == "StorageError", snapshot
        obstruction.unlink()
        wait(lambda: (status() or {}).get("State") == "paused", "planner did not recover without a restart or edit")
        print("COMPANION_OWNER_PASS zero_cap status independent_bus_conflict bus_loss storage_error no_unowned_deletion help_without_bus custom_profile_no_activation")
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
