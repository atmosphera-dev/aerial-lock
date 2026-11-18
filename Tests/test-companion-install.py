#!/usr/bin/env python3
"""Staged layout and conventional private dbus-daemon Exec activation.

The real installed systemd/dbus-broker path is a separate owned-VM gate.
"""
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time

from companion_install import check_layout


source = Path(sys.argv[1]).resolve(strict=True)
with tempfile.TemporaryDirectory(prefix="companion-install-") as temporary:
    root = Path(temporary)
    # Use exactly the existing build's configuration, including flags. This is
    # an incremental install, not a competing second build configuration.
    options = (source / "build/native-config").read_text().splitlines()
    config = dict(line.split("=", 1) for line in options)
    prefix = config["DATADIR"].removesuffix("/share/aerial-lock")
    stage = root / "stage"
    installed = subprocess.run(["make", "-C", str(source), "install", "DESTDIR=" + str(stage), "PREFIX=" + prefix, *options],
                               capture_output=True, text=True, timeout=120)
    assert installed.returncode == 0, installed.stdout + installed.stderr
    descriptor = check_layout(stage, prefix, config["LIBEXECDIR"])
    services = root / "services"
    services.mkdir()
    # Conventional daemon activation honors Exec, ignoring SystemdService.
    # Only the executable is remapped from its installation path to this build.
    text = descriptor.read_text().replace("Exec=" + config["LIBEXECDIR"] + "/aerial-companion",
                                          'Exec="' + str(source / "companion/aerial-companion") + '"')
    (services / descriptor.name).write_text(text)
    profile = root / "profile"
    env = dict(os.environ, AERIAL_LOCK_SHELL=str(source))
    for key, leaf in (("XDG_CONFIG_HOME", "config"), ("XDG_CACHE_HOME", "cache"),
                      ("XDG_STATE_HOME", "state"), ("XDG_RUNTIME_DIR", "runtime")):
        path = profile / leaf
        path.mkdir(mode=0o700, parents=True)
        env[key] = str(path)
    settings = profile / "config/aerial-lock/config.json"
    settings.parent.mkdir()
    settings.write_text('{"sources":[],"acquisition":{"automatic":{"enabled":false}}}')
    busconfig = root / "bus.conf"
    busconfig.write_text('<busconfig><type>session</type><auth>EXTERNAL</auth><listen>unix:path=' + str(root / "bus")
                         + '</listen><servicedir>' + str(services) + '</servicedir>'
                         '<policy context="default"><allow user="*"/><allow send_destination="*"/>'
                         '<allow receive_sender="*"/><allow own="*"/></policy></busconfig>')
    with (root / "bus.log").open("w") as log:
        bus = subprocess.Popen(["dbus-daemon", "--config-file=" + str(busconfig), "--nofork", "--nopidfile", "--print-address=1"],
                               env=env, stdout=subprocess.PIPE, stderr=log)
        try:
            assert select.select([bus.stdout], [], [], 5)[0], "private bus address missing"
            address = bus.stdout.readline().decode().strip()
            assert address.startswith("unix:"), address
            env["DBUS_SESSION_BUS_ADDRESS"] = address
            reply = subprocess.run(["busctl", "--address=" + address, "call", "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                    "org.freedesktop.DBus", "StartServiceByName", "su", "org.aeriallock.Companion1", "0"],
                                   env=env, capture_output=True, text=True, timeout=10)
            assert reply.returncode == 0 and reply.stdout.strip() == "u 1", reply.stdout + reply.stderr + (root / "bus.log").read_text()
            status = subprocess.run([str(source / "cli/aerialctl"), "status", "--json"], env=env,
                                    capture_output=True, text=True, timeout=10)
            assert status.returncode == 0, status.stderr
            snapshot = json.loads(status.stdout)
            assert snapshot["State"] == "paused" and snapshot["ReadyClips"] == 0, snapshot
            pid = subprocess.check_output(["busctl", "--address=" + address, "call", "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                          "org.freedesktop.DBus", "GetConnectionUnixProcessID", "s", "org.aeriallock.Companion1"], text=True)
            service_pid = int(pid.split()[1])
        finally:
            bus.terminate()
            bus.wait(timeout=10)
            if "service_pid" in locals():
                deadline = time.monotonic() + 10
                while Path(f"/proc/{service_pid}").exists() and time.monotonic() < deadline:
                    if Path(f"/proc/{service_pid}/stat").read_text().split()[2] == "Z":
                        break
                    time.sleep(0.025)
                else:
                    assert not Path(f"/proc/{service_pid}").exists(), "activated owner survived bus loss"
    print("COMPANION_INSTALL_PASS staged_unit relative_vendor_link descriptor private_dbus_exec_activation bus_loss_exit")
