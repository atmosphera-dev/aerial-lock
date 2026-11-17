#!/usr/bin/env python3
"""Normal/emergency lifecycle against explicit compile-time children, no lock."""
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time
from private_runtime import PrivateRuntime

binary = Path(sys.argv[1]).resolve(strict=True)


def wait(predicate, label, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.02)
    raise AssertionError("timed out: " + label)


with tempfile.TemporaryDirectory(prefix="emergency-supervisor-") as directory:
    root = Path(directory)
    for name, normal, emergency, expected, normal_count, emergency_count in (
        ("start72", [72], [0], 0, 1, 1),
        ("start126", [126], [0], 0, 1, 1),
        ("start127", [127], [0], 0, 1, 1),
        ("failed-exec", None, [0], 0, 1, 1),
        ("normal-crashes", [70, 70, 70], [70, 70, 0], 0, 3, 3),
        ("emergency-exhaustion", [70, 70, 70], [70, 70, 70], 2, 3, 3),
        ("refused", [72], [4], 3, 1, 1),
        ("unavailable", [72], [72], 3, 1, 1),
        ("clean-normal", [0], [], 0, 1, 0),
    ):
        case = root / name
        case.mkdir()
        with PrivateRuntime("wayland") as runtime:
            listener = socket.socket(socket.AF_UNIX)
            listener.bind(str(runtime / "wayland"))
            listener.listen(32)
            runner = case / "normal"
            if normal is not None:
                runner.write_text("#!/usr/bin/python3\nfrom pathlib import Path\nimport json\n"
                                  f"p=Path({str(case / 'normal-count')!r})\n"
                                  "n=int(p.read_text()) if p.exists() else 0\np.write_text(str(n+1))\n"
                                  f"raise SystemExit({normal!r}[n])\n")
                runner.chmod(0o700)
            bundle = case / "bundle"
            (bundle / "Config").mkdir(parents=True)
            (bundle / "Config/defaults.json").write_text(json.dumps(dict(pamService="custom literal service", pamWatchdogTimeoutMs=987)))
            counter = case / "emergency-count"
            arguments = case / "arguments.json"
            env = {key: value for key, value in os.environ.items() if key not in (
                "HYPRLAND_INSTANCE_SIGNATURE", "AERIAL_LOCK_VIDEO_POOL", "WAYLAND_SOCKET", "AERIAL_LOCK_PAM_SERVICE")}
            env.update(WAYLAND_DISPLAY=str(runtime / "wayland"), XDG_RUNTIME_DIR=str(runtime),
                       AERIAL_LOCK_RUNNER=str(runner), AERIAL_LOCK_SHELL=str(bundle),
                       XDG_CONFIG_HOME=str(case / "empty-config"), AERIAL_TEST_EMERGENCY_MODE="sequence",
                       AERIAL_TEST_EMERGENCY_CODES=json.dumps(emergency), AERIAL_TEST_EMERGENCY_COUNTER=str(counter),
                       AERIAL_TEST_EMERGENCY_ARGUMENTS=str(arguments))
            result = subprocess.run(["dbus-run-session", "--", str(binary)], env=env,
                                    capture_output=True, text=True, timeout=20)
            output = result.stdout + result.stderr
            assert result.returncode == expected, (name, result.returncode, output)
            assert output.count("launching locker, attempt") == normal_count, (name, output)
            assert output.count("launching emergency locker, attempt") == emergency_count, (name, output)
            assert output.count("EMERGENCY_STUB_STDERR") == emergency_count, output
            if emergency_count:
                assert int(counter.read_text()) == emergency_count
                assert json.loads(arguments.read_text())["argv"] == ["custom literal service", "987"]
                transition = output.index("switching to phase=emergency")
                assert "launching locker, attempt" not in output[transition:], output
            listener.close()
        print("SUPERVISOR_EMERGENCY_PASS", name, flush=True)

    # Same PID-checked readiness, duplicate waiting and explicit release while
    # the emergency child owns the lifecycle. No fixture can run a real client.
    case = root / "channel"
    case.mkdir()
    with PrivateRuntime("wayland", "bus") as runtime:
        listener = socket.socket(socket.AF_UNIX)
        listener.bind(str(runtime / "wayland"))
        listener.listen(32)
        runner = case / "normal"
        runner.write_text("#!/usr/bin/python3\nimport os,socket,sys,time\nfrom pathlib import Path\n"
                          f"sys.path.insert(0, {str(Path(__file__).resolve().parent)!r})\nfrom fixture_files import publish\n"
                          f"root=Path({str(case)!r})\n"
                          "if '--emergency-stub' not in sys.argv: raise SystemExit(72)\n"
                          "publish(root/'pid', str(os.getpid()))\n"
                          "while not (root/'engage').exists(): time.sleep(.01)\n"
                          "s=socket.socket(socket.AF_UNIX); s.connect(os.environ['AERIAL_LOCK_ENGAGEMENT_SOCKET']); s.sendall(b'locked\\n')\n"
                          "data=s.makefile('rb').readline(); assert data==b'release\\n'\n"
                          "publish(root/'release', data)\n")
        runner.chmod(0o700)
        bus = subprocess.Popen(["dbus-daemon", "--session", "--nofork", "--print-address=1", "--address=unix:path=" + str(runtime / "bus")], stdout=subprocess.PIPE, text=True)
        address = bus.stdout.readline().strip()
        env = {key: value for key, value in os.environ.items() if key not in ("HYPRLAND_INSTANCE_SIGNATURE", "AERIAL_LOCK_VIDEO_POOL", "WAYLAND_SOCKET")}
        env.update(DBUS_SESSION_BUS_ADDRESS=address, WAYLAND_DISPLAY=str(runtime / "wayland"), XDG_RUNTIME_DIR=str(runtime),
                   AERIAL_LOCK_RUNNER=str(runner), AERIAL_LOCK_SHELL=str(case), AERIAL_TEST_EMERGENCY_MODE="runner")
        supervisor = subprocess.Popen([str(binary)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        duplicate = None
        try:
            wait(lambda: (case / "pid").exists(), "emergency direct child")
            pid = int((case / "pid").read_text())
            service = "org.aeriallock.Supervisor.s" + hashlib.sha256(os.path.realpath(runtime / "wayland").encode()).hexdigest()[:32]

            def state():
                return subprocess.check_output(["busctl", "--address=" + address, "call", service,
                    "/org/aeriallock/Supervisor", "org.aeriallock.Supervisor", "State"], text=True, timeout=5).strip()

            assert state() == 's "engaging"'
            foreign = socket.socket(socket.AF_UNIX)
            foreign.connect(str(runtime / "aerial-lock" / f"supervisor-{supervisor.pid}.sock"))
            foreign.sendall(b"locked\n")
            duplicate = subprocess.Popen([str(binary)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            time.sleep(0.25)
            assert state() == 's "engaging"' and duplicate.poll() is None
            foreign.close()
            (case / "engage").touch()
            wait(lambda: state() == 's "locked"', "emergency current-PID confirmation")
            assert duplicate.wait(5) == 0, duplicate.stdout.read()
            assert Path(f"/proc/{pid}").exists()
            supervisor.send_signal(signal.SIGUSR1)
            assert supervisor.wait(5) == 0
            output = supervisor.stdout.read()
            assert (case / "release").read_bytes() == b"release\n"
            assert output.count("launching locker, attempt") == 1 and output.count("launching emergency locker, attempt") == 1
            assert "ignored an engagement report from pid" in output and "release delivered to the locker" in output
            assert not Path(f"/proc/{pid}").exists()
        finally:
            if supervisor.poll() is None:
                (case / "engage").touch()
                supervisor.send_signal(signal.SIGUSR1)
                supervisor.wait(10)
            if duplicate is not None and duplicate.poll() is None:
                duplicate.wait(15)
            bus.terminate()
            bus.wait(5)
            listener.close()
        print("SUPERVISOR_EMERGENCY_PASS direct-pid foreign-rejected duplicate explicit-release natural-exit", flush=True)
