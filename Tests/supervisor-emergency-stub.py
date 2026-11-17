#!/usr/bin/python3
"""Compile-time supervisor test child. Never installed or found through PATH."""
import json
import os
from pathlib import Path
import socket
import sys

mode = os.environ.get("AERIAL_TEST_EMERGENCY_MODE", "crash")
arguments = os.environ.get("AERIAL_TEST_EMERGENCY_ARGUMENTS")
if arguments:
    Path(arguments).write_text(json.dumps(dict(pid=os.getpid(), argv=sys.argv[1:])))
counter = os.environ.get("AERIAL_TEST_EMERGENCY_COUNTER")
ordinal = 0
if counter:
    path = Path(counter)
    ordinal = int(path.read_text()) if path.exists() else 0
    path.write_text(str(ordinal + 1))
print("EMERGENCY_STUB_ATTEMPT", ordinal + 1, flush=True)
print("EMERGENCY_STUB_STDERR", file=sys.stderr, flush=True)
if mode == "runner":
    os.execv(os.environ["AERIAL_LOCK_RUNNER"], [os.environ["AERIAL_LOCK_RUNNER"], "--emergency-stub"])
if mode == "release":
    stream = socket.socket(socket.AF_UNIX)
    stream.connect(os.environ["AERIAL_LOCK_ENGAGEMENT_SOCKET"])
    stream.sendall(b"locked\n")
    assert stream.makefile("rb").readline() == b"release\n"
    sys.exit(0)
if mode == "refuse":
    sys.exit(4)
if mode == "unavailable":
    sys.exit(72)
if mode == "sequence":
    sys.exit(json.loads(os.environ["AERIAL_TEST_EMERGENCY_CODES"])[ordinal])
sys.exit(70)
