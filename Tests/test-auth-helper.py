#!/usr/bin/env python3
"""Protocol and actual PAM conversation coverage; no account/host mutations."""
import os
from pathlib import Path
import selectors
import subprocess
import sys
import tempfile
import unittest

HELPER, MODULE = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
sys.argv[1:] = []


class Helper(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="auth-protocol-")
        self.root = Path(self.scratch.name)
        self.children = []

    def tearDown(self):
        for child in self.children:
            if child.poll() is None:
                child.kill()
            child.wait(timeout=5)
            child.stdin.close()
            child.stdout.close()
            child.stderr.close()
        self.scratch.cleanup()

    def start(self, mode="success"):
        (self.root / "fixture").write_text(f"auth required {MODULE} {mode}\n")
        p = subprocess.Popen([HELPER, "fixture", "unused-fixture-user", str(self.root)],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.children.append(p)
        return p

    def line(self, p):
        # Unbuffered one-byte reads avoid hiding ready lines in a Python buffer.
        result = b""
        with selectors.DefaultSelector() as selector:
            selector.register(p.stdout, selectors.EVENT_READ)
            while not result.endswith(b"\n"):
                self.assertTrue(selector.select(5), "helper response deadline")
                char = os.read(p.stdout.fileno(), 1)
                self.assertTrue(char, "unexpected helper EOF")
                result += char
        return result.decode().rstrip("\n")

    def response(self, p, text):
        p.stdin.write(b"R " + text.encode().hex().encode() + b"\n")
        p.stdin.flush()

    def prompt(self, p):
        self.assertEqual(self.line(p), "I " + "Touch sensor\nsecond line".encode().hex())
        self.assertEqual(self.line(p), "E " + b"Try again".hex())
        self.assertEqual(self.line(p), "P0 " + b"Password:".hex())

    def test_multi_prompt(self):
        p = self.start()
        self.prompt(p)
        self.response(p, "fixture-answer")
        self.assertEqual(self.line(p), "P1 " + b"Visible challenge:".hex())
        self.response(p, "visible\nanswer")
        self.assertEqual(self.line(p), "P0 " + b"Last challenge:".hex())
        self.response(p, "")
        self.assertEqual(self.line(p), "D success")
        self.assertEqual(p.wait(timeout=5), 0)
        self.assertEqual(p.stderr.read(), b"")

    def test_rejected_response(self):
        p = self.start()
        self.prompt(p)
        self.response(p, "wrong")
        self.line(p)
        self.response(p, "visible\nanswer")
        self.line(p)
        self.response(p, "")
        self.assertEqual(self.line(p), "D failed")
        self.assertEqual(p.wait(timeout=5), 0)

    def test_abort_eof_malformed_oversize(self):
        for answer in (b"A\n", b"R 0g\n", b"R 00\n", b"R " + b"61" * 512 + b"\n", None):
            with self.subTest(answer_kind="eof" if answer is None else len(answer)):
                p = self.start()
                self.prompt(p)
                if answer is None:
                    p.stdin.close()
                else:
                    p.stdin.write(answer)
                    p.stdin.flush()
                self.assertEqual(self.line(p), "D error")
                self.assertEqual(p.wait(timeout=5), 0)

    def test_completion_crash_hang(self):
        for mode in ("max-tries", "error"):
            p = self.start(mode)
            self.assertEqual(self.line(p), "D " + mode)
            self.assertEqual(p.wait(timeout=5), 0)
        p = self.start("crash")
        self.assertEqual(p.wait(timeout=5), 7)
        p = self.start("hang")
        with self.assertRaises(subprocess.TimeoutExpired):
            p.wait(timeout=0.1)
        p.kill()
        self.assertEqual(p.wait(timeout=5), -9)


if __name__ == "__main__":
    unittest.main()
