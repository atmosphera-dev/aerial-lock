#!/usr/bin/env python3
"""One real lock/PAM cycle, only inside a disposable CI container."""
import importlib.util
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import time

spec = importlib.util.spec_from_file_location("remediation", Path(__file__).with_name("remediation-driver.py"))
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


def prepare():
    if os.geteuid() != 0:
        raise RuntimeError("run only as root in a fresh disposable CI container")
    subprocess.run(["useradd", "--create-home", "--home-dir", "/work/user", "aerial-ci"],
                   check=True, timeout=10)
    # Generated per container, kept in memory, delivered only over stdin.
    password = secrets.token_hex(16)
    subprocess.run(["chpasswd"], input=f"aerial-ci:{password}\n", text=True, check=True, timeout=10)
    Path("/etc/pam.d/aerial-lock").write_text("auth required pam_unix.so\n")
    result = subprocess.run(["runuser", "-u", "aerial-ci", "--", "dbus-run-session", "--",
                             sys.executable, str(Path(__file__).resolve()), "--session"],
                            input=password, text=True, timeout=100)
    return result.returncode


def session():
    start = time.monotonic()
    password = sys.stdin.read()
    wrong = secrets.token_hex(16)
    assert password and password != wrong and os.geteuid() != 0
    root = Path("/work/user")
    for name in ("runtime", "tmp", "results", "config/aerial-lock", "cache"):
        (root / name).mkdir(mode=0o700, parents=True, exist_ok=True)
    (root / "config/aerial-lock/config.json").write_text(json.dumps({"sources": [], "preferredResolution": 1080}))
    config = root / "sway.conf"
    config.write_text("output HEADLESS-1 mode 1280x800\nseat seat0 fallback true\nxwayland disable\n")
    env = dict(os.environ, HOME=str(root), USER="aerial-ci", LOGNAME="aerial-ci",
               XDG_RUNTIME_DIR=str(root / "runtime"), TMPDIR=str(root / "tmp"),
               XDG_CONFIG_HOME=str(root / "config"), XDG_CACHE_HOME=str(root / "cache"),
               WLR_BACKENDS="headless", WLR_HEADLESS_OUTPUTS="1", WLR_RENDERER="pixman",
               LIBGL_ALWAYS_SOFTWARE="1", QSG_RHI_BACKEND="opengl")
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "SWAYSOCK", "NIRI_SOCKET", "HYPRLAND_INSTANCE_SIGNATURE",
                "QML_IMPORT_PATH", "QT_QUICK_BACKEND", "WAYLAND_DEBUG"):
        env.pop(key, None)
    compositor = driver.ObservedProcess(["sway", "-c", str(config)], env, root / "results/compositor.log")
    locker = None
    keyboard = None
    try:
        deadline = time.monotonic() + 15
        while True:
            sockets = [p for p in (root / "runtime").glob("wayland-*") if p.is_socket()]
            if sockets:
                assert len(sockets) == 1
                env["WAYLAND_DISPLAY"] = sockets[0].name
                break
            if compositor.process.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("headless Sway did not publish its Wayland socket")
            time.sleep(0.01)
        env["SWAYSOCK"] = str(root / "runtime" / f"sway-ipc.{os.getuid()}.{compositor.process.pid}.sock")
        # Match Session.start_sway: the headless seat must have a keyboard
        # before the lock takes focus, and keep it when submit's wtype exits.
        keyboard = subprocess.Popen(["wtype", "-s", "90000"], env=env)
        deadline = time.monotonic() + 5
        while True:
            inputs = subprocess.run(["swaymsg", "-r", "-t", "get_inputs"], env=env,
                                    capture_output=True, text=True, check=True, timeout=5)
            if any(device["type"] == "keyboard" for device in json.loads(inputs.stdout)):
                break
            if keyboard.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("headless virtual keyboard did not become ready")
            time.sleep(0.01)
        env["WAYLAND_DEBUG"] = "1"  # shared observer discards all keyboard/IME traffic
        locker = driver.ObservedProcess(["aerial-lock"], env, root / "results/locker.log")
        locker.wait_line(lambda line: "lock requested -> engaged reason=compositor-confirmed" in line)
        locker.wait_line(lambda line: "ext_session_lock_v1" in line and ".locked()" in line)
        locker.wait_line(lambda line: "state: locked" in line)
        children = Path(f"/proc/{locker.process.pid}/task/{locker.process.pid}/children").read_text().split()
        assert len(children) == 1, "supervisor must own one real locker"
        child = children[0]
        assert Path(f"/proc/{child}/comm").read_text().strip() == "qs"

        def submit(value):
            key_env = env.copy()
            key_env.pop("WAYLAND_DEBUG")
            subprocess.run(["wtype", "-d", "10", "-"], input=value, text=True,
                           env=key_env, check=True, timeout=10)
            subprocess.run(["wtype", "-k", "Return"], env=key_env, check=True, timeout=10)

        submit(wrong)
        failed = locker.wait_line(lambda line: " -> failed reason=pam-failed" in line, 20)
        assert locker.process.poll() is None and Path(f"/proc/{child}").exists()
        assert not any(".unlock_and_destroy()" in line or " -> released " in line
                       for _, line in tuple(locker.lines)), "wrong password released the lock"
        submit(password)
        success = locker.wait_line(lambda line: " -> success reason=pam-success" in line, 20)
        released = locker.wait_line(lambda line: " -> released reason=compositor-unlocked" in line)
        locker.wait_line(lambda line: ".unlock_and_destroy()" in line)
        assert failed < success < released
        assert locker.finish(15) == 0, "supervisor exit must be 0"
        assert not Path(f"/proc/{child}").exists(), "locker must exit naturally"
        locker.wait_line(lambda line: "child finished: exit=0 status=0" in line)
        locker.wait_line(lambda line: "locker running -> exiting reason=exit-code-0" in line)
        locker.wait_line(lambda line: "state: ended" in line)
        assert sum("launching locker, attempt" in line for _, line in tuple(locker.lines)) == 1
    finally:
        # Never issue an unlock or kill a failed locker to manufacture success.
        # A failed container is discarded by the caller's bounded podman run.
        if keyboard is not None:
            keyboard.terminate()
            keyboard.wait(timeout=5)
        if locker is None or locker.process.poll() is not None:
            compositor.process.terminate()
            compositor.finish(5)
        for path in sorted((root / "results").glob("*.log")):
            text = path.read_text()
            if password in text or wrong in text:
                raise RuntimeError("fixture credential leaked into retained output")
            print(f"--- {path.name} ---", flush=True)
            print(text, end="", flush=True)
    print(f"SMOKE PASS: real lock, wrong-password rejection, PAM unlock, locker=0 supervisor=0 ({time.monotonic() - start:.2f}s)")
    return 0


if __name__ == "__main__":
    if not any(Path(p).exists() for p in ("/run/.containerenv", "/.dockerenv")):
        raise RuntimeError("run only inside a disposable CI container")
    sys.exit(session() if sys.argv[1:] == ["--session"] else prepare())
