#!/usr/bin/env python3
"""Real guest user-manager qualification in a disposable task account."""
import json
import os
from pathlib import Path
import pwd
import subprocess
import shlex
import time

from fixture_files import publish
from settings_fixture_cleanup import Cleanup, access_paths, stop_process


def unit_argument(value):
    value = str(value).replace("\\", "\\\\").replace('"', '\\"').replace("%", "%%").replace("$", "$$")
    return '"' + value.replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t") + '"'


def retire_launchers(cleanup, processes, host):
    for process in reversed(processes):
        if process is not host:
            cleanup("launcher", lambda process=process: stop_process(process))


def missing_executable_unit(text, binary, missing):
    return text.replace(unit_argument(binary), unit_argument(missing))


def unit_start_usec(run, unit):
    manager = "org.freedesktop.systemd1"
    result = shlex.split(run(["busctl", "--user", "call", manager, "/org/freedesktop/systemd1", manager + ".Manager", "GetUnit", "s", unit], capture_output=True, text=True).stdout)
    if len(result) != 2 or result[0] != "o": raise RuntimeError("invalid unit object path reply")
    stamp = run(["busctl", "--user", "get-property", manager, result[1], manager + ".Service", "ExecMainStartTimestamp"], capture_output=True, text=True).stdout.split()
    if len(stamp) != 2 or stamp[0] != "t": raise RuntimeError("invalid raw unit timestamp reply")
    return int(stamp[1])


