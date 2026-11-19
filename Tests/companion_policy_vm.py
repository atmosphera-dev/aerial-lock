"""Installed CLI/service policy matrix on private session and observation buses."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import threading
import time
from types import SimpleNamespace

from companion_locker import Companion, wait
from companion_policy_peer import PolicyBus
from fixture_files import publish
from owned_instance import OwnedInstance, digest


def policy(args):
    owned = OwnedInstance(args.vm_record, args.root)
    owned.check(require_binaries=True)
    for path, expected in owned.record["installed_hashes"].items():
        assert digest(path) == expected, path
    assert Path(os.environ["TMPDIR"]) == args.root / "tmp"
    output = args.root / "results" / ("policy-" + time.strftime("%Y-%m-%d_%H%M%S"))
    output.mkdir(mode=0o700)
    fixture = output / "fixture.mp4"
    subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                    "testsrc2=size=96x64:rate=10", "-frames:v", "12", "-c:v", "libx264", "-threads", "1",
                    "-movflags", "+faststart", str(fixture)], check=True, timeout=30)
    small = fixture.read_bytes()
    bodies = {"/small.mp4": small, "/large.mp4": small + bytes(4096)}
    bodies["/manifest.json"] = json.dumps({"assets": [
        {"id": name, "url-1080-H264": name + ".mp4"} for name in ("large", "small")]}).encode()
    requests = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def reply(self, head):
            requests.append({"method": self.command, "path": self.path})
            body = bodies.get(self.path)
            self.send_response(200 if body else 404)
            self.send_header("Content-Length", str(len(body or b"")))
            self.send_header("ETag", '"policy-fixture"')
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
    policy_bus = companion = None
    result = {"case": "policy", "exit": 1, "cases": []}
    result["sourceIdentity"] = owned.record["current_source_identity"]
    try:
        policy_bus = PolicyBus(output, owned.check_binary(owned.paths["build"] / "companion-transfer-probe"), owned.record["uuid"])

        source = {"id": "fixture", "name": "Fixture", "type": "community",
                  "source": f"http://127.0.0.1:{server.server_port}/manifest.json"}
        prefix = "acquisition.automatic."
        cases = [("metered", {"Metered": 1}, "allowMetered", False, True, "Metered"),
                 ("vpn-metered", {"Type": "vpn", "DeviceMetered": 1}, "allowMetered", False, True, "Metered"),
                 ("battery", {"OnBattery": True}, "power.battery", False, True, "PowerDisallowed"),
                 ("ac", {}, "power.ac", False, True, "PowerDisallowed"),
                 ("unknown-power", {"OnBattery": None}, "power.unknown", False, True, "PowerDisallowed"),
                 ("paused", {}, "enabled", False, True, "Paused"),
                 ("offline", {"State": 20, "Connectivity": 1}, "enabled", True, True, "Offline"),
                 ("resolution", {}, "allowedResolutions", [], [1080], "NoAllowedResolution")]
        for name, wire in (("ethernet", "802-3-ethernet"), ("wifi", "802-11-wireless"), ("cellular", "gsm"),
                           ("bluetooth", "bluetooth"), ("vpn", "vpn"), ("other", "dummy"), ("unknown", "")):
            cases.append(("type-" + name, {"Type": wire}, "connectionTypes." + name, False, True, "ConnectionTypeDisallowed"))
        for label, observations, key, disallowed, allowed, reason in cases:
            baseline = {"State": 70, "Connectivity": 4, "Metered": 2, "DeviceMetered": 2,
                        "Type": "802-3-ethernet", "OnBattery": False}
            policy_bus.observe(dict(baseline, **observations))
            case_root = output / label
            case_root.mkdir()
            session = SimpleNamespace(output=case_root, env=dict(os.environ), shell=None)
            companion = Companion(SimpleNamespace(ObservedProcess=ObservedService), session,
                                  {"sources": [source], "acquisition": {"automatic": {"enabled": False}}})
            companion.environment["DBUS_SYSTEM_BUS_ADDRESS"] = policy_bus.address
            companion.start()
            wire = dict(baseline, **observations)
            types = {"802-3-ethernet": "ethernet", "802-11-wireless": "wifi", "gsm": "cellular",
                     "bluetooth": "bluetooth", "vpn": "vpn", "": "unknown"}
            expected = {"network": "offline" if wire["State"] == 20 else "online", "connectionType": types.get(wire["Type"], "other"),
                         "power": "unknown" if wire["OnBattery"] is None else "battery" if wire["OnBattery"] else "ac",
                         "metering": "metered" if wire["Metered"] == 1 or wire["DeviceMetered"] == 1 else "unmetered"}
            expected["connectionTypes"] = [expected["connectionType"]]
            wait(lambda: value if (value := companion.status()) and value["Observations"] == expected else None,
                 "complete private observation snapshot")
            companion.cli("config", "set", prefix + key, json.dumps(disallowed))
            if key != "enabled":
                companion.cli("content", "resume")
            blocked = wait(lambda: value if (value := companion.status()) and value["DeferReason"] == reason else None,
                           label + " actual planner defer")
            before = len(requests)
            time.sleep(0.2)
            assert len(requests) == before and blocked["ReadyClips"] == 0, "disallowed automatic network request"
            companion.cli("fetch", "fixture", "--limit", "1", "--wait", "--json")
            manual = companion.status()
            assert manual["ReadyClips"] == 1, "manual policy bypass did not publish content"
            if label == "offline":
                policy_bus.observe(baseline)
            companion.cli("config", "set", prefix + key, json.dumps(allowed))
            ready = wait(lambda: value if (value := companion.status()) and value["ReadyClips"] == 2 else None,
                         label + " reallowed automatic fill")
            result["cases"].append({"name": label, "blocked": blocked, "manual": manual, "allowed": ready,
                                    "requests": requests[before:]})
            companion.close()
            companion = None
        result["exit"] = 0
    except (RuntimeError, subprocess.SubprocessError, OSError, AssertionError, ValueError) as error:
        result["error"] = str(error)
    finally:
        if companion:
            companion.close()
        if policy_bus:
            policy_bus.close()
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
        publish(output / "result.json", json.dumps(result, indent=2))
    print("COMPANION_POLICY_" + ("PASS " if result["exit"] == 0 else "FAILED ") + str(output))
    return result["exit"]


class ObservedService:
    """No protocol traffic or credentials in these headless service logs."""
    def __init__(self, argv, environment, log):
        self.stream = log.open("w")
        self.process = subprocess.Popen(argv, env=environment, stdout=self.stream, stderr=subprocess.STDOUT)

    def finish(self, seconds):
        code = self.process.wait(timeout=seconds)
        self.stream.close()
        return code
