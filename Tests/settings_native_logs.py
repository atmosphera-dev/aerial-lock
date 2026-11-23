"""Native-only log contract; retain originals and audit through fixture teardown."""
import json
import os
from pathlib import Path
import re
import subprocess

QT_LOG_ENV = {"QT_FORCE_STDERR_LOGGING": "1",
              "QT_MESSAGE_PATTERN": "AERIAL_QT|%{type}|%{category}|%{message}"}
ANSI = re.compile(r"\x1b\[[0-9;]*m")
DISABLE_SURFACE = re.compile(
    r"virtual void QtWaylandClient::QWaylandTextInputv3::disableSurface\(wl_surface\*\) "
    r"Trying to disable 0x[0-9a-f]+ but 0x0 is focused")


def accepted_warning(message, category, version):
    # Approved Qt >= 6.12 exception only. Documents/Aerial/TODO.md, stock Qt
    # disableSurface item; also reproduced in the installed app on the real bus.
    return (tuple(version) >= (6, 12, 0) and category == "qt.qpa.wayland.textinput"
            and DISABLE_SURFACE.fullmatch(message) is not None)


def inspect_text(text, version):
    accepted, unexpected = [], []
    for raw in text.splitlines():
        line = ANSI.sub("", raw)
        if line.startswith("AERIAL_QT|"):
            _, severity, category, message = line.split("|", 3)
            if severity in ("warning", "critical", "fatal"):
                if severity == "warning" and accepted_warning(message, category, version):
                    accepted.append(line)
                else: unexpected.append(line)
        elif re.search(r"\b(?:QWARN|WARN|WARNING|ERROR|CRITICAL|FATAL)\b|QQmlApplicationEngine failed|ReferenceError:|TypeError:", line):
            # Includes Quickshell's own handler and QtTest, which bypass the
            # normal Qt message pattern. Neither gets a broad text exception.
            unexpected.append(line)
    return accepted, unexpected


def require_quiet(text, version):
    """For probes that otherwise promise no output, allow only the exact warning."""
    accepted, unexpected = inspect_text(text, version)
    remainder = [line for line in text.splitlines() if line not in accepted]
    if unexpected or any(line.strip() for line in remainder):
        raise RuntimeError("unexpected native probe log: " + text)


def parse_qt_version(text):
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", text):
        raise ValueError("expected a numeric Qt major.minor.patch version")
    return tuple(map(int, text.split(".")))


def qt_version(build):
    return parse_qt_version((build / "settings-qt-version.txt").read_text().strip())


def journal_cursor(text):
    # journalctl -n 0 can prefix its cursor with "-- No entries --".
    # Read the explicit cursor record, not the presentation of the entry list.
    cursors = [line.removeprefix("-- cursor: ") for line in text.splitlines()
               if line.startswith("-- cursor: ")]
    if len(cursors) != 1 or not cursors[0]:
        raise RuntimeError("journal cursor unavailable")
    return cursors[0]


def inspect_journal(entries, version):
    accepted, unexpected = [], []
    for entry in entries:
        exe = Path(entry.get("_EXE", "")).name
        if not (exe in ("aerial-settings", "qs", "quickshell") or exe.startswith("settings-") or "QT_CATEGORY" in entry):
            continue
        message = entry.get("MESSAGE", "")
        priority = int(entry.get("PRIORITY", 6))
        yes, no = inspect_text(message, version)
        if priority < 4:
            unexpected.append(message)
        elif priority == 4 and not no and (
                yes or accepted_warning(message, entry.get("QT_CATEGORY"), version)):
            accepted.extend(yes or [message])
        elif yes or no or priority == 4:
            unexpected.extend(no or [message])
    return accepted, unexpected


class NativeLogGate:
    def __init__(self, root, case):
        self.root, self.case = root, case
        self.version = qt_version(root / "build/debug/aerial-tests")
        self.before = {p: p.stat().st_mtime_ns for p in (root / "results").rglob("*.log")}
        cursor = subprocess.check_output(["sudo", "journalctl", "-n", "0", "--show-cursor", "--no-pager"], text=True, timeout=10)
        self.cursor = journal_cursor(cursor)
        self.result = None

    def finish(self):
        if self.result is not None: return self.result
        subprocess.run(["sudo", "journalctl", "--sync"], check=True, timeout=10)
        journal = subprocess.check_output(["sudo", "journalctl", "--after-cursor=" + self.cursor,
                                           "-o", "json", "--no-pager"], text=True, timeout=30)
        output = self.root / "results" / ("settings-" + self.case + "-journal.jsonl")
        output.write_text(journal)
        accepted, unexpected = inspect_journal([json.loads(line) for line in journal.splitlines()], self.version)
        logs = []
        for path in (self.root / "results").rglob("*.log"):
            if self.before.get(path) == path.stat().st_mtime_ns: continue
            yes, no = inspect_text(path.read_text(errors="replace"), self.version)
            accepted.extend({"file": str(path), "message": message} for message in yes)
            unexpected.extend({"file": str(path), "message": message} for message in no)
            logs.append(str(path))
        self.result = dict(qt=self.version, accepted=accepted, unexpected=unexpected, logs=logs, journal=str(output))
        (self.root / "results" / ("settings-" + self.case + "-logs.json")).write_text(json.dumps(self.result, indent=2))
        return self.result
