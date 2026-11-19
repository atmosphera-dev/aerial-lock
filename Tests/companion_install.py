"""Shared installed-layout assertions, without starting a service."""
import configparser
import hashlib
import os
from pathlib import Path


def section(path, name):
    document = configparser.ConfigParser(interpolation=None)
    document.optionxform = str
    document.read_string(path.read_text())
    assert path.stat().st_mode & 0o777 == 0o644, path
    return dict(document[name])


def private_executables(stage):
    """Resolve the single verified private payload, not an Arch-only default."""
    stage = Path(stage)
    runners = list(stage.rglob("aerial-lock-run"))
    if len(runners) != 1:
        raise RuntimeError("expected one staged private executable directory")
    directory = runners[0].parent
    for name in ("aerial-lock-run", "aerial-lock-auth", "aerial-lock-emergency", "aerial-media-probe"):
        path = directory / name
        if not path.is_file() or path.is_symlink() or path.stat().st_mode & 0o777 != 0o755:
            raise RuntimeError("invalid private executable: " + str(path))
    return directory


def installed_private_executable(owned, name):
    staged = owned.check_binary(private_executables(owned.paths["stage"]) / name)
    installed = Path("/") / staged.relative_to(owned.paths["stage"])
    if hashlib.sha256(installed.read_bytes()).hexdigest() != owned.record["installed_hashes"].get(str(installed)):
        raise RuntimeError("installed private executable provenance mismatch: " + str(installed))
    return installed


def check_layout(stage, prefix, libexec, bindir=None):
    stage, prefix, libexec = map(Path, (stage, prefix, libexec))
    bindir = Path(bindir) if bindir else prefix / "bin"
    systemd = stage / prefix.relative_to("/") / "lib/systemd/user"
    unit = systemd / "aerial.service"
    assert section(unit, "Service") == {
        "Type": "dbus", "BusName": "org.aeriallock.Aerial1",
        "ExecStart": str(bindir / "aerial"), "Restart": "on-failure",
        "RestartSec": "5", "UMask": "0077"}
    assert section(unit, "Install") == {"WantedBy": "default.target"}
    assert "After=" not in unit.read_text() and "Requires=" not in unit.read_text()
    link = systemd / "default.target.wants/aerial.service"
    assert link.is_symlink() and os.readlink(link) == "../aerial.service", link
    assert link.resolve() == unit.resolve()
    descriptor = stage / prefix.relative_to("/") / "share/dbus-1/services/org.aeriallock.Aerial1.service"
    binary = stage / bindir.relative_to("/") / "aerial"
    if stage != Path("/"):
        assert private_executables(stage) == stage / libexec.relative_to("/")
    assert binary.is_file() and binary.stat().st_mode & 0o777 == 0o755
    assert not (stage / libexec.relative_to("/") / "aerial").exists()
    assert section(descriptor, "D-BUS Service") == {
        "Name": "org.aeriallock.Aerial1", "Exec": str(bindir / "aerial"),
        "SystemdService": "aerial.service"}
    for path in (unit, descriptor):
        assert "@BINDIR@" not in path.read_text()
        if stage != Path("/"):
            assert str(stage) not in path.read_text()
    licenses = stage / prefix.relative_to("/") / "share/licenses/aerial-lock"
    assert (licenses / "LICENSE").is_file()
    notice = licenses / "third-party-notices.md"
    assert notice.stat().st_mode & 0o777 == 0o644
    text = notice.read_text()
    assert "MIT License" in text and "2015-present Guillaume Louel, and contributors" in text
    assert "THE SOFTWARE IS PROVIDED" in text and "SOFTWARE." in text
    return descriptor
