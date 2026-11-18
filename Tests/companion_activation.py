"""Installed systemd/dbus-broker and conventional Exec acceptance."""
import http.server
import json
import os
from pathlib import Path
import select
import subprocess
import threading
import time

from companion_install import check_layout
from fixture_files import publish
from owned_instance import OwnedInstance, digest


def run(argv, **kwargs):
    return subprocess.run(argv, check=True, capture_output=True, text=True, timeout=30, **kwargs)


def wait(read, label):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        value = read()
        if value:
            return value
        time.sleep(0.05)
    raise RuntimeError("outcome not observed: " + label)


def activation(args):
    owned = OwnedInstance(args.vm_record, args.root)
    owned.check(require_binaries=True)
    for path, expected in owned.record["installed_hashes"].items():
        if digest(path) != expected:
            raise RuntimeError("installed payload changed: " + path)
    check_layout(Path("/"), "/usr", "/usr/lib/aerial-lock")
    output = args.root / "results" / ("activation-" + time.strftime("%Y-%m-%d_%H%M%S"))
    output.mkdir(mode=0o700)
    media = output / "fixture.mp4"
    run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=96x64:rate=10",
         "-frames:v", "12", "-c:v", "libx264", "-movflags", "+faststart", str(media)])
    small = media.read_bytes()
    bodies = {"/small.mp4": small, "/large.mp4": small + bytes(4096)}
    bodies["/manifest.json"] = json.dumps({"assets": [
        {"id": "large", "url-1080-H264": "large.mp4"},
        {"id": "small", "url-1080-H264": "small.mp4"}]}).encode()
    requests = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def reply(self, head):
            requests.append({"method": self.command, "path": self.path})
            body = bodies.get(self.path)
            self.send_response(200 if body is not None else 404)
            self.send_header("Content-Length", str(len(body or b"")))
            self.send_header("ETag", '"activation-fixture"')
            self.end_headers()
            if not head and body:
                self.wfile.write(body)

        def do_HEAD(self):
            self.reply(True)

        def do_GET(self):
            self.reply(False)

        def log_message(self, *unused):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    helper = ["sudo", "python3", str(Path(__file__).with_name("companion-login-fixture.py")),
              "--root", str(args.root), "--vm-record", str(args.vm_record)]
    account = login = bus = None
    result = {"case": "activation", "exit": 1}
    try:
        document = {"sources": [{"id": "fixture", "type": "community", "name": "Fixture",
                                "source": f"http://127.0.0.1:{server.server_port}/manifest.json"}]}
        account = json.loads(run([*helper, "create"], input=json.dumps(document)).stdout)
        uid, name = account["uid"], account["name"]
        userenv = ["sudo", "runuser", "-u", name, "--", "env", f"XDG_RUNTIME_DIR=/run/user/{uid}",
                   f"DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/{uid}/bus"]

        def user(*argv, checked=True):
            return subprocess.run([*userenv, *argv], capture_output=True, text=True, timeout=15, check=checked)

        def status():
            response = user("aerialctl", "status", "--json", checked=False)
            return json.loads(response.stdout) if response.returncode == 0 else None

        def manager_login(label):
            log = (output / (label + ".log")).open("w")
            process = subprocess.Popen(["sudo", "machinectl", "shell", "--quiet", name + "@", "/usr/bin/sleep", "180"],
                                       stdin=subprocess.DEVNULL, stdout=log, stderr=log)
            log.close()
            return process

        login = manager_login("login")
        # Another account's runtime root is deliberately private. Observe its
        # socket as that account, not via tester's inaccessible pathname walk.
        wait(lambda: user("test", "-S", f"/run/user/{uid}/bus", checked=False).returncode == 0, "new login user bus")
        # Observe the vendor-started unit before invoking any activatable CLI.
        wait(lambda: user("systemctl", "--user", "is-active", "aerial-companion.service", checked=False).stdout.strip() == "active",
             "vendor wants link started companion without a client")
        initial = wait(status, "future-manager automatic startup")
        ready = wait(lambda: (value if (value := status()) and value["ReadyClips"] == 2 else None), "automatic fixture acquisition")
        payloads = [row["path"] for row in requests if row["method"] == "GET" and row["path"].endswith(".mp4")]
        assert payloads == ["/small.mp4", "/large.mp4"], payloads
        unit = user("systemctl", "--user", "show", "aerial-companion.service", "-p", "Type", "-p", "MainPID",
                    "-p", "UMask", "-p", "FragmentPath", "-p", "ActiveState").stdout
        broker = user("systemctl", "--user", "show", "dbus.service", "-p", "FragmentPath").stdout
        assert "Type=dbus" in unit and "ActiveState=active" in unit and "UMask=0077" in unit, unit
        assert "dbus-broker.service" in broker, broker
        assert user("systemctl", "--user", "is-active", "aerial-idle.service", checked=False).stdout.strip() != "active"
        assert run(["loginctl", "show-user", name, "-p", "Linger"]).stdout.strip() == "Linger=no"
        run([*helper, "stop"])
        login.wait(timeout=15)
        login = None
        run([*helper, "mask"])
        login = manager_login("masked-login")
        wait(lambda: user("test", "-S", f"/run/user/{uid}/bus", checked=False).returncode == 0, "masked login user bus")
        masked = user("systemctl", "--user", "is-enabled", "aerial-companion.service", checked=False)
        assert masked.stdout.strip() == "masked", masked.stdout + masked.stderr
        blocked = user("aerialctl", "status", "--json", checked=False)
        assert blocked.returncode == 1, blocked.stdout + blocked.stderr
        assert user("systemctl", "--user", "is-active", "aerial-companion.service", checked=False).stdout.strip() != "active"
        run([*helper, "unmask"])
        user("systemctl", "--user", "daemon-reload")
        # The owner is absent; the actual CLI triggers broker -> systemd, not a
        # private wrapper or a direct companion launch.
        activated = wait(status, "installed broker/systemd D-Bus backup")
        assert activated["ReadyClips"] == 2
        user("systemctl", "--user", "stop", "aerial-companion.service")
        user("systemctl", "--user", "start", "aerial-companion.service")
        explicit = wait(status, "documented explicit start-now")
        assert explicit["ReadyClips"] == 2
        run([*helper, "stop"])
        login.wait(timeout=15)
        login = None
        run([*helper, "remove"])
        account = None

        # Unmodified installed descriptor on a conventional daemon. Isolated
        # roots and an explicit empty source list prevent accidental live work.
        profile = output / "private"
        environment = dict(os.environ)
        for key, leaf in (("XDG_CONFIG_HOME", "config"), ("XDG_CACHE_HOME", "cache"),
                          ("XDG_STATE_HOME", "state"), ("XDG_RUNTIME_DIR", "runtime")):
            path = profile / leaf
            path.mkdir(mode=0o700, parents=True)
            environment[key] = str(path)
        environment.pop("AERIAL_LOCK_SHELL", None)
        config = profile / "config/aerial-lock/config.json"
        config.parent.mkdir()
        config.write_text('{"sources":[],"acquisition":{"automatic":{"enabled":false}}}')
        busconfig = output / "bus.conf"
        busconfig.write_text('<busconfig><type>session</type><auth>EXTERNAL</auth><listen>unix:path=' + str(output / "bus")
                             + '</listen><servicedir>/usr/share/dbus-1/services</servicedir><policy context="default">'
                               '<allow user="*"/><allow send_destination="*"/><allow receive_sender="*"/>'
                               '<allow own="*"/></policy></busconfig>')
        with (output / "private-bus.log").open("w") as log:
            bus = subprocess.Popen(["dbus-daemon", "--config-file=" + str(busconfig), "--nofork", "--nopidfile", "--print-address=1"],
                                   env=environment, stdout=subprocess.PIPE, stderr=log)
        assert select.select([bus.stdout], [], [], 5)[0]
        address = bus.stdout.readline().decode().strip()
        assert address.startswith("unix:")
        environment["DBUS_SESSION_BUS_ADDRESS"] = address
        exec_activation = run(["busctl", "--address=" + address, "call", "org.freedesktop.DBus", "/org/freedesktop/DBus",
                               "org.freedesktop.DBus", "StartServiceByName", "su", "org.aeriallock.Companion1", "0"], env=environment)
        assert exec_activation.stdout.strip() == "u 1", exec_activation.stdout
        private_status = json.loads(run(["aerialctl", "status", "--json"], env=environment).stdout)
        assert private_status["State"] == "paused" and private_status["ReadyClips"] == 0
        result.update(exit=0, startup=initial, ready=ready, firstPayloads=payloads, unit=unit, broker=broker,
                      maskRespected=True, brokerBackup=True, explicitStart=True, privateExec=private_status)
    except (RuntimeError, subprocess.SubprocessError, OSError, AssertionError, ValueError) as error:
        result["error"] = str(error)
        raise
    finally:
        if bus:
            bus.terminate()
            bus.wait(timeout=10)
        if account:
            run([*helper, "remove"])
        if login:
            login.wait(timeout=15)
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
        result["requests"] = requests
        publish(output / "result.json", json.dumps(result, indent=2))
    print("COMPANION_ACTIVATION_PASS " + str(output))
    return 0
