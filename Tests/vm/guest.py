#!/usr/bin/env python3
"""Guest-side preparation for aerial-lock's disposable VM test platform."""
import hashlib
import io
import importlib.util
import json
import os
from pathlib import Path
import pwd
import secrets
import subprocess
import sys
import tarfile
import time


def file_digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def binary_hashes(*roots):
    # Provisioning deploys this script alone; the ownership module arrives
    # with the product source for run-group. Record links and payloads using
    # the same digest contract as the manifest validator.
    from owned_instance import digest

    return {str(path): digest(path) for root in roots for path in root.rglob('*')
            if path.is_file() and (os.access(path, os.X_OK) or path.suffix == '.so')}


def inventory():
    root = Path('/var/lib/aerial-ci')
    lock = json.loads((root / 'provision/image.lock.json').read_text())
    index = {}
    databases = {}
    for database in sorted(Path('/var/lib/pacman/sync').glob('*.db')):
        repository = database.stem
        databases[repository] = file_digest(database)
        with tarfile.open(database) as archive:
            for member in archive:
                if not member.name.endswith('/desc'):
                    continue
                fields = {}
                for block in archive.extractfile(member).read().decode().strip().split('\n\n'):
                    lines = block.splitlines()
                    fields[lines[0].strip('%')] = lines[1:]
                name, version = fields['NAME'][0], fields['VERSION'][0]
                index[(name, version)] = dict(name=name, version=version,
                    architecture=fields['ARCH'][0], sha256=fields['SHA256SUM'][0],
                    url=f'https://archive.archlinux.org/repos/{lock["snapshot"]}/{repository}/os/x86_64/{fields["FILENAME"][0]}')
    installed = subprocess.check_output(['pacman', '-Q'], text=True).splitlines()
    packages = []
    for line in installed:
        name, version = line.split()
        if name in lock['aur']:
            if version != lock['aur'][name]['version']:
                raise RuntimeError('AUR package version drift: ' + name)
        else:
            packages.append(index[(name, version)])
    lock.update(repositories=databases, packages=sorted(packages, key=lambda item: item['name']))
    (root / 'resolved-lock.json').write_text(json.dumps(lock, indent=2) + '\n')


def write_json(path, value):
    pending = path.with_name('.' + path.name + '.pending')
    pending.write_text(json.dumps(value, indent=2) + '\n')
    pending.chmod(0o600)
    pending.replace(path)


def start_outer(driver, root, env, config, session_identity):
    runtime = Path(env['XDG_RUNTIME_DIR'])
    compositor = driver.ObservedProcess(['sway', '-c', str(config)], env, root / 'results/outer-compositor.log')
    deadline = time.monotonic() + 30
    ipc = runtime / f'sway-ipc.{os.getuid()}.{compositor.process.pid}.sock'
    while time.monotonic() < deadline:
        sockets = [p for p in runtime.glob('wayland-*') if p.is_socket()]
        if ipc.is_socket():
            for socket in sockets:
                try:
                    session = session_identity(compositor.process.pid, 'sway', runtime, socket, ipc)
                except RuntimeError:
                    continue  # disk-backed workspace may retain an earlier boot's socket
                env.update(WAYLAND_DISPLAY=socket.name, SWAYSOCK=str(ipc))
                outputs = json.loads(subprocess.check_output(['swaymsg', '-r', '-t', 'get_outputs'], env=env, timeout=10))
                return session, outputs, compositor
        if compositor.process.poll() is not None:
            raise RuntimeError('outer Sway exited before readiness')
        time.sleep(0.1)
    raise TimeoutError('outer Sway readiness deadline')


