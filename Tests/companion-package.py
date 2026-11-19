#!/usr/bin/env python3
"""Build/install an exact-tree acceptance package inside the attested owned VM.

The retained release recipe is not retargeted or published. Only its source and
checksum differ in the labelled test copy; normal distro build options remain.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tarfile
import time

from companion_install import check_layout
from fixture_files import publish
from owned_instance import OwnedInstance, digest


def run(argv, **kwargs):
    return subprocess.run(argv, check=True, capture_output=True, text=True, timeout=60, **kwargs)


def manager():
    return run(["systemctl", "--user", "show", "aerial.service", "-p", "ActiveState", "-p", "MainPID",
                "-p", "InvocationID"]).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--vm-record", type=Path, required=True)
    parser.add_argument("--packaging", type=Path, required=True)
    args = parser.parse_args()
    owned = OwnedInstance(args.vm_record, args.root)
    owned.check(require_binaries=True)
    root, source = owned.root, owned.paths["source"]
    assert Path(os.environ["TMPDIR"]) == root / "tmp"
    assert run(["sudo", "cat", "/sys/class/dmi/id/product_uuid"]).stdout.strip() == owned.record["uuid"]
    packaging = args.packaging.resolve(strict=True)
    assert packaging.is_relative_to(root / "packaging") and not args.packaging.is_symlink()
    for absolute, expected in owned.record["installed_hashes"].items():
        assert digest(absolute) == expected, "installed bytes drifted before package acceptance: " + absolute
    # Keep tester's ordinary profile network-free throughout package replacement.
    settings = Path.home() / ".config/aerial-lock/config.json"
    assert json.loads(settings.read_text())["sources"] == []
    for proc in Path("/proc").iterdir():
        if not proc.name.isdecimal():
            continue
        try:
            assert Path(os.readlink(proc / "exe")).name not in {"qs", "quickshell"}, "live locker blocks package replacement"
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            pass
    output = root / "results" / ("package-" + time.strftime("%Y-%m-%d_%H%M%S"))
    output.mkdir(mode=0o700)
    original, test = output / "release-recipe", output / "test-only-recipe"
    original.mkdir()
    test.mkdir()
    for name in ("PKGBUILD", "aerial-lock.install", ".SRCINFO"):
        assert (packaging / name).is_file() and not (packaging / name).is_symlink()
        shutil.copy2(packaging / name, original / name)
    srcinfo = run(["makepkg", "--printsrcinfo"], cwd=original).stdout
    assert srcinfo == (original / ".SRCINFO").read_text(), "release .SRCINFO is not generated/current"
    original_text = (original / "PKGBUILD").read_text()
    version = re.search(r"^pkgver=([0-9.]+)$", original_text, re.M).group(1)
    name = re.search(r"^pkgname=([a-z-]+)$", original_text, re.M).group(1)
    archive = test / (name + "-" + version + ".tar.gz")
    with tarfile.open(archive, "w:gz") as target:
        for relative, expected in sorted(owned.record["source_files"].items()):
            path = source / relative
            assert digest(path) == expected["sha256"] and path.lstat().st_mode == expected["mode"]
            target.add(path, arcname=name + "-" + version + "/" + relative, recursive=False)
    archive_hash = digest(archive)
    test_text, source_count = re.subn(r"^source=\([^\n]*\)$", 'source=("' + archive.name + '")', original_text, flags=re.M)
    test_text, checksum_count = re.subn(r"^sha256sums=\([^\n]*\)$", "sha256sums=('" + archive_hash + "')", test_text, flags=re.M)
    assert source_count == checksum_count == 1
    (test / "PKGBUILD").write_text(test_text)
    shutil.copy2(original / "aerial-lock.install", test / "aerial-lock.install")
    (test / "TEST-ONLY.md").write_text("Exact task-tree acceptance package, not a release recipe or publication.\n")
    (test / ".SRCINFO").write_text(run(["makepkg", "--printsrcinfo"], cwd=test).stdout)
    # Preserve debug/LTO and all other distro options, pinning bulk work to disk.
    makepkg_config = output / "makepkg.conf"
    config_text = Path("/etc/makepkg.conf").read_text()
    for setting, leaf in (("BUILDDIR", "build"), ("PKGDEST", "packages"), ("SRCDEST", "sources"),
                          ("SRCPKGDEST", "source-packages"), ("LOGDEST", "logs")):
        destination = output / leaf
        destination.mkdir()
        config_text += "\n" + setting + "=" + shlex.quote(str(destination)) + "\n"
    config_text += "\nMAKEFLAGS=-j2\n"
    makepkg_config.write_text(config_text)
    makepkg = ["makepkg", "--config", str(makepkg_config)]
    result = {"case": "package", "exit": 1, "uuid": owned.record["uuid"],
              "sourceIdentity": owned.record["current_source_identity"], "archiveSha256": archive_hash,
              "originalFiles": {name: digest(original / name) for name in ("PKGBUILD", "aerial-lock.install", ".SRCINFO")}}
    started = False
    try:
        with (output / "makepkg.log").open("w") as log:
            subprocess.run([*makepkg, "--noconfirm", "--cleanbuild", "--force"], cwd=test,
                           env=dict(os.environ, MAKEFLAGS="-j2"), stdout=log, stderr=subprocess.STDOUT, check=True, timeout=1800)
        packages = [Path(path).resolve(strict=True) for path in run([*makepkg, "--packagelist"], cwd=test).stdout.splitlines()]
        assert packages and all(path.is_relative_to(output / "packages") for path in packages)
        metadata_by_path = {path: run(["bsdtar", "-xOf", str(path), ".PKGINFO"]).stdout for path in packages}
        main_packages = [path for path, metadata in metadata_by_path.items() if "pkgname = aerial-lock\n" in metadata]
        assert len(main_packages) == 1
        package = main_packages[0]
        result["allPackages"] = {str(path.relative_to(output)): digest(path) for path in packages}
        result["packageSha256"] = digest(package)
        metadata = metadata_by_path[package]
        publish(output / "PKGINFO.txt", metadata)
        dependencies = re.findall(r"^depend = (.+)$", metadata, re.M)
        assert {"qt6-base", "dbus", "libarchive", "ffmpeg"}.issubset(dependencies)
        assert not any("optdepend = " + item + ":" in metadata for item in ("curl", "python", "tar"))
        tool_owner = run(["pacman", "-Qo", "/usr/lib/qt6/bin/qdbusxml2cpp"]).stdout
        publish(output / "qdbusxml2cpp-owner.txt", tool_owner)
        payload = output / "payload"
        payload.mkdir()
        run(["bsdtar", "-xf", str(package), "-C", str(payload)])
        check_layout(payload, "/usr", "/usr/lib/aerial-lock")
        manifest = {}
        overwrite = []
        for path in sorted(payload.rglob("*")):
            if not path.is_file() and not path.is_symlink():
                continue
            relative = str(path.relative_to(payload))
            if relative.startswith("."):
                continue
            installed = Path("/") / relative
            manifest[relative] = {"sha256": digest(path), "mode": path.lstat().st_mode,
                                  "link": os.readlink(path) if path.is_symlink() else None}
            if installed.exists() or installed.is_symlink():
                owner = subprocess.run(["pacman", "-Qqo", str(installed)], capture_output=True, text=True, timeout=10)
                if owner.returncode == 0:
                    assert owner.stdout.strip() == "aerial-lock", "another package owns " + relative
                else:
                    assert str(installed) in owned.record["installed_hashes"] and digest(installed) == owned.record["installed_hashes"][str(installed)]
                    overwrite.append(relative)
        publish(output / "package-manifest.json", json.dumps(manifest, indent=2))
        before = manager()
        assert "ActiveState=inactive" in before and "MainPID=0" in before
        linger = run(["loginctl", "show-user", str(os.getuid()), "-p", "Linger"]).stdout
        idle = run(["systemctl", "--user", "show", "aerial-idle.service", "-p", "ActiveState", "-p", "MainPID"]).stdout
        command = ["sudo", "pacman", "-U", "--noconfirm"]
        for relative in overwrite:
            # pacman splits commas even inside one glob argument. Its only
            # candidates are this package's already fully attested payload;
            # replace literal commas with one-character matches, not a broad *.
            pattern = relative.replace("[", "[[]").replace("*", "[*]").replace("?", "[?]").replace(",", "?")
            command.extend(["--overwrite", pattern])
        install = run([*command, str(package)])
        publish(output / "install.log", install.stdout + install.stderr)
        assert manager() == before, "package install started a current-session companion"
        for text in ("next login/user-manager startup", "aerialctl fetch", "no metered connections",
                     "AC/battery allowed", "10 GB", "new clips play next lock", "directly from Apple",
                     "not affiliated with Apple", "aerialscreensaver.github.io/legal", "remains optional"):
            assert text in install.stdout, "missing install disclosure: " + text
        for relative, entry in manifest.items():
            installed = Path("/") / relative
            assert digest(installed) == entry["sha256"] and installed.lstat().st_mode == entry["mode"], relative
        run(["systemctl", "--user", "daemon-reload"])
        run(["systemctl", "--user", "start", "aerial.service"])
        started = True
        active = manager()
        assert "ActiveState=active" in active and "MainPID=0\n" not in active
        upgrade = run(["sudo", "pacman", "-U", "--noconfirm", str(package)])
        publish(output / "upgrade.log", upgrade.stdout + upgrade.stderr)
        assert manager() == active, "package upgrade restarted the running companion"
        assert run(["loginctl", "show-user", str(os.getuid()), "-p", "Linger"]).stdout == linger
        assert run(["systemctl", "--user", "show", "aerial-idle.service", "-p", "ActiveState", "-p", "MainPID"]).stdout == idle
        for relative in manifest:
            path, staged = payload / relative, owned.paths["stage"] / relative
            staged.parent.mkdir(parents=True, exist_ok=True)
            if path.is_symlink():
                if staged.exists() or staged.is_symlink():
                    staged.unlink()
                staged.symlink_to(os.readlink(path))
            else:
                assert not staged.is_symlink()
                shutil.copy2(path, staged)
        result.update(exit=0, installNoStart=True, upgradeNoRestart=True, beforeInstall=before,
                      runningBeforeAndAfterUpgrade=active, manifest="package-manifest.json",
                      requiresPrepareRefresh=True, qdbusxml2cppOwner=tool_owner.strip())
    except subprocess.CalledProcessError as error:
        result["error"] = {"command": error.cmd, "exit": error.returncode,
                           "stdout": error.stdout, "stderr": error.stderr}
        raise
    finally:
        if started:
            run(["systemctl", "--user", "stop", "aerial.service"])
        publish(output / "result.json", json.dumps(result, indent=2))
    print("COMPANION_PACKAGE_PASS " + str(output))


if __name__ == "__main__":
    main()
