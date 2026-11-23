#!/usr/bin/env python3
"""Real portal and desktop consumers in the receipt-checked project guest.

Called by settings-vm.py. Protocol denials/missing peers are tested separately.
No desktop service is replaced; chooser/file manager/browser are real programs.
"""
import functools
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import threading
import time

from fixture_files import publish
from private_runtime import PrivateRuntime, bus_address, bus_config
from settings_fixture_cleanup import Cleanup, stop_process
from desktop_entry import exec_argument


def new_chooser(windows, before, title):
    candidates = [w for w in windows if w["id"] not in before and w["title"] == title]
    if len(candidates) > 1: raise RuntimeError("ambiguous new chooser ownership; no dialog was selected")
    return candidates[0] if candidates else None


def restore_mime_override(path, original, handler):
    """Revert only our still-owned scheme entries, never another writer's file."""
    def entries(lines):
        section = ""
        for index, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith("[") and stripped.endswith("]"): section = stripped
            elif section in ("[Default Applications]", "[Added Associations]") and "=" in line and not stripped.startswith(("#", ";")):
                key, value = line.split("=", 1)
                if key.strip() in ("x-scheme-handler/http", "x-scheme-handler/https"):
                    yield index, (section, key.strip()), [item for item in value.strip().split(";") if item]
    if not path.exists(): return
    baseline = {key: values for _, key, values in entries((original or b"").decode().splitlines(keepends=True))}
    lines = path.read_text().splitlines(keepends=True)
    for index, key, values in entries(lines):
        if handler not in values: continue
        # The first application owns the preference. A newer writer can retain
        # our handler as a fallback without granting us ownership of its default.
        prior = baseline.get(key, []) if values[0] == handler else []
        restored = list(dict.fromkeys([*prior, *(value for value in values if value != handler)]))
        lines[index] = key[1] + "=" + ";".join(restored) + ";\n" if restored else ""
    publish(path, ''.join(lines))


def profile_processes(profile):
    result = set()
    for process in Path("/proc").iterdir():
        if not process.name.isdecimal(): continue
        try:
            arguments = [os.fsdecode(value) for value in (process / "cmdline").read_bytes().split(b"\0") if value]
            if "--profile" in arguments and str(profile) in arguments: result.add(int(process.name))
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            continue
    return result


def profile_windows(windows, profile):
    pids = profile_processes(profile)
    return [window for window in windows if window.get("pid") in pids]


