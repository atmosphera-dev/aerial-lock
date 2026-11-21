#!/usr/bin/env python3
"""Settings Qt probes: fail on warnings anywhere, not just the last test case."""
import os
import re
import subprocess
import sys
from private_runtime import PrivateRuntime, bus_config

# Protocol peers must not activate a real portal or user manager from the
# distribution's service directories when a peer intentionally goes away.
with PrivateRuntime("bus") as runtime:
    config = runtime / "bus.conf"
    config.write_text(bus_config(runtime / "bus"))
    result = subprocess.run(["dbus-run-session", "--config-file=" + str(config), "--", *sys.argv[1:]], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=os.environ)
text = result.stdout.decode(errors="replace")
print(text, end="")
sys.exit(result.returncode or int(bool(re.search(r"^QWARN\s*:|QQmlApplicationEngine failed|ReferenceError:|TypeError:", text, re.M))))
