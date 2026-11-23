#!/usr/bin/env python3
"""Receipt-bound settings acceptance, only in the owned guest; never locks.

Build-tree app qualification; `install` builds and installs an exact-tree guest
test package. Native portal peers use real desktop consumers.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import signal
import subprocess
import sys
import time

from fixture_files import publish
from owned_instance import OwnedInstance, digest, session_identity
from private_runtime import PrivateRuntime, bus_address, bus_config
from settings_fixture_cleanup import Cleanup, stop_process
from settings_build import binary_paths, quick_test_inputs, verify_build
from settings_native_logs import NativeLogGate, QT_LOG_ENV, qt_version, require_quiet


def run(argv, **kwargs):
    return subprocess.run(argv, check=True, timeout=30, **kwargs)


def probe_command(owned, *arguments):
    version = ".".join(map(str, qt_version(owned.paths["build"])))
    return [sys.executable, str(owned.paths["source"] / "Tests/run-settings-test.py"),
            "--qt-version", version, "--", *map(str, arguments)]


def prepare(args):
    root = args.root.resolve(strict=True)
    if root != args.root or root == Path.home() or not root.is_relative_to(Path.home()):
        raise RuntimeError("requires an owned guest-home workspace")
    if Path(os.environ.get("TMPDIR", "")).resolve() != root / "tmp":
        raise RuntimeError("TMPDIR must be the disk-backed workspace/tmp")
    mount = json.loads(run(["findmnt", "-J", "-T", str(root)], capture_output=True, text=True).stdout)["filesystems"][0]
    if mount["fstype"] in ("tmpfs", "ramfs"):
        raise RuntimeError("workspace is not disk-backed")
    receipt = json.loads(args.host_receipt.read_text())
    identity = receipt["identity"]
    if receipt["schema"] != 1 or receipt["status"]["phase"] != "ready" or hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest() != receipt["creation_identity_sha256"]:
        raise RuntimeError("invalid fork creation receipt")
    uuid = run(["sudo", "cat", "/sys/class/dmi/id/product_uuid"], capture_output=True, text=True).stdout.strip()
    if uuid != identity["uuid"] or identity["mac"] not in {p.read_text().strip() for p in Path("/sys/class/net").glob("*/address")}:
        raise RuntimeError("guest does not match fork receipt")
    freeze = json.loads((root / "run/source-input.json").read_text())
    host = freeze["host_attestation"]
    if (host["uuid"], host["mac"], host["domain"]) != (uuid, identity["mac"], identity["name"]):
        raise RuntimeError("host attestation does not match guest")
    paths = {"root": root, "source": root / "src/aerial-lock", "build": root / "build/debug/aerial-tests", "stage": root / "stage", "run": root / "run", "results": root / "results", "media": root / "media"}
    build = verify_build(root, freeze, binary_paths(root))
    binaries = build["binary_hashes"]
    sessions = []
    for proc in Path("/proc").iterdir():
        try:
            if not proc.name.isdecimal() or proc.stat().st_uid != os.getuid():
                continue
            argv = proc.joinpath("cmdline").read_bytes().split(b"\0")
            if Path(os.fsdecode(argv[0])).name != "niri" or b"--session" not in argv:
                continue
            runtime = Path("/run/user") / str(os.getuid())
            sockets = list(runtime.glob(f"niri.wayland-*.{proc.name}.sock"))
            if len(sockets) != 1:
                continue
            wayland = sockets[0].name.split(".")[1]
            session = session_identity(int(proc.name), "niri-drm", runtime, wayland, sockets[0])
            sessions.append((session, {"XDG_RUNTIME_DIR": str(runtime), "WAYLAND_DISPLAY": wayland, "NIRI_SOCKET": str(sockets[0])}))
        except (OSError, IndexError):
            continue
    if len(sessions) != 1:
        raise RuntimeError("could not uniquely discover the native session")
    session, environment = sessions[0]
    record = dict(version=1, host=host["host"], domain=identity["name"], base_domain=Path(identity["base"]).name, uuid=uuid, mac=identity["mac"], host_identity={"uuid": uuid, "mac": identity["mac"]}, guest={"user": pwd.getpwuid(os.getuid()).pw_name, "uid": os.getuid(), "boot_id": Path("/proc/sys/kernel/random/boot_id").read_text().strip()}, paths={k: str(v) for k,v in paths.items()}, ownership={"task_created": [str(root)], "pre_existing": []}, source_freeze_identity=freeze["identity"], current_source_identity=freeze["identity"], source_files=freeze["files"], binary_hashes=binaries, binary_source_identity=build["source_identity"], session=session, environment=environment, recovery_authorization=None, pam_fixture=None)
    publish(args.vm_record, json.dumps(record, indent=2))
    owned = OwnedInstance(args.vm_record, root)
    owned.check(require_binaries=True)
    owned.check_session(session, environment)
    return {"source_identity": freeze["identity"], "uuid": uuid, "scope": "build-tree app, no PAM or default-profile activation"}


def native(args):
    owned = OwnedInstance(args.vm_record, args.root)
    owned.check(require_binaries=True)
    env = dict(os.environ, **owned.record["environment"], LIBGL_ALWAYS_SOFTWARE="1", QSG_RHI_BACKEND="opengl")
    env.pop("QT_QPA_PLATFORM", None)
    env.pop("QT_QUICK_BACKEND", None)
    owned.check_session(owned.record["session"], env)
    source = owned.paths["source"]
    version = qt_version(owned.paths["build"])
    binary = owned.check_binary(source / "settings/aerial-settings")
    output = args.root / "results" / ("settings-" + args.case + "-" + str(time.time_ns()))
    output.mkdir(mode=0o700)
    processes = []
    handles = []
    def spawn(argv, name, environment):
        handle = (output / (name + ".log")).open("wb")
        handles.append(handle)
        process = subprocess.Popen(argv, env=environment, stdout=handle, stderr=handle)
        processes.append(process)
        return process
    def windows():
        return json.loads(run(["niri", "msg", "-j", "windows"], env=env, capture_output=True, text=True).stdout)
    def wait_window(process, app_id="org.aeriallock.AerialSettings"):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            assert process.poll() is None, "app exited before native window"
            matches = [w for w in windows() if w["pid"] == process.pid and w["app_id"] == app_id]
            if len(matches) == 1:
                return matches[0]
            time.sleep(.05)
        raise RuntimeError("native settings window not observed")
    def capture(window, name):
        run(["niri", "msg", "action", "focus-window", "--id", str(window["id"])], env=env, capture_output=True)
        time.sleep(.3)
        path = output / (name + ".png")
        run(["niri", "msg", "action", "screenshot-window", "--path", str(path)], env=env, capture_output=True)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if path.is_file() and path.read_bytes().endswith(b"IEND\xaeB`\x82"):
                break
            time.sleep(.03)
        else:
            raise RuntimeError("compositor did not finish the PNG capture")
        return str(path)
    try:
        with PrivateRuntime("bus") as runtime:
            config = output / "bus.conf"
            config.write_text(bus_config(runtime / "bus"))
            bus = spawn(["dbus-daemon", "--nofork", "--nopidfile", "--config-file=" + str(config)], "bus", env)
            deadline = time.monotonic() + 5
            while not (runtime / "bus").exists():
                assert bus.poll() is None and time.monotonic() < deadline
                time.sleep(.03)
            env.update(DBUS_SESSION_BUS_ADDRESS=bus_address(runtime / "bus"), AERIAL_LOCK_SHELL=str(source), QT_NO_XDG_DESKTOP_PORTAL="1")
            for key in ("XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"):
                env[key] = str(output / key)
            user = Path(env["XDG_CONFIG_HOME"]) / "aerial-lock/config.json"
            user.parent.mkdir(parents=True)
            publish(user, '{"sources":[],"acquisition":{"automatic":{"enabled":false}}}')
            daemon = spawn([str(source / "companion/aerial")], "service", env)
            app = spawn([str(binary)], "app", env)
            window = wait_window(app)
            run(["niri", "msg", "action", "set-window-width", "--id", str(window["id"]), "960"], env=env, capture_output=True)
            time.sleep(.5)
            first = capture(window, "downloads")
            forwarded = run([str(binary)], env=env, capture_output=True, text=True)
            require_quiet(forwarded.stderr, version)
            assert len([w for w in windows() if w["pid"] == app.pid]) == 1
            visual = owned.check_binary(owned.paths["build"] / "settings-visual-probe")
            peer = spawn([str(visual), "--focus-peer"], "focus-peer", env)
            peer_window = wait_window(peer, "org.aeriallock.SettingsFocusWitness")
            run(["niri", "msg", "action", "focus-window", "--id", str(peer_window["id"])], env=env, capture_output=True)
            assert next(w for w in windows() if w["pid"] == peer.pid)["is_focused"]
            # Compositor spawning supplies a real activation token, unlike an
            # unrelated SSH process trying to steal focus without user context.
            forwarded_env = [key + "=" + env[key] for key in ("DBUS_SESSION_BUS_ADDRESS", "AERIAL_LOCK_SHELL", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME", "TMPDIR", "QT_NO_XDG_DESKTOP_PORTAL", *QT_LOG_ENV)]
            run(["niri", "msg", "action", "spawn", "--", "/usr/bin/env", *forwarded_env, str(binary)], env=env, capture_output=True)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                matches = [w for w in windows() if w["pid"] == app.pid]
                if len(matches) == 1 and matches[0]["is_focused"]:
                    window = matches[0]
                    break
                time.sleep(.03)
            else:
                raise RuntimeError("activation-token forwarding did not raise/focus the existing window")
            run(["niri", "msg", "action", "close-window", "--id", str(peer_window["id"])], env=env, capture_output=True)
            assert peer.wait(timeout=10) == 0
            run([str(source / "cli/aerialctl"), "config", "set", "acquisition.checkIntervalSeconds", "12345"], env=env, capture_output=True)
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                reply = run(["gdbus", "call", "--session", "--dest", "org.aeriallock.AerialSettings", "--object-path", "/org/aeriallock/AerialSettings", "--method", "org.freedesktop.DBus.Properties.Get", "org.freedesktop.Application", "IntervalText"], env=env, capture_output=True, text=True)
                if reply.stdout.strip() == "(<'Custom: 12345'>,)": break
                time.sleep(.03)
            else: raise RuntimeError("existing Downloads control did not display Custom: 12345")
            second = capture(window, "downloads-cli-custom")
            daemon.terminate()
            assert daemon.wait(timeout=10) == -signal.SIGTERM
            def wait_panel(expected):
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    assert app.poll() is None, "existing settings app exited"
                    reply = run(["gdbus", "call", "--session", "--dest", "org.aeriallock.AerialSettings", "--object-path", "/org/aeriallock/AerialSettings", "--method", "org.freedesktop.DBus.Properties.Get", "org.freedesktop.Application", "Panel"], env=env, capture_output=True, text=True)
                    if reply.stdout.strip() == "(<'" + expected + "'>,)":
                        return
                    time.sleep(.03)
                raise RuntimeError("existing app did not report panel: " + expected)
            wait_panel("unavailable")
            unavailable = capture(window, "unavailable")
            daemon = spawn([str(source / "companion/aerial")], "service-restarted", env)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                check = subprocess.run([str(source / "cli/aerialctl"), "status", "--json"], env=env, capture_output=True, timeout=10)
                if not check.returncode:
                    break
                time.sleep(.03)
            else:
                raise RuntimeError("private service did not restart")
            wait_panel("settings")
            recovered = capture(window, "recovered")
            run(["niri", "msg", "action", "close-window", "--id", str(window["id"])], env=env, capture_output=True)
            assert app.wait(timeout=10) == 0
            assert daemon.poll() is None, "closing settings stopped service"
            for handle in handles:
                handle.flush()
            require_quiet((output / "app.log").read_text(), version)
            captures = [first, second, unavailable, recovered]
            if args.case == "visuals":
                for palette in ("dark", "light"):
                    visual = owned.check_binary(owned.paths["build"] / "settings-visual-probe")
                    path = output / ("downloads-" + palette + ".png")
                    probe = run(probe_command(owned, visual, palette, path), env=env, capture_output=True, text=True)
                    publish(output / ("visual-" + palette + ".log"), probe.stdout + probe.stderr)
                    require_quiet(probe.stdout + probe.stderr, version)
                    assert path.is_file()
                    captures.append(str(path))
                    pages = output / ("pages-" + palette); pages.mkdir()
                    probe = run(probe_command(owned, visual, "--pages", palette, pages), env=env, capture_output=True, text=True)
                    publish(output / ("visual-pages-" + palette + ".log"), probe.stdout + probe.stderr)
                    require_quiet(probe.stdout + probe.stderr, version)
                    captures += sorted(str(p) for p in pages.glob("*.png"))
            result = {"window": window, "forwarded": True, "raised_from_peer": True, "natural_exit": 0, "service_survives": True, "captures": captures, "source_identity": owned.record["current_source_identity"]}
            publish(output / "result.json", json.dumps(result, indent=2))
            return result
    finally:
        cleanup = Cleanup()
        for process in reversed(processes):
            cleanup("child", lambda process=process: stop_process(process))
        for handle in handles:
            cleanup("log close", handle.close)
        cleanup.finish()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--vm-record", required=True, type=Path)
    parser.add_argument("--host-receipt", type=Path)
    parser.add_argument("--case", required=True, choices=("prepare", "cold-load", "controls", "failure-recovery", "portals", "tray", "units", "install", "visuals"))
    args = parser.parse_args()
    if Path(os.environ.get("TMPDIR", "")).resolve() != args.root / "tmp":
        raise RuntimeError("TMPDIR does not name the guest's disk-backed workspace/tmp")
    result_path = args.root / "results" / ("settings-" + args.case + ".json")
    # Child Qt processes must not route around redirected logs via journald.
    # Also audit the full corresponding journal, including disposable accounts.
    os.environ.update(QT_LOG_ENV)
    gate = None
    try:
        if args.case != "prepare":
            gate = NativeLogGate(args.root, args.case)
        if args.case == "prepare":
            result = prepare(args)
        elif args.case in ("cold-load", "visuals"):
            result = native(args)
        elif args.case in ("controls", "failure-recovery", "portals", "tray", "units", "install"):
            owned = OwnedInstance(args.vm_record, args.root); owned.check(require_binaries=True)
            environment = dict(os.environ, **owned.record["environment"], LIBGL_ALWAYS_SOFTWARE="1", QSG_RHI_BACKEND="opengl")
            environment.pop("QT_QPA_PLATFORM", None)
            environment.pop("QT_QUICK_BACKEND", None)
            owned.check_session(owned.record["session"], environment)
            output = args.root / "results" / ("settings-" + args.case + "-" + str(time.time_ns())); output.mkdir(mode=0o700)
            if args.case in ("portals", "tray", "units", "install"):
                from importlib.machinery import SourceFileLoader
                module = SourceFileLoader("settings_native_" + args.case, str(owned.paths["source"] / ("Tests/settings-native-" + args.case + ".py"))).load_module()
                result = getattr(module, args.case)(owned, output)
            else:
                # Native QuickTest drives the actual production controls and
                # checks CLI readback, source drag, drafts, outage and repair.
                test_inputs = quick_test_inputs(owned.paths["source"], owned.record["source_files"])
                commands = [probe_command(owned, owned.paths["build"] / "settings-quick-probe", "-input", owned.paths["source"] / "settings/tests", "-import", owned.paths["source"])]
                if args.case == "controls":
                    commands.append(probe_command(owned, owned.paths["build"] / "settings-appearance-probe"))
                if args.case == "failure-recovery":
                    commands += [probe_command(owned, owned.paths["build"] / name) for name in ("settings-model-probe", "settings-pages-probe")]
                for index, command in enumerate(commands):
                    # Qt 6.12's v3 input context warns on QuickTest synthetic
                    # clicks even without Aerial. See the QuickTest item in
                    # Documents/Aerial/TODO.md and companion-ui/checkpoint-10.md.
                    # Only this native QuickTest lane selects v1; real apps and
                    # other probes retain the default context and warning gate.
                    probe_environment = dict(environment, QT_WAYLAND_TEXT_INPUT_PROTOCOL="zwp_text_input_v1") if index == 0 else environment
                    probe = subprocess.run(command, env=probe_environment, capture_output=True, text=True, timeout=120)
                    text = probe.stdout + probe.stderr; publish(output / (str(index) + ".log"), text)
                    assert probe.returncode == 0 and "QWARN" not in text and "FAIL!" not in text, text
                result = {"nativeProductionControls": True, "cliReadback": True, "draftOutageAndRepair": True, "quickTestTextInputProtocol": "zwp_text_input_v1", "testInputs": test_inputs, "output": str(output)}
        else:
            raise RuntimeError("case belongs to a later implementation phase: " + args.case)
        if gate:
            audit, gate = gate, None
            logs = audit.finish()
            if logs["unexpected"]: raise RuntimeError("unexpected native warnings: " + json.dumps(logs["unexpected"]))
            result["logs"] = logs
        publish(result_path, json.dumps({"case": args.case, "exit": 0, "observations": result}, indent=2))
        print("SETTINGS_" + args.case.upper().replace("-", "_") + "_PASS " + str(result_path))
    except Exception as error:
        failure = {"case": args.case, "exit": 1, "error": str(error)}
        cleanup = Cleanup()
        if gate:
            cleanup("collect native logs", gate.finish)
            if cleanup.failures:
                failure["logError"] = cleanup.failures[0]
        cleanup("write failure result", lambda: publish(result_path, json.dumps(failure, indent=2)))
        cleanup.finish()
        raise


if __name__ == "__main__":
    main()
