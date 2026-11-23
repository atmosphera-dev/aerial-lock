#!/usr/bin/env python3
"""Build and installed consumers, without source/build paths or environment help."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

source, build, probe = map(lambda value: Path(value).resolve(), sys.argv[1:])
env = {key: value for key, value in os.environ.items()
       if key not in ("QML_IMPORT_PATH", "QML2_IMPORT_PATH", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH")}


def consumer(binary, cwd=None):
    result = subprocess.run([str(binary)], cwd=cwd, env=env, capture_output=True, text=True, timeout=20)
    assert result.returncode == 0 and "INTERACTIVE_CONSUMER_PASS" in result.stdout, result.stdout + result.stderr
    assert not result.stderr.strip(), result.stderr
    print(result.stdout, end="")


consumer(probe)
cache = dict(line.split("=", 1) for line in (build / "CMakeCache.txt").read_text().splitlines()
             if "=" in line and not line.startswith(("#", "//")))
bindir = Path(cache["AERIAL_SETTINGS_BINDIR:PATH"])
runtime = Path(cache["AERIAL_INTERACTIVE_ROOT:PATH"])
with tempfile.TemporaryDirectory(prefix="aerial-private-runtime-") as temporary:
    stage = Path(temporary)
    subprocess.run(["cmake", "--install", str(build), "--component", "aerial-settings"],
                   env=dict(env, DESTDIR=str(stage)), check=True, capture_output=True, text=True)
    installed = stage / bindir.relative_to("/")
    installed_probe = installed / probe.name
    shutil.copy2(probe, installed_probe)
    for binary in [installed / "aerial-settings", *(stage / runtime.relative_to("/")).rglob("*.so")]:
        dynamic = subprocess.check_output(["readelf", "-d", str(binary)], text=True)
        paths = [line for line in dynamic.splitlines() if "RPATH" in line or "RUNPATH" in line]
        assert paths and all("$ORIGIN" in line for line in paths), (binary, paths)
        assert not any(str(path) in "\n".join(paths) for path in (source, build, stage)), paths
        deps = subprocess.check_output(["ldd", str(binary)], text=True, env=env)
        assert "not found" not in deps, deps
    # Hide the exact frozen roots, not just unset environment variables. Always
    # restore them, including a failed consumer, before CTest writes its results.
    hidden = []
    try:
        roots = (source,) if build.is_relative_to(source) else (build,) if source.is_relative_to(build) else (source, build)
        for path in roots:
            destination = path.with_name(path.name + ".runtime-unavailable")
            assert not destination.exists(), destination
            path.rename(destination)
            hidden.append((path, destination))
        assert not source.exists() and not build.exists()
        consumer(installed_probe, cwd=stage)
    finally:
        for path, destination in reversed(hidden):
            destination.rename(path)
print("INTERACTIVE_PRIVATE_RUNTIME_PASS source/build unavailable; origin-relative RUNPATH")
