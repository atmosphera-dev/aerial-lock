#!/usr/bin/env python3
"""Real supervisor retains one local generation/pin through normal respawns.

Stub children exit naturally on fixture commands. No compositor lock, qs or
credentials are used; the native facade's consumption is covered separately.
"""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import tempfile
import time

from private_runtime import PrivateRuntime
from fixture_files import publish


def wait_for(predicate, description):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise AssertionError("timed out: " + description)


with tempfile.TemporaryDirectory(prefix="companion-episode-") as temporary, PrivateRuntime("bus", "wayland") as runtime:
    root = Path(temporary)
    cache = root / "cache/aerial-lock/managed-v1"
    (cache / "objects").mkdir(parents=True)
    (cache / "generations").mkdir()
    (cache / "playback.lock").touch(mode=0o600)
    marks = root / "marks"
    marks.mkdir()
    controls = root / "controls"
    controls.mkdir()
    listener = socket.socket(socket.AF_UNIX)
    listener.bind(str(runtime / "wayland"))
    listener.listen(16)
    runner = root / "runner"
    runner.write_text("#!/usr/bin/python3\n"
        "import json, os, time\nfrom pathlib import Path\n"
        f"import sys\nsys.path.insert(0, {str(Path(__file__).resolve().parent)!r})\n"
        "from fixture_files import publish\n"
        f"marks, controls = Path({str(marks)!r}), Path({str(controls)!r})\n"
        "ordinal = len(list(marks.iterdir())) + 1\n"
        "publish(marks / str(ordinal), json.dumps({k: os.environ.get(k) for k in "
        "['AERIAL_LOCK_CONTENT_ROOT', 'AERIAL_LOCK_CONTENT_GENERATION']}))\n"
        "while not (controls / str(ordinal)).exists(): time.sleep(0.02)\n"
        "raise SystemExit(int((controls / str(ordinal)).read_text()))\n")
    runner.chmod(0o700)
    bus = subprocess.Popen(["dbus-daemon", "--session", "--nofork", "--print-address=1",
        "--address=unix:path=" + str(runtime / "bus")], stdout=subprocess.PIPE, text=True)
    processes = []

    def generation(names):
        entries = []
        for name in names:
            key = hashlib.sha256(name.encode()).hexdigest()
            relative = f"objects/{key}.media"
            path = cache / relative
            if not path.exists():
                path.write_bytes(b"complete-fixture")
                path.chmod(0o600)
            st = path.stat()
            entries.append(dict(key=key, sourceId="fixture", assetId=name, path=relative, bytes=st.st_size,
                identity=dict(device=st.st_dev, inode=st.st_ino, seconds=st.st_mtime_ns // 10**9,
                    nanoseconds=st.st_mtime_ns % 10**9)))
        raw = json.dumps(dict(version=1, entries=entries), separators=(",", ":")).encode()
        key = hashlib.sha256(raw).hexdigest()
        publish(cache / f"generations/{key}.json", raw.decode())
        publish(cache / "current.json", json.dumps(dict(version=1, generation=key)))
        return key

    def release(number, code=0):
        publish(controls / str(number), str(code))

    def record(number):
        path = marks / str(number)
        wait_for(path.exists, f"child {number}")
        return json.loads(path.read_text())

    def pin_held():
        with (cache / "playback.lock").open("r+") as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                return True
            return False

    try:
        assert select.select([bus.stdout], [], [], 10)[0], "private bus did not start"
        address = bus.stdout.readline().strip()
        assert address
        env = {k: v for k, v in os.environ.items() if k not in (
            "HYPRLAND_INSTANCE_SIGNATURE", "AERIAL_LOCK_VIDEO_POOL", "WAYLAND_SOCKET", "AERIAL_LOCK_VIDEO")}
        env.update(DBUS_SESSION_BUS_ADDRESS=address, XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY="wayland",
            XDG_CACHE_HOME=str(root / "cache"), AERIAL_LOCK_RUNNER=str(runner), AERIAL_LOCK_SHELL=str(root),
            AERIAL_LOCK_PLUGIN_ROOT=str(root), AERIAL_LOCK_CONTENT_GENERATION="inherited-wrong",
            AERIAL_LOCK_CONTENT_ROOT="inherited-wrong")

        def start():
            process = subprocess.Popen([sys.argv[1]], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            processes.append(process)
            return process

        old = generation(["first"])
        first = start()
        initial = record(1)
        assert initial == dict(AERIAL_LOCK_CONTENT_ROOT=str(cache), AERIAL_LOCK_CONTENT_GENERATION=old), initial
        new = generation(["first", "second"])
        assert pin_held(), "the supervisor must pin the entire frozen generation"
        release(1, 70)
        assert record(2) == initial, "normal respawn recaptured current.json"
        assert pin_held(), "the supervisor lost its shared pin during client respawn"
        release(2)
        assert first.wait(15) == 0, first.stdout.read()
        assert not pin_held(), "episode end must release its shared pin"
        second = start()
        assert record(3)["AERIAL_LOCK_CONTENT_GENERATION"] == new, "next episode did not see publication"
        release(3)
        assert second.wait(15) == 0, second.stdout.read()
        # A contended/empty initial snapshot is also final for that episode.
        with (cache / "playback.lock").open("r+") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            empty = start()
            assert record(4)["AERIAL_LOCK_CONTENT_GENERATION"] == ""
        release(4, 70)
        assert record(5)["AERIAL_LOCK_CONTENT_GENERATION"] == "", "empty snapshot changed on respawn"
        release(5)
        assert empty.wait(15) == 0, empty.stdout.read()
        print("COMPANION_EPISODE_PASS supervisor_pin respawn_same_generation next_episode_new_generation contended_empty inherited_overrides_replaced")
    finally:
        # Only these non-locking stub processes are ours. Ask them to exit,
        # retaining logs on a failure; never terminate a real locker here.
        for number in range(1, 16):
            release(number)
        for process in processes:
            if process.poll() is None:
                process.wait(20)
        bus.terminate()
        bus.wait(10)
        listener.close()