def run_normal_group(summary, source, root, paths, manifest, env, request):
    summary['phase'] = 'niri-unlock'
    before = set(paths['results'].glob('unlock-niri-*/result.json'))
    with (root / 'results/group.log').open('w') as log:
        result = subprocess.run(['bash', str(source / 'Tests/run-remediation.sh'), '--root', str(root),
            '--lane', 'debug', '--vm-record', str(manifest), '--backend', 'niri', '--case', 'unlock', '--cycles', '1'],
            env=env, stdout=log, stderr=subprocess.STDOUT, timeout=request['group_seconds'])
    cases = set(paths['results'].glob('unlock-niri-*/result.json')) - before
    if result.returncode != 0 or len(cases) != 1:
        raise RuntimeError('niri unlock did not complete exactly one fresh case')
    path = next(iter(cases))
    outcome = json.loads(path.read_text())
    cycles = json.loads(path.with_name('cycles.json').read_text())
    if outcome['exit'] != 0 or outcome['cycles_exercised'] != 1 or len(cycles) != 1 \
            or not cycles[0]['unlock_and_destroy'] or not cycles[0]['desktop_input'] or cycles[0]['exit'] != 0:
        raise RuntimeError('required unlock and desktop-input consumer outcomes absent')
    summary.update(status='PASS', exercised=1, cycle=cycles[0], phase='complete', no_surviving_locker=True)


