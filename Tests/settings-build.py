#!/usr/bin/env python3
"""Build the frozen owned-VM tree, then attest its inputs and exact outputs."""
import json
import os
from pathlib import Path
import subprocess
import sys
from fixture_files import publish
from settings_build import binary_paths, hashes, source_identity, verify_configuration

root = Path(sys.argv[1]).resolve(strict=True)
if Path(os.environ.get("TMPDIR", "")).resolve() != root / "tmp":
    raise RuntimeError("TMPDIR must name the owned disk-backed workspace/tmp")
freeze = json.loads((root / "run/source-input.json").read_text())
identity = source_identity(root, freeze)
receipt = root / "run/settings-build.json"
receipt.unlink(missing_ok=True)
verify_configuration(root)
source = root / "src/aerial-lock"
for command in (["make", "clean"],
                ["make", "DEBUG_DISMISS=0", "DEBUG_VERBOSE=0"],
                ["cmake", "--build", str(root / "build/debug/aerial-tests"), "--clean-first"]):
    subprocess.run(command, cwd=source, check=True, timeout=600)
if json.loads((root / "run/source-input.json").read_text()) != freeze or source_identity(root, freeze) != identity:
    raise RuntimeError("build inputs changed while compiling")
verify_configuration(root)
publish(receipt, json.dumps(dict(source_identity=identity, binary_hashes=hashes(binary_paths(root))), indent=2))
print("SETTINGS_BUILD_ATTESTED " + identity)
