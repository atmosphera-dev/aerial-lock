#!/usr/bin/env python3
"""Transient bus denial must not strand systemd discovery until owner change."""
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile

from private_runtime import bus_config

with tempfile.TemporaryDirectory(prefix="settings-discovery-") as temporary:
    root = Path(temporary)
    config = root / "bus.conf"
    denial = '<deny send_destination="org.freedesktop.DBus" send_interface="org.freedesktop.DBus" send_member="GetNameOwner"/>'
    text = bus_config(root / "bus")
    assert text.count('</policy>') == 1
    config.write_text(text.replace('</policy>', denial + '</policy>'))
    with (root / "bus.log").open('w+') as log:
        bus = subprocess.Popen(['dbus-daemon', '--nofork', '--nopidfile', '--print-address=1', '--config-file=' + str(config)], stdout=subprocess.PIPE, stderr=log)
        try:
            assert select.select([bus.stdout], [], [], 5)[0], 'private bus did not publish its address'
            address = bus.stdout.readline().decode().strip()
            log.flush(); log.seek(0)
            assert address.startswith('unix:'), address + log.read()
            env = dict(os.environ, DBUS_SESSION_BUS_ADDRESS=address, DBUS_SYSTEM_BUS_ADDRESS=address, AERIAL_TEST_DISCOVERY_CONFIG=str(config))
            result = subprocess.run([sys.argv[1], 'discoveryRetriesWithoutOwnerChange'], env=env, capture_output=True, text=True, timeout=30)
            print(result.stdout, end=''); print(result.stderr, end='', file=sys.stderr)
            assert result.returncode == 0 and 'QWARN' not in result.stdout + result.stderr, 'discovery probe failed'
        finally:
            bus.terminate(); bus.wait(timeout=5)
