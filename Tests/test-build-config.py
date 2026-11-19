#!/usr/bin/env python3
"""Incremental native build contracts, with reusable CTest fixture phases."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time

from private_runtime import PrivateRuntime
from companion_install import check_layout

PHASES = ("paths", "dev", "flags", "install", "lto")


class BuildFixture:
    """One owned workspace and incremental object graph for the entire sequence."""
    def __init__(self, source, root, prepare=False):
        self.source, self.root = source, root
        self.tree = root / "source with spaces"
        self.marker = root / "owner.json"
        if prepare:
            root.mkdir(mode=0o700)
            identity = root.stat()
            with self.marker.open("x") as stream:
                os.fchmod(stream.fileno(), 0o600)
                json.dump({"source": str(source), "device": identity.st_dev,
                           "inode": identity.st_ino, "uid": identity.st_uid}, stream)
        identity = root.lstat()
        owner = json.loads(self.marker.read_text())
        assert stat.S_ISDIR(identity.st_mode) and not root.is_symlink()
        assert owner == {"source": str(source), "device": identity.st_dev,
                         "inode": identity.st_ino, "uid": os.getuid()}
        if prepare:
            shutil.copytree(source, self.tree, ignore=shutil.ignore_patterns(".git", "build", "__pycache__", "*.so"))

    def locations(self):
        return self.root, self.tree, self.root / "a", self.root / "b"

    def make(self, prefix, *args):
        started = time.monotonic()
        result = subprocess.run(["make", "-C", str(self.tree), "PREFIX=" + str(prefix), *args],
                                capture_output=True, text=True, timeout=120)
        commands = result.stdout.replace("\\\n", " ")
        objects = [line for line in commands.splitlines()
                   if re.search(r"\s-c\s", line) and re.search(r"\s-o\s+\S+\.o(?:\s|$)", line)]
        metric = {"arguments": list(args), "prefix": str(prefix), "seconds": round(time.monotonic() - started, 3),
                  "objects": len(objects), "qdbusCodegen": len(re.findall(r"^.*qdbusxml2cpp.*$", commands, re.M))}
        print("BUILD_CONFIG_MAKE " + json.dumps(metric), flush=True)
        assert result.returncode == 0, result.stdout + result.stderr
        return result

    def objects(self):
        return sorted((self.tree / "build").glob("*.o"))

    def environment(self, runtime):
        env = {k: v for k, v in os.environ.items()
               if k not in ("AERIAL_LOCK_RUNNER", "AERIAL_LOCK_SHELL", "AERIAL_LOCK_PLUGIN_ROOT",
                            "AERIAL_LOCK_VIDEO_POOL", "HYPRLAND_INSTANCE_SIGNATURE", "WAYLAND_SOCKET")}
        env.update(WAYLAND_DISPLAY=str(runtime / "wayland"), XDG_RUNTIME_DIR=str(runtime))
        return env


def helper_path(plugin, expected, unexpected):
    # Check linked consumers, including LTO, rather than make variable echoes.
    encoding = "utf-16-le" if sys.byteorder == "little" else "utf-16-be"
    payload = plugin.read_bytes()
    assert str(expected).encode(encoding) in payload, f"helper path missing from {plugin}: {expected}"
    assert str(unexpected).encode(encoding) not in payload, f"stale helper path in {plugin}: {unexpected}"


def paths(fixture, runtime):
    root, tree, a, b = fixture.locations()
    make = fixture.make
    make(a, "clean")
    make(a)
    objects = fixture.objects()
    assert {p.name for p in objects} == {
        "aerial-unlock-main.o", "aerial-unlock-proto.o",
        "aerial-supervisor-main.o", "aerial-supervisor-proto.o",
        "aerial-idle-main.o", "aerial-idle-proto.o", "connection-guard.o", "surface-input.o",
        "auth-context.o", "auth-client.o", "aerial-lock-auth.o", "power-actions.o",
        "local-config.o", "config-document.o", "config-repository.o",
        "local-content.o", "companion-hint.o", "owned-directory.o", "content-snapshot.o",
        "aerial-main.o", "aerial-service.o", "companion_adaptor.o", "policy-monitor.o",
        "job-object.o", "job_adaptor.o", "aerialctl.o", "companion_client.o",
        "media-probe.o",
        "source-resolver.o", "cache-store.o", "http-operation.o", "manifest-store.o", "local-probe.o",
        "transfer.o", "planner-state.o", "acquisition-planner.o", "transfer-queue.o",
        "emergency-main.o", "emergency-wayland.o", "emergency-render.o", "emergency-input.o", "emergency-proto.o"
    }, objects
    (root / "objects.json").write_text(json.dumps([p.name for p in objects]))
    before = [p.stat().st_mtime_ns for p in objects]
    make(b, "install", "PAMDIR=" + str(root / "pam"))
    after = [p.stat().st_mtime_ns for p in objects]
    assert all(new > old for old, new in zip(before, after)), "install-path changes reused native objects"

    marker = root / "runner-args.json"
    runner = b / "libexec/aerial-lock/aerial-lock-run"
    runner.write_text("#!/usr/bin/python3\nimport json,sys\nfrom pathlib import Path\n"
                      f"Path({str(marker)!r}).write_text(json.dumps(sys.argv[1:]))\n")
    runner.chmod(0o700)
    listener = socket.socket(socket.AF_UNIX)
    listener.bind(str(runtime / "wayland"))
    listener.listen(16)
    env = fixture.environment(runtime)
    result = subprocess.run(["dbus-run-session", "--", str(b / "bin/aerial-lock-supervisor")],
                            env=env, capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stdout + result.stderr
    assert json.loads(marker.read_text()) == ["--shell-dir", str(b / "share/aerial-lock"),
                                              "--plugin-root", str(b / "lib/aerial-lock/qt-plugins")]
    idle_env = dict(env, XDG_CONFIG_HOME=str(root / "empty-config"))
    result = subprocess.run([str(b / "bin/aerial-idle"), "--print-config"], env=idle_env,
                            capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    defaults = json.loads((b / "share/aerial-lock/Config/defaults.json").read_text())
    assert json.loads(result.stdout) == defaults["idle"]
    listener.close()
    make(b)
    assert after == [p.stat().st_mtime_ns for p in objects], "unchanged configuration rebuilt native objects"


def dev(fixture, runtime):
    root, tree, a, b = fixture.locations()
    make = fixture.make
    # Only the copied launcher is stubbed; real dev prerequisites/build still run.
    (tree / "Scripts/dev/run.sh").write_text("#!/usr/bin/env bash\nprintf 'DEV_RUNNER_STUB\\n'\n")
    plugin = tree / "native/qt-plugins/generic/libAerialLockSurfaceInput.so"
    auth_object = tree / "build/auth-context.o"
    installed_helper = b / "libexec/aerial-lock/aerial-lock-auth"
    dev_helper = tree / "native/aerial-lock-auth"
    installed_emergency = b / "libexec/aerial-lock/aerial-lock-emergency"
    dev_emergency = tree / "emergency/aerial-lock-emergency"
    emergency = b / "libexec/aerial-lock/aerial-lock-emergency"
    assert os.access(emergency, os.X_OK) and emergency.stat().st_mode & 0o777 == 0o755
    assert str(installed_helper).encode() in emergency.read_bytes()
    helper_path(plugin, installed_helper, dev_helper)
    helper_path(b / "bin/aerial-lock-supervisor", installed_emergency, dev_emergency)
    installed_stamp = auth_object.stat().st_mtime_ns
    assert "DEV_RUNNER_STUB" in make(b, "dev").stdout
    helper_path(plugin, dev_helper, installed_helper)
    helper_path(tree / "supervisor/aerial-lock-supervisor", dev_emergency, installed_emergency)
    dev_stamp = auth_object.stat().st_mtime_ns
    assert dev_stamp > installed_stamp, "dev helper-path selection reused auth-context.o"
    assert os.access(dev_helper, os.X_OK), "dev build omitted its helper"
    make(b, "dev")
    assert auth_object.stat().st_mtime_ns == dev_stamp, "unchanged dev selection rebuilt auth-context.o"
    make(b, "install", "PAMDIR=" + str(root / "pam"))
    helper_path(b / "lib/aerial-lock/qt-plugins/generic/libAerialLockSurfaceInput.so", installed_helper, dev_helper)
    helper_path(b / "bin/aerial-lock-supervisor", installed_emergency, dev_emergency)
    assert auth_object.stat().st_mtime_ns > dev_stamp, "install after dev reused auth-context.o"
    print("AUTH_HELPER_PATH_PASS installed dev dev_noop installed_again source_path_spaces", flush=True)


def flags(fixture, runtime):
    root, tree, a, b = fixture.locations()
    make = fixture.make
    # Keep the original paths phase's object inventory across process boundaries.
    objects = [tree / "build" / name for name in json.loads((root / "objects.json").read_text())]
    after = [p.stat().st_mtime_ns for p in objects]
    make(b, "CXXFLAGS=-O1 -frecord-gcc-switches")
    assert all(p.stat().st_mtime_ns > old for p, old in zip(objects, after)), "changed flags reused native objects"


def install(fixture, runtime):
    root, tree, a, b = fixture.locations()
    make = fixture.make
    dev_helper = tree / "native/aerial-lock-auth"
    for label, prefix, bindir in (("default", Path("/usr"), Path("/usr/bin")),
                                  ("prefix", Path("/usr/local"), Path("/usr/local/bin")),
                                  ("bindir", Path("/usr/local"), Path("/opt/aerial/bin"))):
        stage = root / (label + "-stage")
        options = ["DESTDIR=" + str(stage)]
        if label == "bindir":
            options.append("BINDIR=" + str(bindir))
        make(prefix, "install", *options)
        unit = stage / prefix.relative_to("/") / "lib/systemd/user/aerial-idle.service"
        commands = [line for line in unit.read_text().splitlines() if line.startswith("ExecStart=")]
        assert commands == [f"ExecStart={bindir}/aerial-idle"], (label, commands)
        assert unit.stat().st_mode & 0o777 == 0o644, (label, oct(unit.stat().st_mode))
        assert (stage / bindir.relative_to("/") / "aerial-idle").is_file()
        check_layout(stage, prefix, prefix / "libexec/aerial-lock", bindir)
        helper_path(stage / prefix.relative_to("/") / "lib/aerial-lock/qt-plugins/generic/libAerialLockSurfaceInput.so",
                    prefix / "libexec/aerial-lock/aerial-lock-auth", dev_helper)
        emergency = stage / prefix.relative_to("/") / "libexec/aerial-lock/aerial-lock-emergency"
        assert emergency.stat().st_mode & 0o777 == 0o755
        payload = emergency.read_bytes()
        assert str(prefix / "libexec/aerial-lock/aerial-lock-auth").encode() in payload
        assert str(stage).encode() not in payload
        helper_path(stage / bindir.relative_to("/") / "aerial-lock-supervisor",
                    prefix / "libexec/aerial-lock/aerial-lock-emergency",
                    stage / prefix.relative_to("/") / "libexec/aerial-lock/aerial-lock-emergency")
        print(f"UNIT_INSTALL_PASS {label} {commands[0]} mode=644", flush=True)
        make(prefix, "uninstall", *options)
        assert not any(p.is_file() for p in stage.rglob("*")), f"{label} uninstall left installed files"

    arch_stage = root / "arch-stage"
    make("/usr", "install", "DESTDIR=" + str(arch_stage), "LIBEXECDIR=/usr/lib/aerial-lock")
    check_layout(arch_stage, "/usr", "/usr/lib/aerial-lock")
    emergency = arch_stage / "usr/lib/aerial-lock/aerial-lock-emergency"
    assert emergency.stat().st_mode & 0o777 == 0o755
    assert b"/usr/lib/aerial-lock/aerial-lock-auth" in emergency.read_bytes()
    helper_path(arch_stage / "usr/bin/aerial-lock-supervisor",
                Path("/usr/lib/aerial-lock/aerial-lock-emergency"),
                Path("/usr/libexec/aerial-lock/aerial-lock-emergency"))
    needed = subprocess.check_output(["readelf", "-d", str(emergency)], text=True)
    assert not any(name in needed for name in ("libQt", "libpam", "libGL", "libEGL", "libcairo", "libfontconfig", "libmpv")), needed
    make("/usr", "uninstall", "DESTDIR=" + str(arch_stage), "LIBEXECDIR=/usr/lib/aerial-lock")
    assert not any(p.is_file() for p in arch_stage.rglob("*"))


def lto(fixture, runtime):
    root, tree, a, b = fixture.locations()
    make = fixture.make
    plugin = tree / "native/qt-plugins/generic/libAerialLockSurfaceInput.so"
    installed_helper = b / "libexec/aerial-lock/aerial-lock-auth"
    dev_helper = tree / "native/aerial-lock-auth"
    idle_env = dict(fixture.environment(runtime), XDG_CONFIG_HOME=str(root / "empty-config"))
    defaults = json.loads((b / "share/aerial-lock/Config/defaults.json").read_text())
    # Exercise the distributor's real LTO link/code-generation phase too.
    packaging_config = Path("/etc/makepkg.conf")
    if packaging_config.is_file():
        result = subprocess.run(["bash", "--noprofile", "--norc", "-c",
            'source "$1"; printf "%s\\0" "$CFLAGS $LTOFLAGS" '
            '"$CXXFLAGS $LTOFLAGS" "$LDFLAGS $LTOFLAGS"', "bash", str(packaging_config)],
            capture_output=True, timeout=10, check=True)
        packaging_flags = result.stdout.decode().split("\0")[:3]
    else:
        packaging_flags = ["-O2 -flto", "-O2 -flto", "-flto"]
    assert len(packaging_flags) == 3 and all("-flto" in value for value in packaging_flags)
    flags = [key + "=" + value for key, value in
             zip(("CFLAGS", "CXXFLAGS", "LDFLAGS"), packaging_flags)]
    print("PACKAGING_LTO_FLAGS " + json.dumps(flags), flush=True)
    make(b, "clean")
    make(b, *flags)
    result = subprocess.run([str(tree / "idle/aerial-idle"), "--print-config"], env=idle_env,
                            capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    assert json.loads(result.stdout) == defaults["idle"]
    helper_path(plugin, installed_helper, dev_helper)
    print("BUILD_CONFIG_PASS paths_consumed_by_installed_supervisor_and_idle auth_helper_dev_install stable_noop flags_rebuilt unit_install_paths packaging_lto")


def cleanup(source, workspace):
    if not workspace.exists():
        return
    fixture = BuildFixture(source, workspace)
    shutil.rmtree(fixture.root)


def run_phase(source, workspace, phase):
    started = time.monotonic()
    fixture = BuildFixture(source, workspace, prepare=phase == "paths")
    with PrivateRuntime("wayland") as runtime:
        globals()[phase](fixture, runtime)
    print(f"BUILD_CONFIG_PHASE_PASS {phase} seconds={time.monotonic() - started:.3f}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=lambda value: Path(value).resolve(strict=True))
    parser.add_argument("--case", choices=(*PHASES, "cleanup", "all"), default="all")
    parser.add_argument("--workspace", type=Path)
    args = parser.parse_args()
    if args.case != "all":
        if args.workspace is None:
            parser.error("--workspace is required for a CTest phase")
        workspace = args.workspace.absolute()
        if args.case == "cleanup":
            cleanup(args.source, workspace)
        else:
            run_phase(args.source, workspace, args.case)
        return
    with tempfile.TemporaryDirectory(prefix="aerial-build-config-") as parent:
        workspace = Path(parent) / "fixture"
        try:
            for phase in PHASES:
                run_phase(args.source, workspace, phase)
        finally:
            cleanup(args.source, workspace)


if __name__ == "__main__":
    main()
