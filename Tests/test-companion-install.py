#!/usr/bin/env python3
"""Staged layout and conventional private dbus-daemon Exec activation.

The real installed systemd/dbus-broker path is a separate owned-VM gate.
"""
import argparse
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time
from process_state import process_exited

from companion_install import check_layout
from private_runtime import bus_config
from settings_fixture_cleanup import Cleanup


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=lambda value: Path(value).resolve(strict=True))
parser.add_argument("--build-settings", type=int, choices=(0, 1), default=1)
args = parser.parse_args()
source = args.source
with tempfile.TemporaryDirectory(prefix="companion-install-") as temporary:
    root = Path(temporary)
    # Use exactly the existing build's configuration, including flags. This is
    # an incremental install, not a competing second build configuration.
    options = (source / "build/native-config").read_text().splitlines()
    config = dict(line.split("=", 1) for line in options)
    prefix = config["DATADIR"].removesuffix("/share/aerial-lock")
    stage = root / "stage"
    installed = subprocess.run(["make", "-C", str(source), "install", "BUILD_SETTINGS=" + str(args.build_settings),
                                "DESTDIR=" + str(stage), "PREFIX=" + prefix, *options],
                               capture_output=True, text=True, timeout=120)
    assert installed.returncode == 0, installed.stdout + installed.stderr
    descriptor = check_layout(stage, prefix, config["LIBEXECDIR"], config["BINDIR"], settings=bool(args.build_settings))
    services = root / "services"
    services.mkdir()
    # Conventional daemon activation honors Exec, ignoring SystemdService.
    # Only the executable is remapped from its installation path to this build.
    text = descriptor.read_text().replace('Exec="' + config["BINDIR"] + '/aerial"',
                                          'Exec="' + str(source / "companion/aerial") + '"')
    (services / descriptor.name).write_text(text)
    if args.build_settings:
        desktop = descriptor.with_name("org.aeriallock.AerialSettings.service")
        text = desktop.read_text().replace('Exec="' + config["BINDIR"] + '/aerial-settings"',
                                           'Exec="' + str(source / "settings/aerial-settings") + '"')
        assert text != desktop.read_text()
        (services / desktop.name).write_text(text)
    profile = root / "profile"
    env = dict(os.environ, AERIAL_LOCK_SHELL=str(source), QT_QPA_PLATFORM="offscreen")
    for key, leaf in (("XDG_CONFIG_HOME", "config"), ("XDG_CACHE_HOME", "cache"),
                      ("XDG_STATE_HOME", "state"), ("XDG_RUNTIME_DIR", "runtime")):
        path = profile / leaf
        path.mkdir(mode=0o700, parents=True)
        env[key] = str(path)
    settings = profile / "config/aerial-lock/config.json"
    settings.parent.mkdir()
    settings.write_text('{"sources":[],"acquisition":{"automatic":{"enabled":false}}}')
    busconfig = root / "bus.conf"
    busconfig.write_text(bus_config(root / "bus", [services]))
    with (root / "bus.log").open("w") as log:
        bus = subprocess.Popen(["dbus-daemon", "--config-file=" + str(busconfig), "--nofork", "--nopidfile", "--print-address=1"],
                               env=env, stdout=subprocess.PIPE, stderr=log)
        try:
            assert select.select([bus.stdout], [], [], 5)[0], "private bus address missing"
            address = bus.stdout.readline().decode().strip()
            assert address.startswith("unix:"), address
            env["DBUS_SESSION_BUS_ADDRESS"] = address
            reply = subprocess.run(["busctl", "--address=" + address, "call", "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                    "org.freedesktop.DBus", "StartServiceByName", "su", "org.aeriallock.Aerial1", "0"],
                                   env=env, capture_output=True, text=True, timeout=10)
            assert reply.returncode == 0 and reply.stdout.strip() == "u 1", reply.stdout + reply.stderr + (root / "bus.log").read_text()
            status = subprocess.run([str(source / "cli/aerialctl"), "status", "--json"], env=env,
                                    capture_output=True, text=True, timeout=10)
            assert status.returncode == 0, status.stderr
            snapshot = json.loads(status.stdout)
            assert snapshot["State"] == "paused" and snapshot["ReadyClips"] == 0, snapshot
            pid = subprocess.check_output(["busctl", "--address=" + address, "call", "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                          "org.freedesktop.DBus", "GetConnectionUnixProcessID", "s", "org.aeriallock.Aerial1"], text=True)
            service_pid = int(pid.split()[1])
            def service_owner():
                return subprocess.check_output(["busctl", "--address=" + address, "call", "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                "org.freedesktop.DBus", "GetNameOwner", "s", "org.aeriallock.Aerial1"], text=True, timeout=10).strip()
            original_owner = service_owner()
            # Desktop activation (DBusActivatable=true) starts the app from its
            # installed descriptor; it is a client and never replaces the service.
            if args.build_settings:
                reply = subprocess.run(["busctl", "--address=" + address, "call", "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                        "org.freedesktop.DBus", "StartServiceByName", "su", "org.aeriallock.AerialSettings", "0"],
                                       env=env, capture_output=True, text=True, timeout=20)
                assert reply.returncode == 0 and reply.stdout.strip() == "u 1", reply.stdout + reply.stderr + (root / "bus.log").read_text()
                pid = subprocess.check_output(["busctl", "--address=" + address, "call", "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                              "org.freedesktop.DBus", "GetConnectionUnixProcessID", "s", "org.aeriallock.AerialSettings"], text=True)
                app_pid = int(pid.split()[1])
                assert Path(os.readlink(f"/proc/{app_pid}/exe")) == source / "settings/aerial-settings"
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    assert not process_exited(app_pid), "settings exited before exporting Activate: " + (root / "bus.log").read_text()
                    try:
                        activated = subprocess.run(["busctl", "--address=" + address, "call", "org.aeriallock.AerialSettings", "/org/aeriallock/AerialSettings",
                                                    "org.freedesktop.Application", "Activate", "a{sv}", "0"], env=env, capture_output=True, text=True,
                                                   timeout=min(1, max(.01, deadline - time.monotonic())))
                    except subprocess.TimeoutExpired:
                        continue
                    if activated.returncode == 0: break
                    time.sleep(.025)
                else:
                    raise AssertionError("settings Activate did not become ready: " + (root / "bus.log").read_text())
                assert activated.returncode == 0, activated.stderr
                os.kill(app_pid, 15)
                deadline = time.monotonic() + 10
                while not process_exited(app_pid):
                    assert time.monotonic() < deadline, "settings app ignored SIGTERM"
                    time.sleep(0.025)
                assert service_owner() == original_owner, "settings lifetime replaced the companion owner"
                again = subprocess.run([str(source / "cli/aerialctl"), "status", "--json"], env=env, capture_output=True, text=True, timeout=10)
                assert again.returncode == 0 and json.loads(again.stdout)["State"] == "paused", "settings exit affected the service"
        finally:
            cleanup = Cleanup()
            if "app_pid" in locals() and not process_exited(app_pid):
                def stop_app():
                    try:
                        os.kill(app_pid, 15)
                    except ProcessLookupError:
                        pass
                    deadline = time.monotonic() + 10
                    while not process_exited(app_pid) and time.monotonic() < deadline:
                        time.sleep(.025)
                    assert process_exited(app_pid), "settings app survived cleanup"
                cleanup("settings app", stop_app)
            cleanup("bus terminate", bus.terminate)
            cleanup("bus reap", lambda: bus.wait(timeout=10))
            if "service_pid" in locals():
                def wait_service():
                    deadline = time.monotonic() + 10
                    while not process_exited(service_pid) and time.monotonic() < deadline:
                        time.sleep(.025)
                    assert process_exited(service_pid), "activated owner survived bus loss"
                cleanup("service exit", wait_service)
            cleanup.finish()
    print("COMPANION_INSTALL_PASS staged_unit relative_vendor_link descriptor private_dbus_exec_activation bus_loss_exit"
          + (" settings_desktop_activation" if args.build_settings else " core_only_no_settings_payload"))