def portals(owned, output):
    root = owned.paths["root"]
    probe = owned.paths["build"] / "settings-desktop-probe"
    env = dict(os.environ, **owned.record["environment"], LIBGL_ALWAYS_SOFTWARE="1", QSG_RHI_BACKEND="opengl")
    env.pop("QT_QPA_PLATFORM", None)
    env.pop("QT_NO_XDG_DESKTOP_PORTAL", None)
    env.setdefault("LANG", "C.UTF-8")
    owned.check_session(owned.record["session"], env)
    desktop_bus = "unix:path=" + env["XDG_RUNTIME_DIR"] + "/bus"
    env["DBUS_SESSION_BUS_ADDRESS"] = desktop_bus
    processes, handles, owned_windows = [], [], set()
    def run(argv, **kwargs):
        kwargs.setdefault("env", env)
        return subprocess.run(argv, check=True, timeout=30, **kwargs)
    def windows():
        return json.loads(run(["niri", "msg", "-j", "windows"], capture_output=True, text=True).stdout)
    def wait(read, label, seconds=15):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            value = read()
            if value:
                return value
            time.sleep(.05)
        raise RuntimeError("not observed: " + label)
    def spawn(argv, name):
        handle = (output / (name + ".log")).open("wb"); handles.append(handle)
        process = subprocess.Popen(argv, env=env, stdout=handle, stderr=handle); processes.append(process)
        return process
    def close(window):
        run(["niri", "msg", "action", "close-window", "--id", str(window["id"])], capture_output=True)
        wait(lambda: not any(w["id"] == window["id"] for w in windows()), "consumer window close")
        owned_windows.discard(window["id"])
    folder = output / "Portal folder # % \u00e9"; folder.mkdir()
    video = output / "Video # % \u00e9.mp4"; video.write_bytes(b"portal selection fixture, not footage")
    requested = []
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            requested.append(self.path)
            payload = b"<!doctype html><title>Aerial portal destination</title><p>Portal destination reached</p>"
            self.send_response(200); self.send_header("Content-Length", str(len(payload))); self.end_headers(); self.wfile.write(payload)
        def log_message(self, *args):
            pass
    mime = Path(env.get("XDG_CONFIG_HOME") or Path.home() / ".config") / "mimeapps.list"
    old_mime = mime.read_bytes() if mime.exists() else None
    entry = Path.home() / ".local/share/applications/aerial-portal-test-browser.desktop"
    if entry.exists():
        raise RuntimeError("test desktop handler already exists; inspect before reuse")
    profile = output / "browser-profile"; profile.mkdir()
    publish(profile / "user.js", '\n'.join('user_pref("' + key + '", ' + value + ');' for key, value in {
        "browser.aboutwelcome.enabled": "false", "browser.shell.checkDefaultBrowser": "false",
        "browser.startup.homepage": '"about:blank"', "datareporting.healthreport.uploadEnabled": "false",
        "network.captive-portal-service.enabled": "false", "browser.startup.firstrunSkipsHomepage": "true"}.items()))
    entry.parent.mkdir(parents=True, exist_ok=True)
    results = []
    keyboard = None
    def key(code):
        run(["ydotool", "key", f"{code}:1", f"{code}:0"], env=dict(env, YDOTOOL_SOCKET=str(socket)))
    cleanup = Cleanup()
    server = None
    thread = None
    try:
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        publish(entry, '[Desktop Entry]\nType=Application\nName=Aerial portal test browser\nExec=/usr/bin/firefox --no-remote --profile ' + exec_argument(profile) + ' %u\nMimeType=x-scheme-handler/http;x-scheme-handler/https;\nTerminal=false\n')
        run(["xdg-mime", "default", entry.name, "x-scheme-handler/http"])
        run(["xdg-mime", "default", entry.name, "x-scheme-handler/https"])
        with PrivateRuntime("portals") as runtime:
            config = output / "private-bus.conf"
            publish(config, bus_config(runtime / "bus"))
            bus = spawn(["dbus-daemon", "--nofork", "--nopidfile", "--config-file=" + str(config)], "bus")
            wait(lambda: (runtime / "bus").exists(), "private service bus")
            env["AERIAL_TEST_SERVICE_BUS"] = bus_address(runtime / "bus")
            socket = runtime / "ydotool.sock"
            keyboard = spawn(["sudo", "-n", "ydotoold", "--socket-path=" + str(socket), "--socket-own=%d:%d" % (os.getuid(), os.getgid())], "ydotoold")
            wait(lambda: socket.exists(), "guest uinput keyboard")
            time.sleep(1)
            for name, kind, expected in [("folder", "folder", str(folder)), ("file", "file", str(video)), ("cancel", "folder", "cancel"), ("escape", "file", "closed")]:
                receipt = output / (name + ".json")
                before = {w["id"] for w in windows()}
                process = spawn([str(probe), "--native-choose", kind, expected, str(receipt)], name)
                title = "Choose a video folder" if kind == "folder" else "Choose a video file"
                def chooser():
                    if process.poll() is not None:
                        raise RuntimeError("chooser probe exited " + str(process.returncode) + ": " + (output / (name + ".log")).read_text())
                    return new_chooser(windows(), before, title)
                window = wait(chooser, "real GTK chooser")
                owned_windows.add(window["id"])
                run(["niri", "msg", "action", "focus-window", "--id", str(window["id"])], capture_output=True)
                wait(lambda: any(w["id"] == window["id"] and w["is_focused"] for w in windows()), "chooser keyboard focus")
                wtype = str(root / "tools/bin/wtype")
                if name == "cancel":
                    # GTK's Cancel button is three reverse focus steps from
                    # the chooser's initial focus; Return activates it.
                    for _ in range(3):
                        run(["ydotool", "key", "42:1", "15:1", "15:0", "42:0"], env=dict(env, YDOTOOL_SOCKET=str(socket))); time.sleep(.3)
                    run(["niri", "msg", "action", "screenshot-window", "--path", str(output / "cancel-focus.png")], capture_output=True)
                    key(28)
                elif name == "escape":
                    key(1)
                else:
                    # Text goes through the virtual keyboard; GTK's chooser
                    # does not activate on a virtual Return, so the accept key
                    # is a real uinput key event through the compositor.
                    run([wtype, "-M", "ctrl", "-k", "l", "-m", "ctrl"]); time.sleep(.8)
                    run([wtype, "-d", "20", expected]); time.sleep(.8)
                    run(["niri", "msg", "action", "screenshot-window", "--path", str(output / (name + "-typed.png"))], capture_output=True)
                    key(28)
                assert process.wait(timeout=20) == 0, (output / (name + ".log")).read_text()
                owned_windows.discard(window["id"])
                results.append(json.loads(receipt.read_text()))
            before = {w["id"] for w in windows()}
            process = spawn([str(probe), "--native-folder", str(folder), "accepted", str(output / "open-folder.json")], "open-folder")
            assert process.wait(timeout=20) == 0, (output / "open-folder.log").read_text()
            manager = wait(lambda: next((w for w in windows() if w["id"] not in before and folder.name in w["title"]), None), "file manager at selected folder")
            owned_windows.add(manager["id"]); results.append({"fileManager": manager})
            close(manager)
            before = {w["id"] for w in windows()}
            url = f'http://127.0.0.1:{server.server_port}/aerial-portal-destination'
            process = spawn([str(probe), "--native-link", url, "accepted", str(output / "open-link.json")], "open-link")
            assert process.wait(timeout=20) == 0, (output / "open-link.log").read_text()
            browser = wait(lambda: next(iter(profile_windows(windows(), profile)), None), "fixture browser profile window", 30)
            owned_windows.add(browser["id"])
            browser = wait(lambda: next((w for w in profile_windows(windows(), profile) if "Aerial portal destination" in w["title"]), None), "browser at portal destination", 30)
            owned_windows.add(browser["id"])
            assert "/aerial-portal-destination" in requested
            results.append({"browser": browser, "requested": requested})
            close(browser)
        publish(output / "result.json", json.dumps(results, indent=2))
        return {"realChooserAndServiceRoundTrip": results[:4], "fileManagerAndBrowser": results[4:], "output": str(output)}
    finally:
        current_windows = cleanup("list consumer windows", windows) or []
        for window in cleanup("identify fixture browser", lambda: profile_windows(current_windows, profile)) or []:
            owned_windows.add(window["id"])
        for window in current_windows:
            if window["id"] in owned_windows:
                cleanup("close consumer window", lambda window=window: close(window))
        for process in reversed(processes):
            if process is keyboard and process.poll() is None:
                cleanup("keyboard stop", lambda: subprocess.run(["sudo", "-n", "kill", str(process.pid)], check=True, timeout=10))
                cleanup("keyboard reap", lambda: process.wait(timeout=10))
            else:
                cleanup("child", lambda process=process: stop_process(process))
        for handle in handles:
            cleanup("log close", handle.close)
        if server is not None:
            if thread is not None and thread.is_alive():
                cleanup("HTTP shutdown", server.shutdown)
            cleanup("HTTP close", server.server_close)
        if thread is not None:
            cleanup("HTTP thread", lambda: thread.join(timeout=5))
        cleanup("restore owned MIME entries", lambda: restore_mime_override(mime, old_mime, entry.name))
        cleanup("remove desktop handler", lambda: entry.unlink(missing_ok=True))
        cleanup.finish()
