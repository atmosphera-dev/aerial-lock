#!/usr/bin/env python3
"""Real loopback TLS through the production HTTP transaction, not fake replies."""
import http.server
import json
import os
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading


def run(argv, **kwargs):
    return subprocess.run(argv, check=True, capture_output=True, text=True, timeout=20, **kwargs)


with tempfile.TemporaryDirectory(prefix="companion-tls-") as directory:
    root = Path(directory)

    def certificate(name, issuer=None):
        key, csr, cert = (root / (name + suffix) for suffix in (".key", ".csr", ".pem"))
        run(["openssl", "req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=" + name,
             "-keyout", str(key), "-out", str(csr)])
        if issuer is None:
            run(["openssl", "x509", "-req", "-in", str(csr), "-signkey", str(key), "-days", "2",
                 "-out", str(cert), "-extfile", str(root / "root.ext")])
        else:
            run(["openssl", "x509", "-req", "-in", str(csr), "-CA", str(root / (issuer + ".pem")),
                 "-CAkey", str(root / (issuer + ".key")), "-set_serial", "7", "-days", "2",
                 "-extfile", str(root / ("wrong.ext" if name == "wrong" else "leaf.ext")), "-out", str(cert)])
        return cert, key

    (root / "root.ext").write_text("basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n")
    (root / "leaf.ext").write_text("basicConstraints=critical,CA:FALSE\nsubjectAltName=DNS:sylvan.apple.com,DNS:custom.test\nextendedKeyUsage=serverAuth\n")
    (root / "wrong.ext").write_text("basicConstraints=critical,CA:FALSE\nsubjectAltName=DNS:wrong.test\nextendedKeyUsage=serverAuth\n")
    certificate("normal-root")
    certificate("fallback-root")
    normal = certificate("normal", "normal-root")
    fallback = certificate("fallback", "fallback-root")
    wrong = certificate("wrong", "fallback-root")

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.server.requests.append({"host": self.headers["Host"], "path": self.path})
            destination = self.server.redirects.get(self.path)
            self.send_response(302 if destination else 200)
            if destination:
                self.send_header("Location", destination)
            self.send_header("Content-Length", "0" if destination else "8")
            self.end_headers()
            if not destination:
                self.wfile.write(b"verified")

        def log_message(self, *unused):
            pass

    def case(name, pair, url, expected, hosts, redirects=None, fallback_attempts=()):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(*map(str, pair))
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        server.requests, server.redirects = [], redirects or {}
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            config = root / "input.json"
            config.write_text(json.dumps({"normalRoot": str(root / "normal-root.pem"),
                                         "fallbackRoot": str(root / "fallback-root.pem"),
                                         "port": server.server_port, "url": url}))
            reply = run([sys.argv[1], str(config)], env=dict(os.environ, QT_LOGGING_RULES="qt.network.ssl.warning=false"))
            value = json.loads(reply.stdout)
            assert value["error"] == expected, (name, value, reply.stderr)
            assert value["defaultTrustUnchanged"], (name, value)
            assert [row["host"] for row in value["requests"]] == hosts, (name, value)
            counts = [row["caCount"] for row in value["requests"]]
            assert all(count == counts[0] + (1 if index in fallback_attempts else 0)
                       for index, count in enumerate(counts)), (name, counts)
            if not expected:
                assert value["body"] == "verified" and value["status"] == 200, (name, value)
            else:
                assert not value["body"], (name, value)
            print("COMPANION_HTTP", name, json.dumps(value), "wire", json.dumps(server.requests), flush=True)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)
            assert not thread.is_alive()

    case("normal-system-trust", normal, "https://sylvan.apple.com/clip", "", ["sylvan.apple.com"])
    case("apple-fallback", fallback, "https://sylvan.apple.com/clip", "", ["sylvan.apple.com", "sylvan.apple.com"], fallback_attempts=(1,))
    case("custom-no-fallback", fallback, "https://custom.test/clip", "TlsError", ["custom.test"])
    case("hostname-still-verified", wrong, "https://sylvan.apple.com/clip", "TlsError", ["sylvan.apple.com", "sylvan.apple.com"], fallback_attempts=(1,))
    case("redirect-no-trust-leak", fallback, "https://sylvan.apple.com/start", "TlsError",
         ["sylvan.apple.com", "sylvan.apple.com", "custom.test"], {"/start": "https://custom.test/clip"}, (1,))
    case("redirect-normal-trust", normal, "https://sylvan.apple.com/start", "",
         ["sylvan.apple.com", "custom.test"], {"/start": "https://custom.test/clip"})
    case("https-downgrade", fallback, "https://sylvan.apple.com/start", "UnsafeRedirect",
         ["sylvan.apple.com", "sylvan.apple.com"], {"/start": "http://custom.test/clip"}, (1,))
    case("same-host-redirect-revalidates", fallback, "https://sylvan.apple.com/start", "",
         ["sylvan.apple.com"] * 4, {"/start": "/clip"}, (1, 3))
    case("redirect-bound", normal, "https://custom.test/loop", "UnsafeRedirect",
         ["custom.test"] * 11, {"/loop": "/loop"})
