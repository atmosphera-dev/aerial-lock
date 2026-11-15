#!/usr/bin/env python3
"""Native transport fixture. Not used by production QML or its helper."""
import os
import signal
import sys
import time

mode = sys.argv[1]
if mode == "crash":
    os.kill(os.getpid(), signal.SIGKILL)
if mode == "hang":
    while True:
        signal.pause()
if mode == "success-hang":
    print("D success", flush=True)
    while True:
        signal.pause()
if mode == "truncated":
    sys.stdout.write("D success")
elif mode == "success-crash":
    print("D success", flush=True)
    sys.exit(1)
elif mode == "extra":
    print("D success\nD success", flush=True)
elif mode == "malformed":
    print("P0 0z", flush=True)
elif mode == "oversize":
    print("I " + "61" * 4096, flush=True)
else:
    for frame in ("I 546f7563682073656e736f72", "E 54727920616761696e", "P0 50617373776f72643a"):
        # Deliberate fragmentation exercises arbitrary transport boundaries.
        for char in frame + "\n":
            os.write(1, char.encode())
        time.sleep(0.01)
    answer = sys.stdin.buffer.readline()
    if not answer.startswith(b"R "):
        sys.exit(2)
    print("P1 4368616c6c656e67653a", flush=True)
    sys.stdin.buffer.readline()
    print("D " + mode, flush=True)
