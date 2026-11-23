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


def check_layout(stage, prefix, libexec, bindir=None, settings=True, interactive_root=None):
    stage, prefix, libexec = map(Path, (stage, prefix, libexec))
    bindir = Path(bindir) if bindir else prefix / "bin"
    systemd = stage / prefix.relative_to("/") / "lib/systemd/user"
    unit = systemd / "aerial.service"
    assert section(unit, "Service") == {
        "Type": "dbus", "BusName": "org.aeriallock.Aerial1",
        "ExecStart": '"' + str(bindir / "aerial") + '"', "Restart": "on-failure",
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
        "Name": "org.aeriallock.Aerial1", "Exec": '"' + str(bindir / "aerial") + '"',
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
    if settings:
        check_settings_layout(stage, prefix, bindir, interactive_root)
    else:
        share = stage / prefix.relative_to("/") / "share"
        private = Path(interactive_root) if interactive_root else prefix / "lib/aerial-lock/interactive"
        for path in (stage / bindir.relative_to("/") / "aerial-settings",
                     share / "applications/org.aeriallock.AerialSettings.desktop",
                     share / "dbus-1/services/org.aeriallock.AerialSettings.service",
                     systemd / "aerial-tray.service", stage / private.relative_to("/"),
                     share / "licenses/aerial-lock/interactive",
                     share / "icons/hicolor/scalable/apps/org.aeriallock.AerialSettings.svg",
                     share / "icons/hicolor/scalable/status/org.aeriallock.AerialSettings-symbolic.svg"):
            assert not path.exists(), "core-only install contains settings payload: " + str(path)
    return descriptor


THEMES = ("Macintosh", "Windows7", "MaterialYou", "Enfield", "Clockwork")


def check_settings_layout(stage, prefix, bindir, interactive_root=None):
    """Desktop app payload; the tray is installed but never enabled or autostarted."""
    stage, prefix, bindir = map(Path, (stage, prefix, bindir))
    share = stage / prefix.relative_to("/") / "share"
    binary = stage / bindir.relative_to("/") / "aerial-settings"
    assert binary.is_file() and not binary.is_symlink() and binary.stat().st_mode & 0o777 == 0o755, binary
    private = Path(interactive_root) if interactive_root else prefix / "lib/aerial-lock/interactive"
    runtime = stage / private.relative_to("/")
    backing = []
    for module in ("InterActive", "InterActive/Foundation", "InterActive/Components"):
        directory = runtime / "qml" / module
        qmldir = directory / "qmldir"
        lines = [line.split() for line in qmldir.read_text().splitlines()]
        def value(key):
            return next(words[-1] for words in lines if key in words[:-1])
        links = [path for path in directory.iterdir() if path.is_symlink()]
        assert len(links) == 1, links
        library = runtime / "lib" / links[0].name
        plugin = directory / ("lib" + value("plugin") + ".so")
        assert library.is_file() and not library.is_symlink(), library
        assert plugin.is_file() and not plugin.is_symlink(), plugin
        link = directory / library.name
        assert link.is_symlink() and not os.path.isabs(os.readlink(link)) and link.resolve() == library.resolve(), link
        assert (directory / value("typeinfo")).is_file()
        backing.append(library.name)
    assert sorted(path.name for path in (runtime / "lib").iterdir()) == sorted(backing)
    licenses = share / "licenses/aerial-lock/interactive"
    for name in ("LICENSE.md", "COPYING", "COPYING.LESSER", "material-color-utilities/LICENSE", "material-color-utilities/README.md"):
        assert (licenses / name).is_file(), name
    assert section(share / "applications/org.aeriallock.AerialSettings.desktop", "Desktop Entry") == {
        "Name": "Aerial", "Type": "Application", "Exec": "aerial-settings", "Icon": "org.aeriallock.AerialSettings",
        "Terminal": "false", "DBusActivatable": "true", "Categories": "Settings;DesktopSettings;"}
    activation = share / "dbus-1/services/org.aeriallock.AerialSettings.service"
    assert section(activation, "D-BUS Service") == {"Name": "org.aeriallock.AerialSettings", "Exec": '"' + str(bindir / "aerial-settings") + '"'}
    unit = stage / prefix.relative_to("/") / "lib/systemd/user/aerial-tray.service"
    assert section(unit, "Unit") == {"Description": "Aerial tray icon", "PartOf": "graphical-session.target",
                                     "After": "graphical-session.target", "ConditionEnvironment": "WAYLAND_DISPLAY"}
    assert section(unit, "Service") == {"Type": "exec", "ExecStart": '"' + str(bindir / "aerial-settings") + '" --tray', "Restart": "on-failure"}
    assert section(unit, "Install") == {"WantedBy": "graphical-session.target"}
    for path in (activation, unit):
        assert "@BINDIR@" not in path.read_text()
        if stage != Path("/"):
            assert str(stage) not in path.read_text()
    # Only the content service has a vendor wants link; no tray/idle startup.
    vendor = [unit.parent, stage / "etc/systemd/user"]
    links = sorted(str(p.relative_to(stage)) for directory in vendor for p in directory.glob("*.wants/*")
                   if p.name.startswith("aerial") or p.name.startswith("org.aeriallock"))
    assert links == [str((unit.parent / "default.target.wants/aerial.service").relative_to(stage))], links
    autostart = [stage / "etc/xdg/autostart", share / "autostart"]
    assert not [p for directory in autostart for p in directory.glob("*") if "aerial" in p.name.lower()]
    if stage != Path("/"):
        assert not [p for p in stage.rglob("*") if p.name == "autostart" or p.suffix == ".desktop" and p.parent.name != "applications"]
    icons = share / "icons/hicolor/scalable"
    for icon in (icons / "apps/org.aeriallock.AerialSettings.svg", icons / "status/org.aeriallock.AerialSettings-symbolic.svg"):
        assert icon.is_file() and icon.stat().st_mode & 0o777 == 0o644 and icon.read_bytes().startswith(b"<svg"), icon
    owned_icons = sorted(str(p.relative_to(icons.parent)) for p in (share / "icons/hicolor").glob("*/*/org.aeriallock.*"))
    assert owned_icons == ["scalable/apps/org.aeriallock.AerialSettings.svg", "scalable/status/org.aeriallock.AerialSettings-symbolic.svg"], owned_icons
    previews = share / "aerial-lock/Themes/previews"
    assert sorted(p.name for p in previews.iterdir()) == sorted(name + ".png" for name in THEMES), previews
    for preview in previews.iterdir():
        assert preview.stat().st_mode & 0o777 == 0o644 and preview.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"), preview
