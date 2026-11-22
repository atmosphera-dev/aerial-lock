#!/usr/bin/env python3
"""Signed, complete and non-destructive local vendor refresh contracts."""
import hashlib
from importlib.machinery import SourceFileLoader
from importlib.util import module_from_spec, spec_from_loader
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1]
loader = SourceFileLoader("vendor_refresh", str(SOURCE / "Scripts/update-interactive"))
spec = spec_from_loader(loader.name, loader)
vendor_refresh = module_from_spec(spec)
loader.exec_module(vendor_refresh)


class VendorRefresh(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="aerial-vendor-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.upstream = self.root / "upstream"
        self.repo = self.root / "aerial"
        self.upstream.mkdir()
        (self.repo / "Scripts").mkdir(parents=True)
        shutil.copy2(SOURCE / "Scripts/update-interactive", self.repo / "Scripts/update-interactive")
        self.key = self.root / "signer"
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(self.key)], check=True)
        trust = self.root / "allowed_signers"
        trust.write_text('fixture@example.invalid namespaces="git" ' + self.key.with_suffix(".pub").read_text())
        for repo in (self.upstream, self.repo):
            self.git(repo, "init", "-q")
            for key, value in {"user.name": "Vendor fixture", "user.email": "fixture@example.invalid",
                               "gpg.format": "ssh", "user.signingkey": str(self.key),
                               "gpg.ssh.allowedSignersFile": str(trust), "commit.gpgsign": "true", "tag.gpgSign": "false"}.items():
                self.git(repo, "config", key, value)
        (self.upstream / "COPYING").write_text("fixture license\n")
        (self.upstream / "COPYING.LESSER").write_text("fixture LGPL\n")
        (self.upstream / "LICENSE.md").write_text("fixture license summary\n")
        tool = self.upstream / "tool"
        tool.write_text("#!/bin/sh\nexit 0\n")
        tool.chmod(0o755)
        (self.upstream / "third_party/mcu").mkdir(parents=True)
        (self.upstream / "third_party/mcu/NOTICE").write_text("fixture dependency notice\n")
        self.git(self.upstream, "add", ".")
        self.git(self.upstream, "commit", "-qm", "fixture source")
        self.git(self.upstream, "tag", "-s", "v1", "-m", "fixture release")

    def git(self, repo, *args):
        return subprocess.check_output(["git", "-C", str(repo), *args], stderr=subprocess.STDOUT)

    def update(self, tag="v1", success=True):
        result = subprocess.run([sys.executable, str(self.repo / "Scripts/update-interactive"),
                                 "--source", str(self.upstream), "--", tag], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    @property
    def vendor(self):
        return self.repo / "third_party/interactive"

    def test_complete_export_and_noop(self):
        self.git(self.upstream, "config", "tar.umask", "0022")
        self.update()
        self.assertEqual((self.vendor / "tool").stat().st_mode & 0o777, 0o755)
        self.assertEqual((self.vendor / "third_party/mcu/NOTICE").read_text(), "fixture dependency notice\n")
        archive = self.git(self.upstream, "-c", "tar.umask=0002", "archive", "--format=tar", "v1")
        self.assertIn(hashlib.sha256(archive).hexdigest(), (self.vendor / "VENDOR.md").read_text())
        for name in ("LICENSE.md", "COPYING", "COPYING.LESSER"):
            self.assertTrue((self.vendor / name).is_file())
            self.assertIn(name, (self.vendor / "VENDOR.md").read_text())
        before = {p: p.stat().st_mtime_ns for p in self.vendor.rglob("*")}
        self.assertIn("unchanged", self.update().stdout)
        self.assertEqual(before, {p: p.stat().st_mtime_ns for p in self.vendor.rglob("*")})

    def test_missing_unsigned_and_lightweight_tags(self):
        self.git(self.upstream, "tag", "--no-sign", "lightweight")
        self.git(self.upstream, "tag", "--no-sign", "-a", "unsigned", "-m", "unsigned fixture")
        for tag in ("absent", "unsigned", "lightweight", "--help", "../bad"):
            with self.subTest(tag=tag):
                self.update(tag, success=False)
                self.assertFalse(self.vendor.exists())

    def test_tag_reads_keep_one_object(self):
        tag = self.git(self.upstream, "rev-parse", "refs/tags/v1").strip().decode()
        commit = self.git(self.upstream, "rev-parse", "v1^{commit}").strip().decode()
        with patch.object(vendor_refresh, "git", wraps=vendor_refresh.git) as read:
            vendor_refresh.refresh(self.upstream, self.repo, "v1")
        calls = [call.args[1:] for call in read.call_args_list]
        self.assertEqual(calls[0], ("rev-parse", "--verify", "refs/tags/v1"))
        self.assertIn(("cat-file", "-t", tag), calls)
        self.assertIn(("rev-parse", tag + "^{commit}"), calls)
        self.assertIn(("-c", "tar.umask=0002", "archive", "--format=tar", commit), calls)
        self.assertEqual(sum("refs/tags/v1" in arg for call in calls for arg in call), 1)

    def test_rollback_failure_preserves_backup(self):
        self.vendor.mkdir(parents=True)
        (self.vendor / "retained").write_text("previous")
        stage = self.vendor.parent / "stage"
        stage.mkdir()
        rename = Path.rename
        def fail_publication(path, target):
            if target == self.vendor:
                raise OSError("fixture could not rename into destination")
            return rename(path, target)
        with patch.object(Path, "rename", fail_publication):
            with self.assertRaisesRegex(ValueError, "previous vendor retained"):
                vendor_refresh.publish(stage, self.vendor, vendor_refresh.inventory(self.vendor, include_notice=True))
        backups = list(self.vendor.parent.glob(".interactive-backup-*/previous/retained"))
        self.assertEqual([p.read_text() for p in backups], ["previous"])

    def test_dirty_and_unexpected_vendor_preserved(self):
        self.vendor.mkdir(parents=True)
        sentinel = self.vendor / "personal"
        sentinel.write_text("preserve me")
        self.update(success=False)
        self.assertEqual(sentinel.read_text(), "preserve me")

    def test_manual_edit_extra_file_and_symlink_refused(self):
        self.update()
        self.git(self.repo, "add", ".")
        self.git(self.repo, "commit", "-qm", "vendor fixture")
        tool = self.vendor / "tool"
        tool.write_text("manual edit")
        self.update(success=False)
        self.assertEqual(tool.read_text(), "manual edit")
        tool.write_bytes((self.upstream / "tool").read_bytes())
        extra = self.vendor / "extra"
        extra.write_text("keep extra")
        self.update(success=False)
        self.assertEqual(extra.read_text(), "keep extra")
        extra.unlink()
        extra.symlink_to(self.root / "outside")
        self.update(success=False)
        self.assertTrue(extra.is_symlink())

    def test_signed_upgrade(self):
        self.update()
        self.git(self.repo, "add", ".")
        self.git(self.repo, "commit", "-qm", "vendor fixture")
        self.git(self.upstream, "config", "tar.umask", "0022")
        (self.upstream / "COPYING").write_text("new fixture license\n")
        self.git(self.upstream, "commit", "-qam", "fixture update")
        self.git(self.upstream, "tag", "-s", "v2", "-m", "second fixture release")
        self.update("v2")
        self.assertEqual((self.vendor / "COPYING").read_text(), "new fixture license\n")
        self.assertIn("unchanged", self.update("v2").stdout)
        # An uncommitted refresh may be repeated, but not replaced by another tag.
        self.update("v1", success=False)
        self.assertEqual((self.vendor / "COPYING").read_text(), "new fixture license\n")

    def test_publication_preserves_edits_after_validation(self):
        self.update()
        for tag, name in (("v2", "COPYING"), ("v3", "VENDOR.md")):
            with self.subTest(name=name):
                self.git(self.repo, "add", "third_party/interactive")
                self.git(self.repo, "commit", "-qm", "vendor fixture")
                (self.upstream / "COPYING").write_text(tag + " license\n")
                self.git(self.upstream, "commit", "-qam", "fixture update")
                self.git(self.upstream, "tag", "-s", tag, "-m", "fixture release")
                publish = vendor_refresh.publish

                def edit_before_publish(*args):
                    (self.vendor / name).write_text("late local edit\n")
                    return publish(*args)

                with patch.object(vendor_refresh, "publish", side_effect=edit_before_publish):
                    with self.assertRaisesRegex(ValueError, "previous vendor retained") as error:
                        vendor_refresh.refresh(self.upstream, self.repo, tag)
                previous = Path(str(error.exception).rsplit(" at ", 1)[1])
                self.assertEqual((previous / name).read_text(), "late local edit\n")
                self.assertEqual((self.vendor / "COPYING").read_text(), tag + " license\n")

    def test_export_attributes_cannot_omit_tracked_source(self):
        (self.upstream / ".gitattributes").write_text("tool export-ignore\n")
        self.git(self.upstream, "add", ".gitattributes")
        self.git(self.upstream, "commit", "-qm", "fixture excluded source")
        self.git(self.upstream, "tag", "-s", "incomplete", "-m", "incomplete fixture")
        self.update("incomplete", success=False)
        self.assertFalse(self.vendor.exists())
        (self.upstream / ".gitattributes").write_text("tool export-subst\n")
        (self.upstream / "tool").write_text("$Format:%H$\n")
        self.git(self.upstream, "commit", "-qam", "fixture rewritten export")
        self.git(self.upstream, "tag", "-s", "rewritten", "-m", "rewritten fixture")
        self.update("rewritten", success=False)
        self.assertFalse(self.vendor.exists())

    def test_dirty_metadata_is_not_used_as_a_git_operand(self):
        self.update()
        self.git(self.repo, "add", ".")
        self.git(self.repo, "commit", "-qm", "vendor fixture")
        notice = self.vendor / "VENDOR.md"
        text = notice.read_text()
        commit = self.git(self.upstream, "rev-parse", "v1^{commit}").decode().strip()
        notice.write_text(text.replace("Commit: " + commit, "Commit: --remote=invalid"))
        result = self.update(success=False)
        self.assertIn("vendor is dirty", result.stderr)
        self.assertIn("Commit: --remote=invalid", notice.read_text())


if __name__ == "__main__":
    unittest.main()
