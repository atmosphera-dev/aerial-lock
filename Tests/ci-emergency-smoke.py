#!/usr/bin/env python3
"""Installed native emergency PAM smoke. No product bypass or preview mode."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import time

spec = importlib.util.spec_from_file_location("emergency_observer", Path(__file__).with_name("remediation-driver.py"))
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


def run_session(root, service, password, binary="/usr/libexec/aerial-lock/aerial-lock-emergency", desktop_probe=None):
    """Caller owns the VM/account/PAM fixture; credentials never enter argv."""
    assert os.geteuid() != 0 and password
    root = Path(root).resolve()
    assert not root.is_relative_to(Path("/tmp"))
    start = time.monotonic()
    for name in ("runtime", "tmp", "results", "config", "cache"):
        (root / name).mkdir(mode=0o700, parents=True, exist_ok=True)
    config = root / "sway.conf"
    config.write_text("output HEADLESS-1 mode 1280x800\nseat seat0 fallback true\nxwayland disable\n")
    env = dict(os.environ, XDG_RUNTIME_DIR=str(root / "runtime"), TMPDIR=str(root / "tmp"),
               XDG_CONFIG_HOME=str(root / "config"), XDG_CACHE_HOME=str(root / "cache"),
               WLR_BACKENDS="headless", WLR_HEADLESS_OUTPUTS="1", WLR_RENDERER="pixman",
               LIBGL_ALWAYS_SOFTWARE="1", QSG_RHI_BACKEND="opengl", QT_QPA_PLATFORM="wayland")
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "SWAYSOCK", "NIRI_SOCKET",
                "HYPRLAND_INSTANCE_SIGNATURE", "QML_IMPORT_PATH", "QT_QUICK_BACKEND", "WAYLAND_DEBUG",
                "AERIAL_LOCK_ENGAGEMENT_SOCKET", "AERIAL_LOCK_RUNTIME_DIR", "AERIAL_LOCK_PAM_SERVICE"):
        env.pop(key, None)
    compositor = driver.ObservedProcess(["sway", "-c", str(config)], env, root / "results/compositor.log")
    locker = keyboard = probe = None
    wrong = secrets.token_hex(16)
    try:
        deadline = time.monotonic() + 15
        ipc = root / "runtime" / f"sway-ipc.{os.getuid()}.{compositor.process.pid}.sock"
        while True:
            sockets = [p for p in (root / "runtime").glob("wayland-*") if p.is_socket()]
            if len(sockets) == 1 and ipc.is_socket():
                env.update(WAYLAND_DISPLAY=sockets[0].name, SWAYSOCK=str(ipc))
                break
            if compositor.process.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("emergency Sway socket readiness failed")
            time.sleep(0.02)
        keyboard = subprocess.Popen(["wtype", "-s", "90000"], env=env)
        deadline = time.monotonic() + 5
        while True:
            devices = json.loads(subprocess.check_output(["swaymsg", "-r", "-t", "get_inputs"], env=env, timeout=5))
            if any(d["type"] == "keyboard" for d in devices):
                break
            if keyboard.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("emergency virtual keyboard readiness failed")
            time.sleep(0.02)
        observed_env = dict(env, WAYLAND_DEBUG="1")  # observer filters all input before persistence
        locker = driver.ObservedProcess([str(binary), service, "60000"], observed_env, root / "results/emergency.log")
        locker.wait_line(lambda line: "lock-state: lock-confirmed" in line)
        locker.wait_line(lambda line: "lock-state: surface-submitted" in line)
        locker.wait_line(lambda line: "ext_session_lock_v1" in line and ".locked()" in line)

        def submit(value):
            subprocess.run(["wtype", "-d", "10", "-"], input=value, text=True, env=env, check=True, timeout=10)
            subprocess.run(["wtype", "-k", "Return"], env=env, check=True, timeout=10)

        submit(wrong)
        rejected = locker.wait_line(lambda line: "lock-state: auth-failed" in line, 20)
        assert locker.process.poll() is None
        assert not any(".unlock_and_destroy()" in line for _, line in tuple(locker.lines))
        # Wait until the failed owned worker has retired, not just its error log.
        # An empty Return opens a fresh prompt; following text must answer it.
        subprocess.run(["wtype", "-s", "150", "-k", "Return"], env=env, check=True, timeout=10)
        prompt = locker.wait_line(lambda line: "lock-state: auth-prompt" in line)
        submit(password)
        authenticated = locker.wait_line(lambda line: "lock-state: auth-success" in line, 20)
        locker.wait_line(lambda line: ".unlock_and_destroy()" in line)
        confirmed = locker.wait_line(lambda line: "lock-state: release-confirmed" in line)
        assert rejected < prompt < authenticated < confirmed
        assert locker.finish(10) == 0
        assert not any(".error(" in line for _, line in tuple(locker.lines))
        if desktop_probe:
            probe = driver.ObservedProcess([str(desktop_probe)], env, root / "results/desktop.log")
            probe.wait_line(lambda line: "DESKTOP_INPUT_READY" in line)
            subprocess.run(["wtype", "-k", "F12"], env=env, check=True, timeout=10)
            probe.wait_line(lambda line: "DESKTOP_INPUT_RECEIVED" in line)
            assert probe.finish(5) == 0
        outcome = dict(status="PASS", real_pam=True, rejected=True, empty_return_prompt=True,
                       unlock_and_destroy=True, sync_confirmed=True, natural_exit=0,
                       desktop_input=bool(desktop_probe), seconds=time.monotonic() - start)
        (root / "results/outcome.json").write_text(json.dumps(outcome, indent=2) + "\n")
        print("EMERGENCY_SMOKE_PASS " + json.dumps(outcome), flush=True)
        return outcome
    finally:
        if keyboard is not None:
            keyboard.terminate()
            keyboard.wait(timeout=5)
        # A failed live locker is retained, not unlocked/killed into a PASS.
        if locker is None or locker.process.poll() is not None:
            compositor.process.terminate()
            compositor.finish(5)
        for path in (root / "results").glob("*.log"):
            payload = path.read_text()
            if password in payload or wrong in payload:
                raise RuntimeError("fixture credential leak; do not export")


def prepare():
    if os.geteuid() != 0 or not any(Path(p).exists() for p in ("/run/.containerenv", "/.dockerenv")):
        raise RuntimeError("prepare only in a disposable CI container")
    root = Path("/work/emergency")
    subprocess.run(["useradd", "--create-home", "--home-dir", str(root), "aerial-emergency-ci"], check=True, timeout=10)
    password = secrets.token_hex(16)
    subprocess.run(["chpasswd"], input=f"aerial-emergency-ci:{password}\n", text=True, check=True, timeout=10)
    Path("/etc/pam.d/aerial-emergency-ci").write_text("auth required pam_unix.so\n")
    return subprocess.run(["runuser", "-u", "aerial-emergency-ci", "--", "dbus-run-session", "--",
                           sys.executable, str(Path(__file__).resolve()), "--session"],
                          input=password, text=True, timeout=100).returncode


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--session", action="store_true")
    args = parser.parse_args()
    if args.session:
        run_session("/work/emergency", "aerial-emergency-ci", sys.stdin.read())
    else:
        raise SystemExit(prepare())
