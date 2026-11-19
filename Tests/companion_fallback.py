"""Installed local fallback through the actual selection/render/auth owners."""
import http.server
import json
from pathlib import Path
import subprocess
import threading

from companion_locker import Companion, launch_episode, read_json, run, wait
from fixture_files import publish
from companion_install import installed_private_executable


def fallback(args, installed_session):
    driver, session = installed_session(args, "fallback")
    companion = server = thread = None
    result = {"case": "fallback", "exit": 1, "observations": {},
              "sourceIdentity": session.owned.record["current_source_identity"]}
    try:
        session.start()
        exported = session.output / "export"
        run([str(session.root / "build/debug/aerial-tests/companion-transfer-probe"), "--export", str(exported),
             str(installed_private_executable(session.owned, "aerial-media-probe"))], session.env)
        media = (exported / "fixture.mp4").read_bytes()
        bodies = {"/other.mp4": media, "/last.mp4": media + bytes(1024)}
        bodies["/manifest.json"] = json.dumps({"assets": [
            {"id": name, "url-1080-H264": name + ".mp4", "title": "Mountain"} for name in ("other", "last")]}).encode()

        class Handler(http.server.BaseHTTPRequestHandler):
            def reply(self, head):
                body = bodies.get(self.path)
                self.send_response(200 if body else 404)
                self.send_header("Content-Length", str(len(body or b"")))
                self.send_header("ETag", '"fallback-fixture"')
                self.end_headers()
                if body and not head:
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
        broken = session.output / "broken.mp4"
        broken.write_bytes(b"not a video")
        source = {"id": "fixture", "name": "Fixture", "type": "community",
                  "source": f"http://127.0.0.1:{server.server_port}/manifest.json"}
        companion = Companion(driver, session, {"sources": [
            {"id": "broken", "name": "Broken", "type": "file", "source": str(broken)}, source],
            "preferredResolution": 1080, "theme": "FallbackProbe.qml",
            "acquisition": {"automatic": {"enabled": False}}})
        # User-theme extension point, not a modified installed bundle or a
        # product test switch. The theme reports no editable/auth material.
        themes = companion.config.parent / "themes"
        themes.mkdir()
        theme = (session.root / "src/aerial-lock/Tests/resource-theme.qml").read_text()
        theme = theme.replace('import "../Services"', 'import "root:/Services"')
        theme = theme.replace('import "../Modules/Lock"', 'import "root:/Modules/Lock"')
        theme = theme.replace('preferredTopics: []', 'preferredTopics: ["broken"]')
        publish(themes / "FallbackProbe.qml", theme)
        companion.start()
        companion.cli("fetch", "fixture", "--limit", "2", "--wait")
        entries = {entry["assetId"]: entry for entry in companion.inventory()["entries"]}
        assert set(entries) == {"other", "last"}
        companion.stop()

        def seed_last():
            publish(companion.played, json.dumps({"version": 1, "key": entries["last"]["key"], "playedAt": 0}))

        def observed_video(expected):
            played = wait(lambda: value if (value := read_json(companion.played))
                          and value["key"] == entries[expected]["key"] and value["playedAt"] > 0 else None,
                          "fallback decoded-frame recency: " + expected)
            observed = None

            def match(line):
                nonlocal observed
                if "RESOURCE_MEDIA " not in line:
                    return False
                observed = json.loads(line.split("RESOURCE_MEDIA ", 1)[1])
                return observed["hasVideo"] and observed["position"] > 0 and observed["path"].endswith(entries[expected]["path"])

            session.locker.wait_line(match, 20)
            return {"playback": played, "consumer": observed}

        seed_last()
        launch_episode(driver, session, 1)
        session.locker.wait_line(lambda line: "video background failed:" in line)
        result["observations"]["lastPlayed"] = observed_video("last")
        session.unlock(1)
        (companion.cache / entries["last"]["path"]).unlink()
        seed_last()
        launch_episode(driver, session, 2)
        session.locker.wait_line(lambda line: "video background failed:" in line)
        result["observations"]["lastGoneOtherCached"] = observed_video("other")
        session.unlock(2)
        (companion.cache / entries["other"]["path"]).unlink()
        seed_last()
        launch_episode(driver, session, 3)
        session.locker.wait_line(lambda line: "video background failed:" in line)
        session.locker.wait_line(lambda line: "RESOURCE_MEDIA " in line and '"hasVideo":false' in line and '"path":""' in line)
        assert read_json(companion.played)["playedAt"] == 0
        result["observations"]["nothingLeftSolid"] = {"hasVideo": False, "path": "", "noFrameReported": True}
        session.unlock(3)
        result["exit"] = 0
    except (RuntimeError, subprocess.SubprocessError, OSError, AssertionError, ValueError) as error:
        result["error"] = str(error)
    finally:
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
        result["legs"] = session.results
        publish(session.output / "result.json", json.dumps(result, indent=2))
    print("COMPANION_FALLBACK_" + ("PASS " if result["exit"] == 0 else "FAILED ") + str(session.output))
    return result["exit"]
