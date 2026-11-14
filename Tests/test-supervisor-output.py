#!/usr/bin/env python3
"""Child output reaches durable sinks live, without supervisor-sized buffers."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest

from private_runtime import PrivateRuntime

SUPERVISOR = Path(sys.argv.pop(1)).resolve(strict=True)

CHILD = '''#!/usr/bin/python3
import json, os, socket
s = socket.socket(socket.AF_UNIX)
s.connect(os.environ['OUTPUT_CONTROL'])
f = s.makefile('rb')
def identity(fd):
    st = os.fstat(fd)
    return [st.st_dev, st.st_ino]
s.sendall(json.dumps(dict(pid=os.getpid(), stdout=identity(1), stderr=identity(2))).encode() + b'\\n')
while True:
    line = f.readline()
    if line == b'quit\\n': break
    if not line: raise SystemExit(1)
    volume = int(line)
    for fd in (1, 2):
        left = volume
        block = b'x' * 65536
        while left:
            written = os.write(fd, block[:min(left, len(block))])
            left -= written
        os.write(fd, ('\\nCHILD_' + str(fd) + '_' + str(volume) + '\\n').encode())
    s.sendall(b'done\\n')
'''


class SupervisorOutput(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="supervisor-output-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.runtime = PrivateRuntime("wayland", "bus", "control")
        self.addCleanup(self.runtime.cleanup)
        runtime = self.runtime.path
        self.listener = socket.socket(socket.AF_UNIX)
        self.addCleanup(self.listener.close)
        self.listener.bind(str(runtime / "wayland"))
        self.listener.listen(16)
        control = socket.socket(socket.AF_UNIX)
        self.addCleanup(control.close)
        control.bind(str(runtime / "control"))
        control.listen(1)
        control.settimeout(15)
        child = self.root / "child"
        child.write_text(CHILD)
        child.chmod(0o700)
        self.bus = subprocess.Popen([
            "dbus-daemon", "--session", "--nofork", "--print-address=1",
            "--address=unix:path=" + str(runtime / "bus")], stdout=subprocess.PIPE, text=True)
        self.addCleanup(self.stop_bus)
        address = self.bus.stdout.readline().strip()
        env = {k: v for k, v in os.environ.items()
               if k not in ("HYPRLAND_INSTANCE_SIGNATURE", "AERIAL_LOCK_VIDEO_POOL", "WAYLAND_SOCKET")}
        env.update(DBUS_SESSION_BUS_ADDRESS=address, XDG_RUNTIME_DIR=str(runtime),
                   WAYLAND_DISPLAY=str(runtime / "wayland"), AERIAL_LOCK_RUNNER=str(child),
                   AERIAL_LOCK_SHELL=str(self.root), OUTPUT_CONTROL=str(runtime / "control"))
        self.stdout = self.root / "stdout"
        self.stderr = self.root / "stderr"
        with self.stdout.open("wb") as out, self.stderr.open("wb") as err:
            self.process = subprocess.Popen([str(SUPERVISOR)], env=env, stdout=out, stderr=err)
        self.channel = None
        self.addCleanup(self.stop_child)
        self.channel, _ = control.accept()
        self.channel.settimeout(30)
        self.reader = self.channel.makefile("rb")
        self.addCleanup(self.reader.close)
        self.child = json.loads(self.reader.readline())

    def stop_bus(self):
        self.bus.terminate()
        self.bus.wait(10)
        self.bus.stdout.close()

    def stop_child(self):
        if self.channel is not None:
            self.channel.sendall(b"quit\n")
            self.channel.close()
        self.assertEqual(self.process.wait(15), 0)

    def emit(self, size):
        # Completion is acknowledged only after both exact byte volumes and
        # markers have been written. The child then blocks on the next command.
        self.channel.sendall(str(size).encode() + b"\n")
        self.assertEqual(self.reader.readline(), b"done\n")
        self.assertTrue(Path(f"/proc/{self.child['pid']}").exists())
        self.assertIsNone(self.process.poll())

    def test_both_channels_reach_supervisor_sinks_while_child_is_alive(self):
        self.emit(4096)
        self.assertIn(b"CHILD_1_4096\n", self.stdout.read_bytes())
        self.assertIn(b"CHILD_2_4096\n", self.stderr.read_bytes())
        # Prove direct inheritance, not a supervisor-side copy through pipes.
        for name, path in (("stdout", self.stdout), ("stderr", self.stderr)):
            st = path.stat()
            self.assertEqual(self.child[name], [st.st_dev, st.st_ino])

    def test_supervisor_memory_does_not_scale_with_completed_output_volume(self):
        def rss():
            for line in Path(f"/proc/{self.process.pid}/status").read_text().splitlines():
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) * 1024
            self.fail("missing supervisor RSS")
        self.emit(65536)
        before = rss()
        # Finite volumes and explicit completion acknowledgements, not a timed
        # producer or sample window. Only a 64 KiB block lives in the child.
        for size in (8 * 1024 * 1024, 32 * 1024 * 1024):
            self.emit(size)
            growth = rss() - before
            self.assertLess(growth, 4 * 1024 * 1024,
                            f"supervisor RSS grew {growth} bytes with child output")
            for fd, path in ((1, self.stdout), (2, self.stderr)):
                with path.open("rb") as stream:
                    stream.seek(-80, os.SEEK_END)
                    self.assertIn(f"CHILD_{fd}_{size}\n".encode(), stream.read())


if __name__ == "__main__":
    unittest.main()
