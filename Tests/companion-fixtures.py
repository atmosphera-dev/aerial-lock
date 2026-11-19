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

from shadow_fixture import account_line, restore_account


def restore_fixture(private, service, secret, uuid, user):
    """Recover even when preparation or a previous cleanup stopped halfway."""
    if not private.exists():
        assert not service.exists() and not secret.exists()
        return
    assert not private.is_symlink() and stat.S_IMODE(private.stat().st_mode) == 0o700
    journal_path = private / "journal.json"
    if not journal_path.exists():
        # The last cleanup operation was unlinking the journal. An empty
        # directory is the only state that needs no recovery authority.
        assert not service.exists() and not secret.exists() and not list(private.iterdir())
        private.rmdir()
        return
    journal = json.loads(journal_path.read_text())
    assert journal["uuid"] == uuid and journal["user"] == user
    backup = private / "original-shadow"
    if backup.exists():
        metadata = backup.lstat()
        assert stat.S_ISREG(metadata.st_mode) and metadata.st_uid == private.stat().st_uid
        assert stat.S_IMODE(metadata.st_mode) == 0o600 and metadata.st_nlink == 1
        original = backup.read_bytes()
    else:
        assert journal.get("restored") is True
        original = journal["original"].encode()
    assert account_line(original, user) == original and original.endswith(b"\n") and original.count(b":") == 8
    if service.exists():
        assert not service.is_symlink() and service.read_text() == "auth required pam_unix.so\n"
    if not journal.get("restored"):
        restore_account(user, original)
        # Retain recovery authority until the very last unlink. This also
        # permits a retry after the redundant standalone backup was removed.
        journal.update(restored=True, original=original.decode())
        staged = private / "journal.next"
        with staged.open("w") as stream:
            os.fchmod(stream.fileno(), 0o600)
            json.dump(journal, stream)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(staged, journal_path)
        directory = os.open(private, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    service.unlink(missing_ok=True)
    secret.unlink(missing_ok=True)
    backup.unlink(missing_ok=True)
    (private / "journal.next").unlink(missing_ok=True)
    journal_path.unlink()
    private.rmdir()


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
    service = Path("/etc/pam.d/aerial-test")
    secret = root / "run/pam-fixture-secret"
    if private.exists():
        assert not private.is_symlink() and private.stat().st_uid == 0 and stat.S_IMODE(private.stat().st_mode) == 0o700
    if args.operation == "prepare":
        if private.exists():
            journal_path = private / "journal.json"
            if journal_path.exists():
                journal = json.loads(journal_path.read_text())
                assert journal["uuid"] == record["uuid"] and journal["user"] == account.pw_name
                if not journal.get("restored") and service.exists() and secret.exists():
                    assert service.read_text() == "auth required pam_unix.so\n"
                    print("COMPANION_FIXTURE_REUSED")
                    return
            restore_fixture(private, service, secret, record["uuid"], account.pw_name)
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
        restore_fixture(private, service, secret, record["uuid"], account.pw_name)
        print("COMPANION_FIXTURE_RESTORED original_shadow_line_byte_identical=true")


if __name__ == "__main__":
    main()
