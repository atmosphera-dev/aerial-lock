#!/usr/bin/env python3
"""Real recovery against an owned headless Sway; run as a disposable VM user."""
import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time


def wait_for(probe, description):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        result = probe()
        if result:
            return result
        time.sleep(0.02)
    raise AssertionError(description)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bindir", type=Path, default=Path("/usr/bin"))
    args = parser.parse_args()
    assert os.geteuid() != 0 and os.environ["TMPDIR"]
    with tempfile.TemporaryDirectory(prefix="unlock-") as scratch:
        root = Path(scratch)
        env = dict(os.environ, XDG_RUNTIME_DIR=scratch, WLR_BACKENDS="headless",
                   WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1",
                   AERIAL_LOCK_RUNNER=str(root / "missing-runner"),
                   XDG_CONFIG_HOME=str(root / "config"), XDG_CACHE_HOME=str(root / "cache"))
        for key in ("WAYLAND_SOCKET", "WAYLAND_DISPLAY", "DISPLAY", "SWAYSOCK",
                    "NIRI_SOCKET", "HYPRLAND_INSTANCE_SIGNATURE"):
            env.pop(key, None)
        config = root / "sway.conf"
        config.write_text("output HEADLESS-1 mode 1024x768\nxwayland disable\n")
        compositor = subprocess.Popen(["sway", "-c", str(config)], env=env,
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        decoy = subprocess.Popen(["aerial-lock-development-shell", "60"], executable="/usr/bin/sleep")
        supervisor = None
        unreported = None
        try:
            sock = wait_for(lambda: next((p for p in root.glob("wayland-*") if p.is_socket()), None),
                            "private Sway did not start")
            env["WAYLAND_DISPLAY"] = str(sock)
            binary = str(args.bindir.resolve() / "aerial-unlock")

            def unlock(*opts):
                return subprocess.run([binary, *opts], env=env, capture_output=True, text=True, timeout=12)

            def launch(n):
                log = root / f"supervisor-{n}.log"
                with log.open("w") as output:
                    proc = subprocess.Popen([str(args.bindir.resolve() / "aerial-lock-supervisor")],
                                            env=env, stdout=output, stderr=subprocess.STDOUT)
                return proc, log

            supervisor, log = launch(1)
            wait_for(lambda: "reason=compositor-confirmed" in log.read_text(), "first lock not confirmed")
            refused = unlock()
            assert refused.returncode == 3, refused.stderr
            assert not re.search(rf"\bpid {decoy.pid}\b", refused.stderr), refused.stderr
            children = Path(f"/proc/{supervisor.pid}/task/{supervisor.pid}/children")
            (holder,) = children.read_text().split()
            assert re.search(rf"\bpid {holder}\b", refused.stderr), refused.stderr
            supervisor.send_signal(signal.SIGUSR1)
            assert supervisor.wait(timeout=10) == 0, log.read_text()
            assert "release-confirmed" in log.read_text(), log.read_text()

            # A genuine lock with no supervisor confirmation is still not a
            # purge candidate, even when its executable is an Aerial binary.
            with (root / "unreported.log").open("w+") as output:
                unreported = subprocess.Popen(["/usr/libexec/aerial-lock/aerial-lock-emergency", "login", "1000"],
                                              env=env, stdout=output, stderr=subprocess.STDOUT)
                wait_for(lambda: "lock-state: lock-confirmed" in Path(output.name).read_text(),
                         "unsupervised locker did not engage")
                refusal = unlock("--purge")
                assert refusal.returncode == 3 and "no confirmed Aerial holder" in refusal.stderr, refusal.stderr
                assert unreported.poll() is None and decoy.poll() is None
                unreported.terminate()
                unreported.wait(timeout=5)
                dead = unlock()
                assert dead.returncode == 0 and "took over the lock and released it" in dead.stderr, dead.stderr

            supervisor, log = launch(2)
            wait_for(lambda: "reason=compositor-confirmed" in log.read_text(), "second lock not confirmed")
            purged = unlock("--purge")
            assert purged.returncode == 0 and "took over the lock and released it" in purged.stderr, purged.stderr
            assert not re.search(rf"\bpid {decoy.pid}\b", purged.stderr), purged.stderr
            assert decoy.poll() is None, "--purge stopped the unrelated decoy"
            supervisor.wait(timeout=10)
            assert log.read_text().count("launching emergency locker") == 1, log.read_text()
            print("RECOVERY_PASS: SIGUSR1, unknown-holder refusal, dead-lock takeover, confirmed purge, decoy alive")
        finally:
            # This compositor and both fixtures are private to the test. Close
            # Wayland before reaping an outstanding supervisor/locker pair.
            compositor.terminate()
            compositor.wait(timeout=5)
            if decoy.poll() is None:
                decoy.terminate()
            decoy.wait(timeout=5)
            if unreported is not None:
                unreported.wait(timeout=5)
            if supervisor is not None:
                supervisor.wait(timeout=10)


if __name__ == "__main__":
    main()
