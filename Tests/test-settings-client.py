#!/usr/bin/env python3
"""Private bus with a harmless activation marker; no desktop services."""
import os
from pathlib import Path
import select
import shlex
import subprocess
import sys
import tempfile
from private_runtime import bus_config

with tempfile.TemporaryDirectory(prefix="settings-client-") as temporary:
    root = Path(temporary)
    services = root / "services"
    services.mkdir()
    marker = root / "activation"
    activator = root / "activate.sh"
    activator.write_text("#!/bin/sh\ntouch " + shlex.quote(str(marker)) + "\nexit 1\n")
    (services / "org.aeriallock.Aerial1.service").write_text(
        "[D-BUS Service]\nName=org.aeriallock.Aerial1\nExec=/bin/sh " + shlex.quote(str(activator)) + "\n")
    config = root / "bus.conf"
    config.write_text(bus_config(root / "bus", [services]))
    bus = subprocess.Popen(["dbus-daemon", "--config-file=" + str(config), "--nofork", "--nopidfile", "--print-address=1"],
                           stdout=subprocess.PIPE)
    try:
        if not select.select([bus.stdout], [], [], 5)[0]: raise RuntimeError("bus address absent")
        address = bus.stdout.readline().decode().strip()
        if not address.startswith("unix:"): raise RuntimeError("invalid bus address: " + address)
        env = dict(os.environ, DBUS_SESSION_BUS_ADDRESS=address, DBUS_SYSTEM_BUS_ADDRESS=address,
                   AERIAL_ACTIVATION_MARKER=str(marker), AERIAL_TEST_PYTHON=sys.executable)
        result = subprocess.run(sys.argv[1:], env=env, capture_output=True, text=True, timeout=120)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        if result.returncode != 0: raise RuntimeError("probe failed: " + str(result.returncode))
        if "QWARN" in result.stdout + result.stderr: raise RuntimeError("unexpected Qt warning")
    finally:
        bus.terminate()
        bus.wait(timeout=5)
