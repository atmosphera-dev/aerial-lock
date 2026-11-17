#!/usr/bin/env python3
"""Root-only fixtures inside the operator-recorded disposable power-test VM.

No product runtime uses this file. Never run on a physical host or a base.
Secrets and original credentials remain in the guest and are not evidence.
"""
import argparse
import json
import os
from pathlib import Path
import pwd
import secrets
import stat
import subprocess
import time
from shadow_fixture import restore_account


def run(*argv, **kwargs):
    return subprocess.run(argv, check=True, timeout=20, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--vm-record', required=True, type=Path)
    parser.add_argument('operation', choices=('prepare', 'restore', 'policy'))
    parser.add_argument('policy', nargs='?', choices=('challenge', 'no', 'restore'))
    args = parser.parse_args()
    assert os.geteuid() == 0 and int(os.environ['SUDO_UID']) != 0
    caller = int(os.environ['SUDO_UID'])
    metadata = args.vm_record.stat()
    assert metadata.st_uid == caller and stat.S_IMODE(metadata.st_mode) == 0o600
    record = json.loads(args.vm_record.read_text())
    root = args.root.resolve(strict=True)
    assert args.root == root and root == Path(record['paths']['root'])
    assert root.is_relative_to(pwd.getpwuid(caller).pw_dir) and root != Path(pwd.getpwuid(caller).pw_dir)
    assert root.stat().st_uid == caller and record['guest']['uid'] == caller
    assert args.vm_record.resolve() == root / 'run/owned-vm.json'
    assert record['domain'] != record['base_domain'] and record['host_identity']['uuid'] == record['uuid']
    assert Path('/sys/class/dmi/id/product_uuid').read_text().strip() == record['uuid']
    private = root / 'run/fixture-private'
    rule = Path('/etc/polkit-1/rules.d/00-aerial-power-actions-test.rules')
    pam = Path('/etc/pam.d/aerial-power-actions-test')
    other_pam = Path('/etc/pam.d/aerial-power-other-session')
    other = 'aerial-power-other'
    unit = 'aerial-power-other-session.service'
    if args.operation == 'prepare':
        assert not private.exists() and not pam.exists() and not rule.exists() and not other_pam.exists()
        private.mkdir(mode=0o700)
        original = next(line for line in Path('/etc/shadow').read_text().splitlines(keepends=True)
                        if line.startswith(record['guest']['user'] + ':'))
        (private / 'tester-shadow').write_text(original)
        os.chmod(private / 'tester-shadow', 0o600)
        password = secrets.token_hex(16)
        run('chpasswd', input=record['guest']['user'] + ':' + password + '\n', text=True)
        secret = root / 'run/pam-fixture-secret'
        secret.write_text(password + '\n')
        os.chmod(secret, 0o600)
        os.chown(secret, caller, pwd.getpwuid(caller).pw_gid)
        pam.write_text('auth required pam_unix.so\n')
        os.chmod(pam, 0o644)
        (private / 'journal.json').write_text(json.dumps(dict(uuid=record['uuid'], pam=str(pam),
            rule=str(rule), credential_backup='tester-shadow', no_personal_account=True)))
        print('FIXTURES_PREPARED')
        return
    assert private.stat().st_uid == 0 and stat.S_IMODE(private.stat().st_mode) == 0o700
    if args.operation == 'policy':
        if args.policy == 'restore':
            if rule.exists():
                assert 'aerial-power-actions-test' in rule.read_text()
                rule.unlink()
            if (private / 'other-created').exists():
                load = subprocess.check_output(['systemctl', 'show', unit, '-p', 'LoadState', '--value'], text=True).strip()
                if load != 'not-found':
                    run('systemctl', 'stop', unit)
                if subprocess.run(['pgrep', '-u', other], capture_output=True).returncode == 0:
                    run('loginctl', 'terminate-user', other)
                deadline = time.monotonic() + 10
                while subprocess.run(['pgrep', '-u', other], capture_output=True).returncode == 0:
                    if time.monotonic() >= deadline:
                        raise RuntimeError('fixture user processes remain after logout')
                    time.sleep(.05)
                run('userdel', other)
                assert other_pam.read_text() == 'account required pam_unix.so\nsession required pam_systemd.so class=user\n'
                other_pam.unlink()
                (private / 'other-created').unlink()
            print('POLICY_RESTORED')
            return
        assert args.policy in ('challenge', 'no') and not rule.exists()
        assert not (private / 'other-created').exists() and not other_pam.exists()
        active = subprocess.check_output(['systemctl', 'show', unit, '-p', 'ActiveState', '--value'], text=True).strip()
        if active == 'failed':
            run('systemctl', 'reset-failed', unit)
        try:
            pwd.getpwnam(other)
        except KeyError:
            pass
        else:
            raise RuntimeError('fixture user already exists')
        run('useradd', '--no-create-home', '--shell', '/bin/sh', other)
        (private / 'other-created').write_text(other)
        other_pam.write_text('account required pam_unix.so\nsession required pam_systemd.so class=user\n')
        os.chmod(other_pam, 0o644)
        run('systemd-run', '--unit=' + unit, '--uid=' + other, '--property=PAMName=aerial-power-other-session',
            '--setenv=XDG_SESSION_CLASS=user', '--property=Type=exec', '/usr/bin/sleep', 'infinity')
        result = 'AUTH_ADMIN' if args.policy == 'challenge' else 'NO'
        # set-reboot-* permissions imply reboot in systemd's installed policy.
        rule.write_text('// aerial-power-actions-test: owned guest fixture\n'
            'polkit.addRule(function(action, subject) {\n'
            ' if (subject.user === ' + json.dumps(record['guest']['user']) +
            ' && /^org\\.freedesktop\\.login1\\.(suspend|hibernate|reboot|power-off|set-reboot-.*)(-|$)/.test(action.id))\n'
            '  return polkit.Result.' + result + ';\n});\n')
        os.chmod(rule, 0o644)
        run('loginctl', 'list-sessions', '--no-legend')
        print('POLICY_' + args.policy.upper())
        return
    assert not rule.exists() and not other_pam.exists() and not (private / 'other-created').exists()
    restore_account(record['guest']['user'], (private / 'tester-shadow').read_bytes())
    assert pam.read_text() == 'auth required pam_unix.so\n'
    pam.unlink()
    (root / 'run/pam-fixture-secret').unlink()
    (private / 'tester-shadow').unlink()
    (private / 'journal.json').unlink()
    private.rmdir()
    print('FIXTURES_RESTORED original_shadow_line_byte_identical=true no_personal_account=true')


if __name__ == '__main__':
    main()
