#!/usr/bin/env python3
"""Actual acquisition -> immutable index -> real Quickshell SourceCache.

Run inside the named owned VM after prepare, never on the source host.
No lock is acquired; installed real-lock acceptance remains a separate gate.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from owned_instance import OwnedInstance
from fixture_files import publish


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--vm-record", type=Path, required=True)
    args = parser.parse_args()
    owned = OwnedInstance(args.vm_record, args.root)
    owned.check(require_binaries=True)
    root = args.root.resolve(strict=True)
    if Path(os.environ.get("TMPDIR", "")).resolve() != root / "tmp":
        raise RuntimeError("requires disk-backed task TMPDIR")
    record = json.loads(args.vm_record.read_text())
    source = root / "src/aerial-lock"
    output = root / "results/phaseB-source-cache"
    output.mkdir(exist_ok=True)
    # Preserve failed fixture trees/processes for inspection. Never let a
    # subprocess timeout kill qs, even though this fixture acquires no lock.
    work = Path(tempfile.mkdtemp(prefix="source-cache-", dir=root / "tmp"))
    try:
        fixture = work / "fixture"
        subprocess.run([str(root / "build/debug/aerial-tests/companion-transfer-probe"), "--export", str(fixture),
                        str(source / "companion/aerial-media-probe")], check=True, timeout=30)
        shell = work / "shell"
        from source_tree import copy_source_tree
        copy_source_tree(source, shell)
        (shell / "shell.qml").write_text('import QtQuick\nimport Quickshell\nShellRoot { settings.watchFiles: false; '
                                        'Loader { source: "root:/Tests/companion-source-cache.qml" } '
                                        'Timer { interval: 9000; running: true; onTriggered: Qt.exit(2) } }\n')
        config = fixture / "config/aerial-lock"
        (config / "i18n").mkdir(parents=True, exist_ok=True)
        (config / "i18n/en.json").write_text("{}")
        # Compatible legacy inventory is empty but fully resolved. Managed media
        # and metadata must be supplied by the new index, not a .done marker.
        legacy = fixture / "cache/aerial-lock/Fixture"
        (legacy / "videos").mkdir(parents=True)
        (legacy / "entries.json").write_text('{"assets":[]}')
        environment = dict(os.environ, **record["environment"],
                           XDG_CONFIG_HOME=str(fixture / "config"), XDG_CACHE_HOME=str(fixture / "cache"),
                           XDG_STATE_HOME=str(fixture / "state"), LIBGL_ALWAYS_SOFTWARE="1", QSG_RHI_BACKEND="opengl")
        original = json.loads((config / "config.json").read_text())["sources"][0]
        personal = fixture / "personal"
        personal.mkdir()
        shutil.copyfile(fixture / "fixture.mp4", personal / "personal.mp4")
        legacy_off = fixture / "cache/aerial-lock/Apple-Off"
        (legacy_off / "videos").mkdir(parents=True)
        shutil.copyfile(fixture / "fixture.mp4", legacy_off / "videos/legacy.mp4")
        (legacy_off / "videos/legacy.mp4.done").touch()
        (legacy_off / "entries.json").write_text('{"assets":[{"id":"legacy","title":"Mountain legacy"}]}')
        disabled = [dict(original, enabled=False),
                    dict(id="apple-off", type="apple", name="Apple Off", source="https://example.test/off.json", enabled=False),
                    dict(id="folder-off", type="folder", name="Folder Off", source=str(personal), enabled=False),
                    dict(id="file-off", type="file", name="File Off", source=str(fixture / "fixture.mp4"), enabled=False)]
        cases = {"managed": [original], "disabled": disabled,
                 "mixed": disabled[1:] + [original], "empty": [],
                 "reenabled": [dict(original, enabled=True)]}
        results = {}
        for label, sources in cases.items():
            (config / "config.json").write_text(json.dumps({"sources": sources}))
            raw_log = output / (label + "-raw.log")
            with raw_log.open("w") as log:
                process = subprocess.Popen(["bash", str(source / "Scripts/aerial-lock-run"), "--shell-dir", str(shell),
                                            "--plugin-root", str(source / "native/qt-plugins")], env=environment,
                                           stdout=log, stderr=subprocess.STDOUT)
                try:
                    code = process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    publish(root / "run/blocked-qs-source-cache.json", json.dumps({"pid": process.pid, "fixture": str(work), "log": str(raw_log)}))
                    raise RuntimeError("source-cache fixture remains running; retained for inspection: " + str(raw_log))
            text = re.sub(r"\x1b\[[0-9;]*m", "", raw_log.read_text())
            (output / (label + ".log")).write_text(text)
            if code != 0:
                raise RuntimeError(text)
            lines = [line.split("SOURCE_CACHE_RESULT ", 1)[1] for line in text.splitlines() if "SOURCE_CACHE_RESULT " in line]
            if len(lines) != 1 or re.search(r"(ReferenceError|TypeError|is not a type|Cannot assign)", text):
                raise RuntimeError(text)
            observed = json.loads(lines[0]); results[label] = observed
            if label in ("managed", "mixed", "reenabled"):
                assert len(observed["playlist"]) == 1 and len(observed["metadata"]) == 1, observed
                metadata = observed["metadata"][0]
                assert metadata["id"] == "opaque/asset" and metadata["secondaryName"] == "Mountain fixture", observed
                assert metadata["sourceName"] == "Fixture" and metadata["poi"] == {"0": "Summit"}, observed
                assert len(metadata["assetKey"]) == 64 and metadata["assetKey"] != metadata["id"], observed
                playback = json.loads((fixture / "state/aerial-lock/last-playback.json").read_text())
                assert playback["key"] == metadata["assetKey"], playback
                results["lastPlayback"] = playback
            else:
                assert observed == {"playlist": [], "metadata": []}, observed
        publish(output / "result.json", json.dumps({"result": "pass", "cases": results}, indent=2))
    except BaseException:
        raise
    else:
        shutil.rmtree(work)
    print("SOURCE_CACHE_CONSUMER_PASS " + str(output / "result.json"))


if __name__ == "__main__":
    main()
