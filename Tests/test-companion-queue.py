#!/usr/bin/env python3
"""Real headless process -> HTTP -> probe -> index, with isolated XDG roots."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

from fixture_files import publish


def wait(predicate, label, process=None):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if process and process.poll() is not None:
            raise AssertionError("companion exited: " + label)
        result = predicate()
        if result:
            return result
        time.sleep(0.025)
    raise AssertionError("timed out: " + label)


def read(path):
    try:
        return json.loads(path.read_text())
    except FileNotFoundError:
        return {}


with tempfile.TemporaryDirectory(prefix="companion-queue-") as temporary:
    root = Path(temporary)
    video = root / "fixture.mp4"
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=96x64:rate=10",
                    "-frames:v", "12", "-c:v", "libx264", "-movflags", "+faststart", "-y", str(video)], check=True, timeout=20)
    media = video.read_bytes()
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def do_HEAD(self):
            self.reply(True)

        def do_GET(self):
            self.reply(False)

        def reply(self, head):
            requests.append((self.command, self.path))
            if self.path == "/manifest.json":
                payload = json.dumps({"assets": [{"id": name, "url-1080-H264": name + ".mp4"}
                                                for name in ("large", "small")]}).encode()
            else:
                payload = media + (b"\0" * 5000 if self.path == "/large.mp4" else b"")
            self.send_response(200)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("ETag", '"fixture-one"')
            self.end_headers()
            if not head:
                self.wfile.write(payload)

        def log_message(self, *_args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    source = {"type": "community", "id": "fixture", "name": "Fixture",
              "source": f"http://127.0.0.1:{server.server_port}/manifest.json", "maxResolution": 1080}
    processes = []
    # This CTest owns a private session bus without NM/UPower peers. Use it for
    # optional observations too, so neither first start nor restart reads the
    # execution machine's real metering, connectivity or power state.
    fixture_env = dict(os.environ, DBUS_SYSTEM_BUS_ADDRESS=os.environ["DBUS_SESSION_BUS_ADDRESS"])

    def start(label, sources, maximum):
        profile = root / label
        config = profile / "config/aerial-lock/config.json"
        config.parent.mkdir(parents=True)
        publish(config, json.dumps({"sources": sources, "cache": {"maxBytes": maximum}}))
        env = dict(fixture_env, AERIAL_LOCK_SHELL=sys.argv[2])
        for kind in ("CONFIG", "CACHE", "STATE", "RUNTIME"):
            path = profile / kind.lower()
            path.mkdir(exist_ok=True)
            path.chmod(0o700)
            env["XDG_" + kind + "_HOME" if kind != "RUNTIME" else "XDG_RUNTIME_DIR"] = str(path)
        log = (profile / "service.log").open("wb")
        process = subprocess.Popen([sys.argv[1]], env=env, stdout=log, stderr=subprocess.STDOUT)
        processes.append((process, log))
        return process, profile, config

    def stop(process):
        process.terminate()
        process.wait(timeout=10)

    try:
        process, profile, config = start("zero", [source], 0)
        wait(lambda: (profile / "config/aerial-lock/companion.owner.lock").exists(), "zero-cap startup", process)
        time.sleep(0.2)
        assert not (profile / "cache/aerial-lock/managed-v1").exists()
        assert requests == [], requests
        # The sole repository watches editor replacement. Increasing the cap
        # initializes actual acquisition without a daemon restart.
        publish(config, json.dumps({"sources": [source], "cache": {"maxBytes": 1000000}}))
        cache = profile / "cache/aerial-lock/managed-v1"

        def inventory():
            pointer = read(cache / "current.json")
            return read(cache / ("generations/" + pointer["generation"] + ".json")) if pointer else {}

        wait(lambda: len(inventory().get("entries", [])) == 2, "production completed media", process)
        entries = inventory()["entries"]
        assert [entry["assetId"] for entry in entries] == ["small", "large"], entries
        for entry in entries:
            assert entry["probe"]["initialFrameDecoded"] is True
            assert (cache / entry["path"]).read_bytes() == (media if entry["assetId"] == "small" else media + b"\0" * 5000)
        jobs = profile / "state/aerial-lock/planner-v1.json"
        wait(lambda: any(job["state"] == "succeeded" for job in read(jobs).get("jobs", {}).values()), "durable completion", process)
        assert [path for method, path in requests if method == "GET" and path.endswith(".mp4")] == ["/small.mp4", "/large.mp4"]
        stop(process)
        # A new process reconciles the durable cache and never re-downloads a
        # representation solely because its previous process is gone.
        env = dict(fixture_env, AERIAL_LOCK_SHELL=sys.argv[2], XDG_CONFIG_HOME=str(profile / "config"),
                   XDG_CACHE_HOME=str(profile / "cache"), XDG_STATE_HOME=str(profile / "state"), XDG_RUNTIME_DIR=str(profile / "runtime"))
        previous = len(read(jobs)["jobs"])
        with (profile / "restart.log").open("wb") as log:
            process = subprocess.Popen([sys.argv[1]], env=env, stdout=log, stderr=subprocess.STDOUT)
            processes.append((process, None))
            wait(lambda: len(read(jobs)["jobs"]) > previous and all("finishedAt" in job for job in read(jobs)["jobs"].values()), "restart reconciliation", process)
            assert len([1 for method, path in requests if method == "GET" and path.endswith(".mp4")]) == 2
            stop(process)
        before = len(requests)
        process, profile, _ = start("empty", [], 1000000)
        wait(lambda: (profile / "state/aerial-lock/planner-v1.json").exists(), "empty-profile planner", process)
        time.sleep(0.2)
        assert len(requests) == before, requests[before:]
        stop(process)
        print("COMPANION_QUEUE_PROCESS_PASS zero_cap no_implicit_source smallest_first decoded_index restart_no_refetch external_config")
    finally:
        for process, log in processes:
            if process.poll() is None:
                stop(process)  # Only our headless companion, never a locker.
            if log:
                log.close()
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
