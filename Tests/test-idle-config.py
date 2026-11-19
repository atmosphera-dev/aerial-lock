#!/usr/bin/env python3
"""Compare the actual shared native/QML facade and startup-only idle consumer.

The companion-config suite separately exercises QML construction and settings
transactions. This test sends the same on-disk fixtures to both real readers;
it contains no settings merge/schema reimplementation or extracted QML logic.
"""
import json
import os
import copy
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
probe = Path(sys.argv[2]).resolve()
source = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="idle-config-") as directory:
    root = Path(directory)
    for fixture in json.loads((source / "Tests/fixtures/idle-config.json").read_text()):
        config = root / fixture["name"] / "aerial-lock"
        config.mkdir(parents=True)
        path = config / "config.json"
        if not fixture.get("missing"):
            path.write_text(json.dumps(fixture["user"]))
        original = path.read_bytes() if path.exists() else None
        env = dict(os.environ, XDG_CONFIG_HOME=str(config.parent), QT_QPA_PLATFORM="offscreen",
                   QT_FORCE_STDERR_LOGGING="1", LC_ALL="C.UTF-8", AERIAL_LOCK_SHELL=str(source))
        native = subprocess.run([str(binary), "--print-config"], env=env,
                                capture_output=True, text=True, timeout=10)
        assert native.returncode == 0, (fixture["name"], native.stdout, native.stderr)
        effective = json.loads(native.stdout)
        assert native.stderr.count("using default") == fixture.get("warnings", 0), native.stderr
        facade = subprocess.run([str(probe), "--read", str(source), str(path)], env=env,
                                capture_output=True, text=True, timeout=10)
        assert facade.returncode == 0, (facade.stdout, facade.stderr)
        assert json.loads(facade.stdout)["idle"] == effective, fixture["name"]
        assert (path.read_bytes() if path.exists() else None) == original
        print("IDLE_CONFIG", fixture["name"], "native=LocalConfig", effective)
    defaults = json.loads((source / "Config/defaults.json").read_text())
    schema = json.loads((source / "Config/settings-schema.json").read_text())
    for name, change, valid in (
            ("empty-bundle", lambda d, s: (d.clear(), s.clear()), False),
            ("missing-idle", lambda d, s: d.pop("idle"), False),
            ("missing-idle-schema", lambda d, s: s["properties"]["idle"].clear(), False),
            ("bad-idle", lambda d, s: d["idle"].update(lockOnSuspend="yes"), False),
            ("bad-content", lambda d, s: d["cache"].update(maxBytes="invalid"), True),
            ("disabled-idle", lambda d, s: d["idle"].update(timeoutSeconds=0), True)):
        bundle = root / name
        (bundle / "Config").mkdir(parents=True)
        d, s = copy.deepcopy(defaults), copy.deepcopy(schema)
        change(d, s)
        (bundle / "Config/defaults.json").write_text(json.dumps(d))
        (bundle / "Config/settings-schema.json").write_text(json.dumps(s))
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / "absent"), AERIAL_LOCK_SHELL=str(bundle),
                   QT_FORCE_STDERR_LOGGING="1", LC_ALL="C.UTF-8")
        result = subprocess.run([str(binary), "--print-config"], env=env, capture_output=True, text=True, timeout=10)
        assert result.returncode == (0 if valid else 1), (name, result.returncode, result.stdout, result.stderr)
        if valid:
            assert json.loads(result.stdout) == d["idle"], (name, result.stdout)
        if name == "bad-content":
            assert "cache.maxBytes" in result.stderr
        print("IDLE_CONFIG_BUNDLE", name, "accepted" if valid else "rejected")
    print("IDLE_CONFIG all actual-reader shared fixtures passed")
