#!/usr/bin/env python3
"""Faults must not prevent independent fixture retirement or hide the cause."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
import io
from contextlib import redirect_stderr
from unittest.mock import Mock, patch
from urllib.parse import unquote

from settings_fixture_cleanup import Cleanup, access_paths, stop_process
from private_runtime import bus_address, bus_config
from xml.etree.ElementTree import fromstring
import hashlib
import json
import os
import select
import sys
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler
from settings_build import binary_paths, hashes, quick_test_inputs, source_identity, verify_build


class CleanupTest(unittest.TestCase):


    def configure_source(self, root):
        cache = root / "build/debug/aerial-tests/CMakeCache.txt"
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text("AERIAL_LOCK_SOURCE:PATH=" + str(root / "src/aerial-lock") + "\n")
        return cache
    def test_cleanup_diagnostics_without_python311_exception_api(self):
        class LegacyError(RuntimeError):
            add_note = None
        output = io.StringIO(); original = LegacyError("original failure")
        with self.assertRaises(LegacyError) as caught, redirect_stderr(output):
            try:
                raise original
            finally:
                cleanup = Cleanup(); cleanup("fixture", lambda: (_ for _ in ()).throw(RuntimeError("retirement failed")))
                cleanup.finish()
        self.assertIs(caught.exception, original)
        self.assertIn("cleanup failed: fixture: retirement failed", output.getvalue())


    def test_quicktest_rejects_inputs_added_after_the_freeze(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); tests = root / "settings/tests"; tests.mkdir(parents=True)
            tracked = tests / "tst_frozen.qml"; tracked.write_text("import QtQuick\n")
            name = str(tracked.relative_to(root)); files = {name: dict(sha256=hashlib.sha256(tracked.read_bytes()).hexdigest(), mode=tracked.lstat().st_mode)}
            self.assertEqual(quick_test_inputs(root, files), {name: files[name]["sha256"]})
            (tests / "tst_unfrozen.qml").write_text("import QtQuick\n")
            with self.assertRaisesRegex(RuntimeError, "unfrozen settings test input"):
                quick_test_inputs(root, files)



    def test_client_wrapper_rejects_failures_under_optimization(self):
        wrapper = Path(__file__).with_name("test-settings-client.py")
        clean = subprocess.run([sys.executable, "-O", str(wrapper), sys.executable, "-c", "print('clean probe')"], capture_output=True, text=True, timeout=15)
        self.assertEqual(clean.returncode, 0, clean.stdout + clean.stderr); self.assertIn("clean probe", clean.stdout)
        for code, expected in (("raise SystemExit(7)", "probe failed: 7"), ("print('QWARN : injected warning')", "unexpected Qt warning")):
            result = subprocess.run([sys.executable, "-O", str(wrapper), sys.executable, "-c", code], capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(expected, result.stderr)

    def test_builder_rebuilds_changed_sources_with_old_mtimes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); source = root / "src/aerial-lock"; build = root / "build/debug/aerial-tests"
            source.mkdir(parents=True); build.mkdir(parents=True); (root / "run").mkdir(); (root / "tmp").mkdir()
            for directory in ("settings", "companion", "cli", "idle"): (source / directory).mkdir()
            input = source / "input.c"; input.write_text('#include <stdio.h>\nint main(void) { puts("old"); }\n')
            names = ["settings-client-probe", "settings-quick-probe", "settings-visual-probe", "settings-model-probe", "virtual-pointer-probe"]
            (source / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.16)\nproject(provenance C)\n" + ''.join(f'add_executable({name} input.c)\n' for name in names))
            (source / "Makefile").write_text("all: settings/aerial-settings\nsettings/aerial-settings: input.c\n\t$(CC) input.c -o $@\n\tcp $@ companion/aerial\n\tcp $@ cli/aerialctl\n\tcp $@ idle/aerial-idle\nclean:\n\trm -f settings/aerial-settings companion/aerial cli/aerialctl idle/aerial-idle\n")
            subprocess.run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", "-DAERIAL_LOCK_SOURCE=" + str(source)], check=True, capture_output=True)
            subprocess.run(["make"], cwd=source, check=True, capture_output=True)
            subprocess.run(["cmake", "--build", str(build)], check=True, capture_output=True)
            input.write_text('#include <stdio.h>\nint main(void) { puts("new source"); }\n'); os.utime(input, ns=(1, 1))
            files = {str(path.relative_to(source)): dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(), mode=path.lstat().st_mode)
                     for path in (input, source / "Makefile", source / "CMakeLists.txt")}
            freeze = dict(files=files, identity=hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest())
            (root / "run/source-input.json").write_text(json.dumps(freeze))
            result = subprocess.run([sys.executable, str(Path(__file__).with_name("settings-build.py")), str(root)], env=dict(os.environ, TMPDIR=str(root / "tmp")), capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("SETTINGS_BUILD_ATTESTED", result.stdout)
            for binary in [source / "settings/aerial-settings", *(build / name for name in names)]:
                self.assertEqual(subprocess.check_output([str(binary)], text=True).strip(), "new source")
    def test_private_bus_starts_with_percent_in_socket_path(self):
        with tempfile.TemporaryDirectory(prefix="address%&~-") as temporary:
            root = Path(temporary); config = root / "bus.conf"; config.write_text(bus_config(root / "bus"))
            child = subprocess.Popen(["dbus-daemon", "--nofork", "--nopidfile", "--print-address=1", "--config-file=" + str(config)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                self.assertTrue(select.select([child.stdout], [], [], 5)[0], "private bus address deadline")
                address = child.stdout.readline().strip()
                self.assertTrue(address.startswith("unix:"), "private bus did not start: " + str(child.poll()))
                self.assertEqual(unquote(address.split(",")[0]), "unix:path=" + str(root / "bus"))
                result = subprocess.run(["gdbus", "call", "--address", bus_address(root / "bus"), "--dest", "org.freedesktop.DBus", "--object-path", "/org/freedesktop/DBus", "--method", "org.freedesktop.DBus.ListNames"], capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
            finally:
                if child.poll() is None: child.terminate()
                child.communicate(timeout=5)

    def test_executable_ancestors_are_included_in_access_paths(self):
        home = Path.home(); root = home / "private-task"
        probe = root / "build/debug/aerial-tests/settings-units-probe"
        app = root / "src/aerial-lock/settings/aerial-settings"
        paths = access_paths(root / "results/run", "/run/user/1000", "/run/user/1000/wayland-1", probe, app)
        for file in (probe, app):
            for path in [file, *file.parents]:
                if path.is_relative_to(home): self.assertIn(path, paths)

    def test_build_source_symlinks_use_the_shared_inventory_digest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); source = root / "src/aerial-lock"; source.mkdir(parents=True)
            target = source / "target"; target.write_bytes(b"content not link text")
            link = source / "link"; link.symlink_to("target")
            files = {"link": dict(sha256=hashlib.sha256(os.fsencode(os.readlink(link))).hexdigest(), mode=link.lstat().st_mode)}
            freeze = dict(files=files, identity=hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest())
            self.assertEqual(source_identity(root, freeze), freeze["identity"])

    def test_private_bus_preserves_special_paths(self):
        tree = fromstring(bus_config("/home/tester/lab&test/bus", ["/home/tester/a<b"]))
        self.assertEqual(tree.findtext("listen"), "unix:path=/home/tester/lab%26test/bus")
        self.assertEqual(tree.findtext("servicedir"), "/home/tester/a<b")

    def test_replacing_the_models_peer_invalidates_the_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); source = root / "src/aerial-lock"; source.mkdir(parents=True); (root / "run").mkdir()
            file = source / "input"; file.write_text("frozen source")
            files = {"input": dict(sha256=hashlib.sha256(file.read_bytes()).hexdigest(), mode=file.lstat().st_mode)}
            freeze = dict(files=files, identity=hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest())
            outputs = binary_paths(root); peer = root / "build/debug/aerial-tests/settings-client-probe"
            self.assertIn(peer, outputs)
            for path in outputs: path.parent.mkdir(parents=True, exist_ok=True); path.write_text("verified output")
            (root / "run/settings-build.json").write_text(json.dumps(dict(source_identity=source_identity(root, freeze), binary_hashes=hashes(outputs))))
            self.configure_source(root)
            verify_build(root, freeze, outputs); peer.write_text("replaced peer")
            with self.assertRaisesRegex(RuntimeError, "build provenance"): verify_build(root, freeze, outputs)

    def test_build_provenance_rejects_refreshed_source_and_stale_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); source = root / "src/aerial-lock"; source.mkdir(parents=True)
            (root / "run").mkdir(); file = source / "input"; file.write_bytes(b"old input")
            output = root / "app"; output.write_bytes(b"built old input")
            def freeze():
                files = {"input": dict(sha256=hashlib.sha256(file.read_bytes()).hexdigest(), mode=file.lstat().st_mode)}
                return dict(files=files, identity=hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest())
            old = freeze(); receipt = root / "run/settings-build.json"
            receipt.write_text(json.dumps(dict(source_identity=source_identity(root, old), binary_hashes=hashes([output]))))
            cache = self.configure_source(root)
            verify_build(root, old, [output])
            cache.write_text("AERIAL_LOCK_SOURCE:PATH=" + str(root / "foreign-source") + "\n")
            with self.assertRaisesRegex(RuntimeError, "frozen source"): verify_build(root, old, [output])
            self.configure_source(root)
            file.write_bytes(b"new input")
            with self.assertRaisesRegex(RuntimeError, "source changed"):
                verify_build(root, old, [output])
            with self.assertRaisesRegex(RuntimeError, "build provenance"):
                verify_build(root, freeze(), [output])
            file.write_bytes(b"old input"); output.write_bytes(b"replaced binary")
            with self.assertRaisesRegex(RuntimeError, "build provenance"):
                verify_build(root, old, [output])

    def test_launcher_timeout_still_retires_account_and_acls(self):
        child = Mock()
        child.poll.return_value = None
        child.wait.side_effect = subprocess.TimeoutExpired("launcher", 10)
        cleanup, steps = Cleanup(), []
        cleanup("launcher", lambda: stop_process(child))
        for label in ("linger", "terminate-user", "acl", "userdel", "log-close"):
            cleanup(label, lambda label=label: steps.append(label))
        self.assertEqual(steps, ["linger", "terminate-user", "acl", "userdel", "log-close"])
        child.kill.assert_called_once()
        self.assertEqual(child.wait.call_count, 2)
        with self.assertRaisesRegex(RuntimeError, "launcher"):
            cleanup.finish()

    def test_original_failure_kept(self):
        cleanup = Cleanup()
        original = ValueError("test failed")
        try:
            raise original
        except ValueError:
            cleanup("compositor", lambda: (_ for _ in ()).throw(RuntimeError("niri unavailable")))
            cleanup.finish()
        self.assertIn("niri unavailable", original.__notes__[0])

    def test_every_private_output_ancestor_is_included(self):
        home = Path.home()
        output = home / "workspaces/task/results/private/run"
        paths = access_paths(output, "/run/user/1000", "/run/user/1000/wayland-1")
        self.assertEqual(paths[:6], [home, home / "workspaces", home / "workspaces/task", home / "workspaces/task/results",
                                     home / "workspaces/task/results/private", output])
        self.assertEqual(len(paths), len(set(paths)))




if __name__ == "__main__":
    unittest.main()
