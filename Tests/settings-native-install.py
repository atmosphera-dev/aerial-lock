#!/usr/bin/env python3
"""Installed desktop-app qualification from an exact-tree guest test package.

The retained release recipe is copied read-only; only its source/checksum and
the build tools CMake/Ninja differ in the labelled test copy. A disposable task
account runs the real user manager with installed vendor units, so tester's
default profile is never activated.
"""
import json
import os
from pathlib import Path
import pwd
import re
import shlex
import shutil
import subprocess
import tarfile
import time

from companion_install import check_layout
from fixture_files import publish
from settings_fixture_cleanup import Cleanup, access_paths, stop_process
from owned_instance import digest

APP = Path("/usr/bin/aerial-settings")
DESKTOP = Path("/usr/share/applications/org.aeriallock.AerialSettings.desktop")
TRAY_UNIT = Path("/usr/lib/systemd/user/aerial-tray.service")


def run(argv, **kwargs):
    kwargs.setdefault("timeout", 60)
    kwargs.setdefault("check", True)
    kwargs.setdefault("capture_output", True)
    kwargs.setdefault("text", True)
    return subprocess.run(list(map(str, argv)), **kwargs)


def wait(read, label, seconds=25):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = read()
        if value:
            return value
        time.sleep(.08)
    raise RuntimeError("not observed: " + label)


def processes(executable):
    # Any user's process: argv[0] is world-readable, exe links are not.
    found = []
    for proc in Path("/proc").iterdir():
        if proc.name.isdecimal():
            try:
                if (proc / "cmdline").read_bytes().split(b"\0")[0] == str(executable).encode():
                    found.append(int(proc.name))
            except OSError:
                pass
    return found


def executable(pid):
    return Path(run(["sudo", "readlink", "/proc/%d/exe" % pid]).stdout.strip())


def build_package(owned, output, packaging):
    source = owned.paths["source"]
    original, test = output / "release-recipe", output / "test-only-recipe"
    original.mkdir(); test.mkdir()
    for name in ("PKGBUILD", "aerial-lock.install", ".SRCINFO"):
        assert (packaging / name).is_file() and not (packaging / name).is_symlink()
        shutil.copy2(packaging / name, original / name)
    srcinfo = run(["makepkg", "--printsrcinfo"], cwd=original).stdout
    assert srcinfo == (original / ".SRCINFO").read_text(), "release .SRCINFO is not generated/current"
    text = (original / "PKGBUILD").read_text()
    version = re.search(r"^pkgver=([0-9.]+)$", text, re.M).group(1)
    name = re.search(r"^pkgname=([a-z-]+)$", text, re.M).group(1)
    archive = test / (name + "-" + version + ".tar.gz")
    with tarfile.open(archive, "w:gz") as target:
        for relative, expected in sorted(owned.record["source_files"].items()):
            path = source / relative
            assert digest(path) == expected["sha256"] and path.lstat().st_mode == expected["mode"], relative
            target.add(path, arcname=name + "-" + version + "/" + relative, recursive=False)
    text, sources = re.subn(r"^source=\([^\n]*\)$", 'source=("' + archive.name + '")', text, flags=re.M)
    text, sums = re.subn(r"^sha256sums=\([^\n]*\)$", "sha256sums=('" + digest(archive) + "')", text, flags=re.M)
    # Build-only finding since the shared client subbuild: CMake and Ninja.
    text, tools = re.subn(r"^makedepends=\('wayland-protocols'\)$", "makedepends=('wayland-protocols' 'cmake' 'ninja')", text, flags=re.M)
    assert sources == sums == tools == 1
    (test / "PKGBUILD").write_text(text)
    shutil.copy2(original / "aerial-lock.install", test / "aerial-lock.install")
    (test / "TEST-ONLY.md").write_text("Exact task-tree acceptance package, not a release recipe or publication.\n")
    config = Path("/etc/makepkg.conf").read_text()
    for setting, leaf in (("BUILDDIR", "build"), ("PKGDEST", "packages"), ("SRCDEST", "sources"),
                          ("SRCPKGDEST", "source-packages"), ("LOGDEST", "logs")):
        (output / leaf).mkdir()
        config += "\n" + setting + "=" + shlex.quote(str(output / leaf)) + "\n"
    (output / "makepkg.conf").write_text(config + "\nMAKEFLAGS=-j2\n")
    makepkg = ["makepkg", "--config", str(output / "makepkg.conf")]
    with (output / "makepkg.log").open("w") as log:
        subprocess.run([*makepkg, "--noconfirm", "--cleanbuild", "--force"], cwd=test, env=dict(os.environ, MAKEFLAGS="-j2"),
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=2400)
    packages = [Path(p).resolve(strict=True) for p in run([*makepkg, "--packagelist"], cwd=test).stdout.splitlines()]
    main = [p for p in packages if "pkgname = aerial-lock\n" in run(["bsdtar", "-xOf", p, ".PKGINFO"]).stdout]
    assert len(main) == 1
    declared = re.findall(r"^\tdepends = (.+)$", srcinfo, re.M)
    return main[0], declared, digest(archive)