def units(owned, output):
    source, build = owned.paths["source"], owned.paths["build"]
    probe = owned.check_binary(build / "settings-units-probe")
    binary = owned.check_binary(source / "settings/aerial-settings")
    idle_binary = owned.check_binary(source / "idle/aerial-idle")
    user = "aerial-ui-unit-" + str(os.getpid())
    home = output / "home"
    home.mkdir(mode=0o755)
    env = dict(os.environ, **owned.record["environment"])
    processes, handles, acls = [], [], {}
    created = False
    task_env = {}

    def run(argv, **kwargs):
        kwargs.setdefault("timeout", 30)
        kwargs.setdefault("check", True)
        return subprocess.run(argv, **kwargs)

    def wait(read, label, seconds=25):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            value = read()
            if value:
                return value
            time.sleep(.08)
        raise RuntimeError("not observed: " + label)

    def as_user(argv, **kwargs):
        return run(["sudo", "-u", user, "env", *[key + "=" + value for key, value in task_env.items()], *map(str, argv)], **kwargs)

    def ctl(*args):
        return as_user(["systemctl", "--user", *args], capture_output=True, text=True).stdout.strip()

    def show(unit):
        text = ctl("show", unit, "--timestamp=unix", "-p", "ActiveState", "-p", "MainPID", "-p", "InvocationID", "-p", "Result",
                   "-p", "ExecMainStatus", "-p", "ControlGroup", "-p", "UnitFileState", "-p", "ExecMainStartTimestamp")
        return dict(line.split("=", 1) for line in text.splitlines())

    def spawn(argv, name):
        handle = (output / (name + ".log")).open("wb"); handles.append(handle)
        process = subprocess.Popen(["sudo", "-u", user, "env", *[key + "=" + value for key, value in task_env.items()], *map(str, argv)],
                                   stdout=handle, stderr=handle)
        processes.append(process)
        return process

    def host_call(method, *args):
        return as_user(["qs", "-p", host_file, "ipc", "call", "proof", method, *map(str, args)], capture_output=True, text=True).stdout.strip()

    def info():
        try:
            return json.loads(host_call("info"))
        except (subprocess.CalledProcessError, json.JSONDecodeError):
            return {}

    def windows():
        return json.loads(run(["niri", "msg", "-j", "windows"], env=env, capture_output=True, text=True).stdout)

    def tray_windows():
        pid = int(show("aerial-tray.service")["MainPID"])
        return [w for w in windows() if pid and w["pid"] == pid]

    def harness(mode, name):
        result_file = home / (name + ".json")
        tested = as_user([probe, source, result_file, mode], capture_output=True, text=True, timeout=120, check=False)
        publish(output / (name + ".log"), tested.stdout + tested.stderr)
        assert tested.returncode == 0 and not tested.stdout + tested.stderr, (name, tested.returncode, tested.stdout + tested.stderr)
        receipt = json.loads(as_user(["cat", result_file], capture_output=True, text=True).stdout)
        captures = []
        for image in receipt.get("captures", []):
            target = output / Path(image).name
            target.write_bytes(as_user(["cat", image], capture_output=True).stdout)
            captures.append(str(target))
        receipt["captures"] = captures
        return receipt

    def session(start):
        # graphical-session.target refuses manual start; bind it like a compositor unit does.
        ctl("start" if start else "stop", "aerial-test-session.target")

    cleanup = Cleanup()

    host = None
    host_file = home / "host.qml"
    try:
        run(["sudo", "useradd", "--user-group", "--no-create-home", "--home-dir", str(home), "--shell", "/bin/bash", user]); created = True
        uid = pwd.getpwnam(user).pw_uid
        runtime = Path("/run/user") / str(uid)
        task_env = dict(HOME=str(home), USER=user, LOGNAME=user, XDG_RUNTIME_DIR=str(runtime),
                        DBUS_SESSION_BUS_ADDRESS="unix:path=" + str(runtime / "bus"),
                        WAYLAND_DISPLAY=str(Path(env["XDG_RUNTIME_DIR"]) / env["WAYLAND_DISPLAY"]),
                        AERIAL_LOCK_SHELL=str(source), TMPDIR=str(home / "tmp"),
                        LIBGL_ALWAYS_SOFTWARE="1", QSG_RHI_BACKEND="opengl", NO_AT_BRIDGE="1", GTK_A11Y="none")
        # Everything the user manager reads at startup exists before it starts:
        # no default-source service and no installed vendor binary may run.
        units_dir = home / ".config/systemd/user"
        for directory in ("tmp", ".config/systemd/user", ".config/aerial-lock", ".config/environment.d"):
            (home / directory).mkdir(parents=True, exist_ok=True)
        publish(home / ".config/aerial-lock/config.json", json.dumps({"sources": [], "acquisition": {"automatic": {"enabled": False}},
                                                                     "idle": {"timeoutSeconds": 0, "lockOnSuspend": False, "logindLockUnlock": False}}))
        publish(home / ".config/environment.d/50-aerial-test.conf",
                "".join(key + "=" + value + "\n" for key, value in task_env.items() if key not in ("HOME", "USER", "LOGNAME", "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS")))
        tray_unit = units_dir / "aerial-tray.service"
        tray_text = (source / "systemd/aerial-tray.service.in").read_text().replace('"@BINDIR@/aerial-settings"', unit_argument(binary))
        publish(tray_unit, tray_text)
        publish(units_dir / "aerial-idle.service", (source / "systemd/aerial-idle.service.in").read_text().replace('"@BINDIR@/aerial-idle"', unit_argument(idle_binary)))
        publish(units_dir / "aerial.service", "[Service]\nType=exec\nExecStart=" + unit_argument(source / "companion/aerial") + "\n")
        publish(units_dir / "aerial-test-session.target", "[Unit]\nDescription=Task graphical login\nBindsTo=graphical-session.target\nBefore=graphical-session.target\n")
        publish(host_file, (source / "Tests/settings-tray-host.qml").read_text())
        run(["sudo", "chown", "-R", f"{uid}:{uid}", str(home)])
        for path in access_paths(output, env["XDG_RUNTIME_DIR"], task_env["WAYLAND_DISPLAY"], probe, binary, idle_binary):
            acls[str(path)] = run(["getfacl", "-p", str(path)], capture_output=True, text=True).stdout
            run(["sudo", "setfacl", "-m", f"u:{uid}:" + ("rw" if path.is_socket() else "rx" if path.is_file() else "x"), str(path)])
        run(["sudo", "loginctl", "enable-linger", user])
        wait(lambda: subprocess.run(["sudo", "test", "-S", str(runtime / "bus")]).returncode == 0, "task user manager bus")
        wait(lambda: as_user(["systemctl", "--user", "is-system-running"], capture_output=True, text=True, check=False).stdout.strip() in ("running", "degraded"),
             "task user manager startup")
        ctl("start", "aerial.service"); session(True)
        observations = {"initial": {"tray": show("aerial-tray.service"), "idle": show("aerial-idle.service")}}
        assert observations["initial"]["tray"]["UnitFileState"] == "disabled" and observations["initial"]["tray"]["MainPID"] == "0"
        assert observations["initial"]["idle"]["UnitFileState"] == "disabled"
        # Idle: Turn on enables/starts; save never restarts; explicit restart is a new invocation after the save.
        idle = harness("idle", "idle")
        config_mtime = int(as_user(["python3", "-c", "import os,sys; print(os.stat(sys.argv[1]).st_mtime_ns)", home / ".config/aerial-lock/config.json"], capture_output=True, text=True).stdout)
        unit = show("aerial-idle.service")
        assert unit["ActiveState"] == "active" and unit["UnitFileState"] == "enabled"
        assert unit["InvocationID"] == idle["restarted"]["invocation"] != idle["afterSave"]["invocation"] == idle["turnedOn"]["invocation"]
        started = unit_start_usec(as_user, "aerial-idle.service")
        assert started >= config_mtime // 1000, (started, config_mtime)
        timeout = json.loads(as_user([source / "cli/aerialctl", "config", "show", "--json"], capture_output=True, text=True).stdout)["idle"]["timeoutSeconds"]
        assert timeout == 3600
        observations["idle"] = dict(idle, final=unit, configMtime=config_mtime, startTimestampUsec=started, savedTimeout=timeout)
        ctl("disable", "--now", "aerial-idle.service")
        # Starting a disabled unit is legal: Turn on must not claim that a
        # subsequent no-op StartUnit applied the currently displayed settings.
        ctl("start", "aerial-idle.service")
        observations["idleExisting"] = harness("idle-existing", "idle-existing")
        ctl("disable", "--now", "aerial-idle.service")
        # Window-first: About enables/starts; the unit's --tray forwards to the open owner and exits 0.
        host = spawn(["qs", "-p", host_file], "host")
        wait(lambda: info().get("count") == 0, "real tray host")
        observations["windowFirst"] = harness("tray", "window-first")
        wait(lambda: info().get("count") == 0, "window-first harness teardown")
        # Native facts behind window-first completion: an enabled Type=exec run that
        # forwarded to the open window owner stays loaded, inactive and successful.
        owner = spawn([binary], "window-owner")
        owner_window = wait(lambda: [w for w in windows() if w.get("app_id") == "org.aeriallock.AerialSettings" and os.stat("/proc/%d" % w["pid"]).st_uid == uid], "window-first owner")[0]
        before = show("aerial-tray.service")["InvocationID"]
        ctl("enable", "aerial-tray.service"); ctl("start", "aerial-tray.service")
        forwarded = wait(lambda: (lambda unit: unit if unit["ActiveState"] == "inactive" and unit["InvocationID"] else None)(show("aerial-tray.service")), "forwarding unit exit")
        assert forwarded["Result"] == "success" and forwarded["ExecMainStatus"] == "0" and forwarded["InvocationID"] != before, forwarded
        wait(lambda: info().get("count") == 1, "forwarded tray shown by the window owner")
        host_call("trigger", 6)
        wait(lambda: info().get("count") == 0 and show("aerial-tray.service")["UnitFileState"] == "disabled", "owner Hide disables startup")
        assert owner.poll() is None and any(w["id"] == owner_window["id"] for w in windows())
        run(["niri", "msg", "action", "close-window", "--id", str(owner_window["id"])], env=env, capture_output=True)
        assert owner.wait(timeout=15) == 0
        observations["windowFirst"]["nativeForwardedUnit"] = forwarded
        # The next graphical login starts exactly one actual binary from its unit.
        ctl("enable", "aerial-tray.service"); session(False); session(True)
        wait(lambda: info().get("count") == 1, "enabled tray at next graphical session")
        unit = show("aerial-tray.service"); pid = int(unit["MainPID"])
        assert pid > 0 and not tray_windows()
        as_user([binary, "--tray"], capture_output=True)
        assert info()["count"] == 1 and int(show("aerial-tray.service")["MainPID"]) == pid
        as_user(["gdbus", "call", "--session", "--dest", "org.aeriallock.AerialSettings", "--object-path", "/org/aeriallock/AerialSettings",
                 "--method", "org.freedesktop.Application.Activate", "{}"], capture_output=True)
        window = wait(tray_windows, "unit-owned settings window")[0]
        host_call("trigger", 6)
        wait(lambda: info().get("count") == 0, "Hide unregisters")
        wait(lambda: show("aerial-tray.service")["UnitFileState"] == "disabled", "Hide disables startup")
        assert show("aerial-tray.service")["MainPID"] == str(pid) and tray_windows(), "Hide killed a window cgroup"
        run(["niri", "msg", "action", "close-window", "--id", str(window["id"])], env=env, capture_output=True)
        wait(lambda: show("aerial-tray.service")["ActiveState"] == "inactive", "normal unit exit")
        observations["trayFirst"] = {"started": unit, "afterHideAndClose": show("aerial-tray.service"), "samePidSurvivedHide": pid}
        session(False); session(True)
        assert show("aerial-tray.service")["MainPID"] == "0" and info()["count"] == 0
        # A real Type=exec unit whose executable cannot start surfaces an error in the open About page.
        publish_as = lambda path, text: run(["sudo", "-u", user, "tee", str(path)], input=text, text=True, capture_output=True)
        publish_as(tray_unit, missing_executable_unit(tray_text, binary, output / "missing-aerial-settings"))
        ctl("daemon-reload")
        observations["failure"] = harness("failure", "failure")
        publish_as(tray_unit, tray_text); ctl("daemon-reload"); ctl("disable", "aerial-tray.service"); ctl("reset-failed")
        assert show("aerial.service")["ActiveState"] == "active", "service stopped by settings lifetimes"
        host_call("quit"); assert host.wait(timeout=10) == 0
        publish(output / "result.json", json.dumps(observations, indent=2))
        return {"realUserManager": True, "idleTurnOnSaveRestart": True, "aboutEnableHideRace": True, "graphicalStartupAndDisable": True,
                "trayOwnedWindowSurvivesHide": True, "failureActionable": True, "output": str(output)}
    finally:
        if created:
            if host and host.poll() is None:
                cleanup("host quit", lambda: host_call("quit"))
                cleanup("host wait", lambda: host.wait(timeout=100))
            # Only task-created units/processes and account are retired.
            cleanup("units", lambda: as_user(["systemctl", "--user", "stop", "aerial-test-session.target", "aerial-tray.service",
                                              "aerial-idle.service", "aerial.service"], capture_output=True, check=False))
            retire_launchers(cleanup, processes, host)
            if host is None or host.poll() is not None:
                cleanup("linger", lambda: run(["sudo", "loginctl", "disable-linger", user]))
                cleanup("terminate", lambda: run(["sudo", "loginctl", "terminate-user", user]))
                cleanup("manager exit", lambda: wait(lambda: subprocess.run(["pgrep", "-u", user], capture_output=True).returncode == 1, "task processes gone"))
                for path, acl in reversed(list(acls.items())):
                    cleanup("acl " + path, lambda acl=acl: run(["sudo", "setfacl", "-P", "--restore=-"], input=acl, text=True))
                cleanup("userdel", lambda: run(["sudo", "userdel", user]))
                cleanup("chown", lambda: run(["sudo", "chown", "-R", f"{os.getuid()}:{os.getgid()}", str(home)]))
            else:
                cleanup.failures.append("Quickshell host still running; task account and access retained for explicit cleanup approval")
        for handle in handles:
            cleanup("log close", handle.close)
        cleanup.finish()
