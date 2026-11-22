#!/usr/bin/env python3
"""Owned QEMU/KVM image builder and group transport. Never run on a desktop host."""
import argparse
import base64
import fnmatch
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import secrets
import shlex
import shutil
import socket
import subprocess
import sys
import tarfile
import threading
import time
import urllib.request
import uuid
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
BOOT_MARKER = 'AERIAL_CI_READY '
PROVISION_OBSERVER = 'AERIAL_PROVISION_OBSERVER\n'
DOWNLOAD_ATTEMPTS = 4


class DiskMeter:
    """Observe available filesystem storage during the whole image build."""
    def __init__(self, path):
        self.path = Path(path)
        while not self.path.exists():
            self.path = self.path.parent
        self.initial = self.minimum = shutil.disk_usage(self.path).free
        self.samples = 1
        self.done = threading.Event()

    def sample(self):
        self.minimum = min(self.minimum, shutil.disk_usage(self.path).free)
        self.samples += 1

    def watch(self):
        while not self.done.wait(1):
            self.sample()

    def __enter__(self):
        self.worker = threading.Thread(target=self.watch, daemon=True)
        self.worker.start()
        return self

    def __exit__(self, *_):
        self.done.set()
        self.worker.join()
        self.sample()

    def result(self):
        return dict(initial_free_bytes=self.initial, minimum_free_bytes=self.minimum,
                    sampled_peak_growth_bytes=self.initial - self.minimum,
                    sample_interval_seconds=1, samples=self.samples)


def serial_identity(text, expected_uuid, expected_mac):
    for line in text.splitlines(keepends=True):
        if BOOT_MARKER not in line or not line.endswith('\n'):
            continue
        value = json.loads(line.split(BOOT_MARKER, 1)[1])
        if value['uuid'] != expected_uuid or expected_mac not in value['macs']:
            raise RuntimeError('serial guest UUID/MAC mismatch')
        if not value.get('key', '').startswith('ssh-ed25519 ') or not value.get('boot_id') or not value.get('machine_id'):
            raise RuntimeError('incomplete serial identity')
        return value
    return None


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def write_json(path, value):
    path = Path(path)
    pending = path.with_name('.' + path.name + '.pending')
    with pending.open('x') as stream:
        os.chmod(pending, 0o600)
        json.dump(value, stream, indent=2)
        stream.write('\n')
    pending.replace(path)


def command(argv, timeout=60, **kwargs):
    return subprocess.run([str(value) for value in argv], check=True, timeout=timeout, **kwargs)


def failure_details(error):
    value = dict(error=type(error).__name__, reason=str(error))
    if isinstance(error, subprocess.CalledProcessError):
        value['returncode'] = error.returncode
    stderr = getattr(error, 'stderr', None)
    if stderr:
        value['stderr'] = stderr.decode(errors='replace') if isinstance(stderr, bytes) else stderr
    return value


def parse_provision_status(text):
    if text == 'running\n':
        return None
    if re.fullmatch(r'(?:0|[1-9][0-9]{0,2})\n', text):
        status = int(text)
        if status <= 255:
            return status
    raise RuntimeError('invalid provisioning status: ' + repr(text))


def pre_auth_transport_loss(error, port):
    """Only exact pre-auth diagnostics with no remote output are retryable."""
    if error.returncode != 255 or error.stdout not in ('', b'', None):
        return False
    reset = ('kex_exchange_identification: read: Connection reset by peer\n'
             f'Connection reset by 127.0.0.1 port {port}\n')
    refused = f'ssh: connect to host 127.0.0.1 port {port}: Connection refused\n'
    return error.stderr in (reset, refused)


def wait_for_provision(vm, seconds, output):
    # One absolute budget covers running, unavailable, SSH and poll delays.
    deadline = time.monotonic() + seconds
    observation = dict(polls=0, transport_retries=0, transport_recoveries=0,
                       outcome='failed')
    unavailable = False
    try:
        while (remaining := deadline - time.monotonic()) > 0:
            if vm.process.poll() is not None:
                raise RuntimeError('QEMU exited during image provisioning')
            observation['polls'] += 1
            try:
                state = vm.provision_status(timeout=min(30, remaining))
            except subprocess.CalledProcessError as error:
                if not pre_auth_transport_loss(error, vm.port):
                    raise
                unavailable = True
                observation['transport_retries'] += 1
                print('PROVISION_TRANSPORT_UNAVAILABLE ' + json.dumps(failure_details(error)), flush=True)
            else:
                if unavailable:
                    unavailable = False
                    observation['transport_recoveries'] += 1
                    print('PROVISION_TRANSPORT_RECOVERED', flush=True)
                if time.monotonic() >= deadline:
                    break
                if state is not None:
                    if state != 0:
                        raise RuntimeError('image provisioning failed with exit ' + str(state))
                    observation['outcome'] = 'complete'
                    return
            time.sleep(min(5, max(0, deadline - time.monotonic())))
        observation['outcome'] = 'deadline'
        raise TimeoutError('image provisioning deadline exceeded')
    finally:
        print('PROVISION_OBSERVATION ' + json.dumps(observation), flush=True)
        primary = sys.exc_info()[1]
        try:
            write_json(Path(output) / 'provision-observation.json', observation)
        except Exception as error:
            if primary is None:
                raise
            print('PROVISION_OBSERVATION_ERROR ' + str(error), file=sys.stderr)