def run_group(request_path):
    request = json.loads(Path(request_path).read_text())
    root = Path(request['root'])
    if os.geteuid() == 0 or root.resolve() != root or not root.is_relative_to(Path.home()) or root == Path.home():
        raise RuntimeError('requires a private, unprivileged guest workspace')
    source = root / 'src/aerial-lock'
    sys.path.insert(0, str(source / 'Tests'))
    from owned_instance import OwnedInstance, digest as source_digest, session_identity
    spec = importlib.util.spec_from_file_location('remediation', source / 'Tests/remediation-driver.py')
    driver = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(driver)
    paths = dict(root=root, source=source, build=root / 'build/debug/aerial-tests',
                 stage=root / 'stage', run=root / 'run', results=root / 'results', media=root / 'media')
    runtime = root / 'runtime'
    for path in (*paths.values(), root / 'tmp', runtime):
        path.mkdir(parents=True, exist_ok=True, mode=0o700)
        if path.resolve() != path or path.stat().st_uid != os.getuid():
            raise RuntimeError('unowned workspace or symlink: ' + str(path))
    for relative, item in request['source_files'].items():
        path = source / relative
        if '..' in Path(relative).parts or not path.is_relative_to(source):
            raise RuntimeError('source escape')
        if source_digest(path) != item['sha256'] or path.lstat().st_mode != item['mode']:
            raise RuntimeError('delivered source drift: ' + relative)
    os.environ.update(TMPDIR=str(root / 'tmp'))
    env = dict(os.environ, TMPDIR=str(root / 'tmp'), MAKEFLAGS='-j2', CMAKE_BUILD_PARALLEL_LEVEL='2',
               LIBGL_ALWAYS_SOFTWARE='1', QSG_RHI_BACKEND='opengl', QT_QPA_PLATFORM='wayland',
               XDG_RUNTIME_DIR=str(runtime), WLR_BACKENDS='headless',
               WLR_HEADLESS_OUTPUTS='1', WLR_RENDERER='pixman')
    for key in ('WAYLAND_DISPLAY', 'DISPLAY', 'NIRI_SOCKET', 'SWAYSOCK', 'WAYLAND_DEBUG',
                'QT_QUICK_BACKEND', 'QML_IMPORT_PATH', 'LD_PRELOAD'):
        env.pop(key, None)
    group = request.get('group', 'niri-smoke')
    import acceptance
    if group not in ('niri-smoke', 'niri-emergency', *acceptance.GROUPS):
        raise RuntimeError('unknown group')
    planned = (len(acceptance.cases(request['backend'], group)) if group in acceptance.GROUPS else
               {'recovery': 2, 'outputs': 1, 'failures': 8}[request.get('case', 'recovery')] if group == 'niri-emergency' else 1)
    summary = dict(schema=1, group=group, status='INFRA_ERROR', planned=planned, exercised=0,
        instance_uuid=request['uuid'], image_sha256=request['image_sha256'],
        source_identity=request['source_freeze_identity'], source_revision=request.get('source_revision'),
        phase='build')
    compositor = None
    password = None
    backup = root / 'run/shadow-backup'
    user = pwd.getpwuid(os.getuid()).pw_name
    service = 'aerial-ci-' + request['uuid'].split('-')[0]
    pam_path = '/etc/pam.d/' + service
    fixture_ready = False
    backup_ready = False
    start = time.monotonic()
    try:
        with (root / 'results/build.log').open('w') as log:
            for argv in (['make', 'DEBUG_DISMISS=0', 'DEBUG_VERBOSE=0'],
                         ['cmake', '-S', 'Tests', '-B', str(paths['build']), '-G', 'Ninja', '-DAERIAL_LOCK_SOURCE=' + str(source)],
                         ['cmake', '--build', str(paths['build']), '--parallel', '2'],
                         ['make', 'install', 'DESTDIR=' + str(paths['stage']), 'DEBUG_DISMISS=0', 'DEBUG_VERBOSE=0']):
                subprocess.run(argv, cwd=source, env=env, check=True, stdout=log, stderr=subprocess.STDOUT,
                               timeout=request['build_seconds'])
        # The native AuthContext execs the normal compiled-in /usr helper path.
        # Install that exact staged helper in this disposable guest, not in the image.
        installed = {}
        for name in ('aerial-lock-auth', 'aerial-lock-emergency'):
            payload = paths['stage'] / 'usr/libexec/aerial-lock' / name
            target = '/usr/libexec/aerial-lock/' + name
            subprocess.run(['sudo', '-n', 'install', '-Dm755', str(payload), target], check=True, timeout=10)
            if file_digest(payload) != file_digest(target):
                raise RuntimeError('installed compiled-path payload does not match staged bytes')
            installed[target] = file_digest(target)
        summary['phase'] = 'outer-session'
        # Arch grants Sway a scheduler file capability. That makes its /proc
        # identity non-dumpable to the unprivileged owner, breaking the existing
        # socket/process ownership contract. Headless pixman needs no scheduler
        # privilege; normalize only this disposable guest, as container CI does.
        capabilities = subprocess.check_output(['getcap', '/usr/bin/sway'], text=True, timeout=10)
        (root / 'results/compositor-capabilities.txt').write_text(capabilities)
        if capabilities.strip():
            subprocess.run(['sudo', '-n', 'setcap', '-r', '/usr/bin/sway'], check=True, timeout=10)
        sway_config = root / 'run/sway.conf'
        sway_config.write_text('output HEADLESS-1 mode 1280x800\nseat seat0 fallback true\nxwayland disable\n')
        if request.get('outer_session'):
            supplied = request['outer_session']
            env.update(supplied['environment'])
            session = session_identity(supplied['pid'], 'niri-drm', env['XDG_RUNTIME_DIR'],
                                       env['WAYLAND_DISPLAY'], env['NIRI_SOCKET'])
            outputs = json.loads(subprocess.check_output(['niri', 'msg', '-j', 'outputs'], env=env, timeout=10))
            outputs = [dict(name=name, active=value['current_mode'] is not None) for name, value in outputs.items()]
        else:
            session, outputs, compositor = start_outer(driver, root, env, sway_config, session_identity)
        if not any(item['active'] for item in outputs):
            raise RuntimeError('outer compositor has no active output')
        subprocess.run(['sudo', '-n', 'cp', '--preserve=mode,ownership,timestamps', '/etc/shadow', str(backup)], check=True, timeout=10)
        subprocess.run(['sudo', '-n', 'chmod', '600', str(backup)], check=True, timeout=10)
        backup_ready = True
        password = secrets.token_hex(16)
        subprocess.run(['sudo', '-n', 'chpasswd'], input=f'{user}:{password}\n', text=True, check=True, timeout=10)
        subprocess.run(['sudo', '-n', 'test', '!', '-e', pam_path], check=True, timeout=10)
        subprocess.run(['sudo', '-n', 'tee', pam_path], input='auth required pam_unix.so\n',
                       text=True, check=True, stdout=subprocess.DEVNULL, timeout=10)
        fixture_ready = True
        secret = root / 'run/pam-fixture-secret'
        secret.write_text(password + '\n')
        secret.chmod(0o600)
        binaries = binary_hashes(paths['stage'], paths['build'])
        identity = hashlib.sha256(json.dumps(request['source_files'], sort_keys=True).encode()).hexdigest()
        record = dict(version=1, host=request['host'], domain=request['domain'], base_domain=request['base_domain'],
            uuid=request['uuid'], mac=request['mac'], host_identity=dict(uuid=request['uuid'], mac=request['mac']),
            guest=dict(user=user, uid=os.getuid(), boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip()),
            paths={key: str(value) for key, value in paths.items()}, ownership=dict(task_created=[str(root)], pre_existing=[]),
            source_freeze_identity=request['source_freeze_identity'], source_files=request['source_files'],
            current_source_identity=identity, binary_source_identity=identity, binary_hashes=binaries,
            session=session, environment={key: env[key] for key in ('XDG_RUNTIME_DIR', 'WAYLAND_DISPLAY', 'SWAYSOCK', 'NIRI_SOCKET') if key in env},
            initial_outputs=[item['name'] for item in outputs if item['active']],
            compiled_payload_hashes=installed,
            pam_fixture=dict(prepared=True, uuid=request['uuid'], uid=os.getuid(), operator_session=request['uuid'],
                             service=service, secret_file=str(secret)), recovery_authorization=None)
        manifest = root / 'run/owned-vm.json'
        write_json(manifest, record)
        tools = root / 'tools/bin'
        tools.mkdir(parents=True, exist_ok=True)
        (tools / 'wtype').symlink_to('/usr/local/bin/wtype')
        OwnedInstance(manifest, root).check(require_binaries=True)
        summary['phase'] = 'ownership-regression'
        with (root / 'results/ownership.log').open('w') as log:
            subprocess.run([sys.executable, str(source / 'Tests/test-owned-instance.py'), '--vm-record', str(manifest)],
                           env=env, check=True, timeout=60, stdout=log, stderr=subprocess.STDOUT)
        if group in acceptance.GROUPS:
            import guest_acceptance
            os.environ.update(env)
            guest_acceptance.run(summary, source, root, paths, manifest, env, request)
        elif group == 'niri-emergency':
            summary['phase'] = 'emergency-recovery'
            case, backend = request.get('case', 'recovery'), request.get('backend', 'niri')
            before = set(paths['results'].glob(f'{case}-{backend}-*/result.json'))
            argv = [sys.executable, str(source / 'Tests/emergency-integration.py'), '--root', str(root),
                    '--vm-record', str(manifest), '--backend', backend, '--case', case]
            if backend == 'hyprland': argv += ['--outputs', '2', '--hyprland-virtual-outputs']
            if request.get('capture_dir'): argv += ['--capture-dir', request['capture_dir']]
            with (root / 'results/group.log').open('w') as log:
                subprocess.run(argv, env=env, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=request['group_seconds'])
            cases = set(paths['results'].glob(f'{case}-{backend}-*/result.json')) - before
            if len(cases) != 1:
                raise RuntimeError('emergency group did not produce exactly one fresh case directory')
            result_path = next(iter(cases))
            outcome = json.loads(result_path.read_text())
            outcomes = json.loads(result_path.with_name('outcomes.json').read_text())
            if outcome['exit'] != 0:
                raise RuntimeError('emergency acceptance did not complete naturally')
            summary.update(status='PASS', exercised=len(outcomes), outcomes=outcomes, phase='complete', no_surviving_locker=True)
        else:
            run_normal_group(summary, source, root, paths, manifest, env, request)
        for process in Path('/proc').iterdir():
            if not process.name.isdigit(): continue
            try:
                if process.stat().st_uid == os.getuid() and (process / 'comm').read_text().strip() in ('qs', 'aerial-lock-eme'):
                    raise RuntimeError('locker process survived the completed group')
            except (FileNotFoundError, ProcessLookupError):
                pass
    except Exception as error:
        summary.update(status='BLOCKED' if (root / 'run/BLOCKED.json').exists() else 'FAIL',
                       error=type(error).__name__, reason=str(error))
    finally:
        if backup_ready:
            subprocess.run(['sudo', '-n', sys.executable, str(source / 'Tests/shadow_fixture.py'), user, str(backup)],
                           check=True, timeout=30)
        if backup.exists():
            subprocess.run(['sudo', '-n', 'rm', '-f', str(backup)], check=True, timeout=10)
        if fixture_ready:
            subprocess.run(['sudo', '-n', 'rm', '-f', pam_path], check=True, timeout=10)
        (root / 'run/pam-fixture-secret').unlink(missing_ok=True)
        if summary['status'] == 'PASS' and compositor is not None:
            compositor.process.terminate()
            compositor.finish(10)
        summary['elapsed_seconds'] = time.monotonic() - start
        summary['versions'] = subprocess.check_output(['pacman', '-Q', 'niri', 'quickshell', 'qt6-base', 'qt6-mpvqml'], text=True).splitlines()
        if password and password in json.dumps(summary):
            summary.update(status='FAIL', reason='credential leak; public export forbidden', export_forbidden=True)
        if password:
            for path in paths['results'].rglob('*'):
                if path.is_file() and password.encode() in path.read_bytes():
                    summary.update(status='FAIL', reason='credential leak; public export forbidden', export_forbidden=True)
                    break
        write_json(root / 'results/summary.json', summary)
    return 0 if summary['status'] == 'PASS' else 1


