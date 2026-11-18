"""Shared installed-layout assertions, without starting a service."""
import configparser
import os
from pathlib import Path


def section(path, name):
    document = configparser.ConfigParser(interpolation=None)
    document.optionxform = str
    document.read_string(path.read_text())
    assert path.stat().st_mode & 0o777 == 0o644, path
    return dict(document[name])


def check_layout(stage, prefix, libexec):
    stage, prefix, libexec = map(Path, (stage, prefix, libexec))
    systemd = stage / prefix.relative_to("/") / "lib/systemd/user"
    unit = systemd / "aerial-companion.service"
    assert section(unit, "Service") == {
        "Type": "dbus", "BusName": "org.aeriallock.Companion1",
        "ExecStart": str(libexec / "aerial-companion"), "Restart": "on-failure",
        "RestartSec": "5", "UMask": "0077"}
    assert section(unit, "Install") == {"WantedBy": "default.target"}
    assert "After=" not in unit.read_text() and "Requires=" not in unit.read_text()
    link = systemd / "default.target.wants/aerial-companion.service"
    assert link.is_symlink() and os.readlink(link) == "../aerial-companion.service", link
    assert link.resolve() == unit.resolve()
    descriptor = stage / prefix.relative_to("/") / "share/dbus-1/services/org.aeriallock.Companion1.service"
    assert section(descriptor, "D-BUS Service") == {
        "Name": "org.aeriallock.Companion1", "Exec": str(libexec / "aerial-companion"),
        "SystemdService": "aerial-companion.service"}
    for path in (unit, descriptor):
        assert "@LIBEXECDIR@" not in path.read_text()
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
