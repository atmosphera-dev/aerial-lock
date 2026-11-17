#!/usr/bin/env python3
"""Real Quickshell controller and native private-bus service, inside an owned VM."""
import argparse
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--private', action='store_true')
    args = parser.parse_args()
    if not args.private:
        return subprocess.call(['dbus-run-session', '--', sys.executable, __file__,
                                '--build', str(args.build), '--private'])
    source = Path(__file__).resolve().parent.parent
    peer = subprocess.Popen([str(args.build / 'power-actions-bus-fixture')],
                            stdout=subprocess.PIPE, text=True)
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(peer.stdout, selectors.EVENT_READ)
            assert selector.select(5), 'private peer readiness timeout'
        assert peer.stdout.readline() == 'READY\n', 'private peer did not start'
        with tempfile.TemporaryDirectory(prefix='power-controller-') as directory:
            root = Path(directory)
            shell = root / 'shell'
            shutil.copytree(source, shell, ignore=shutil.ignore_patterns(
                '.git', '.envrc', 'build', '__pycache__'))
            (shell / 'shell.qml').write_text(
                'import QtQuick\nimport Quickshell\nimport AerialLock.PowerTest\n'
                'ShellRoot { settings.watchFiles: false; FixtureActions { id: power } '
                'Loader { Component.onCompleted: setSource("root:/Tests/power-actions-shell.qml", '
                '{power: power}) } }\n')
            config = root / 'config/aerial-lock'
            (config / 'i18n').mkdir(parents=True)
            (config / 'i18n/en.json').write_text('{}')
            (config / 'config.json').write_text(json.dumps({'preferredResolution': 1080, 'sources': []}))
            env = dict(os.environ, XDG_CONFIG_HOME=str(root / 'config'),
                       XDG_CACHE_HOME=str(root / 'cache'),
                       QML_IMPORT_PATH=str(args.build / 'power-test'),
                       AERIAL_LOCK_VERBOSE='1')
            result = subprocess.run(['bash', str(source / 'Scripts/aerial-lock-run'),
                '--shell-dir', str(shell), '--plugin-root', str(source / 'native/qt-plugins')],
                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=25)
            log = re.sub(r'\x1b\[[0-9;]*m', '', result.stdout)
            print(log, end='')
            assert result.returncode == 0 and 'POWER_CONTROLLER_PASS ' in log, 'controller failed'
            assert not re.search(r'(?:WARN|ERROR|TypeError|ReferenceError)', log), 'unexpected warning'
            for secret in ('queued-synthetic', 'retained-draft'):
                assert secret not in log, 'input leaked in log'
    finally:
        subprocess.run(['busctl', '--user', 'call', 'org.aeriallock.PowerFixture', '/fixture',
                        'org.aeriallock.PowerFixture', 'Quit', 's', '{}'],
                       capture_output=True, timeout=5)
        peer.wait(timeout=5)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