def export_vm_diagnostics(vm, output):
    # Private seeds/keys/disks are deliberately not part of this allowlist.
    for name in ('serial.log', 'qemu.log', 'ssh-errors.log', 'instance.json'):
        source = vm.directory / name
        if source.is_file() and not source.is_symlink():
            shutil.copyfile(source, Path(output) / name)


def config(path):
    path = Path(path).resolve(strict=True)
    value = json.loads(path.read_text())
    for key in ('instance', 'host', 'user', 'storage', 'guest_root'):
        if not value.get(key):
            raise ValueError('missing configuration: ' + key)
    for key in ('instance', 'user'):
        if not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9._-]*', value[key]):
            raise ValueError('invalid configuration: ' + key)
    storage = Path(value['storage']).expanduser()
    if not storage.is_absolute():
        storage = path.parent / storage
    if storage.resolve() != storage.absolute() or '..' in storage.parts:
        raise ValueError('storage must not contain symlinks or traversal')
    value['storage'] = str(storage)
    for key in ('ssh_key', 'firmware'):
        if value.get(key):
            selected = Path(value[key]).expanduser()
            value[key] = str((selected if selected.is_absolute() else path.parent / selected).resolve())
    if value.get('qemu') and '/' in value['qemu']:
        selected = Path(value['qemu']).expanduser()
        value['qemu'] = str((selected if selected.is_absolute() else path.parent / selected).resolve())
    guest_root = Path(value['guest_root'])
    if not guest_root.is_absolute() or '..' in guest_root.parts:
        raise ValueError('absolute guest_root without traversal required')
    value.setdefault('cpus', 2)
    value.setdefault('memory_mib', 4096)
    value.setdefault('boot_seconds', 300)
    value.setdefault('build_seconds', 3600)
    value.setdefault('group_seconds', 600)
    value.setdefault('export_seconds', 60)
    value.setdefault('ssh_port', 0)
    value.setdefault('qemu', 'qemu-system-x86_64')
    for key in ('cpus', 'memory_mib', 'boot_seconds', 'build_seconds', 'group_seconds', 'export_seconds'):
        if not isinstance(value[key], int) or value[key] < 1:
            raise ValueError('positive integer required: ' + key)
    if not isinstance(value['ssh_port'], int) or not 0 <= value['ssh_port'] <= 65535:
        raise ValueError('invalid SSH port')
    return value


def validate_lock(path, resolving=False):
    lock = json.loads(Path(path).read_text())
    if lock['schema'] != 1 or not re.fullmatch(r'\d{4}/\d{2}/\d{2}', lock['snapshot']):
        raise ValueError('invalid image lock')
    for item in (lock['cloud_image'], lock['wtype']):
        if not item['url'].startswith('https://') or not re.fullmatch('[a-f0-9]{64}', item['sha256']):
            raise ValueError('HTTPS input and SHA256 pin required')
    if not resolving and (not lock['packages'] or not lock['repositories']):
        raise ValueError('unresolved lock; generate a candidate with --resolve-lock first')
    for name, item in lock['aur'].items():
        if digest(HERE / 'aur' / name / 'PKGBUILD') != item['recipe_sha256']:
            raise ValueError('recipe hash mismatch: ' + name)
    return lock


def download(url, destination, expected, attempts=DOWNLOAD_ATTEMPTS):
    """Fetch a pinned input, retrying failed transfers; only the pinned digest is accepted."""
    destination = Path(destination)
    if not destination.exists():
        pending = destination.with_suffix('.download')
        for attempt in range(1, attempts + 1):
            pending.unlink(missing_ok=True)
            try:
                with urllib.request.urlopen(url, timeout=60) as response, pending.open('xb') as output:
                    expected_size = response.headers.get('Content-Length')
                    received = 0
                    while chunk := response.read(1024 * 1024):
                        output.write(chunk)
                        received += len(chunk)
                    if expected_size is not None and received != int(expected_size):
                        raise OSError(f'incomplete download: received {received} of {expected_size} bytes')
                break
            except (OSError, http.client.HTTPException) as error:
                if attempt == attempts:
                    raise
                print(f'DOWNLOAD_RETRY {attempt}/{attempts} {url}: {error}', file=sys.stderr, flush=True)
                time.sleep(5 * attempt)
        pending.replace(destination)
    if digest(destination) != expected:
        raise ValueError('download checksum mismatch: ' + str(destination))


def response(connection, deadline, nonce=None, greeting=False):
    """Discard stale QGA bytes only before its matching sync response.

    The 0xff sentinel resets parser state even when split across recv calls.
    After synchronization, malformed/error responses are actual command errors.
    One absolute deadline includes every stale response, not just idle reads.
    """
    pending = b''
    while time.monotonic() < deadline:
        connection.settimeout(max(0.001, deadline - time.monotonic()))
        chunk = connection.recv(65536)
        if not chunk:
            raise ConnectionError('QEMU channel closed')
        pending += chunk
        if b'\xff' in pending:
            pending = pending.rsplit(b'\xff', 1)[-1]
        while b'\n' in pending:
            line, pending = pending.split(b'\n', 1)
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except (UnicodeDecodeError, json.JSONDecodeError):
                if nonce is not None:
                    continue
                raise RuntimeError('malformed synchronized QEMU response') from None
            if nonce is not None:
                if value.get('return') == nonce:
                    return nonce
                continue
            if greeting and 'QMP' in value:
                return value
            if 'error' in value:
                raise RuntimeError('QEMU command failed: ' + str(value['error']))
            if 'return' in value:
                return value['return']
    raise TimeoutError('QEMU response deadline exceeded')


