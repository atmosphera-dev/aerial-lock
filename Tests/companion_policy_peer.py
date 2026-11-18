"""Private read-only policy observations for installed executable VM tests."""
import json
import os
from pathlib import Path
import select
import subprocess


class PolicyBus:
    def __init__(self, output, probe, uuid):
        self.output = Path(output)
        self.address = "unix:abstract=aerial-policy-" + uuid + "-" + str(os.getpid())
        config = self.output / "policy-bus.conf"
        config.write_text('<busconfig><type>session</type><auth>EXTERNAL</auth><listen>' + self.address
                          + '</listen><policy context="default"><allow user="*"/>'
                          '<allow send_destination="*"/><allow receive_sender="*"/><allow own="*"/>'
                          '</policy></busconfig>')
        self.log = (self.output / "policy-peer.log").open("w")
        self.bus = self.peer = None
        try:
            self.bus = subprocess.Popen(["dbus-daemon", "--config-file=" + str(config), "--nofork", "--nopidfile", "--print-address=1"],
                                        stdout=subprocess.PIPE, stderr=self.log)
            assert select.select([self.bus.stdout], [], [], 5)[0]
            address = self.bus.stdout.readline().decode().strip()
            assert address.startswith(self.address)
            self.peer = subprocess.Popen([str(probe), "--policy-peer"], env=dict(os.environ, DBUS_SESSION_BUS_ADDRESS=address),
                                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log)
            assert select.select([self.peer.stdout], [], [], 5)[0] and self.peer.stdout.readline().strip() == b"POLICY_PEER_READY"
        except BaseException:
            self.close()
            raise

    def observe(self, patch):
        self.peer.stdin.write((json.dumps(patch) + "\n").encode())
        self.peer.stdin.flush()
        assert select.select([self.peer.stdout], [], [], 5)[0] and self.peer.stdout.readline().strip() == b"POLICY_PEER_UPDATED"

    def close(self):
        if self.peer:
            self.peer.stdin.close()
            self.peer.wait(timeout=10)
        if self.bus:
            self.bus.terminate()
            self.bus.wait(timeout=10)
        self.log.close()
