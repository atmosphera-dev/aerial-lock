"""A build owns the source-to-output receipt; preparation only verifies it."""
import hashlib
import json
from owned_instance import digest


def source_identity(root, freeze):
    files = freeze["files"]
    identity = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
    if identity != freeze["identity"]:
        raise RuntimeError("source freeze identity mismatch")
    source = root / "src/aerial-lock"
    for name, info in files.items():
        path = source / name
        if (digest(path) != info["sha256"]
                or path.lstat().st_mode != info["mode"]):
            raise RuntimeError("source changed: " + name)
    return identity


def binary_paths(root):
    source, build = root / "src/aerial-lock", root / "build/debug/aerial-tests"
    probes = ["settings-client-probe", "settings-quick-probe", "settings-visual-probe", "settings-model-probe", "virtual-pointer-probe"]
    for name in ("desktop", "pages", "tray", "units"):
        if (source / ("settings/tests/" + name + ".cpp")).is_file():
            probes.append("settings-" + name + "-probe")
    return ([source / name for name in ("settings/aerial-settings", "companion/aerial", "cli/aerialctl", "idle/aerial-idle")]
            + [build / name for name in probes])


def hashes(paths):
    return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}


def verify_build(root, freeze, outputs):
    identity = source_identity(root, freeze)
    receipt = json.loads((root / "run/settings-build.json").read_text())
    if receipt["source_identity"] != identity or receipt["binary_hashes"] != hashes(outputs):
        raise RuntimeError("build provenance does not match current source and outputs; rebuild")
    return receipt


def quick_test_inputs(source, files):
    covered = {}
    for path in (source / "settings/tests").rglob("*"):
        if not path.is_file() and not path.is_symlink(): continue
        name = str(path.relative_to(source)); expected = files.get(name)
        if expected is None or digest(path) != expected["sha256"] or path.lstat().st_mode != expected["mode"]:
            raise RuntimeError("unfrozen settings test input: " + name)
        if path.suffix == ".qml": covered[name] = expected["sha256"]
    if not covered: raise RuntimeError("no frozen settings QML inputs")
    return dict(sorted(covered.items()))
