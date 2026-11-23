#!/usr/bin/env python3
"""Incremental native build contracts, with reusable CTest fixture phases."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time
from unittest.mock import patch

from private_runtime import PrivateRuntime
from companion_install import check_layout
from source_tree import copy_source_tree

PHASES = ("paths", "dev", "flags", "install", "lto")


class BuildFixture:
    """One owned workspace and incremental object graph for the entire sequence."""
    def __init__(self, source, root, prepare=False, build_settings=True):
        self.source, self.root = source, root
        self.settings = build_settings
        self.tree = root / "source with spaces"
        self.marker = root / "owner.json"
        if prepare:
            root.mkdir(mode=0o700)
            identity = root.stat()
            with self.marker.open("x") as stream:
                os.fchmod(stream.fileno(), 0o600)
                json.dump({"source": str(source), "build_settings": build_settings, "device": identity.st_dev,
                           "inode": identity.st_ino, "uid": identity.st_uid}, stream)
        identity = root.lstat()
        owner = json.loads(self.marker.read_text())
        assert stat.S_ISDIR(identity.st_mode) and not root.is_symlink()
        assert owner == {"source": str(source), "build_settings": build_settings, "device": identity.st_dev,
                         "inode": identity.st_ino, "uid": os.getuid()}
        if prepare:
            copy_source_tree(source, self.tree, "*.so")

    def locations(self):
        return self.root, self.tree, self.root / "a", self.root / "b"

    def make(self, prefix, *args):
        started = time.monotonic()
        result = subprocess.run(["make", "-C", str(self.tree), "PREFIX=" + str(prefix),
                                 "BUILD_SETTINGS=" + str(int(self.settings)), *args],
                                capture_output=True, text=True, timeout=240 if self.settings else 120)
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


def embedded(binary, expected, unexpected):
    payload = binary.read_bytes()
    assert str(expected).encode() in payload, f"path missing from {binary}: {expected}"
    assert str(unexpected).encode() not in payload, f"stale path in {binary}: {unexpected}"


def touch_after_outputs(header, *outputs):
    """Establish a newer prerequisite even on coarse inode timestamp clocks."""
    baseline = max(path.stat().st_mtime_ns for path in outputs)
    deadline = time.monotonic() + 5
    while True:
        header.touch()
        if header.stat().st_mtime_ns > baseline:
            return
        assert time.monotonic() < deadline, "filesystem did not advance the header timestamp"
        time.sleep(.01)


FOREIGN = ("share/applications/foreign.desktop", "share/icons/hicolor/scalable/apps/foreign.svg",
           "share/icons/hicolor/scalable/status/foreign-symbolic.svg", "share/dbus-1/services/org.example.Foreign.service",
           "lib/systemd/user/foreign.service")


def paths(fixture, runtime):
    root, tree, a, b = fixture.locations()
    make = fixture.make
    make(a, "clean")
    make(a)
    settings = tree / "settings/aerial-settings"
    if fixture.settings:
        embedded(settings, a / "share/aerial-lock", b / "share/aerial-lock")
        settings_before = settings.stat().st_mtime_ns
    else:
        assert not settings.exists(), "core-only build produced settings"
    objects = fixture.objects()
    assert {p.name for p in objects} == {
        "aerial-unlock-main.o", "aerial-unlock-proto.o",
        "aerial-supervisor-main.o", "aerial-supervisor-proto.o",
        "aerial-idle-main.o", "aerial-idle-proto.o", "connection-guard.o", "surface-input.o",
        "auth-context.o", "auth-client.o", "aerial-lock-auth.o", "power-actions.o",
        "local-config.o", "config-document.o", "config-repository.o",
        "local-content.o", "companion-hint.o", "owned-directory.o", "content-snapshot.o",
        "aerial-main.o", "aerial-service.o", "companion_adaptor.o", "policy-monitor.o",
        "job-object.o", "job_adaptor.o", "aerialctl.o",
        "media-probe.o",
        "source-resolver.o", "cache-store.o", "http-operation.o", "manifest-store.o", "local-probe.o",
        "transfer.o", "planner-state.o", "acquisition-planner.o", "transfer-queue.o",
        "emergency-main.o", "emergency-wayland.o", "emergency-render.o", "emergency-input.o", "emergency-proto.o"
    }, objects
    client_archive = tree / "build/client/libaerial-client.a"
    assert client_archive.is_file(), "shared client archive missing"
    client_before = client_archive.stat().st_mtime_ns
    (root / "objects.json").write_text(json.dumps([p.name for p in objects]))
    before = [p.stat().st_mtime_ns for p in objects]
    make(b, "install", "PAMDIR=" + str(root / "pam"))
    after = [p.stat().st_mtime_ns for p in objects]
    assert all(new > old for old, new in zip(before, after)), "install-path changes reused native objects"
    assert client_archive.stat().st_mtime_ns == client_before, "path-only changes rebuilt path-independent client"
    if fixture.settings:
        assert settings.stat().st_mtime_ns > settings_before, "install-path change reused the settings app"
        embedded(settings, b / "share/aerial-lock", a / "share/aerial-lock")
        embedded(b / "bin/aerial-settings", b / "share/aerial-lock", a / "share/aerial-lock")
        settings_after = settings.stat().st_mtime_ns

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
    if fixture.settings:
        assert settings.stat().st_mtime_ns == settings_after, "unchanged configuration rebuilt the settings app"
        # Make must see vendor-only inputs, not just the app's own sources.
        vendor = tree / "third_party/interactive/src"
        for name, owner in (("controls/DialogButtonBoxLayout.h", "controls"),
                            ("controls/IAButton.qml", "controls"),
                            ("foundation/icons/check.svg", "foundation"),
                            ("components/CMakeLists.txt", None)):
            backing = tree / "build/settings/interactive/lib" / ("libinteractive_" + str(owner) + ".so")
            outputs = [settings] + ([backing] if owner else [])
            before_vendor = [path.stat().st_mtime_ns for path in outputs]
            touch_after_outputs(vendor / name, *outputs)
            make(b, "settings/aerial-settings")
            assert all(path.stat().st_mtime_ns > stamp for path, stamp in zip(outputs, before_vendor)), name
    # Registration embeds LocalConfig/ConfigDocument's layout; it must rebuild
    # for a transitive header edit, including one not named in its manual rule.
    registration = tree / "build/surface-input.o"
    before_registration = registration.stat().st_mtime_ns
    header = tree / "common/config-document.h"
    touch_after_outputs(header, registration)
    make(b)
    assert registration.stat().st_mtime_ns > before_registration, "transitive config layout left stale QML registration"
    assert "common/config-document.h" in (tree / "build/surface-input.d").read_text()
    if fixture.settings:
        nested_settings = tree / "build/settings/aerial-settings"
        settings_object = tree / "build/settings/CMakeFiles/aerial-settings.dir/main.cpp.o"
        settings_before = nested_settings.stat().st_mtime_ns
        object_before = settings_object.stat().st_mtime_ns
        touch_after_outputs(tree / "native/shell-dir.h", settings, nested_settings, settings_object)
        make(b, "settings/aerial-settings")
        assert nested_settings.stat().st_mtime_ns > settings_before, "external shell-dir header left the nested settings app stale"
        assert settings_object.stat().st_mtime_ns > object_before, "external shell-dir header did not recompile its consumer"
    wrapper = root / "cxx-wrapper"
    calls = root / "wrapper-calls.log"
    calls.unlink(missing_ok=True)
    wrapper.write_text("#!/bin/sh\nprintf '%s\\n' \"$*\" >> " + shlex.quote(str(calls)) + "\nexec \"$@\"\n")
    wrapper.chmod(0o700)
    with patch.dict(os.environ, {"PATH": str(root) + os.pathsep + os.environ["PATH"]}):
        make(b, "settings/aerial-settings" if fixture.settings else "build/client/libaerial-client.a", "CXX=" + wrapper.name + " g++")
    if fixture.settings:
        assert "main.cpp" in calls.read_text(), "compiler wrapper did not build settings sources"
    assert "service-connection.cpp" in calls.read_text(), "compiler wrapper did not build the shared client"


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


def interactive_compile_commands(tree):
    # Compare the commands Ninja invalidates when flags change, not mtimes:
    # Qt 6.12's shader depfile does not escape spaces and can dirty a resource
    # object on an otherwise unchanged build of this fixture.
    settings_build = tree / "build/settings"
    entries = json.loads(subprocess.check_output(["ninja", "-t", "compdb"], cwd=settings_build, timeout=10))
    commands = {entry["output"]: entry["command"] for entry in entries
                if entry["output"].startswith("interactive/") and entry["output"].endswith(".o")}
    assert commands, "InterActive was not in the settings compile database"
    return commands


def flags(fixture, runtime):
    root, tree, a, b = fixture.locations()
    make = fixture.make
    # Keep the original paths phase's object inventory across process boundaries.
    objects = [tree / "build" / name for name in json.loads((root / "objects.json").read_text())]
    after = [p.stat().st_mtime_ns for p in objects]
    # Only the InterActive subproject uses a fixed compiler configuration.
    # Establish its baseline as part of the first full build, if needed.
    interactive_include = tree / "Tests/build-config-interactive.cmake"
    scope = (f"CMAKE_PROJECT_InterActive_INCLUDE={interactive_include}",) if fixture.settings else ()
    make(b, "CXXFLAGS=-O1 -frecord-gcc-switches", *scope)
    assert all(p.stat().st_mtime_ns > old for p, old in zip(objects, after)), "changed flags reused native objects"
    # CMake-built consumers take the same flags: the recorded compiler command
    # lines in both the shared client and the settings app carry the change.
    artifacts = [tree / "build/client/libaerial-client.a"]
    if fixture.settings:
        artifacts.append(tree / "settings/aerial-settings")
    for artifact in artifacts:
        recorded = subprocess.run(["readelf", "-p", ".GCC.command.line", str(artifact)], capture_output=True, text=True, timeout=30)
        # The section exists only with -frecord-gcc-switches; every unit has -O1.
        lines = [line for line in recorded.stdout.splitlines() if line.lstrip().startswith("[") and "GNU C++" in line]
        assert recorded.returncode == 0 and lines and all(" -O1 " in line + " " for line in lines), (artifact, recorded.stdout[-2000:], recorded.stderr)
    launcher = root / "ccache"
    launcher.write_text('#!/bin/sh\nexec "$@"\n'); launcher.chmod(0o700)
    native_stamps = [p.stat().st_mtime_ns for p in objects]
    interactive_commands = interactive_compile_commands(tree) if fixture.settings else None
    # These permutations exercise the CXX -> CMake bridge, not native flag
    # invalidation (checked above). Build both consumers through their real
    # dependency graph without rebuilding every unrelated native target.
    for command, cxxflags in (("g++ -O2", "-frecord-gcc-switches"), (launcher.name + " g++ -O2", "-frecord-gcc-switches"),
                              (launcher.name + " g++", "-O2 -frecord-gcc-switches")):
        with patch.dict(os.environ, {"PATH": str(root) + os.pathsep + os.environ["PATH"]}):
            make(b, "settings/aerial-settings" if fixture.settings else "build/client/libaerial-client.a",
                 "CXX=" + command, "CXXFLAGS=" + cxxflags, *scope)
        assert native_stamps == [p.stat().st_mtime_ns for p in objects], "CMake flag checks rebuilt unrelated native objects"
        if fixture.settings:
            assert interactive_compile_commands(tree) == interactive_commands, f"{command}: CMake flag checks changed InterActive compile commands"
        for artifact in artifacts:
            recorded = subprocess.run(["readelf", "-p", ".GCC.command.line", str(artifact)], capture_output=True, text=True, timeout=30)
            lines = [line for line in recorded.stdout.splitlines() if line.lstrip().startswith("[") and "GNU C++" in line]
            assert recorded.returncode == 0 and lines and all(" -O2 " in line + " " for line in lines), (command, artifact, recorded.stdout[-2000:], recorded.stderr)


def install(fixture, runtime):
    root, tree, a, b = fixture.locations()
    make = fixture.make
    dev_helper = tree / "native/aerial-lock-auth"
    layouts = [("default", Path("/usr"), Path("/usr/bin")),
               ("prefix", Path("/usr/local"), Path("/usr/local/bin")),
               ("bindir", Path("/usr/local"), Path("/opt/aerial/bin")),
               ("spaces", root / "prefix with spaces", root / "bin with spaces")]
    if fixture.settings:
        layouts.append(("private", Path("/usr/local"), Path("/opt/aerial/bin")))
    for label, prefix, bindir in layouts:
        stage = root / (label + " stage")
        options = ["DESTDIR=" + str(stage)]
        # Shared desktop directories may already hold other packages' files.
        foreign = {stage / prefix.relative_to("/") / name for name in FOREIGN}
        for path in foreign:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("foreign\n")
        if label in ("bindir", "private", "spaces"):
            options.append("BINDIR=" + str(bindir))
        private = Path("/opt/aerial/runtime") if label == "private" else prefix / "lib/aerial-lock/interactive"
        if fixture.settings:
            options.append("INTERACTIVE_ROOT=" + str(private))
        make(prefix, "install", *options)
        unit = stage / prefix.relative_to("/") / "lib/systemd/user/aerial-idle.service"
        commands = [line for line in unit.read_text().splitlines() if line.startswith("ExecStart=")]
        assert len(commands) == 1 and shlex.split(commands[0].removeprefix("ExecStart=")) == [str(bindir / "aerial-idle")], (label, commands)
        assert unit.stat().st_mode & 0o777 == 0o644, (label, oct(unit.stat().st_mode))
        assert (stage / bindir.relative_to("/") / "aerial-idle").is_file()
        check_layout(stage, prefix, prefix / "libexec/aerial-lock", bindir, settings=fixture.settings, interactive_root=private)
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
        if fixture.settings:
            # The adapter owns exact files, not arbitrary additions or user
            # startup policy, even across reinstall and a custom private root.
            for path in (stage / private.relative_to("/") / "lib/foreign-note",
                         stage / prefix.relative_to("/") / "share/licenses/aerial-lock/interactive/foreign-note",
                         stage / "etc/systemd/user/aerial-tray.service"):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("foreign\n")
                foreign.add(path)
            enabled = stage / "etc/systemd/user/graphical-session.target.wants/aerial-tray.service"
            enabled.parent.mkdir(parents=True)
            enabled.symlink_to("../aerial-tray.service")
            foreign.add(enabled)
            make(prefix, "install", *options)
            assert all(path.read_text() == "foreign\n" for path in foreign)
            assert enabled.is_symlink() and os.readlink(enabled) == "../aerial-tray.service"
        if fixture.settings and label == "private":
            # Removing the build directory is equivalent to an unavailable
            # SETTINGS_BUILD. First reject a mismatched BINDIR without damage.
            payload = {p.relative_to(stage) for p in stage.rglob("*") if p.is_file() or p.is_symlink()}
            wrong = subprocess.run(["make", "-C", str(tree), "uninstall", f"PREFIX={prefix}",
                f"DESTDIR={stage}", f"INTERACTIVE_ROOT={private}", "BINDIR=/nonexistent/aerial/bin"],
                capture_output=True, text=True, timeout=30)
            assert wrong.returncode != 0 and "another executable layout" in wrong.stderr, wrong.stderr
            assert payload == {p.relative_to(stage) for p in stage.rglob("*") if p.is_file() or p.is_symlink()}
            receipt = stage / prefix.relative_to("/") / "share/licenses/aerial-lock/interactive/install-manifest.txt"
            receipt.rename(root / "retained-install-manifest.txt")
            missing = subprocess.run(["make", "-C", str(tree), "uninstall", f"PREFIX={prefix}", *options],
                capture_output=True, text=True, timeout=30)
            assert missing.returncode != 0 and "Missing private runtime install manifest" in missing.stderr, missing.stderr
            (root / "retained-install-manifest.txt").rename(receipt)
            assert payload == {p.relative_to(stage) for p in stage.rglob("*") if p.is_file() or p.is_symlink()}
            options.append("SETTINGS_BUILD=" + str(root / "missing-build"))
        make(prefix, "uninstall", *options)
        remaining = {p for p in stage.rglob("*") if p.is_file() or p.is_symlink()}
        assert remaining == foreign and all(p.read_text() == "foreign\n" for p in foreign), f"{label} uninstall: {remaining ^ foreign}"
        if fixture.settings:
            assert enabled.is_symlink() and os.readlink(enabled) == "../aerial-tray.service"

    arch_stage = root / "arch-stage"
    make("/usr", "install", "DESTDIR=" + str(arch_stage), "LIBEXECDIR=/usr/lib/aerial-lock")
    check_layout(arch_stage, "/usr", "/usr/lib/aerial-lock", settings=fixture.settings)
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


def cleanup(source, workspace, build_settings=True):
    if not workspace.exists():
        return
    fixture = BuildFixture(source, workspace, build_settings=build_settings)
    shutil.rmtree(fixture.root)


def run_phase(source, workspace, phase, build_settings=True):
    started = time.monotonic()
    fixture = BuildFixture(source, workspace, prepare=phase == "paths", build_settings=build_settings)
    with PrivateRuntime("wayland") as runtime:
        globals()[phase](fixture, runtime)
    print(f"BUILD_CONFIG_PHASE_PASS {phase} seconds={time.monotonic() - started:.3f}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=lambda value: Path(value).resolve(strict=True))
    parser.add_argument("--case", choices=(*PHASES, "cleanup", "all"), default="all")
    parser.add_argument("--workspace", type=Path)
    parser.add_argument("--build-settings", type=int, choices=(0, 1), default=1)
    args = parser.parse_args()
    if args.case != "all":
        if args.workspace is None:
            parser.error("--workspace is required for a CTest phase")
        workspace = args.workspace.absolute()
        if args.case == "cleanup":
            cleanup(args.source, workspace, bool(args.build_settings))
        else:
            run_phase(args.source, workspace, args.case, bool(args.build_settings))
        return
    with tempfile.TemporaryDirectory(prefix="aerial-build-config-") as parent:
        workspace = Path(parent) / "fixture"
        try:
            for phase in PHASES:
                run_phase(args.source, workspace, phase, bool(args.build_settings))
        finally:
            cleanup(args.source, workspace, bool(args.build_settings))


if __name__ == "__main__":
    main()