def install(owned, output):
    root, source = owned.root, owned.paths["source"]
    assert Path(os.environ["TMPDIR"]) == root / "tmp"
    env = dict(os.environ, **owned.record["environment"])
    observations = {"sourceIdentity": owned.record["current_source_identity"]}

    def default_profile():
        state = run(["systemctl", "--user", "show", "aerial.service", "aerial-tray.service", "aerial-idle.service",
                     "-p", "Id", "-p", "LoadState", "-p", "ActiveState", "-p", "MainPID"]).stdout
        assert "LoadState=masked" in state.split("\n\n")[0] and "ActiveState=active" not in state, state
        # Installing a formerly absent unit changes not-found to loaded. That
        # is payload discovery, not startup; compare actual process lifetimes.
        return run(["systemctl", "--user", "show", "aerial.service", "aerial-tray.service", "aerial-idle.service",
                    "-p", "Id", "-p", "ActiveState", "-p", "MainPID", "-p", "InvocationID"]).stdout

    default_profile()
    package, declared, archive = build_package(owned, output, (root / "packaging").resolve(strict=True))
    observations.update(package=str(package.relative_to(output)), packageSha256=digest(package), archiveSha256=archive)
    metadata = run(["bsdtar", "-xOf", package, ".PKGINFO"]).stdout
    publish(output / "PKGINFO.txt", metadata)
    depends = re.findall(r"^depend = (.+)$", metadata, re.M)
    assert sorted(depends) == sorted(declared), (depends, declared)
    payload = output / "payload"; payload.mkdir()
    run(["bsdtar", "-xf", package, "-C", payload])
    check_layout(payload, "/usr", "/usr/lib/aerial-lock")
    run(["desktop-file-validate", payload / DESKTOP.relative_to("/")])
    manifest = {str(p.relative_to(payload)): {"sha256": digest(p) if p.is_file() and not p.is_symlink() else None,
                                              "mode": p.lstat().st_mode, "link": os.readlink(p) if p.is_symlink() else None}
                for p in sorted(payload.rglob("*")) if (p.is_file() or p.is_symlink()) and not p.name.startswith(".")}
    publish(output / "package-manifest.json", json.dumps(manifest, indent=2))
    # Earlier phases' build-tree `sudo make install` is removed by the product's
    # own exact-path uninstall rule, so pacman needs no --overwrite.
    if run(["pacman", "-Q", "aerial-lock"], check=False).returncode:
        previous = run(["sudo", "make", "-C", source, "uninstall", "PREFIX=/usr"])
        publish(output / "make-uninstall.log", previous.stdout + previous.stderr)
        observations["replacedMakeInstall"] = True
    else:
        # Repeated qualification still exercises a real fresh installation,
        # not merely a reinstall labelled as fresh. No app/idle may be running.
        assert not processes(APP) and not processes(Path("/usr/bin/aerial-idle"))
        previous = run(["sudo", "pacman", "-R", "--noconfirm", "aerial-lock"], timeout=300)
        publish(output / "package-remove.log", previous.stdout + previous.stderr)
    leftovers = [relative for relative in manifest if (Path("/") / relative).exists()
                 and run(["pacman", "-Qqo", "/" + relative], check=False).returncode]
    assert not leftovers, leftovers
    before = default_profile()
    fresh = run(["sudo", "pacman", "-U", "--noconfirm", package], timeout=300)
    publish(output / "install.log", fresh.stdout + fresh.stderr)
    for relative, entry in manifest.items():
        installed = Path("/") / relative
        assert installed.lstat().st_mode == entry["mode"], relative
        assert (os.readlink(installed) if entry["link"] else digest(installed)) == (entry["link"] or entry["sha256"]), relative
    assert default_profile() == before, "package install changed tester's services"
    assert not processes(APP) and not processes(Path("/usr/bin/aerial-idle")), "install started a tray/idle process"
    run(["desktop-file-validate", DESKTOP])
    verify = run(["systemd-analyze", "--user", "verify", TRAY_UNIT, "/usr/lib/systemd/user/aerial.service",
                  "/usr/lib/systemd/user/aerial-idle.service"], env=env, check=False)
    publish(output / "systemd-analyze.log", verify.stdout + verify.stderr)
    assert verify.returncode == 0 and not verify.stderr.strip(), verify.stderr
    # Every NEEDED library and QML module of the app belongs to a declared dependency.
    needed = re.findall(r"\(NEEDED\)\s+Shared library: \[(.+)\]", run(["readelf", "-d", APP]).stdout)
    owners = {run(["pacman", "-Qqo", "/usr/lib/" + library]).stdout.strip() for library in needed}
    for module in ("QtQuick", "QtQuick/Controls", "QtQuick/Layouts", "QtQuick/Shapes"):
        owners.add(run(["pacman", "-Qqo", "/usr/lib/qt6/qml/" + module + "/qmldir"]).stdout.strip())
    assert owners <= set(depends), (owners, depends)
    observations.update(appLibraryOwners=sorted(owners), declaredDepends=depends)

    user = "aerial-ui-install-" + str(os.getpid())
    home = output / "home"; home.mkdir(mode=0o755)
    spawned, handles, acls, created, task_env = [], [], {}, False, {}

    def as_user(argv, **kwargs):
        return run(["sudo", "-u", user, "env", *[k + "=" + v for k, v in task_env.items()], *argv], **kwargs)

    def ctl(*args):
        return as_user(["systemctl", "--user", *args]).stdout.strip()

    def show(*units):
        text = ctl("show", *units, "-p", "Id", "-p", "ActiveState", "-p", "MainPID", "-p", "InvocationID", "-p", "UnitFileState")
        return {entry["Id"]: entry for entry in (dict(line.split("=", 1) for line in block.splitlines()) for block in text.split("\n\n"))}

    def units():
        return show("aerial.service", "aerial-tray.service", "aerial-idle.service")

    def windows():
        return json.loads(run(["niri", "msg", "-j", "windows"], env=env).stdout)

    def task_windows():
        return [w for w in windows() if w.get("app_id") == "org.aeriallock.AerialSettings" and os.stat("/proc/%d" % w["pid"]).st_uid == uid]

    cleanup = Cleanup()

    try:
        run(["sudo", "useradd", "--user-group", "--no-create-home", "--home-dir", home, "--shell", "/bin/bash", user]); created = True
        uid = pwd.getpwnam(user).pw_uid
        runtime = Path("/run/user") / str(uid)
        task_env = dict(HOME=str(home), USER=user, LOGNAME=user, XDG_RUNTIME_DIR=str(runtime),
                        DBUS_SESSION_BUS_ADDRESS="unix:path=" + str(runtime / "bus"),
                        WAYLAND_DISPLAY=str(Path(env["XDG_RUNTIME_DIR"]) / env["WAYLAND_DISPLAY"]), TMPDIR=str(home / "tmp"),
                        LIBGL_ALWAYS_SOFTWARE="1", QSG_RHI_BACKEND="opengl", NO_AT_BRIDGE="1", GTK_A11Y="none")
        # Config and session wiring exist before the manager starts; the
        # installed vendor units themselves are used unmodified.
        for directory in ("tmp", ".config/systemd/user", ".config/aerial-lock", ".config/environment.d"):
            (home / directory).mkdir(parents=True, exist_ok=True)
        publish(home / ".config/aerial-lock/config.json", json.dumps({"sources": [], "acquisition": {"automatic": {"enabled": False}}}))
        publish(home / ".config/environment.d/50-aerial-test.conf",
                "".join(k + "=" + v + "\n" for k, v in task_env.items() if k not in ("HOME", "USER", "LOGNAME", "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS")))
        publish(home / ".config/systemd/user/aerial-test-session.target",
                "[Unit]\nDescription=Task graphical login\nBindsTo=graphical-session.target\nBefore=graphical-session.target\n")
        run(["sudo", "chown", "-R", f"{uid}:{uid}", home])
        for path in access_paths(output, env["XDG_RUNTIME_DIR"], task_env["WAYLAND_DISPLAY"]):
            acls[str(path)] = run(["getfacl", "-p", path]).stdout
            run(["sudo", "setfacl", "-m", f"u:{uid}:" + ("rw" if path.is_socket() else "x"), path])
        run(["sudo", "loginctl", "enable-linger", user])
        wait(lambda: subprocess.run(["sudo", "test", "-S", str(runtime / "bus")]).returncode == 0, "task user manager bus")
        wait(lambda: as_user(["systemctl", "--user", "is-system-running"], check=False).stdout.strip() in ("running", "degraded"), "task manager")
        first = wait(lambda: (lambda u: u if u["aerial.service"]["ActiveState"] == "active" else None)(units()), "vendor content service")
        ctl("start", "aerial-test-session.target")
        time.sleep(2)
        login = units()
        assert login["aerial-tray.service"]["UnitFileState"] == "disabled" and login["aerial-tray.service"]["MainPID"] == "0", login
        assert login["aerial-idle.service"]["UnitFileState"] == "disabled" and login["aerial-idle.service"]["MainPID"] == "0", login
        assert login["aerial.service"]["InvocationID"] == first["aerial.service"]["InvocationID"]
        observations["freshLogin"] = login
        # Desktop launch through the installed entry is D-Bus activation of the installed binary.
        as_user(["gio", "launch", DESKTOP])
        window = wait(task_windows, "installed app window")[0]
        assert executable(window["pid"]) == APP
        bus_pid = int(as_user(["busctl", "--user", "call", "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                               "GetConnectionUnixProcessID", "s", "org.aeriallock.AerialSettings"]).stdout.split()[1])
        assert bus_pid == window["pid"]
        as_user(["gdbus", "call", "--session", "--dest", "org.aeriallock.AerialSettings", "--object-path", "/org/aeriallock/AerialSettings",
                 "--method", "org.freedesktop.Application.Activate", "{}"])
        time.sleep(1)
        assert [w["id"] for w in task_windows()] == [window["id"]], "desktop Activate created a second window"
        run(["niri", "msg", "action", "close-window", "--id", window["id"]], env=env)
        wait(lambda: not processes(APP), "installed app natural exit")
        observations["desktopLaunch"] = {"window": window, "busOwner": bus_pid, "singleWindowAfterActivate": True}
        # The installed app samples the installed theme previews from the service's bundle.
        trace = home / "previews.strace"
        handle = (output / "traced-app.log").open("wb"); handles.append(handle)
        traced = subprocess.Popen(["sudo", "-u", user, "env", *[k + "=" + v for k, v in task_env.items()], "strace", "-f", "-qq",
                                   "-z", "-e", "trace=openat", "-o", str(trace), str(APP)], stdout=handle, stderr=handle)
        spawned.append(traced)
        window = wait(task_windows, "traced app window")[0]
        expected = {"/usr/share/aerial-lock/Themes/previews/" + n + ".png" for n in ("Macintosh", "Windows7", "MaterialYou", "Enfield", "Clockwork")}
        reads = lambda: set(re.findall(r'openat\([^"]*"([^"]+\.png)"', trace.read_text())) if trace.exists() else set()
        opened = wait(lambda: (lambda s: s if expected <= s else None)(reads()), "installed preview reads")
        run(["niri", "msg", "action", "close-window", "--id", window["id"]], env=env)
        assert traced.wait(timeout=20) == 0
        observations["installedPreviewsRead"] = sorted(expected & opened)
        after = units()
        assert after["aerial.service"]["InvocationID"] == first["aerial.service"]["InvocationID"], "settings lifetime restarted the service"
        # Reinstall/upgrade: no restart, no tray/idle start, user enablement preserved.
        ctl("enable", "aerial-tray.service")
        enabled = units()
        upgrade = run(["sudo", "pacman", "-U", "--noconfirm", package], timeout=300)
        publish(output / "reinstall.log", upgrade.stdout + upgrade.stderr)
        time.sleep(2)
        reinstalled = units()
        assert reinstalled == enabled, (enabled, reinstalled)
        assert reinstalled["aerial-tray.service"]["UnitFileState"] == "enabled" and reinstalled["aerial-tray.service"]["MainPID"] == "0"
        assert not processes(APP) and not processes(Path("/usr/bin/aerial-idle"))
        assert default_profile() == before
        ctl("disable", "aerial-tray.service")
        observations["reinstall"] = {"before": enabled, "after": reinstalled, "trayEnablementPreserved": True, "serviceNotRestarted": True}
        publish(output / "result.json", json.dumps(observations, indent=2))
        return {"exactTreePackage": True, "noNewRuntimeDependency": True, "freshInstallNoStart": True, "desktopActivation": True,
                "installedPreviews": True, "reinstallNoRestart": True, "trayDefaultOff": True, "output": str(output)}
    finally:
        if created:
            cleanup("units", lambda: as_user(["systemctl", "--user", "stop", "aerial-test-session.target", "aerial-tray.service",
                                              "aerial-idle.service", "aerial.service"], check=False))
            for process in reversed(spawned):
                cleanup("launcher", lambda process=process: stop_process(process))
            cleanup("linger", lambda: run(["sudo", "loginctl", "disable-linger", user]))
            cleanup("terminate", lambda: run(["sudo", "loginctl", "terminate-user", user]))
            cleanup("manager exit", lambda: wait(lambda: subprocess.run(["pgrep", "-u", user], capture_output=True).returncode == 1, "task processes gone"))
            for path, acl in reversed(list(acls.items())):
                cleanup("acl " + path, lambda acl=acl: run(["sudo", "setfacl", "-P", "--restore=-"], input=acl))
            cleanup("userdel", lambda: run(["sudo", "userdel", user]))
            cleanup("chown", lambda: run(["sudo", "chown", "-R", f"{os.getuid()}:{os.getgid()}", home]))
        for handle in handles:
            cleanup("log close", handle.close)
        cleanup.finish()
