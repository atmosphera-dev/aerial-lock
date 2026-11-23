#!/usr/bin/env python3
"""Actual app -> SNI/DBusMenu -> real Quickshell host on a private bus."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import select
import threading
import time

from fixture_files import publish
from private_runtime import PrivateRuntime, bus_address, bus_config
from settings_fixture_cleanup import Cleanup, HostLease, stop_process


def menu_stopped(snapshot):
    entries = snapshot.get("entries", [])
    return snapshot.get("count") == 1 and len(entries) == 6 and not any(e.get("text") == "Stop download" for e in entries)


def release_transfer(event, failures, group, seconds=60):
    if event.wait(seconds):
        return True
    failures.append("transfer gate timed out: " + group)
    return False


class FixtureHttpServer(ThreadingHTTPServer):
    # server_close joins these handlers after the gates are released. Bound
    # socket reads/writes too, so a disconnected consumer cannot hold teardown.
    daemon_threads = False

    def get_request(self):
        socket, address = super().get_request()
        socket.settimeout(10)
        return socket, address


def retire_http(server, thread, releases):
    for event in releases:
        event.set()
    server.shutdown()
    server.server_close()
    thread.join(timeout=5)
    if thread.is_alive(): raise RuntimeError("HTTP server thread did not retire")


def tray(owned, output):
    root, source = owned.paths["root"], owned.paths["source"]
    binary = owned.check_binary(owned.paths["build"] / "settings-revamp-probe")
    env = dict(os.environ, **owned.record["environment"], LIBGL_ALWAYS_SOFTWARE="1", QSG_RHI_BACKEND="opengl", NO_AT_BRIDGE="1", GTK_A11Y="none")
    env.pop("QT_QPA_PLATFORM", None)
    owned.check_session(owned.record["session"], env)
    # This SNI-only host runs on a deliberately portal-free private bus. Qt 6.12
    # otherwise registers with its absent host portal and emits a QPA warning.
    # Scope the documented capability opt-out to this private fixture, including
    # its apps. Real desktop/portal qualification keeps the real capability.
    env["QT_NO_XDG_DESKTOP_PORTAL"] = "1"
    def host_environment():
        return dict(env, QT_NO_XDG_DESKTOP_PORTAL="1")
    processes, handles, hosts = [], [], []
    leases = {}
    def run(argv, environment=None, **kwargs):
        return subprocess.run(argv, env=env if environment is None else environment, check=True, timeout=30, **kwargs)
    def wait(read, label, seconds=20):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            value = read()
            if value:
                return value
            time.sleep(.08)
        raise RuntimeError("not observed: " + label)
    def spawn(argv, name, environment=None):
        handle = (output / (name + ".log")).open("wb"); handles.append(handle)
        process = subprocess.Popen(argv, env=env if environment is None else environment, stdout=handle, stderr=handle); processes.append(process)
        return process
    def windows():
        return json.loads(run(["niri", "msg", "-j", "windows"], capture_output=True, text=True).stdout)
    def app_windows():
        return [w for w in windows() if w["pid"] == app.pid]
    def close_windows():
        for w in app_windows():
            run(["niri", "msg", "action", "close-window", "--id", str(w["id"])], capture_output=True)
        wait(lambda: not app_windows(), "settings window destroyed")
    def cli(*args):
        return json.loads(run([str(source / "cli/aerialctl"), *args, "--json"], capture_output=True, text=True).stdout)
    def call(method, *args):
        if method == "quit":
            lease = leases.pop(host_file, None)
            if lease is not None: lease.finish()
        return run(["qs", "-p", str(host_file), "ipc", "call", "proof", method, *map(str, args)], environment=host_environment(), capture_output=True, text=True).stdout.strip()
    def lease_host(host, path):
        environment = host_environment()
        leases[path] = HostLease(host, lambda: subprocess.run(["qs", "-p", str(path), "ipc", "call", "proof", "keepalive"],
            env=environment, capture_output=True, check=True, timeout=10))
    def snap(name):
        path = output / (palette + "-" + name + ".png")
        run(["grim", "-o", info()["screen"], str(path)]); captures.append(str(path))
    def fixture_source():
        return json.dumps([{"id": "tray", "type": "community", "name": "Tray fixture", "source": f"http://127.0.0.1:{server.server_port}/{palette}/manifest.json"}])
    def info():
        try:
            return json.loads(call("info"))
        except (subprocess.CalledProcessError, json.JSONDecodeError):
            return {}
    captures, observations, requests, gate_failures = [], [], [], []
    media_file = output / "fixture.mp4"
    run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=96x64:rate=10", "-frames:v", "12", "-c:v", "libx264", "-movflags", "+faststart", "-y", str(media_file)])
    media = media_file.read_bytes() + b"\0" * 1000000
    releases = {"dark-first": threading.Event(), "dark-retry": threading.Event(), "light": threading.Event()}
    transfer_counts = {"dark": 0, "light": 0}
    transfer_lock = threading.Lock()
    class Handler(BaseHTTPRequestHandler):
        def do_HEAD(self):
            self.reply(True)
        def do_GET(self):
            self.reply(False)
        def reply(self, head):
            requests.append((self.command, self.path))
            group = self.path.split("/")[1]
            payload = json.dumps({"assets": [{"id": "manual-" + group, "accessibilityLabel": "Tray fixture <b>" + group + "</b> & literal", "url-1080-H264": "clip.mp4"}]}).encode() if self.path.endswith("/manifest.json") else media
            self.send_response(200); self.send_header("Content-Length", str(len(payload))); self.send_header("ETag", '"tray-fixture"'); self.end_headers()
            if head:
                return
            try:
                if self.path.endswith("/clip.mp4"):
                    with transfer_lock:
                        transfer_counts[group] += 1
                        gate = group if group == "light" else "dark-first" if transfer_counts[group] == 1 else "dark-retry"
                    self.wfile.write(payload[:65536]); self.wfile.flush()
                    if release_transfer(releases[gate], gate_failures, gate):
                        self.wfile.write(payload[65536:])
                else:
                    self.wfile.write(payload)
            except (BrokenPipeError, ConnectionResetError):
                pass
        def log_message(self, *_):
            pass
    server = FixtureHttpServer(("127.0.0.1", 0), Handler)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True); server_thread.start()
    app = None; pointer = None
    try:
        with PrivateRuntime("tray") as runtime:
            conf = output / "bus.conf"
            publish(conf, bus_config(runtime / "bus"))
            spawn(["dbus-daemon", "--nofork", "--nopidfile", "--config-file=" + str(conf)], "bus")
            wait(lambda: (runtime / "bus").exists(), "private bus")
            env.update(DBUS_SESSION_BUS_ADDRESS=bus_address(runtime / "bus"), DBUS_SYSTEM_BUS_ADDRESS=bus_address(runtime / "bus"), AERIAL_LOCK_SHELL=str(source))
            peer = owned.check_binary(owned.paths["build"] / "settings-portal-settings-peer")
            spawn([str(peer)], "settings-peer")
            wait(lambda: (output / "settings-peer.log").read_text().strip() == "SETTINGS_PEER_READY", "private Settings capability")
            for key in ("XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"):
                env[key] = str(output / key)
            config = Path(env["XDG_CONFIG_HOME"]) / "aerial-lock/config.json"
            config.parent.mkdir(parents=True)
            good_config = '{"sources":[],"acquisition":{"automatic":{"enabled":false}}}'
            publish(config, good_config)
            daemon = spawn([str(source / "companion/aerial")], "service")
            wait(lambda: config.with_name("companion.owner.lock").exists(), "service owner")
            for palette in ("dark", "light"):
                env.update(QT_QPA_PLATFORMTHEME="gtk3", GTK_THEME="Adwaita:dark" if palette == "dark" else "Adwaita")
                host_file = output / ("host-" + palette + ".qml")
                publish(host_file, (source / "Tests/settings-tray-host.qml").read_text())
                host = spawn(["qs", "-p", str(host_file)], "host-" + palette, environment=host_environment()); hosts.append((host, host_file))
                wait(lambda: info().get("count") == 0, "real Quickshell watcher")
                lease_host(host, host_file)
                app = spawn([str(binary), "--tray"], "app-" + palette)
                wait(lambda: len(info().get("entries", [])) == 6 and info().get("iconReady"), "real SNI and DBusMenu consumer")
                assert not app_windows(), "--tray must not create a window"
                assert info()["count"] == 1 and info()["title"] == "Aerial"
                assert info()["tooltipTitle"] == "Aerial" and "videos ready" in info()["tooltip"]
                entries = info()["entries"]
                assert [e["separator"] for e in entries] == [False, True, False, False, True, False], entries
                actions = [e for e in entries if not e["separator"]]
                assert all(e["enabled"] for e in actions), actions
                assert [e["text"] for e in actions] == ["Open Aerial", "Fetch videos now", "Resume automatic downloads", "Hide tray icon"], actions
                run([str(binary), "--tray"], capture_output=True)
                assert info()["count"] == 1 and not app_windows()
                time.sleep(.3); snap("badge-paused")
                if palette == "dark":
                    # Host/watcher replacement while the tray stays wanted re-registers once.
                    call("quit"); assert host.wait(timeout=10) == 0
                    assert app.poll() is None
                    host = spawn(["qs", "-p", str(host_file)], "host-dark-replacement", environment=host_environment()); hosts.append((host, host_file))
                    wait(lambda: info().get("count") == 1 and len(info().get("entries", [])) == 6 and info().get("iconReady"), "re-registration with a replacement watcher")
                    lease_host(host, host_file)
                    observations.append({"replacementHost": info()})
                call("trigger", 3); wait(lambda: cli("config", "show").get("acquisition", {}).get("automatic", {}).get("enabled") is True, "Resume persisted by service")
                wait(lambda: any(e.get("text") == "Pause automatic downloads" for e in info().get("entries", [])), "tray observes resumed state")
                call("trigger", 3); wait(lambda: cli("config", "show")["acquisition"]["automatic"]["enabled"] is False, "Pause persisted by service")
                call("trigger", 0); first = wait(lambda: app_windows(), "Open Aerial shows Overview")[0]
                selected = run(["gdbus", "call", "--session", "--dest", "org.aeriallock.AerialSettings", "--object-path", "/org/aeriallock/AerialSettings", "--method", "org.freedesktop.DBus.Properties.Get", "org.freedesktop.Application", "Page"], capture_output=True, text=True).stdout.strip()
                assert selected == "(<0>,)", "Open Aerial must select Overview: " + selected
                close_windows(); assert app.poll() is None
                call("activate"); reopened = wait(lambda: app_windows(), "fresh settings window")[0]
                assert reopened["id"] != first["id"]
                close_windows()
                position = info()
                publish(output / (palette + "-menu-position.json"), json.dumps(position, indent=2))
                pointer = subprocess.Popen([str(owned.check_binary(owned.paths["build"] / "virtual-pointer-probe")), position["screen"]], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
                processes.append(pointer)
                def input_reply(expected):
                    assert select.select([pointer.stdout], [], [], 5)[0], "native pointer reply timeout"
                    assert pointer.stdout.readline().strip() == expected
                def input_command(command, expected):
                    pointer.stdin.write(command + "\n"); pointer.stdin.flush(); input_reply(expected)
                input_reply("VIRTUAL_POINTER_READY")
                # The bar spans the output's full logical width; its geometry comes from the compositor.
                logical = next(o["logical"] for name, o in json.loads(run(["niri", "msg", "-j", "outputs"], capture_output=True, text=True).stdout).items() if name == position["screen"])
                assert logical["width"] == position["panelWidth"], (logical, position["panelWidth"])
                input_command("move %d %d %d %d" % (position["iconX"], position["iconY"], logical["width"], logical["height"]), "VIRTUAL_POINTER_MOVED")
                time.sleep(.2); input_command("click right", "VIRTUAL_POINTER_CLICKED")
                wait(lambda: info().get("menuVisible"), "native platform menu from real right-click")
                time.sleep(.3)
                assert not app_windows(), "right-click must not open settings"
                capture = output / (palette + "-menu.png")
                run(["grim", "-o", position["screen"], str(capture)]); captures.append(str(capture))
                call("tooltip"); time.sleep(.3)
                capture = output / (palette + "-tooltip.png")
                run(["grim", "-o", position["screen"], str(capture)]); captures.append(str(capture))
                call("dismiss")
                pointer.stdin.close(); assert pointer.wait(timeout=5) == 0; pointer = None
                publish(config, "{\n broken\n")
                wait(lambda: info().get("status") == 2, "native NeedsAttention")
                time.sleep(.3); snap("badge-problem")
                publish(config, good_config)
                wait(lambda: info().get("status") == 1, "native Active after repair")
                publish(config, '{"sources":"invalid","acquisition":{"automatic":{"enabled":false}}}')
                wait(lambda: info().get("status") == 2, "blocking field diagnostic needs attention")
                publish(config, good_config)
                wait(lambda: info().get("status") == 1, "native Active after field repair")
                observations.append(info())
                cli("config", "set", "sources", fixture_source())
                call("trigger", 2)
                wait(lambda: any(e.get("text") == "Stop download" for e in info().get("entries", [])), "manual paused acquisition")
                wait(lambda: cli("status").get("PartialBytes", 0) > 0, "download bytes before cancel")
                def downloading_tooltip():
                    value = info().get("tooltip", "")
                    return value if "Tray fixture" in value and "Downloading" in value else None
                tooltip = wait(downloading_tooltip, "asset title at real SNI consumer")
                assert "Downloading" in tooltip and "&lt;b&gt;" + palette + "&lt;/b&gt; &amp; literal" in tooltip and "<b>" not in tooltip, tooltip
                time.sleep(.3); snap("badge-downloading")
                if palette == "light":
                    releases["light"].set()
                    wait(lambda: menu_stopped(info()) and cli("status").get("PartialBytes", 1) == 0, "light fixture completes", 40)
                    cli("config", "set", "acquisition.automatic.enabled", "true")
                    wait(lambda: cli("status").get("DeferReason") in ("None", "") and cli("status").get("State") != "downloading", "up to date", 40)
                    wait(lambda: "Up to date" in info().get("tooltip", ""), "ready tooltip")
                    time.sleep(.3); snap("badge-ready")
                    cli("config", "set", "acquisition.automatic.enabled", "false")
                if palette == "dark":
                    call("trigger", 2)
                    wait(lambda: menu_stopped(info()), "tray cancellation")
                    releases["dark-first"].set()
                    assert cli("status")["PartialBytes"] > 0
                    call("trigger", 2)
                    wait(lambda: any(e.get("text") == "Stop download" for e in info().get("entries", [])), "second manual acquisition")
                assert not app_windows(), "no settings window before primary activation"
                call("activate"); wait(lambda: app_windows(), "primary activation creates a window")
                call("trigger", 5)
                wait(lambda: info().get("count") == 0, "Hide removes SNI without killing its window")
                assert app.poll() is None and app_windows()
                close_windows(); assert app.wait(timeout=10) == 0
                if palette == "dark":
                    releases["dark-retry"].set(); wait(lambda: cli("status").get("ReadyClips", 0) == 1, "same service completes after settings exit")
                    assert daemon.poll() is None
                    # Up to date: automatic on, nothing left to fetch.
                    app = spawn([str(binary), "--tray"], "app-dark-ready")
                    cli("config", "set", "acquisition.automatic.enabled", "true")
                    wait(lambda: cli("status").get("DeferReason") in ("None", "") and "Up to date" in info().get("tooltip", ""), "dark up to date", 40)
                    time.sleep(.3); snap("badge-ready")
                    cli("config", "set", "acquisition.automatic.enabled", "false")
                    call("trigger", 5)
                    # Without a user manager the failed startup change opens About.
                    wait(lambda: app.poll() is not None or app_windows(), "Hide settles")
                    if app.poll() is None:
                        close_windows()
                    assert app.wait(timeout=10) == 0
                cli("config", "set", "sources", "[]")
                call("quit"); assert host.wait(timeout=10) == 0
            retire_http(server, server_thread, releases.values())
            assert not gate_failures, gate_failures
            publish(output / "result.json", json.dumps({"captures": captures, "hostObservations": observations, "requests": requests}, indent=2))
            return {"captures": captures, "realQuickshellHost": True, "nativeMenuTooltipAndActivation": True, "manualPausedFetchCancelAndIndependentCompletion": True, "output": str(output)}
    finally:
        cleanup = Cleanup()
        for lease in leases.values(): cleanup("host lease", lease.finish)
        leases.clear()
        for event in releases.values():
            event.set()
        for host, path in hosts:
            if host.poll() is None:
                cleanup("host quit", lambda path=path: subprocess.run(["qs", "-p", str(path), "ipc", "call", "proof", "quit"], env=host_environment(), capture_output=True, check=True, timeout=10))
                cleanup("host wait", lambda host=host: host.wait(timeout=100)) # Natural watchdog exit if IPC could not connect.
        for process in reversed(processes):
            if process is pointer and process.poll() is None:
                cleanup("pointer input", process.stdin.close)
            if all(process is not host for host, _ in hosts):
                cleanup("child", lambda process=process: stop_process(process))
        for handle in handles:
            cleanup("log close", handle.close)
        cleanup("HTTP retirement", lambda: retire_http(server, server_thread, releases.values()))
        cleanup.finish()
