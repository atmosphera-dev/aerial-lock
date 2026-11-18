"""One real Apple clip, bounded manual acquisition and installed lock playback."""
import json
from pathlib import Path
import subprocess
import time

from companion_locker import Companion, capture, launch_episode, read_json, run, wait
from fixture_files import publish


def apple_one(args, installed_session):
    driver, session = installed_session(args, "apple-one")
    companion = None
    result = {"case": "apple-one", "backend": args.backend, "exit": 1, "observations": {}}
    result["sourceIdentity"] = session.owned.record["current_source_identity"]
    try:
        session.start()
        defaults = json.loads(Path("/usr/share/aerial-lock/Config/defaults.json").read_text())
        source = dict(defaults["sources"][0], maxResolution=1080)
        assert source["type"] == "apple" and source["id"] == "apple-tv"
        companion = Companion(driver, session, {"sources": [source], "acquisition": {"automatic": {"enabled": False}}})
        companion.start()
        initial = companion.status()
        assert initial["State"] == "paused" and initial["ReadyClips"] == 0
        launch_episode(driver, session, 1)
        result["observations"]["firstLockFrame"] = capture(session, "apple-first-solid")
        # Manual job is the sole live payload request. Automatic fill stays
        # paused, including across the lock hint and service restarts.
        with (session.output / "fetch.log").open("w") as log:
            subprocess.run(["/usr/bin/aerialctl", "fetch", "apple-tv", "--limit", "1", "--wait", "--json"],
                           env=companion.environment, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)
        ready = companion.status()
        assert ready["ReadyClips"] == 1 and ready["State"] == "paused", ready
        entries = companion.inventory()["entries"]
        assert len(entries) == 1 and entries[0]["probe"]["initialFrameDecoded"] is True
        assert not companion.played.exists(), "live completion entered the first frozen episode"
        session.unlock(1)
        companion.stop()
        # Re-observe eligible lengths using the same production source selector
        # and bounded HEAD/range sizer; this makes no further payload download.
        size_path = session.output / "apple-candidate-sizes.json"
        with (session.output / "sizing.log").open("w") as log:
            subprocess.run([str(session.owned.check_binary(session.owned.paths["build"] / "companion-transfer-probe")),
                            "--size-apple-catalogue", str(size_path)], env=companion.environment,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=930)
        sizes = json.loads(size_path.read_text())
        candidates = sizes["candidates"]
        assert candidates and all(item["height"] == 1080 for item in candidates)
        assert candidates[0]["bytes"] > 0 and entries[0]["key"] == candidates[0]["key"], "not the smallest eligible clip"
        assert entries[0]["bytes"] == candidates[0]["bytes"]
        assert all(row["method"] == "HEAD" or row["range"] == "bytes=0-0" for row in sizes["requests"])
        result["observations"].update(ready=ready, entry=entries[0], smallest=candidates[0],
                                      eligibleCandidates=len(candidates), sizeEvidence=size_path.name,
                                      automaticStayedPaused=True, oneCompletedClip=True, firstEpisodeStayedSolid=True)
        launch_episode(driver, session, 2)
        played = wait(lambda: read_json(companion.played), "real Apple rendered-frame report", seconds=45)
        assert played["key"] == entries[0]["key"]
        result["observations"]["nextLockPlayback"] = played
        result["observations"]["nextLockFrame"] = capture(session, "apple-next-lock-video")
        session.unlock(2)
        companion.forget_playback("offline")
        # No companion and no D-Bus activation service directory: the locker has
        # only its complete local object. Do not alter host/guest network links.
        launch_episode(driver, session, 3)
        played = wait(lambda: read_json(companion.played), "cached Apple playback without service", seconds=45)
        assert played["key"] == entries[0]["key"]
        session.unlock(3)
        result["observations"]["cachedNoNetworkOwnerPlayback"] = played
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
        result["legs"] = session.results
        publish(session.output / "result.json", json.dumps(result, indent=2))
    print("COMPANION_APPLE_ONE_" + ("PASS " if result["exit"] == 0 else "FAILED ") + str(session.output))
    return result["exit"]
