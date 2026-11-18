#!/usr/bin/env python3
"""Account-scoped activation fixture administration in an attested owned VM."""
import argparse
import json
import os
from pathlib import Path
import pwd
import stat
import subprocess
import sys


def run(*argv):
    return subprocess.run(argv, check=True, capture_output=True, text=True, timeout=30).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--vm-record", type=Path, required=True)
    parser.add_argument("operation", choices=("create", "mask", "unmask", "stop", "remove"))
    args = parser.parse_args()
    assert os.geteuid() == 0 and int(os.environ["SUDO_UID"]) != 0
    uid = int(os.environ["SUDO_UID"])
    root = args.root.resolve(strict=True)
    assert root == args.root and root.stat().st_uid == uid
    assert root.is_relative_to(Path(pwd.getpwuid(uid).pw_dir)) and root != Path.home()
    record_path = root / "run/vm-record.json"
    assert args.vm_record == record_path and stat.S_IMODE(record_path.stat().st_mode) == 0o600
    record = json.loads(record_path.read_text())
    assert record["guest"]["uid"] == uid and record["domain"] != record["base_domain"]
    assert record["uuid"] == Path("/sys/class/dmi/id/product_uuid").read_text().strip()
    assert record["guest"]["boot_id"] == Path("/proc/sys/kernel/random/boot_id").read_text().strip()
    private = root / "run/companion-login-private"
    name = "aerial-companion-test"
    home = Path("/home") / name
    if args.operation == "create":
        assert not private.exists() and not home.exists()
        try:
            pwd.getpwnam(name)
        except KeyError:
            pass
        else:
            raise RuntimeError("fixture account already exists; refusing adoption")
        document = json.loads(sys.stdin.read())
        private.mkdir(mode=0o700)
        run("useradd", "--create-home", "--home-dir", str(home), "--shell", "/bin/bash", name)
        account = pwd.getpwnam(name)
        journal = {"uuid": record["uuid"], "name": name, "uid": account.pw_uid, "gid": account.pw_gid,
                   "home": str(home), "inode": home.stat().st_ino}
        (private / "journal.json").write_text(json.dumps(journal))
        directory = home / ".config/aerial-lock"
        directory.mkdir(mode=0o700, parents=True)
        settings = directory / "config.json"
        settings.write_text(json.dumps(document))
        settings.chmod(0o600)
        for path in (home / ".config", directory, settings):
            os.chown(path, account.pw_uid, account.pw_gid)
        print(json.dumps(journal))
        return
    assert private.stat().st_uid == 0 and stat.S_IMODE(private.stat().st_mode) == 0o700
    journal = json.loads((private / "journal.json").read_text())
    account = pwd.getpwnam(name)
    assert journal == {"uuid": record["uuid"], "name": name, "uid": account.pw_uid, "gid": account.pw_gid,
                       "home": str(home), "inode": home.stat().st_ino}
    assert account.pw_dir == str(home) and not home.is_symlink()
    if args.operation in ("stop", "remove"):
        # Only this task-created fixture account, never tester's desktop.
        subprocess.run(["loginctl", "terminate-user", name], check=False, capture_output=True, timeout=30)
        run("systemctl", "stop", f"user@{account.pw_uid}.service")
        if args.operation == "remove":
            assert not Path(f"/var/lib/systemd/linger/{name}").exists()
            run("userdel", "--remove", name)
            assert not home.exists()
            (private / "journal.json").unlink()
            private.rmdir()
    else:
        directory = home / ".config/systemd/user"
        directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        for path in (home / ".config/systemd", directory):
            os.chown(path, account.pw_uid, account.pw_gid)
        mask = directory / "aerial-companion.service"
        if args.operation == "mask":
            assert not mask.exists() and not mask.is_symlink()
            mask.symlink_to("/dev/null")
            os.lchown(mask, account.pw_uid, account.pw_gid)
        else:
            assert mask.is_symlink() and os.readlink(mask) == "/dev/null"
            mask.unlink()
    print("COMPANION_LOGIN_FIXTURE_" + args.operation.upper())


if __name__ == "__main__":
    main()
