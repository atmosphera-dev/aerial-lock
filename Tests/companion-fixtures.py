#!/usr/bin/env python3
"""Real-PAM fixture administration inside an attested, disposable owned guest."""
import argparse
import json
import os
from pathlib import Path
import pwd
import secrets
import stat
import subprocess

from shadow_fixture import restore_account


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--vm-record", required=True, type=Path)
    parser.add_argument("operation", choices=("prepare", "restore"))
    args = parser.parse_args()
    assert os.geteuid() == 0 and int(os.environ["SUDO_UID"]) != 0
    uid = int(os.environ["SUDO_UID"])
    account = pwd.getpwuid(uid)
    root = args.root.resolve(strict=True)
    assert root == args.root and root.is_relative_to(Path(account.pw_dir)) and root != Path(account.pw_dir)
    assert root.stat().st_uid == uid
    assert args.vm_record.resolve() == root / "run/vm-record.json"
    metadata = args.vm_record.stat()
    assert metadata.st_uid == uid and stat.S_IMODE(metadata.st_mode) == 0o600
    record = json.loads(args.vm_record.read_text())
    assert record["paths"]["root"] == str(root) and record["guest"]["uid"] == uid
    assert record["domain"] != record["base_domain"]
    assert record["host_identity"]["uuid"] == record["uuid"]
    assert Path("/sys/class/dmi/id/product_uuid").read_text().strip() == record["uuid"]
    private = root / "run/companion-fixture-private"
    service = Path("/etc/pam.d/aerial-companion-test")
    secret = root / "run/pam-fixture-secret"
    if args.operation == "prepare":
        if private.exists():
            journal = json.loads((private / "journal.json").read_text())
            assert journal["uuid"] == record["uuid"] and service.read_text() == "auth required pam_unix.so\n"
            assert secret.exists()
            print("COMPANION_FIXTURE_REUSED")
            return
        assert not service.exists() and not secret.exists()
        private.mkdir(mode=0o700)
        original = next(line for line in Path("/etc/shadow").read_bytes().splitlines(keepends=True)
                        if line.startswith(account.pw_name.encode() + b":"))
        (private / "original-shadow").write_bytes(original)
        os.chmod(private / "original-shadow", 0o600)
        (private / "journal.json").write_text(json.dumps({"uuid": record["uuid"], "user": account.pw_name}))
        password = secrets.token_hex(16)
        subprocess.run(["chpasswd"], input=account.pw_name + ":" + password + "\n", text=True, check=True, timeout=15)
        secret.write_text(password + "\n")
        os.chmod(secret, 0o600)
        os.chown(secret, uid, account.pw_gid)
        password = None
        service.write_text("auth required pam_unix.so\n")
        os.chmod(service, 0o644)
        print("COMPANION_FIXTURE_PREPARED")
    else:
        assert private.stat().st_uid == 0 and stat.S_IMODE(private.stat().st_mode) == 0o700
        journal = json.loads((private / "journal.json").read_text())
        assert journal == {"uuid": record["uuid"], "user": account.pw_name}
        assert service.read_text() == "auth required pam_unix.so\n"
        restore_account(account.pw_name, (private / "original-shadow").read_bytes())
        service.unlink()
        secret.unlink()
        (private / "original-shadow").unlink()
        (private / "journal.json").unlink()
        private.rmdir()
        print("COMPANION_FIXTURE_RESTORED original_shadow_line_byte_identical=true")


if __name__ == "__main__":
    main()