def rpc(path, name, arguments=None, qmp=False):
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(10)
        connection.connect(str(path))
        deadline = time.monotonic() + 10
        if qmp:
            response(connection, deadline, greeting=True)
            connection.sendall(b'{"execute":"qmp_capabilities"}\n')
            response(connection, deadline)
        else:
            nonce = secrets.randbits(52)
            connection.sendall(b'\xff' + json.dumps(dict(execute='guest-sync-delimited', arguments=dict(id=nonce))).encode() + b'\n')
            response(connection, deadline, nonce=nonce)
        payload = dict(execute=name)
        if arguments is not None:
            payload['arguments'] = arguments
        connection.sendall(json.dumps(payload).encode() + b'\n')
        return response(connection, deadline)


class VM:
    def __init__(self, cfg, image, directory, files=None, online=False):
        self.cfg = cfg
        self.directory = Path(directory)
        self.directory.mkdir(mode=0o700)  # refuse reuse of existing instances
        if not os.access('/dev/kvm', os.R_OK | os.W_OK):
            raise RuntimeError('KVM unavailable; TCG fallback is not supported')
        self.uuid = str(uuid.uuid4())
        self.mac = '52:54:00:' + ':'.join(f'{byte:02x}' for byte in secrets.token_bytes(3))
        self.name = cfg['instance']
        self.qga = self.directory / 'qga.sock'
        self.qmp = self.directory / 'qmp.sock'
        if len(str(self.qga).encode()) >= 104:
            raise ValueError('QEMU socket path too long; choose a shorter storage path')
        self.key = Path(cfg.get('ssh_key', self.directory / 'client-key')).expanduser().resolve()
        if 'ssh_key' not in cfg:
            command(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', self.key])
        if not self.key.is_file() or self.key.stat().st_mode & 0o077:
            raise ValueError('SSH private key must exist with private permissions')
        public = Path(str(self.key) + '.pub').read_text().strip()
        port = cfg['ssh_port']
        if not port:
            with socket.socket() as listener:
                listener.bind(('127.0.0.1', 0))
                port = listener.getsockname()[1]
        self.port = port
        seed = dict(users=[dict(name=cfg['user'], groups='wheel', shell='/bin/bash',
            sudo='ALL=(ALL) NOPASSWD:ALL', lock_passwd=True, ssh_authorized_keys=[public])],
            ssh_pwauth=False, disable_root=True, growpart=dict(mode='auto', devices=['/']), resize_rootfs=True,
            write_files=[], bootcmd=[['mkdir', '-p', '/var/lib/aerial-ci/tmp']])
        for path, contents in (files or {}).items():
            seed['write_files'].append(dict(path=path, encoding='b64', permissions='0644',
                content=base64.b64encode(contents).decode()))
        if not seed['write_files']:
            del seed['write_files']  # cloud-init's schema requires a nonempty list
        # The preparation guest receives all provisioning code in its private seed.
        # Final-image boots retain only generic guest prerequisites and per-run keys.
        # runcmd follows cloud-init's account and SSH-key modules. This marker
        # travels through our private serial file, not an unauthenticated network.
        bootstrap = (
            'import json,pathlib,pwd; P=pathlib.Path; a=pwd.getpwnam(' + repr(cfg['user']) + '); '
            'value=dict(uuid=P("/sys/class/dmi/id/product_uuid").read_text().strip().lower(),'
            'macs=[p.read_text().strip() for p in P("/sys/class/net").glob("*/address")],'
            'boot_id=P("/proc/sys/kernel/random/boot_id").read_text().strip(),'
            'machine_id=P("/etc/machine-id").read_text().strip(),'
            'key=P("/etc/ssh/ssh_host_ed25519_key.pub").read_text().strip(),'
            'user=a.pw_name,uid=a.pw_uid,home=a.pw_dir); '
            'P("/dev/ttyS0").write_text("\\nAERIAL_CI_READY "+json.dumps(value)+"\\n")')
        seed['runcmd'] = [['env', 'TMPDIR=/var/lib/aerial-ci/tmp', 'python3', '-c', bootstrap]]
        if files:
            seed['runcmd'].append(['env', 'TMPDIR=/var/lib/aerial-ci/tmp', 'bash', '/var/lib/aerial-ci/provision/provision.sh'])
        (self.directory / 'user-data').write_text('#cloud-config\n' + json.dumps(seed))
        (self.directory / 'meta-data').write_text(json.dumps({'instance-id': self.uuid, 'local-hostname': self.name}))
        command(['cloud-localds', self.directory / 'seed.iso', self.directory / 'user-data', self.directory / 'meta-data'])
        disk = self.directory / 'disk.qcow2'
        command(['qemu-img', 'create', '-f', 'qcow2', '-F', 'qcow2', '-b', Path(image).resolve(), disk])
        command(['qemu-img', 'resize', disk, '8G'])
        network = f'user,id=net0,hostfwd=tcp:127.0.0.1:{port}-:22'
        if not online:
            network += ',restrict=on'
        argv = [cfg['qemu'], '-name', self.name, '-uuid', self.uuid, '-machine', 'q35',
            '-accel', 'kvm', '-cpu', 'host', '-smp', str(cfg['cpus']), '-m', str(cfg['memory_mib']),
            '-display', 'none', '-vga', 'none', '-monitor', 'none',
            '-serial', f'file:{self.directory / "serial.log"}',
            '-qmp', f'unix:{self.qmp},server=on,wait=off',
            '-device', 'virtio-serial', '-chardev', f'socket,path={self.qga},server=on,wait=off,id=qga0',
            '-device', 'virtserialport,chardev=qga0,name=org.qemu.guest_agent.0',
            '-drive', f'file={disk},format=qcow2,if=virtio,discard=unmap',
            '-drive', f'file={self.directory / "seed.iso"},format=raw,media=cdrom,readonly=on',
            '-netdev', network, '-device', f'virtio-net-pci,netdev=net0,mac={self.mac}', '-no-reboot']
        if cfg.get('firmware'):
            argv += ['-bios', cfg['firmware']]
        self.receipt = dict(schema=1, domain=self.name, uuid=self.uuid, mac=self.mac, host=cfg['host'],
            argv=argv, directory=str(self.directory), ssh_port=port, phase='starting')
        self.log = (self.directory / 'qemu.log').open('wb')
        self.started = time.monotonic()
        try:
            self.process = subprocess.Popen(argv, stdout=self.log, stderr=subprocess.STDOUT)
            self.receipt['pid'] = self.process.pid
            self.save()
        except BaseException:
            # Ownership cannot escape the constructor until its receipt exists.
            # Do not use stop(): it writes the same failed receipt filesystem.
            if hasattr(self, 'process'):
                try:
                    try:
                        self.process.kill()
                    finally:
                        self.process.wait(timeout=5)
                except Exception as error:
                    print('INITIAL_VM_DISPOSAL_ERROR ' + str(error), file=sys.stderr)
            self.log.close()
            raise

    def save(self):
        write_json(self.directory / 'instance.json', self.receipt)

    def execute(self, argv, timeout=30):
        return self.ssh(['sudo', '-n', 'env', 'TMPDIR=/var/lib/aerial-ci/tmp', *argv],
                        timeout=timeout, capture_output=True, text=True).stdout

    def provision_status(self, timeout=30):
        # Python and sudo/env may be replaced by the transaction being watched.
        # The status/log files are readable by this unprivileged SSH account.
        text = self.ssh(['/bin/sh', '-c',
            'export TMPDIR=/var/lib/aerial-ci/tmp; '
            'printf "AERIAL_PROVISION_OBSERVER\\n"; '
            'if test -f "$1"; then exec /usr/bin/cat -- "$1"; else printf "running\\n"; fi',
            'aerial-provision-status', '/var/lib/aerial-ci/provision.exit'],
            timeout=timeout, capture_output=True, text=True).stdout
        if not text.startswith(PROVISION_OBSERVER):
            raise RuntimeError('missing provisioning observer marker')
        return parse_provision_status(text[len(PROVISION_OBSERVER):])

    def provision_log(self):
        return self.ssh(['/bin/sh', '-c',
            'export TMPDIR=/var/lib/aerial-ci/tmp; exec /usr/bin/cat -- "$1"',
            'aerial-provision-log', '/var/lib/aerial-ci/provision.log'],
            capture_output=True, text=True).stdout

    def wait_ready(self):
        deadline = time.monotonic() + self.cfg['boot_seconds']
        last = None
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError('QEMU exited before boot; inspect serial.log and qemu.log')
            try:
                acceleration = rpc(self.qmp, 'query-kvm', qmp=True)
                if not acceleration.get('enabled'):
                    raise RuntimeError('QEMU KVM is not enabled')
                serial = self.directory / 'serial.log'
                identity = serial_identity(serial.read_text(errors='replace') if serial.exists() else '', self.uuid, self.mac)
                if identity is not None:
                    break
            except (OSError, ConnectionError, TimeoutError) as error:
                last = str(error)
            time.sleep(1)
        else:
            raise TimeoutError('cloud-init serial readiness deadline: ' + str(last))
        if not identity['key'].startswith('ssh-ed25519 '):
            raise RuntimeError('expected ed25519 host key from owned serial channel')
        self.known_hosts = self.directory / 'known_hosts'
        self.known_hosts.write_text(self.name + ' ' + identity['key'] + '\n')
        self.known_hosts.chmod(0o600)
        if identity['user'] != self.cfg['user']:
            raise RuntimeError('serial guest account mismatch')
        self.receipt.update(phase='serial-ready', guest=identity,
                            serial_ready_seconds=time.monotonic() - self.started)
        self.save()
        return identity

    def ssh_args(self):
        return ['ssh', '-F', '/dev/null', '-i', str(self.key), '-p', str(self.port),
            '-o', 'IdentityAgent=none', '-o', 'IdentitiesOnly=yes', '-o', 'BatchMode=yes',
            '-o', 'StrictHostKeyChecking=yes', '-o', 'HostKeyAlgorithms=ssh-ed25519',
            '-o', 'HostKeyAlias=' + self.name, '-o', 'UserKnownHostsFile=' + str(self.known_hosts),
            '-o', 'GlobalKnownHostsFile=/dev/null', '-o', 'UpdateHostKeys=no',
            '-o', 'ControlMaster=no', '-o', 'ControlPath=none', '-o', 'ProxyCommand=none',
            '-o', 'ProxyJump=none', '-o', 'ConnectTimeout=10', self.cfg['user'] + '@127.0.0.1']

    def ssh(self, argv, timeout=30, **kwargs):
        if not kwargs.get('capture_output') and 'stderr' not in kwargs:
            kwargs['stderr'] = subprocess.PIPE
        try:
            return command(self.ssh_args() + [shlex.join(argv)], timeout, **kwargs)
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            details = failure_details(error)
            print('SSH_FAILURE ' + json.dumps(details), file=sys.stderr)
            try:
                with (self.directory / 'ssh-errors.log').open('a') as log:
                    log.write(json.dumps(details) + '\n')
            except OSError as diagnostic_error:
                print('SSH_DIAGNOSTIC_ERROR ' + str(diagnostic_error), file=sys.stderr)
            raise

    def stop(self, force=False):
        if self.process.poll() is not None:
            self.receipt.update(phase='stopped', qemu_exit=self.process.returncode)
            self.save()
            self.log.close()
            if self.process.returncode != 0 and not force:
                raise RuntimeError('QEMU stopped abnormally: ' + str(self.process.returncode))
            return
        try:
            # Poweroff is asynchronous. Do not wait for a guest-exec-status
            # reply from an agent which successful shutdown necessarily closes.
            rpc(self.qmp, 'system_powerdown', qmp=True)
            result = self.process.wait(timeout=120)
            if result != 0:
                raise RuntimeError('QEMU exited abnormally during shutdown: ' + str(result))
        except (RuntimeError, OSError, TimeoutError, subprocess.TimeoutExpired):
            if not force:
                raise
            # Only this directly owned QEMU process. CI failed-guest disposal is
            # explicitly authorized; this never signals a desktop qs process.
            self.receipt['forced_shutdown'] = self.process.poll() is None
            self.process.terminate()
            try:
                self.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.receipt.update(phase='stopped', qemu_exit=self.process.returncode)
        self.save()
        self.log.close()


def source_files(source):
    source = Path(source).resolve(strict=True)
    if (source / '.git').exists():
        names = command(['git', '-C', source, 'ls-files', '-z', '--cached', '--others', '--exclude-standard'],
                        capture_output=True).stdout.decode().split('\0')
    else:
        # Archived checkouts retain .gitignore, not Git metadata. Honor its
        # simple repository patterns as well as the transport exclusions.
        patterns = [line.strip().strip('/') for line in (source / '.gitignore').read_text().splitlines()
                    if line.strip() and not line.startswith('#')]
        names = [str(path.relative_to(source)) for path in source.rglob('*') if path.is_file() or path.is_symlink()]
        names = [name for name in names if not any(fnmatch.fnmatch(name, pattern) or
                    any(fnmatch.fnmatch(part, pattern) for part in Path(name).parts) for pattern in patterns)]
    result = {}
    for name in sorted(set(names)):
        parts = Path(name).parts
        if not name or any(part in ('.git', '.envrc', '.ci-vm', '__pycache__') for part in parts) \
                or any(part in ('build', 'build-debug', 'build-release') for part in parts[:-1]):
            continue
        path = source / name
        if not path.resolve().is_relative_to(source):
            raise ValueError('source symlink escape: ' + name)
        if not path.is_file() and not path.is_symlink():
            raise ValueError('source entry is not a file: ' + name)
        result[name] = dict(sha256=hashlib.sha256(os.fsencode(os.readlink(path))).hexdigest()
                           if path.is_symlink() else digest(path), mode=path.lstat().st_mode)
    if not result:
        raise ValueError('empty source snapshot')
    return result


def validate_result_identity(summary, request):
    for key in ('instance_uuid', 'image_sha256', 'source_identity'):
        expected = dict(instance_uuid=request['uuid'], image_sha256=request['image_sha256'],
                        source_identity=request['source_freeze_identity'])[key]
        if summary.get(key) != expected:
            raise RuntimeError('result identity mismatch: ' + key)


def validate_summary(summary, request):
    validate_result_identity(summary, request)
    group = request.get('group', 'niri-smoke')
    if group not in ('niri-smoke', 'niri-emergency'):
        import acceptance
        acceptance.validate_group(summary, request)
        return
    if group == 'niri-emergency':
        expected = {'missing-runner', 'main-crash'}
        outcomes = summary.get('outcomes', [])
        if summary.get('status') != 'PASS' or summary.get('group') != group \
                or summary.get('planned') != len(expected) or summary.get('exercised') != len(expected) \
                or summary.get('no_surviving_locker') is not True or len(outcomes) != len(expected) \
                or {o.get('case') for o in outcomes} != expected:
            raise RuntimeError('required emergency case set absent or mixed')
        for outcome in outcomes:
            if outcome.get('status') != 'PASS' or outcome.get('backend') != 'niri' \
                    or outcome.get('instance_uuid') != request['uuid'] \
                    or outcome.get('source_identity') != request['source_freeze_identity'] \
                    or outcome.get('exit') != 0 or not all(outcome.get(k) is True for k in
                        ('real_pam', 'rejected', 'unlock_and_destroy', 'sync_confirmed', 'desktop_input', 'no_lock_time_desktop_input')) \
                    or (outcome['case'] == 'main-crash' and len(set(outcome.get('main_pids', []))) != 3):
                raise RuntimeError('emergency consumer outcome missing/stale')
            if outcome['case'] == 'main-crash':
                frames = outcome.get('sampled_frames') or {}
                if not isinstance(frames.get('samples'), int) or frames['samples'] < 1 or frames.get('error') is not None \
                        or frames.get('samples_with_desktop_marker') != 0:
                    raise RuntimeError('real recovery frame observations absent')
        return
    if group != 'niri-smoke':
        raise RuntimeError('unknown result group')
    cycle = summary.get('cycle', {})
    if summary.get('status') != 'PASS' or summary.get('group') != 'niri-smoke' \
            or summary.get('planned') != 1 or summary.get('exercised') != 1 \
            or summary.get('no_surviving_locker') is not True \
            or not cycle.get('unlock_and_destroy') or not cycle.get('desktop_input') or cycle.get('exit') != 0:
        raise RuntimeError('required niri unlock outcome absent: ' + summary.get('status', 'missing'))


def group_testcases(group, backend):
    import acceptance
    if group in acceptance.GROUPS:
        return [f'{backend}/{group}/{case}' for case in acceptance.cases(backend, group)]
    return {'niri-smoke': ['niri/unlock/1'],
            'niri-emergency': ['niri/emergency/missing-runner', 'niri/emergency/main-crash']}[group]


def run_group(args, cfg):
    if not re.fullmatch('[a-f0-9]{64}', args.sha256) or digest(args.image) != args.sha256:
        raise ValueError('test image checksum mismatch before boot')
    source = Path(args.source).resolve(strict=True)
    inventory = source_files(source)
    identity = hashlib.sha256(json.dumps(inventory, sort_keys=True).encode()).hexdigest()
    results = Path(args.results).resolve()
    results.mkdir(parents=True, exist_ok=False, mode=0o700)
    backend = getattr(args, 'backend', 'niri')
    acceptance_plan = getattr(args, 'acceptance_plan', None)
    vm = VM(cfg, args.image, Path(cfg['storage']) / 'test', online=False)
    request = dict(root=cfg['guest_root'], source_files=inventory, source_freeze_identity=identity,
        image_sha256=args.sha256, domain=vm.name, host=cfg['host'], uuid=vm.uuid, mac=vm.mac,
        base_domain='aerial-ci-image-' + args.sha256[:12], build_seconds=cfg['build_seconds'],
        group_seconds=cfg['group_seconds'], source_revision=args.source_revision, group=args.group,
        backend=backend, acceptance_plan=acceptance_plan)
    vm.receipt.update(image_sha256=args.sha256, source_identity=identity, acceptance_plan=acceptance_plan)
    primary = None
    summary = None

    def retain(name, value):
        nonlocal primary
        try:
            write_json(results / name, value)
        except Exception as error:
            primary = primary or error
            print('RESULT_RECEIPT_ERROR ' + str(error), file=sys.stderr)

    try:
        vm.wait_ready()
        cloud = json.loads(vm.execute(['/usr/bin/cloud-init', 'status', '--wait', '--format', 'json'], timeout=60))
        if cloud.get('status') != 'done' or cloud.get('errors'):
            raise RuntimeError('test cloud-init incomplete')
        vm.execute(['/usr/bin/cloud-init', 'schema', '--system'])
        account = json.loads(vm.ssh(['python3', '-c',
            'import os,pwd,json; print(json.dumps(dict(user=pwd.getpwuid(os.getuid()).pw_name,uid=os.getuid(),home=os.path.expanduser("~"))))'],
            capture_output=True, text=True).stdout)
        if account['user'] != cfg['user'] or not Path(cfg['guest_root']).is_relative_to(account['home']) \
                or cfg['guest_root'] == account['home']:
            raise RuntimeError('guest account/workspace mismatch')
        observed_uuid = vm.ssh(['sudo', '-n', 'cat', '/sys/class/dmi/id/product_uuid'], capture_output=True, text=True).stdout.strip().lower()
        if observed_uuid != vm.uuid:
            raise RuntimeError('SSH guest UUID mismatch')
        root = Path(cfg['guest_root'])
        vm.ssh(['mkdir', '-m', '700', str(root)])
        vm.ssh(['mkdir', '-p', str(root / 'src/aerial-lock'), str(root / 'run'), str(root / 'tmp')])
        archive = vm.directory / 'source.tar'
        with tarfile.open(archive, 'w') as stream:
            for name in inventory:
                stream.add(source / name, arcname=name, recursive=False)
        with archive.open('rb') as stream:
            vm.ssh(['tar', '--no-same-owner', '--same-permissions', '-xf', '-', '-C', str(root / 'src/aerial-lock')], timeout=60, stdin=stream)
        vm.ssh(['tee', str(root / 'run/request.json')], input=json.dumps(request).encode(), stdout=subprocess.DEVNULL)
        with (results / 'guest-command.log').open('wb') as log:
            vm.ssh(['env', 'TMPDIR=' + str(root / 'tmp'), 'dbus-run-session', '--', 'python3',
                    str(root / 'src/aerial-lock/Tests/vm/guest.py'), 'run-group', str(root / 'run/request.json')],
                   timeout=cfg['build_seconds'] + cfg['group_seconds'] + 60, stdout=log, stderr=subprocess.STDOUT)
        summary = json.loads(vm.ssh(['cat', str(root / 'results/summary.json')], capture_output=True, text=True).stdout)
        validate_summary(summary, request)
        write_json(results / 'summary.json', summary)
    except BaseException as error:
        primary = error
        summary = None
        if hasattr(vm, 'known_hosts'):
            try:
                candidate = json.loads(vm.ssh(['cat', str(Path(cfg['guest_root']) / 'results/summary.json')],
                                             capture_output=True, text=True).stdout)
                validate_result_identity(candidate, request)
                if candidate.get('export_forbidden'):
                    raise RuntimeError('guest forbids public result export')
                if candidate.get('status') == 'PASS':
                    validate_summary(candidate, request)
                elif candidate.get('status') not in ('FAIL', 'BLOCKED', 'INFRA_ERROR'):
                    raise RuntimeError('unknown guest failure status')
                summary = candidate
                write_json(results / 'summary.json', summary)
            except Exception:
                pass  # absent/untrusted summary remains an infrastructure failure
        retain('failure.json', failure_details(error) | dict(status=summary.get('status') if summary else 'INFRA_ERROR',
                                   instance_uuid=vm.uuid, phase=vm.receipt['phase']))
    finally:
        retain('request.json', request)
        # This independent SSH request still runs when the group process is
        # waiting behind BLOCKED. Only sanitized results are exported.
        try:
            try:
                root = Path(cfg['guest_root'])
                with (results / 'diagnostics.tar.gz').open('wb') as stream:
                    vm.ssh(['env', 'TMPDIR=' + str(root / 'tmp'), 'python3',
                            str(root / 'src/aerial-lock/Tests/vm/guest.py'), 'export', str(root)],
                           timeout=cfg['export_seconds'], stdout=stream, stderr=subprocess.DEVNULL)
            except Exception as error:
                primary = primary or error
                retain('export-error.json', dict(error=str(error)))
        finally:
            # Disk-full/error reporting must never bypass owned VM disposal.
            try:
                vm.stop(force=True)
                if vm.receipt.get('forced_shutdown') or vm.receipt.get('qemu_exit') != 0:
                    raise RuntimeError('test VM required abnormal disposal; outcome retained but infrastructure failed')
            except Exception as error:
                primary = primary or error
                retain('stop-error.json', dict(error=str(error)))
        retain('instance.json', vm.receipt)
        try:
            export_vm_diagnostics(vm, results)
        except Exception as error:
            primary = primary or error
            retain('diagnostic-error.json', failure_details(error))
        names = group_testcases(args.group, backend)
        suite = ET.Element('testsuite', name=args.group, tests=str(len(names)), failures=str(len(names)) if primary else '0')
        for name in names:
            cell = ET.SubElement(suite, 'testcase', name=name, classname='aerial-lock.vm')
            if primary:
                ET.SubElement(cell, 'failure', message=summary.get('status', 'INFRA_ERROR') if summary else 'INFRA_ERROR').text = str(primary)
        try:
            ET.ElementTree(suite).write(results / 'junit.xml', encoding='utf-8', xml_declaration=True)
        except Exception as error:
            primary = primary or error
            print('RESULT_RECEIPT_ERROR ' + str(error), file=sys.stderr)
        if primary is not None and summary is not None and summary.get('status') == 'PASS':
            summary.update(outcome_status='PASS', status='INFRA_ERROR', phase='export-or-stop')
            retain('summary.json', summary)
    if primary is not None:
        raise primary
    print('VM_GROUP_PASS ' + str(results), flush=True)


def preserve_failure(vm, output, error):
    """Best-effort custody must never replace the primary workload exception."""
    failure = failure_details(error) | dict(cleanup_errors=[])
    if vm.process.poll() is None:
        try:
            (output / 'provision.log').write_text(vm.provision_log())
        except Exception as cleanup_error:
            failure['cleanup_errors'].append(str(cleanup_error))
    try:
        vm.stop(force=True)
    except Exception as cleanup_error:
        failure['cleanup_errors'].append(str(cleanup_error))
    try:
        export_vm_diagnostics(vm, output)
    except Exception as cleanup_error:
        failure['cleanup_errors'].append(str(cleanup_error))
    try:
        write_json(output / 'failure.json', failure)
    except Exception as cleanup_error:
        # Even a full evidence filesystem must not hide the original failure.
        print('FAILURE_RECEIPT_ERROR ' + str(cleanup_error), file=sys.stderr)
    return failure


def build_image(args, cfg):
    started = time.monotonic()
    lock = validate_lock(args.lock, args.resolve_lock)
    output = Path(args.output_dir).resolve()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    base = output / 'official.qcow2'
    download(lock['cloud_image']['url'], base, lock['cloud_image']['sha256'])
    files = {}
    for name in ('provision.sh', 'guest.py'):
        files['/var/lib/aerial-ci/provision/' + name] = (HERE / name).read_bytes()
    files['/var/lib/aerial-ci/provision/image.lock.json'] = Path(args.lock).read_bytes()
    for name in lock['aur']:
        files[f'/var/lib/aerial-ci/provision/aur/{name}/PKGBUILD'] = (HERE / 'aur' / name / 'PKGBUILD').read_bytes()
    vm = VM(cfg, base, Path(cfg['storage']) / 'prepare', files, online=True)
    try:
        vm.wait_ready()
        wait_for_provision(vm, cfg['build_seconds'], output)
        cloud = json.loads(vm.execute(['/usr/bin/cloud-init', 'status', '--wait', '--format', 'json'], timeout=60))
        if cloud.get('status') != 'done' or cloud.get('errors'):
            raise RuntimeError('cloud-init did not finish cleanly: ' + str(cloud))
        vm.execute(['/usr/bin/cloud-init', 'schema', '--system'])
        resolved = json.loads(vm.execute(['/usr/bin/cat', '/var/lib/aerial-ci/resolved-lock.json']))
        (output / 'provision.log').write_text(vm.provision_log())
        (output / 'sshd-journal.log').write_text(vm.execute([
            '/usr/bin/journalctl', '-b', '-u', 'sshd.service', '--no-pager', '-o', 'short-monotonic']))
        write_json(output / 'resolved-lock.json', resolved)
        if not args.resolve_lock and resolved != lock:
            raise RuntimeError('installed package or repository lock drift')
        vm.execute(['/usr/bin/bash', '-c',
            'rm -f /home/*/.ssh/authorized_keys; cloud-init clean --logs --machine-id --seed; '
            'rm -f /etc/ssh/ssh_host_*; sync; fstrim -av'], timeout=60)
        vm.stop()
        image = output / 'aerial-ci-arch-x86_64.qcow2'
        command(['qemu-img', 'convert', '-f', 'qcow2', '-O', 'qcow2', '-c', vm.directory / 'disk.qcow2', image], timeout=600)
        info = json.loads(command(['qemu-img', 'info', '--output=json', image], capture_output=True, text=True).stdout)
        if info.get('backing-filename'):
            raise RuntimeError('published image must be standalone')
        if image.stat().st_size >= 2 * 1024 ** 3:
            raise RuntimeError('compressed image exceeds GitHub release asset limit; preserve and review packaging')
        image_digest = digest(image)
        (output / 'SHA256SUMS').write_text(image_digest + '  ' + image.name + '\n')
        write_json(output / 'image-manifest.json', dict(schema=1, image_sha256=image_digest,
            input=lock['cloud_image'], lock_sha256=digest(output / 'resolved-lock.json'),
            tools={name: digest(HERE / name) for name in ('vm_platform.py', 'provision.sh', 'guest.py')},
            image=info, preparation=vm.receipt, signature_verified=False,
            elapsed_seconds=time.monotonic() - started))
        with (output / 'SHA256SUMS').open('a') as sums:
            sums.write(digest(output / 'image-manifest.json') + '  image-manifest.json\n')
        # A successful standalone conversion no longer needs its own downloaded
        # base or writable preparation disk. Failures above retain both.
        (vm.directory / 'disk.qcow2').unlink()
        base.unlink()
        print('IMAGE_BUILT ' + str(image), flush=True)
    except BaseException as error:
        preserve_failure(vm, output, error)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    build = commands.add_parser('build-image')
    build.add_argument('--config', required=True)
    build.add_argument('--lock', required=True)
    build.add_argument('--output-dir', required=True)
    build.add_argument('--resolve-lock', action='store_true', help='write candidate resolved-lock.json, never modify input lock')
    group = commands.add_parser('run-group')
    group.add_argument('--config', required=True)
    group.add_argument('--image', required=True)
    group.add_argument('--sha256', required=True)
    group.add_argument('--source', required=True)
    group.add_argument('--source-revision')
    import acceptance
    group.add_argument('--group', choices=['niri-smoke', 'niri-emergency', *acceptance.GROUPS], required=True)
    group.add_argument('--backend', choices=acceptance.COMPOSITORS, default='niri')
    group.add_argument('--results', required=True)
    args = parser.parse_args()
    cfg = config(args.config)
    Path(cfg['storage']).mkdir(parents=True, exist_ok=True, mode=0o700)
    if args.command == 'build-image':
        meter = DiskMeter(Path(args.output_dir).parent)
        try:
            with meter:
                build_image(args, cfg)
        finally:
            if Path(args.output_dir).is_dir():
                previous_error = sys.exc_info()[1]
                try:
                    write_json(Path(args.output_dir) / 'disk-usage.json', meter.result())
                except Exception as error:
                    if previous_error is None:
                        raise
                    print('DISK_OBSERVATION_ERROR ' + str(error), file=sys.stderr)
    else:
        run_group(args, cfg)


if __name__ == '__main__':
    main()
