#!/usr/bin/python3
"""Compile-time emergency consumer fixture, never installed."""
import os
import signal
import sys
import time

mode = sys.argv[1]
if mode == "timeout":
    signal.pause()
if mode == "visible":
    print("P1 51", flush=True)
else:
    print("P0 51", flush=True)
answer = sys.stdin.buffer.readline()
if answer != (b"R c3a9\n" if mode == "mixed-locale" else b"R 61\n"):
    sys.exit(2)
if mode == "second":
    print("P0 51", flush=True)
    sys.stdin.buffer.readline()
elif mode == "extra":
    print("D success\nD success", flush=True)
elif mode == "truncated":
    sys.stdout.write("D success")
elif mode == "crash":
    print("D success", flush=True)
    sys.exit(1)
elif mode == "failed":
    print("D failed", flush=True)
else:
    time.sleep(0.05)
    print("D success", flush=True)
