#!/usr/bin/env python3
"""Identity-bound installed companion acceptance. Run only in an owned test VM.

The phase-A prepare/cold-load cases precede content integration. Later cases
must be implemented, not silently treated as success by a missing driver.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import pwd
import select
import subprocess
import sys
import time
from types import SimpleNamespace

from fixture_files import publish
from owned_instance import OwnedInstance, digest, session_identity


def command(argv, **kwargs):
    return subprocess.run(argv, check=True, timeout=30, **kwargs)


def reject_latches(root):
    for pattern in ("BLOCKED.json", "blocked-qs-*.json"):
        if list((root / "run").glob(pattern)):
            raise RuntimeError("persistent BLOCKED live-lock latch; inspection required")


def prepare(args):
    root = args.root.resolve(strict=True)
    if root != args.root or not root.is_relative_to(Path.home()) or root == Path.home():
        raise RuntimeError("workspace must be a private disk-backed guest-home child")
    if Path(os.environ.get("TMPDIR", "")).resolve() != root / "tmp":
        raise RuntimeError("TMPDIR must be the owned disk-backed workspace/tmp")
    if args.vm_record != root / "run/vm-record.json":
        raise RuntimeError("unexpected owned manifest path")
    reject_latches(root)
    receipt = json.loads(args.host_receipt.read_text())
    identity = receipt["identity"]
    if receipt["schema"] != 1 or receipt["status"]["phase"] != "ready":
        raise RuntimeError("requires a ready uvm-fork receipt")
    if hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest() != receipt["creation_identity_sha256"]:
        # uvm canonical encoding is confirmed by its own creation helper; do not
        # invent an attestation from guessed domain/IP/guest hostname values.
        raise RuntimeError("fork creation identity checksum mismatch")
    uuid = command(["sudo", "cat", "/sys/class/dmi/id/product_uuid"], capture_output=True, text=True).stdout.strip()
    macs = {p.read_text().strip() for p in Path("/sys/class/net").glob("*/address")}
    if uuid != identity["uuid"] or identity["mac"] not in macs:
        raise RuntimeError("receipt does not describe this guest")
    if identity["name"] == Path(identity["base"]).name:
        raise RuntimeError("base VM is not an execution target")
    # A source-host generated input inventory is a copy receipt, not a guest
    # guess about which generated files might have been tracked.
    freeze = json.loads((root / "run/source-input.json").read_text())
    host = freeze["host_attestation"]
    if host["uuid"] != uuid or host["mac"] != identity["mac"] or host["domain"] != identity["name"] or not host["host"]:
        raise RuntimeError("source host's hypervisor attestation does not match receipt/guest")
    source_files = freeze["files"]
    if not source_files:
        raise RuntimeError("source input inventory is empty")
    source = root / "src/aerial-lock"
    for relative, expected in source_files.items():
        path = source / relative
        if not path.is_relative_to(source) or ".." in Path(relative).parts:
            raise RuntimeError("source inventory escape")
        if digest(path) != expected["sha256"] or path.lstat().st_mode != expected["mode"]:
            raise RuntimeError("source transfer hash/mode mismatch: " + relative)
    source_identity = hashlib.sha256(json.dumps(source_files, sort_keys=True).encode()).hexdigest()
    if freeze["identity"] != source_identity:
        raise RuntimeError("source freeze checksum mismatch")
    paths = {"root": root, "source": source, "build": root / "build/debug/aerial-tests",
             "stage": root / "stage", "run": root / "run", "results": root / "results", "media": root / "media"}
    for path in paths.values():
        if path.resolve(strict=True) != path or path.stat().st_uid != os.getuid():
            raise RuntimeError("unowned/symlink workspace path")
    stage = paths["stage"] / "usr"
    required = [stage / "lib/aerial-lock" / name for name in
                ("aerial-lock-run", "aerial-lock-auth", "aerial-lock-emergency", "aerial-companion")]
    required += [stage / "lib/aerial-lock/qt-plugins/generic" / name for name in
                 ("libAerialLockConnectionGuard.so", "libAerialLockSurfaceInput.so")]
    required += [paths["build"] / name for name in ("desktop-input-probe", "libfull-caller-faults.so", "companion-transfer-probe")]
    for path in required:
        if not path.is_file() or path.is_symlink():
            raise RuntimeError("missing built/staged artifact: " + str(path))
    binaries = {str(path): digest(path) for path in required}
    for path in paths["stage"].rglob("*"):
        if path.is_file():
            binaries[str(path)] = digest(path)
    # Native helper paths are compiled /usr/lib/aerial-lock, not DESTDIR. They
    # must match the staged payload, as must the bundle consumed at runtime.
    installed = {}
    for path in paths["stage"].rglob("*"):
        if not path.is_file():
            continue
        system = Path("/") / path.relative_to(paths["stage"])
        if digest(system) != digest(path):
            raise RuntimeError("installed/staged payload mismatch: " + str(system))
        installed[str(system)] = digest(system)
    for path in (stage / "share/aerial-lock").rglob("*"):
        if path.is_file():
            relative = str(path.relative_to(stage / "share/aerial-lock"))
            if relative in source_files and digest(path) != source_files[relative]["sha256"]:
                raise RuntimeError("staged data does not match source: " + relative)
    # Discover one actual tester-owned outer niri --session and its listeners.
    sessions = []
    for proc in Path("/proc").iterdir():
        if not proc.name.isdecimal():
            continue
        try:
            if proc.stat().st_uid != os.getuid():
                continue
            argv = proc.joinpath("cmdline").read_bytes().split(b"\0")
            if not argv or Path(os.fsdecode(argv[0])).name != "niri" or b"--session" not in argv:
                continue
            pid = int(proc.name)
            runtime = Path("/run/user") / str(os.getuid())
            sockets = list(runtime.glob(f"niri.wayland-*.{pid}.sock"))
            if len(sockets) != 1:
                continue
            wayland = sockets[0].name.split(".")[1]
            session = session_identity(pid, "niri-drm", runtime, wayland, sockets[0])
            sessions.append((session, {"XDG_RUNTIME_DIR": str(runtime), "WAYLAND_DISPLAY": wayland, "NIRI_SOCKET": str(sockets[0])}))
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    if len(sessions) != 1:
        raise RuntimeError("could not uniquely discover the owned outer compositor")
    session, environment = sessions[0]
    outputs = json.loads(command(["niri", "msg", "-j", "outputs"], env=dict(os.environ, **environment), capture_output=True, text=True).stdout)
    mapped = [name for name, output in outputs.items() if output["current_mode"] is not None and output["logical"] is not None]
    if not mapped:
        raise RuntimeError("outer session has no mapped output")
    record = dict(version=1, host=host["host"], domain=identity["name"], base_domain=Path(identity["base"]).name,
                  uuid=uuid, mac=identity["mac"], host_identity={"uuid": host["uuid"], "mac": host["mac"]},
                  guest={"user": pwd.getpwuid(os.getuid()).pw_name, "uid": os.getuid(),
                         "boot_id": Path("/proc/sys/kernel/random/boot_id").read_text().strip()},
                  paths={key: str(path) for key, path in paths.items()},
                  ownership={"task_created": [str(root)], "pre_existing": []},
                  source_freeze_identity=source_identity, current_source_identity=source_identity, source_files=source_files,
                  binary_hashes=binaries, binary_source_identity=source_identity, installed_hashes=installed,
                  session=session, environment=environment, initial_outputs=mapped,
                  pam_fixture={"prepared": True, "uuid": uuid, "uid": os.getuid(), "operator_session": "receipt-bound-ssh",
                               "service": "aerial-companion-test", "secret_file": str(root / "run/pam-fixture-secret")},
                  recovery_authorization=None)
    publish(args.vm_record, json.dumps(record, indent=2))
    command(["sudo", "python3", str(source / "Tests/companion-fixtures.py"), "--root", str(root), "--vm-record", str(args.vm_record), "prepare"])
    owned = OwnedInstance(args.vm_record, root)
    owned.check(require_binaries=True)
    owned.check_session(record["session"], environment)
    owned.auth_fixture()
    print("COMPANION_PREPARE_PASS " + json.dumps({"domain": identity["name"], "uuid": uuid, "source": source_identity}))


def installed_session(args, case="cold-load"):
    spec = importlib.util.spec_from_file_location("companion_remediation", Path(__file__).with_name("remediation-driver.py"))
    driver = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(driver)

    class InstalledSession(driver.Session):
        def __init__(self, options):
            super().__init__(options)
            local_name = case + "-" + options.backend + "-" + time.strftime("%Y-%m-%d_%H%M%S") + "-" + self.output.name.rsplit("-", 1)[-1]
            destination = self.output.with_name(local_name)
            self.output.rename(destination)
            # These fixture paths were derived by the base constructor before
            # its historical UTC directory name was replaced.
            for key in ("XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
                value = str(destination / Path(self.env[key]).relative_to(self.output))
                self.env[key] = value
                self.outer_env[key] = value
            self.output = destination

        def launch(self, cycle, fault=None):
            self.assert_control()
            for path, expected in self.owned.record["installed_hashes"].items():
                if digest(path) != expected:
                    raise RuntimeError("installed payload changed before cold load: " + path)
            runner = self.owned.check_binary(self.stage / "lib/aerial-lock/aerial-lock-run")
            plugin_root = self.stage / "lib/aerial-lock/qt-plugins"
            for name in ("libAerialLockConnectionGuard.so", "libAerialLockSurfaceInput.so"):
                self.owned.check_binary(plugin_root / "generic" / name)
            env = dict(self.env, WAYLAND_DEBUG="1")
            if fault:
                env["LD_PRELOAD"] = str(self.owned.check_binary(self.owned.paths["build"] / "libfull-caller-faults.so"))
                env["AERIAL_TEST_FULL_CALLER_FAULT"] = fault
            self.locker = driver.ObservedProcess([str(runner), "--shell-dir", str(self.shell), "--plugin-root", str(plugin_root)],
                                                 env, self.output / f"locker-{cycle:03}.redacted.log")
            secure = self.locker.wait_line(lambda line: "ext_session_lock_v1" in line and ".locked()" in line)
            maps = Path(f"/proc/{self.locker.process.pid}/maps").read_text()
            if "libMpvQt" in maps or any(str(plugin_root / "generic" / name) not in maps for name in
                    ("libAerialLockConnectionGuard.so", "libAerialLockSurfaceInput.so")):
                raise RuntimeError("cold-load native plugin provenance mismatch")
            publish(self.output / f"maps-{cycle:03}.txt", maps)
            return secure

    owned = OwnedInstance(args.vm_record, args.root)
    os.environ.update(owned.record["environment"])
    options = SimpleNamespace(root=str(args.root), vm_record=str(args.vm_record), case=case, backend=args.backend,
                              recover_owned_failures=False, outputs=1, hyprland_virtual_outputs=False)
    session = InstalledSession(options)
    return driver, session


def cold_load(args):
    driver, session = installed_session(args)
    # Human-facing artifact timestamps follow the source-host local timezone,
    # supplied by the executor's guest environment (not the guest's UTC clock).
    result = 1
    try:
        session.start()
        for leg, fault in enumerate((None, "core-create", "render-create"), 1):
            session.config(media=fault is not None)
            session.launch(leg, fault)
            if fault:
                marker = "FULL_CALLER_" + fault.replace("-", "_").upper() + "_FAULT"
                session.locker.wait_line(lambda line: marker in line)
                session.locker.wait_line(lambda line: "video background failed:" in line)
            session.unlock(leg)
        result = 0
    except (RuntimeError, subprocess.SubprocessError, OSError) as error:
        publish(session.output / "failure.json", json.dumps({"reason": str(error)}))
        print("COMPANION_COLD_LOAD_FAILED " + str(error), file=sys.stderr, flush=True)
    finally:
        cleanup = session.cleanup()
        result = cleanup or result
        publish(session.output / "result.json", json.dumps({"case": "cold-load", "exit": result, "legs": session.results}, indent=2))
    if result == 0:
        print("COMPANION_COLD_LOAD_PASS " + str(session.output))
    return result


def playback(args):
    """Installed cached playback, secure one-way hint and frame-driven recency.

    This incremental gate does not stand in for the held-transfer/next-episode
    locker acceptance. Export uses the actual acquisition/probe/cache classes.
    """
    driver, session = installed_session(args, "playback")
    bus = service = None
    result = 1
    observations = {}
    try:
        session.start()
        fixture = session.output / "content"
        command([str(session.root / "build/debug/aerial-tests/companion-transfer-probe"), "--export", str(fixture),
                 "/usr/lib/aerial-lock/aerial-media-probe"])
        config = fixture / "config/aerial-lock/config.json"
        document = json.loads(config.read_text())
        document["acquisition"] = {"automatic": {"enabled": False}}
        document["theme"] = "Macintosh.qml"
        publish(config, json.dumps(document))
        state = fixture / "state"
        state.mkdir()
        session.env.update(XDG_CONFIG_HOME=str(fixture / "config"), XDG_CACHE_HOME=str(fixture / "cache"),
                           XDG_STATE_HOME=str(state), AERIAL_LOCK_SHELL=str(session.shell))
        bus = subprocess.Popen(["dbus-daemon", "--session", "--nofork", "--nopidfile", "--print-address=1",
                                "--address=unix:path=" + str(session.output / "bus")],
                               env=session.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if not select.select([bus.stdout], [], [], 5)[0]:
            raise RuntimeError("private bus did not publish its address")
        session.env["DBUS_SESSION_BUS_ADDRESS"] = bus.stdout.readline().decode().strip()
        service = driver.ObservedProcess(["/usr/lib/aerial-lock/aerial-companion"], session.env,
                                         session.output / "companion.log")

        def wait_value(read, label):
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                value = read()
                if value:
                    return value
                if service.process.poll() is not None:
                    raise RuntimeError("companion exited: " + label)
                time.sleep(0.05)
            raise RuntimeError("outcome not observed: " + label)

        def status():
            reply = subprocess.run(["/usr/bin/aerialctl", "status", "--json"], env=session.env,
                                   capture_output=True, text=True, timeout=10)
            return json.loads(reply.stdout) if reply.returncode == 0 else None

        initial = wait_value(status, "matching installed service")
        if initial["ReadyClips"] != 1 or initial["State"] != "paused":
            raise RuntimeError("unexpected completed-content fixture state")
        session.launch(1)
        # Only the lock's native hint can stamp this absent preference. A
        # paused service still accepts the observation without downloading.
        height = wait_value(lambda: json.loads(config.read_text()).get("preferredResolution"), "secure height hint")
        local = state / "aerial-lock/last-playback.json"
        played = wait_value(lambda: json.loads(local.read_text()) if local.exists() else None, "decoded-frame local recency")
        key = played["key"]
        planner = state / "aerial-lock/planner-v1.json"
        recency = wait_value(lambda: json.loads(planner.read_text()).get("played", {}).get(key) if planner.exists() else None,
                             "service playback report")
        # Capture the actual compositor frame, not just mpv's decoded screenshot.
        command(["grim", "-t", "png", str(session.output / "managed-frame.png")], env=session.env)
        session.unlock(1)
        observations = {"readyClips": initial["ReadyClips"], "observedHeight": height, "lastPlayback": played,
                        "servicePlayedAt": recency, "compositorFrame": "managed-frame.png"}
        result = 0
    except (RuntimeError, subprocess.SubprocessError, OSError, ValueError) as error:
        publish(session.output / "failure.json", json.dumps({"reason": str(error)}))
        print("COMPANION_PLAYBACK_FAILED " + str(error), file=sys.stderr, flush=True)
    finally:
        # If an assertion failed with a live lock, ordinary PAM unlock is the
        # first recovery, not process termination or compositor teardown.
        if session.locker and session.locker.process.poll() is None and result:
            try:
                command(["wtype", "-M", "ctrl", "-k", "a", "-m", "ctrl", "-k", "BackSpace"], env=session.env)
                session.unlock(0)
            except (RuntimeError, subprocess.SubprocessError, OSError):
                pass
        cleanup = session.cleanup()
        for process in (service.process if service else None, bus):
            if process and process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
        if service:
            service.thread.join(2)
        result = cleanup or result
        publish(session.output / "result.json", json.dumps({"case": "playback", "exit": result,
                "observations": observations, "legs": session.results}, indent=2))
    if result == 0:
        print("COMPANION_PLAYBACK_PASS " + str(session.output))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--vm-record", required=True, type=Path)
    parser.add_argument("--case", required=True)
    parser.add_argument("--host-receipt", type=Path)
    parser.add_argument("--backend", choices=("niri", "hyprland"), default="niri")
    args = parser.parse_args()
    if args.case == "prepare":
        if not args.host_receipt:
            parser.error("prepare requires --host-receipt")
        prepare(args)
        return 0
    if args.case == "cold-load":
        return cold_load(args)
    if args.case == "playback":
        return playback(args)
    if args.case == "activation":
        from companion_activation import activation
        return activation(args)
    parser.error("case is not implemented yet; no acceptance coverage may be inferred")


if __name__ == "__main__":
    sys.exit(main())
