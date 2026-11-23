#!/usr/bin/env python3
"""Settings Qt probes: fail on warnings anywhere, not just the last test case."""
import argparse
import os
import subprocess
import sys
from private_runtime import PrivateRuntime, bus_config
from settings_native_logs import QT_LOG_ENV, inspect_text, parse_qt_version

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--qt-version", required=True, type=parse_qt_version)
parser.add_argument("command", nargs=argparse.REMAINDER)
args = parser.parse_args()
command = args.command[1:] if args.command[:1] == ["--"] else args.command
if not command:
    parser.error("a settings test command is required")

# Protocol peers must not activate a real portal or user manager from the
# distribution's service directories when a peer intentionally goes away.
with PrivateRuntime("bus") as runtime:
    config = runtime / "bus.conf"
    config.write_text(bus_config(runtime / "bus"))
    environment = dict(os.environ, **QT_LOG_ENV, QT_NO_XDG_DESKTOP_PORTAL="1")
    result = subprocess.run(["dbus-run-session", "--config-file=" + str(config), "--", *command], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=environment)
text = result.stdout.decode(errors="replace")
print(text, end="")
_, unexpected = inspect_text(text, args.qt_version)
sys.exit(result.returncode or int(bool(unexpected)))
