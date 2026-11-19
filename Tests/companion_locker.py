"""Installed supervisor/content acceptance with real nested lock/PAM consumers."""
import http.server
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import threading
import time

from fixture_files import publish
from owned_instance import digest
from private_runtime import PrivateRuntime


def run(argv, environment, **kwargs):
    return subprocess.run(argv, env=environment, check=True, capture_output=True, timeout=30, **kwargs)


def wait(read, label, seconds=30):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = read()
        if value:
            return value
        time.sleep(0.05)
    raise RuntimeError("outcome not observed: " + label)


def read_json(path):
    return json.loads(path.read_text()) if path.exists() else None


class Companion:
    def __init__(self, driver, session, document):
        self.driver, self.session = driver, session
        self.root = session.output / "profile"
        self.environment = dict(session.env, AERIAL_LOCK_SHELL="/usr/share/aerial-lock")
        for key, leaf in (("XDG_CONFIG_HOME", "config"), ("XDG_CACHE_HOME", "cache"), ("XDG_STATE_HOME", "state")):
            path = self.root / leaf
            path.mkdir(mode=0o700, parents=True)
            self.environment[key] = str(path)
        self.config = self.root / "config/aerial-lock/config.json"
        self.config.parent.mkdir()
        publish(self.config, json.dumps(document))
        self.played = self.root / "state/aerial-lock/last-playback.json"
        self.cache = self.root / "cache/aerial-lock/managed-v1"
        # No activation directory: absent/crashed service legs cannot be hidden
        # by an implicitly activated replacement on another environment.
        self.runtime = PrivateRuntime("content-bus")
        config = session.output / "content-bus.conf"
        config.write_text('<busconfig><type>session</type><auth>EXTERNAL</auth><listen>unix:path='
                          + str(self.runtime.path / "content-bus") + '</listen><policy context="default">'
                          '<allow user="*"/><allow send_destination="*"/><allow receive_sender="*"/>'
                          '<allow own="*"/></policy></busconfig>')
        self.bus = subprocess.Popen(["dbus-daemon", "--config-file=" + str(config), "--nofork", "--nopidfile", "--print-address=1"],
                                    env=self.environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if not select.select([self.bus.stdout], [], [], 5)[0]:
            self.bus.terminate()
            self.bus.wait(timeout=10)
            self.runtime.cleanup()
            raise RuntimeError("private content bus did not start")
        self.environment["DBUS_SESSION_BUS_ADDRESS"] = self.bus.stdout.readline().decode().strip()
        if not self.environment["DBUS_SESSION_BUS_ADDRESS"].startswith("unix:"):
            self.bus.wait(timeout=10)
            self.runtime.cleanup()
            raise RuntimeError("private content bus did not publish an address")
        self.service = None
        self.sequence = 0
        session.env.update(self.environment)
        session.shell = Path("/usr/share/aerial-lock")

    def cli(self, *arguments):
        return run(["/usr/bin/aerialctl", *arguments], self.environment)

    def status(self):
        reply = subprocess.run(["/usr/bin/aerialctl", "status", "--json"], env=self.environment,
                               capture_output=True, text=True, timeout=5)
        return json.loads(reply.stdout) if reply.returncode == 0 else None

    def start(self):
        self.sequence += 1
        self.service = self.driver.ObservedProcess(["/usr/bin/aerial"], self.environment,
                                                   self.session.output / f"companion-{self.sequence}.log")
        wait(self.status, "installed companion API")

    def stop(self):
        if self.service and self.service.process.poll() is None:
            self.service.process.send_signal(signal.SIGCONT)
            self.service.process.terminate()
            self.service.finish(10)
        self.service = None

    def inventory(self):
        pointer = read_json(self.cache / "current.json")
        return read_json(self.cache / ("generations/" + pointer["generation"] + ".json")) if pointer else None

    def forget_playback(self, label):
        if self.played.exists():
            self.played.rename(self.session.output / (label + "-prior-playback.json"))

    def close(self):
        self.stop()
        self.bus.terminate()
        self.bus.wait(timeout=10)
        self.runtime.cleanup()


def launch_episode(driver, session, cycle):
    session.assert_control()
    for path, expected in session.owned.record["installed_hashes"].items():
        if digest(path) != expected:
            raise RuntimeError("installed payload drift: " + path)
    environment = dict(session.env, WAYLAND_DEBUG="1", AERIAL_LOCK_SHELL="/usr/share/aerial-lock")
    session.locker = driver.ObservedProcess(["/usr/bin/aerial-lock"], environment,
                                            session.output / f"episode-{cycle:03}.redacted.log")
    session.locker.wait_line(lambda line: "ext_session_lock_v1" in line and ".locked()" in line)
    return session.locker


def capture(session, name):
    """Capture the actual nested lock window through its unlocked outer niri.

    Locked compositor screencopy may be prohibited. The outer VM session owns
    the nested compositor window and can capture it without weakening the lock.
    """
    path = session.output / (name + ".png")
    environment = dict(session.outer_env, **session.owned.record["environment"])
    run(["niri", "msg", "action", "screenshot-window", "--path", str(path)], environment)
    wait(path.exists, "outer compositor window capture")
    return path.name


def fixture_locker(args, installed_session):
    driver, session = installed_session(args, args.case)
    companion = server = thread = None
    release = threading.Event()
    entered = threading.Event()
    requests = []
    result = {"case": args.case, "backend": args.backend, "exit": 1, "observations": {}}
    result["sourceIdentity"] = session.owned.record["current_source_identity"]
    try:
        session.start()
        media = session.output / "fixture.mp4"
        run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
             "testsrc2=size=640x360:rate=15", "-frames:v", "45", "-c:v", "libx264", "-threads", "1",
             "-movflags", "+faststart", str(media)], session.env)
        small = media.read_bytes()
        bodies = {"/small.mp4": small, "/large.mp4": small + bytes(4096)}
        bodies["/manifest.json"] = json.dumps({"assets": [
            {"id": name, "url-1080-H264": name + ".mp4"} for name in ("large", "small")]}).encode()

        class Handler(http.server.BaseHTTPRequestHandler):
            def reply(self, head):
                requests.append({"method": self.command, "path": self.path})
                body = bodies.get(self.path)
                self.send_response(200 if body else 404)
                self.send_header("Content-Length", str(len(body or b"")))
                self.send_header("ETag", '"locker-fixture"')
                self.end_headers()
                if not head and body:
                    if self.path == "/small.mp4" and not release.is_set():
                        half = len(body) // 2
                        self.wfile.write(body[:half])
                        self.wfile.flush()
                        entered.set()
                        if not release.wait(60):
                            return
                        self.wfile.write(body[half:])
                    else:
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
        source = {"id": "fixture", "name": "Fixture", "type": "community",
                  "source": f"http://127.0.0.1:{server.server_port}/manifest.json"}
        companion = Companion(driver, session, {"sources": [source], "theme": "Macintosh.qml"})
        companion.start()
        assert entered.wait(10), "automatic transfer never reached the held payload"
        assert companion.status()["ReadyClips"] == 0
        launch_episode(driver, session, 1)
        # The real secret remains only in memory/stdin. Correct auth after the
        # completion proves the pre-completion draft was not discarded.
        secret = Path(session.owned.auth_fixture()["secret_file"]).read_bytes().removesuffix(b"\n")
        split = len(secret) // 2
        run(["wtype", "-d", "10", "-"], session.env, input=secret[:split])
        result["observations"]["solidFrame"] = capture(session, "held-solid")
        release.set()
        ready = wait(lambda: value if (value := companion.status()) and value["ReadyClips"] == 2 else None,
                     "automatic completed index")
        time.sleep(1)
        assert not companion.played.exists(), "new publication joined an existing empty episode"
        run(["wtype", "-d", "10", "-"], session.env, input=secret[split:])
        secret = None
        run(["wtype", "-k", "Return"], session.env)
        session.locker.wait_line(lambda line: ".unlock_and_destroy()" in line)
        assert session.locker.finish(20) == 0
        session.desktop()
        session.locker = None
        session.results.append({"cycle": 1, "retainedDraft": True, "unlock_and_destroy": True, "exit": 0, "desktop_input": True})
        result["observations"].update(heldSolidThroughCompletion=True, ready=ready)
        payloads = [row["path"] for row in requests if row["method"] == "GET" and row["path"].endswith(".mp4")]
        # An absent display preference is existence-written by the lock hint;
        # that config transaction may safely restart the held representation.
        order = [path for index, path in enumerate(payloads) if not index or path != payloads[index - 1]]
        assert order == ["/small.mp4", "/large.mp4"], payloads
        result["observations"]["payloadOrder"] = payloads
        launch_episode(driver, session, 2)
        played = wait(lambda: read_json(companion.played), "next episode rendered-frame report")
        entries = companion.inventory()["entries"]
        assert played["key"] in {entry["key"] for entry in entries}
        result["observations"]["nextLockFrame"] = capture(session, "next-lock-video")
        result["observations"]["nextLockPlayback"] = played
        # The installed supervisor pins every playlist object, even across a
        # companion restart and a cap reduction. Neither maintenance nor prune
        # may delete a file the current playlist could reopen.
        pinned = {entry["path"]: digest(companion.cache / entry["path"]) for entry in entries}
        companion.stop()
        companion.start()
        companion.cli("config", "set", "cache.maxBytes", "0")
        companion.cli("cache", "prune")
        pinned_status = wait(lambda: value if (value := companion.status()) and value["MaxBytes"] == 0 else None,
                             "lowered cap while playlist pinned")
        assert pinned_status["ManagedBytes"] > 0 and pinned_status["ReadyClips"] == 2
        assert all(digest(companion.cache / path) == expected for path, expected in pinned.items())
        result["observations"]["pinnedAcrossRestartAndPrune"] = pinned_status
        session.unlock(2)
        companion.cli("cache", "prune")
        reclaimed = wait(lambda: value if (value := companion.status()) and value["ReadyClips"] == 1 else None,
                         "reclamation after episode releases reader pin")
        result["observations"]["postUnlockLastFallbackRetained"] = reclaimed
        companion.cli("config", "reset", "cache.maxBytes")
        wait(lambda: value if (value := companion.status()) and value["ReadyClips"] == 2 else None,
             "automatic fill after restoring cache budget")
        # Stalled, absent and crashed content owners must not enter auth/input.
        for cycle, mode in enumerate(("stalled", "absent", "crashed"), 3):
            companion.forget_playback(mode)
            if mode == "stalled":
                companion.service.process.send_signal(signal.SIGSTOP)
            elif mode == "absent":
                companion.stop()
            else:
                companion.start()
            launch_episode(driver, session, cycle)
            if mode == "crashed":
                companion.service.process.kill()  # only our headless companion
                companion.service.finish(10)
                companion.service = None
            wait(lambda: read_json(companion.played), mode + " companion cached rendering")
            session.unlock(cycle)
            if mode == "stalled":
                companion.service.process.send_signal(signal.SIGCONT)
            result["observations"][mode + "Independent"] = True
        # Configured-source removal must also constrain last-played fallback.
        companion.start()
        companion.cli("config", "set", "sources", "[]")
        companion.forget_playback("removed-sources")
        launch_episode(driver, session, 6)
        time.sleep(3)
        assert not companion.played.exists(), "removed sources resurrected cached footage"
        session.unlock(6)
        result["observations"]["removedSourcesStaySolid"] = True
        result["exit"] = 0
    except (RuntimeError, subprocess.SubprocessError, OSError, AssertionError, ValueError) as error:
        result["error"] = str(error)
    finally:
        release.set()
        if session.locker and session.locker.process.poll() is None and result["exit"]:
            try:
                run(["wtype", "-M", "ctrl", "-k", "a", "-m", "ctrl", "-k", "BackSpace"], session.env)
                session.unlock(0)
            except (RuntimeError, subprocess.SubprocessError, OSError):
                pass
        result["exit"] = session.cleanup() or result["exit"]
        if companion:
            companion.close()
        if server:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)
        result.update(requests=requests, legs=session.results)
        publish(session.output / "result.json", json.dumps(result, indent=2))
    print("COMPANION_LOCKER_" + ("PASS " if result["exit"] == 0 else "FAILED ") + str(session.output))
    return result["exit"]