def export_results(root):
    """Only snapshot sanitized diagnostic text/JSON, never keys, seeds or media."""
    root = Path(root).resolve(strict=True)
    results = root / 'results'
    summary = results / 'summary.json'
    if summary.exists() and json.loads(summary.read_text()).get('export_forbidden'):
        raise RuntimeError('credential leak blocks public export')
    secret = root / 'run/pam-fixture-secret'
    password = secret.read_bytes().rstrip(b'\n') if secret.exists() else None
    payloads = {}
    for path in results.rglob('*'):
        if not path.is_file() or path.is_symlink() or path.suffix not in ('.json', '.log', '.txt'):
            continue
        if not path.resolve().is_relative_to(results):
            raise RuntimeError('evidence symlink escape')
        payload = path.read_bytes()
        if password and password in payload:
            raise RuntimeError('fixture credential present in evidence; export forbidden')
        if b'PRIVATE KEY-----' in payload:
            raise RuntimeError('private key material in evidence; export forbidden')
        payloads[str(path.relative_to(results))] = payload
    manifest = {name: hashlib.sha256(payload).hexdigest() for name, payload in payloads.items()}
    payloads['evidence-sha256.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    with tarfile.open(fileobj=sys.stdout.buffer, mode='w|gz') as archive:
        for name, payload in payloads.items():
            info = tarfile.TarInfo(name)
            info.size, info.mode = len(payload), 0o600
            archive.addfile(info, io.BytesIO(payload))


if __name__ == '__main__':
    if sys.argv[1:] == ['inventory']:
        inventory()
    elif len(sys.argv) == 3 and sys.argv[1] == 'run-group':
        raise SystemExit(run_group(sys.argv[2]))
    elif len(sys.argv) == 3 and sys.argv[1] == 'export':
        export_results(sys.argv[2])
    else:
        raise SystemExit('expected inventory or run-group REQUEST')
