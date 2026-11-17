#!/usr/bin/env python3
"""Identity-bound emergency recovery acceptance in an owned guest only.

The reusable probe also drives CI's installed supervisor in headless Sway.
Faults affect only a validated direct child or an exact disposable-guest payload.
No failed live locker is unlocked or killed to manufacture a passing outcome.
"""
import argparse
from contextlib import contextmanager
import importlib.util
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import threading
import time

from owned_instance import digest
from process_state import process_text

spec = importlib.util.spec_from_file_location("emergency_driver", Path(__file__).with_name("remediation-driver.py"))
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)

RECOVERY_CASES = ("missing-runner", "main-crash")


def child_identity(pid):
    # Emergency deliberately disables dumpability, so its /proc/exe is not
    # readable even by the owner. Parentage + UID + starttime + pidfd bind faults;
    # installed-path bytes are checked separately against the staged manifest.
    path = Path('/proc') / str(pid)
    return dict(pid=pid, uid=path.stat().st_uid,
                starttime=(path / 'stat').read_text().rsplit(')', 1)[1].split()[19],
                comm=(path / 'comm').read_text().strip())


def key(env, *args, payload=None):
    clean = dict(env)
    clean.pop("WAYLAND_DEBUG", None)
    return subprocess.run(["wtype", *args], input=payload, env=clean, check=True,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)


class FrameWitness:
    """Sample real compositor frames, not an inference from protocol requests.

    This can detect a desktop flash at a sampled instant, not exclude shorter
    flashes between samples. Keep that limitation explicit in the result.
    """
    def __init__(self, env, output, directory):
        self.env, self.output, self.directory = dict(env), output, Path(directory)
        self.env.pop('WAYLAND_DEBUG', None)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.stop = threading.Event()
        self.frames, self.error = [], None

    def sample(self):
        raw = subprocess.check_output(['grim', '-t', 'ppm', '-o', self.output, '-'], env=self.env, timeout=5)
        header, dimensions, maximum, pixels = raw.split(b'\n', 3)
        if header != b'P6' or maximum != b'255': raise RuntimeError('unexpected frame format')
        width, height = map(int, dimensions.split())
        if len(pixels) != width * height * 3: raise RuntimeError('truncated captured frame')
        at = pixels.find(b'\x03\x71\x2f')
        while at >= 0 and at % 3: at = pixels.find(b'\x03\x71\x2f', at+1)
        return raw, at >= 0

    def control(self):
        deadline = time.monotonic() + 5
        while True:
            raw, visible = self.sample()
            if visible: break
            if time.monotonic() >= deadline: raise RuntimeError('desktop pixel witness control not visible')
            time.sleep(0.02)
        (self.directory / 'desktop-control.ppm').write_bytes(raw)

    def start(self):
        def observe():
            try:
                while not self.stop.is_set():
                    raw, visible = self.sample()
                    stamp = time.monotonic_ns()
                    self.frames.append(dict(monotonic_ns=stamp, desktop_marker=visible))
                    # Exact full-frame observations remain guest-side until the
                    # evidence export. No input events or secret text is saved.
                    (self.directory / (str(len(self.frames)).zfill(4) + '.ppm')).write_bytes(raw)
                    # The execution plan explicitly calls for recording any
                    # Hyprland desktop flash separately from input/auth state.
                    # Do not turn that observation into a zero-exposure claim.
            except Exception as error:
                self.error = str(error)
        self.thread = threading.Thread(target=observe, daemon=True)
        self.thread.start()

    def finish(self, require_samples=True):
        self.stop.set()
        if hasattr(self, 'thread'): self.thread.join(8)
        summary = dict(samples=len(self.frames), frames=self.frames, error=self.error,
                       samples_with_desktop_marker=sum(frame['desktop_marker'] for frame in self.frames),
                       scope='sampled compositor output; sub-sample flashes not excluded')
        if self.frames:
            stamps = [frame['monotonic_ns'] for frame in self.frames]
            summary['maximum_sample_gap_ms'] = max((b-a)/1e6 for a,b in zip(stamps, stamps[1:])) if len(stamps)>1 else None
        (self.directory / 'frames.json').write_text(json.dumps(summary, indent=2) + '\n')
        if require_samples and (self.error or not self.frames):
            raise RuntimeError(self.error or 'no recovery frames sampled')
        return {key: value for key,value in summary.items() if key != 'frames'}


class RecoveryProbe:
    def __init__(self, output, env, supervisor, runner, shell, plugins, desktop, validate=lambda: None, focus_desktop=None, capture=None, frames=None):
        self.output = Path(output)
        self.output.mkdir(parents=True, exist_ok=True)
        self.env = dict(env, WAYLAND_DEBUG="1", AERIAL_LOCK_SHELL=str(shell),
                        AERIAL_LOCK_RUNNER=str(runner), AERIAL_LOCK_PLUGIN_ROOT=str(plugins))
        self.supervisor = str(supervisor)
        self.desktop = str(desktop)
        self.validate = validate
        self.focus_desktop = focus_desktop
        self.capture = capture
        self.frames = frames
        self.frame_summary = None
        self.locker = self.witness = None
        self.stop = threading.Event()
        self.flood = None
        self.flood_error = None
        self.main_pids = []
        self.emergency_pid = None

    def child(self, expected):
        self.validate()
        pid = self.locker.process.pid
        children = process_text(pid, f"task/{pid}/children").split()
        if len(children) != 1:
            raise RuntimeError("supervisor must own exactly one child: " + repr(children))
        child = int(children[0])
        identity = child_identity(child)
        if process_text(child, "comm").strip() != expected or identity["uid"] != os.getuid():
            raise RuntimeError("unexpected direct locker identity")
        return identity

    def crash(self, identity):
        # pidfd binds the signal to this exact process even if a PID is recycled.
        fd = os.pidfd_open(identity["pid"])
        try:
            if child_identity(identity["pid"]) != identity:
                raise RuntimeError("owned child identity changed before fault")
            parent = process_text(identity["pid"], "status")
            if f"PPid:\t{self.locker.process.pid}\n" not in parent:
                raise RuntimeError("fault target is no longer the owned direct child")
            signal.pidfd_send_signal(fd, signal.SIGKILL)
        finally:
            os.close(fd)

    def wait(self, text, after=0, seconds=20):
        hit = self.locker.wait_for(lambda line: text in line, seconds, after=after)
        if hit is None:
            raise RuntimeError("required recovery outcome absent: " + text + "; " + str(self.output))
        return hit[0]

    def start_flood(self):
        def flood():
            try:
                while not self.stop.wait(0.03):
                    key(self.env, "-k", "F11")
            except Exception as error:
                self.flood_error = str(error)
        self.flood = threading.Thread(target=flood, daemon=True)
        self.flood.start()

    def launch(self, mode):
        self.validate()
        self.witness = driver.ObservedProcess([self.desktop, "--witness"], self.env, self.output / "desktop.log")
        self.witness.wait_line(lambda line: "DESKTOP_INPUT_READY" in line, 10)
        if self.frames: self.frames.control()
        env = dict(self.env)
        if mode in ("missing-runner", "helper-missing", "external-release", "emergency-retry", "emergency-missing", "emergency-exhausted", "compositor-death"):
            env["AERIAL_LOCK_RUNNER"] = str(self.output / "absent-runner")
        elif mode == "missing-plugin":
            env["AERIAL_LOCK_PLUGIN_ROOT"] = str(self.output / "absent-plugins")
        elif mode == "missing-qs":
            tools = self.output / "path"
            tools.mkdir()
            # Runner is an actual shell script; retain its non-qs utilities.
            for name in ("bash", "dirname", "readlink", "cat", "mkdir", "chmod", "rm", "env", "id"):
                import shutil
                found = shutil.which(name)
                if found: (tools / name).symlink_to(found)
            env["PATH"] = str(tools)
        self.locker = driver.ObservedProcess([self.supervisor], env, self.output / "supervisor.log", protocol_sync=True)
        if mode == "main-crash":
            after = 0
            for attempt in range(3):
                after = self.wait(".locked()", after=after)
                self.wait("state: locked", after=after)
                identity = self.child("qs")
                self.main_pids.append(identity["pid"])
                if attempt == 0:
                    self.start_flood()
                    if self.frames: self.frames.start()
                self.crash(identity)
                after = self.wait("backend-exit=9", after=after) + 1
        self.wait("phase=emergency")
        self.wait("lock-state: lock-confirmed")
        self.wait("lock-state: surface-submitted")
        self.wait("state: locked", after=self.wait("launching emergency locker"))
        self.emergency_pid = self.child("aerial-lock-eme")["pid"]
        if self.flood is None: self.start_flood()
        if self.capture: self.capture('idle')

    def retained(self):
        if self.locker.process.poll() is not None or self.child("aerial-lock-eme")["pid"] != self.emergency_pid:
            raise RuntimeError("authentication failure replaced or retired the emergency locker")
        if any(".unlock_and_destroy()" in line for _, line in tuple(self.locker.lines)):
            raise RuntimeError("lock released before authentication")

    def submit(self, payload):
        key(self.env, "-d", "10", "-", payload=payload)
        key(self.env, "-k", "Return")

    def authenticate(self, password, wrong=True):
        if wrong:
            key(self.env, '-d', '10', '-', payload=b'deliberately-invalid-fixture-response')
            if self.capture: self.capture('typing')
            key(self.env, '-k', 'Return')
            if self.capture: self.capture('checking')
            rejected = self.wait("lock-state: auth-failed")
            if self.capture: self.capture('wrong')
            self.retained()
            # The prompt log, not a sleep, establishes helper retirement/readiness.
            key(self.env, "-s", "150", "-k", "Return")
            self.wait("lock-state: auth-prompt", after=rejected)
        # Join the transition witness injector before authenticated unlock can
        # legitimately return desktop focus; it ran throughout all crash gaps.
        self.stop.set()
        if self.flood: self.flood.join(12)
        if self.frames: self.frame_summary = self.frames.finish()
        self.submit(password)
        self.wait("lock-state: auth-success")
        return self.complete(authenticated=True, rejected=wrong)

    def complete(self, authenticated, rejected=False):
        unlocked = self.wait(".unlock_and_destroy()")
        sync = self.locker.wait_for(lambda line: re.search(r'wl_display[@#].*\.sync\(', line), after=unlocked)
        if sync is None: raise RuntimeError('wire sync request after unlock absent')
        callback = re.search(r'wl_callback[@#](\d+)', sync[1])
        if callback is None: raise RuntimeError('wire sync callback identity absent')
        pattern = re.compile(r'wl_callback[@#]' + callback[1] + r'\.done\(')
        done = self.locker.wait_for(lambda line: pattern.search(line), after=sync[0])
        if done is None: raise RuntimeError('matching wire sync callback absent')
        self.wait("lock-state: release-confirmed", after=done[0])
        # Stop input before the compositor returns focus to the desktop. The
        # last F11 might otherwise intentionally arrive after a valid unlock.
        # The flood stops on auth-success/release-requested below in watch().
        self.stop.set()
        if self.flood: self.flood.join(12)
        if self.flood_error: raise RuntimeError(self.flood_error)
        if self.locker.finish(15) != 0: raise RuntimeError("supervisor did not exit naturally 0")
        if Path(f"/proc/{self.emergency_pid}").exists(): raise RuntimeError("emergency child survived")
        if self.witness.process.poll() is not None:
            raise RuntimeError("desktop witness received input while protected or expired")
        if any("DESKTOP_LOCK_INPUT_LEAK" in line for _, line in tuple(self.witness.lines)):
            raise RuntimeError("desktop input leaked during recovery")
        if self.focus_desktop: self.focus_desktop(self.witness.process.pid)
        key(self.env, "-k", "F12")
        self.witness.wait_line(lambda line: "DESKTOP_INPUT_RECEIVED" in line)
        if self.witness.finish(5) != 0: raise RuntimeError("desktop input did not return")
        return dict(status="PASS", real_pam=authenticated, rejected=rejected, sampled_frames=self.frame_summary,
                    emergency_pid=self.emergency_pid, main_pids=self.main_pids,
                    unlock_and_destroy=True, sync_confirmed=True, exit=0,
                    desktop_input=True, no_lock_time_desktop_input=True)

    def close(self):
        self.stop.set()
        if self.flood: self.flood.join(12)
        if self.frames and self.frame_summary is None:
            # Diagnostic failure must not bypass the caller's persistent
            # BLOCKED/live-lock retention path by throwing again in finally.
            self.frames.finish(require_samples=False)
        # Caller retains the session on a failed live locker; never cleanup-unlock.
        if self.locker and self.locker.process.poll() is None:
            return False
        if self.witness and self.witness.process.poll() is None:
            self.witness.process.terminate()
            self.witness.finish(5)
        return True


class EmergencySession(driver.Session):
    def start_hyprland(self):
        self.assert_outer()
        version = subprocess.check_output(['Hyprland', '--version'], text=True, timeout=10)
        match = re.search(r'Hyprland (\d+)\.(\d+)', version)
        if not match: raise RuntimeError('unknown Hyprland version')
        lua = tuple(map(int, match.groups())) >= (0, 57)
        config = self.compositor_directory / ('hyprland.lua' if lua else 'hyprland.conf')
        config.write_text('hl.monitor({output="WAYLAND-1", disabled=true})\n'
                          'hl.config({misc={disable_hyprland_logo=true, disable_splash_rendering=true},'
                          'debug={enable_stdout_logs=true}})\n' if lua else
                          'monitor = WAYLAND-1, disable\nmisc {\n disable_hyprland_logo = true\n disable_splash_rendering = true\n}\n')
        if not self.args.hyprland_virtual_outputs:
            raise RuntimeError('Hyprland acceptance requires render-backed virtual outputs')
        self.compositor = driver.ObservedProcess(['Hyprland', '-c', str(config)], self.env,
                                                 self.compositor_directory / 'compositor.log')
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            response = driver.run(['hyprctl', 'instances', '-j'], self.env, capture_output=True, text=True).stdout
            # Some hyprctl versions emit only ']' with no instances yet.
            # Match the existing driver's bounded startup discovery, never
            # treating that response as a usable compositor identity.
            try:
                instances = json.loads(response)
            except json.JSONDecodeError:
                instances = []
            matches = [value for value in instances if value.get('pid') == self.compositor.process.pid]
            if len(matches) == 1 and matches[0].get('wl_socket'):
                value = matches[0]
                self.env.update(WAYLAND_DISPLAY=value['wl_socket'], HYPRLAND_INSTANCE_SIGNATURE=value['instance'])
                self.record_session(self.compositor.process.pid, 'hyprland', self.runtime / 'hypr' / value['instance'] / '.socket.sock')
                for index in range(1, self.args.outputs + 1):
                    name = 'emergency-' + str(index)
                    self.hypr('output', 'create', 'headless', name)
                    if lua:
                        self.hypr('eval', 'hl.monitor({output="' + name + '",mode="1280x800@60",position="' +
                                  str((index-1)*1280) + 'x0",scale=1})')
                    else:
                        self.hypr('keyword', 'monitor', f'{name},1280x800@60,{(index-1)*1280}x0,1')
                self.start_pointer('emergency-1')
                self.keyboard = subprocess.Popen(['wtype', '-s', '600000'], env=self.env,
                                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return
            if self.compositor.process.poll() is not None:
                raise RuntimeError('owned nested Hyprland exited before socket discovery')
            time.sleep(0.05)
        raise RuntimeError('Hyprland startup deadline')

    def probe(self, name):
        output = 'emergency-1' if self.args.backend == 'hyprland' else (sorted(self.drm_original_outputs)[0] if self.args.backend == 'niri-drm' else 'winit')
        frames = FrameWitness(self.env, output, self.output / name / 'frames') if name == 'main-crash' else None
        return RecoveryProbe(self.output / name, self.env,
            self.stage / "bin/aerial-lock-supervisor",
            self.stage / "libexec/aerial-lock/aerial-lock-run", self.shell,
            self.stage / "lib/aerial-lock/qt-plugins",
            self.root / "build/debug/aerial-tests/desktop-input-probe", self.assert_control,
            self.focus_desktop, self.capture if getattr(self.args, 'capture_dir', None) and name == 'missing-runner' else None, frames)

    def capture(self, state):
        target = Path(self.args.capture_dir).resolve()
        if not target.is_relative_to(self.root): raise RuntimeError('capture outside owned workspace')
        target.mkdir(parents=True, exist_ok=True)
        if self.args.backend == 'hyprland':
            self.hypr('dismissnotify')
            output = 'emergency-1'
        elif self.args.backend == 'niri-drm':
            output = sorted(self.drm_original_outputs)[0]
        else:
            output = 'winit'
        time.sleep(0.06)  # allow the submitted state to reach the compositor
        driver.run(['grim', '-o', output, str(target / (self.args.backend + '-emergency-' + state + '.png'))], self.env)

    def focus_desktop(self, pid):
        if self.args.backend.startswith('niri'):
            windows = json.loads(driver.run(['niri', 'msg', '-j', 'windows'], self.env, capture_output=True, text=True).stdout)
            owned = [window for window in windows if window.get('pid') == pid]
            if len(owned) != 1: raise RuntimeError('owned niri desktop witness window not found')
            driver.run(['niri', 'msg', 'action', 'focus-window', '--id', str(owned[0]['id'])], self.env, stdout=subprocess.DEVNULL)
            return
        clients = json.loads(self.hypr('-j', 'clients'))
        windows = [window for window in clients if window.get('pid') == pid]
        if len(windows) != 1: raise RuntimeError('owned desktop witness window not found')
        monitors = json.loads(self.hypr('-j', 'monitors'))
        monitor = next(value for value in monitors if value['id'] == windows[0]['monitor'])
        # A normal desktop click, only after authenticated release. Hyprland
        # need not restore the pre-crash application's keyboard focus itself.
        self.pointer_command('map ' + monitor['name'], b'VIRTUAL_POINTER_MAPPED')
        self.pointer_command('click', b'VIRTUAL_POINTER_CLICKED')

    def installed(self):
        # Compiled absolute paths deliberately have no environment override.
        for name in ("aerial-lock-auth", "aerial-lock-emergency"):
            staged = self.stage / "libexec/aerial-lock" / name
            self.owned.check_binary(staged)
            if digest(staged) != digest(Path("/usr/libexec/aerial-lock") / name):
                raise RuntimeError("compiled-path payload differs from owned staged binary")

    def recovery(self):
        outcomes = []
        for name in RECOVERY_CASES:
            self.start()
            self.config(media=False)
            self.installed()
            if self.keyboard is None:
                self.keyboard = subprocess.Popen(["wtype", "-s", "600000"], env=self.env)
                time.sleep(0.2)
            probe = self.probe(name)
            try:
                probe.launch(name)
                self.locker = probe.locker
                if self.args.backend == 'hyprland':
                    deadline = time.monotonic() + 10
                    while sum('lock-state: surface-submitted' in line for _, line in tuple(probe.locker.lines)) < self.args.outputs:
                        if time.monotonic() > deadline: raise RuntimeError('not all Hyprland outputs submitted frames')
                        time.sleep(0.02)
                outcome = probe.authenticate(Path(self.owned.auth_fixture()["secret_file"]).read_bytes().removesuffix(b"\n"))
                if self.args.backend.startswith('niri') and outcome.get('sampled_frames') \
                        and outcome['sampled_frames']['samples_with_desktop_marker']:
                    raise RuntimeError('niri desktop pixels observed during recovery')
                outcome.update(case=name, backend=self.args.backend,
                               instance_uuid=self.owned.record["uuid"], source_identity=self.owned.record["source_freeze_identity"])
                if self.args.backend == "hyprland":
                    flag = json.loads(self.hypr("-j", "getoption", "misc:allow_session_lock_restore"))
                    if flag.get("bool", flag.get("int")) not in (False, 0): raise RuntimeError("Hyprland restore flag not reset")
                    outcome["restore_flag_reset"] = True
                outcomes.append(outcome)
                (self.output / "outcomes.json").write_text(json.dumps(outcomes, indent=2) + "\n")
            except Exception as error:
                (self.output / "failure.json").write_text(json.dumps(dict(error=type(error).__name__, reason=str(error))) + "\n")
                raise
            finally:
                if not probe.close(): self.locker = probe.locker
                self.cleanup()
        return outcomes

    def password(self):
        return Path(self.owned.auth_fixture()["secret_file"]).read_bytes().removesuffix(b"\n")

    def prepare(self):
        self.start()
        self.config(media=False)
        self.installed()
        if self.keyboard is None:
            self.keyboard = subprocess.Popen(["wtype", "-s", "600000"], env=self.env)
            time.sleep(0.2)

    def outputs(self):
        if self.args.backend != 'niri-drm':
            raise RuntimeError('outputs requires the actual owned dual-output DRM session')
        self.prepare()
        original = self.drm_outputs()
        names = sorted(self.drm_original_outputs)
        if len(names) < 2: raise RuntimeError('two real DRM outputs required')
        probe = self.probe('outputs')
        try:
            # A secondary output has a different scale and orientation.
            driver.run(['niri', 'msg', 'output', names[1], 'scale', '2'], self.env, stdout=subprocess.DEVNULL)
            driver.run(['niri', 'msg', 'output', names[1], 'transform', '90'], self.env, stdout=subprocess.DEVNULL)
            self.start_pointer(names[1])
            probe.launch('missing-runner')
            self.locker = probe.locker
            self.pointer_command('click', b'VIRTUAL_POINTER_CLICKED')
            deadline = time.monotonic() + 10
            while sum('lock-state: surface-submitted' in line for _, line in tuple(probe.locker.lines)) < len(names):
                if time.monotonic() > deadline: raise RuntimeError('not all outputs submitted emergency frames')
                time.sleep(0.02)
            password = self.password()
            middle = len(password) // 2
            key(self.env, '-d', '10', '-', payload=password[:middle])
            self.drm_output(names[0], 'off')
            self.wait_drm_outputs({names[1]})
            self.drm_output(names[0], 'on')
            self.wait_drm_outputs(set(names))
            # Remove the focused output; draft must not belong to its surface.
            self.drm_output(names[1], 'off')
            self.wait_drm_outputs({names[0]})
            self.drm_output(names[0], 'off')
            empty = self.wait_drm_outputs(set())
            probe.retained()
            self.drm_output(names[0], 'on')
            self.wait_drm_outputs({names[0]})
            self.pointer_command('map ' + names[0], b'VIRTUAL_POINTER_MAPPED')
            self.pointer_command('click', b'VIRTUAL_POINTER_CLICKED')
            outcome = probe.authenticate(password[middle:], wrong=False)
            password = None
            outcome.update(case='outputs', backend='niri-drm', mixed_scale=True, rotated=True,
                nonprimary_input=True, zero_mapped_outputs=all(v['current_mode'] is None for v in empty.values()),
                draft_preserved_by_real_pam=True, initial_outputs=names,
                instance_uuid=self.owned.record['uuid'], source_identity=self.owned.record['source_freeze_identity'])
            return [outcome]
        except Exception as error:
            (self.output / 'failure.json').write_text(json.dumps(dict(error=type(error).__name__, reason=str(error))))
            raise
        finally:
            if not probe.close(): self.locker = probe.locker
            # Exact task-owned output changes only; restore original geometry.
            for name in names:
                logical = original[name]['logical']
                driver.run(['niri', 'msg', 'output', name, 'scale', str(logical['scale'])], self.env, stdout=subprocess.DEVNULL)
                transform = {'Normal': 'normal', '90': '90', '180': '180', '270': '270'}.get(logical['transform'])
                if transform is None: raise RuntimeError('unknown original output transform')
                driver.run(['niri', 'msg', 'output', name, 'transform', transform], self.env, stdout=subprocess.DEVNULL)
            self.cleanup()

    @contextmanager
    def absent(self, name):
        self.installed()
        target = Path('/usr/libexec/aerial-lock') / name
        saved = self.root / 'run' / ('disabled-' + name)
        if saved.exists(): raise RuntimeError('unrestored payload fault fixture')
        driver.run(['sudo', '-n', 'mv', str(target), str(saved)], self.env)
        try:
            yield
        finally:
            driver.run(['sudo', '-n', 'mv', str(saved), str(target)], self.env)
            self.installed()

    def failures(self):
        if self.args.backend == 'niri-drm': raise RuntimeError('destructive faults require a nested compositor')
        outcomes = []
        for name in ('missing-qs', 'missing-plugin', 'emergency-retry', 'helper-missing',
                     'external-release', 'emergency-missing', 'emergency-exhausted', 'compositor-death'):
            self.prepare()
            probe = self.probe(name)
            try:
                probe.launch(name)
                self.locker = probe.locker
                if name == 'emergency-retry':
                    stamp = time.monotonic_ns()
                    probe.crash(probe.child('aerial-lock-eme'))
                    probe.wait('lock-state: lock-confirmed', after=stamp)
                    probe.wait('lock-state: surface-submitted', after=stamp)
                    probe.emergency_pid = probe.child('aerial-lock-eme')['pid']
                elif name == 'helper-missing':
                    with self.absent('aerial-lock-auth'):
                        probe.submit(b'deliberately-invalid-fixture-response')
                        probe.wait('lock-state: helper-start-failed')
                        probe.retained()
                    key(self.env, '-k', 'Escape')
                elif name == 'external-release':
                    probe.stop.set()
                    if probe.flood: probe.flood.join(12)
                    os.kill(probe.locker.process.pid, signal.SIGUSR1)
                    outcome = probe.complete(authenticated=False)
                elif name in ('emergency-missing', 'emergency-exhausted'):
                    if name == 'emergency-missing':
                        with self.absent('aerial-lock-emergency'):
                            probe.crash(probe.child('aerial-lock-eme'))
                            if probe.locker.finish(15) != 3: raise RuntimeError('missing emergency exit class')
                    else:
                        for attempt in range(3):
                            stamp = time.monotonic_ns()
                            probe.crash(probe.child('aerial-lock-eme'))
                            if attempt < 2:
                                probe.wait('lock-state: lock-confirmed', after=stamp)
                                probe.wait('lock-state: surface-submitted', after=stamp)
                        if probe.locker.finish(15) != 3: raise RuntimeError('emergency exhaustion exit class')
                    if any('.unlock_and_destroy()' in line for _, line in tuple(probe.locker.lines)):
                        raise RuntimeError('failed recovery issued unlock')
                    probe.stop.set()
                    if probe.flood: probe.flood.join(12)
                    if probe.witness.process.poll() is not None: raise RuntimeError('failed recovery exposed desktop input')
                    outcome = dict(status='PASS', no_automatic_unlock=True, exit=3, protection_previously_confirmed=True)
                elif name == 'compositor-death':
                    probe.stop.set()
                    if probe.flood: probe.flood.join(12)
                    self.compositor.process.terminate()
                    self.compositor.finish(10)
                    if probe.locker.finish(15) != 0: raise RuntimeError('compositor-death-first outcome')
                    outcome = dict(status='PASS', compositor_dead=True, exit=0)
                    # No surviving compositor identity remains to control.
                    self.compositor = None
                if name in ('missing-qs', 'missing-plugin', 'emergency-retry', 'helper-missing'):
                    outcome = probe.authenticate(self.password(), wrong=False)
                log = probe.locker.log.read_text()
                if log.count('launching locker, attempt') != 1:
                    raise RuntimeError('unexpected normal retry after known startup failure/emergency transition')
                outcome.update(case=name, backend=self.args.backend, instance_uuid=self.owned.record['uuid'],
                               source_identity=self.owned.record['source_freeze_identity'])
                outcomes.append(outcome)
                (self.output / 'outcomes.json').write_text(json.dumps(outcomes, indent=2) + '\n')
            except Exception as error:
                (self.output / 'failure.json').write_text(json.dumps(dict(case=name, error=type(error).__name__, reason=str(error))))
                raise
            finally:
                if not probe.close(): self.locker = probe.locker
                self.cleanup()
        return outcomes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True)
    parser.add_argument("--vm-record", required=True)
    parser.add_argument("--backend", choices=("niri", "niri-drm", "hyprland"), required=True)
    parser.add_argument("--case", choices=("recovery", "outputs", "failures"), required=True)
    parser.add_argument("--outputs", type=int, default=1)
    parser.add_argument("--hyprland-virtual-outputs", action="store_true")
    parser.add_argument('--capture-dir', help='test evidence only, inside the owned workspace')
    args = parser.parse_args()
    args.recover_owned_failures = False
    args.pointer_helper = None
    session = EmergencySession(args)
    status = 1
    try:
        outcomes = getattr(session, args.case)()
        (session.output / 'outcomes.json').write_text(json.dumps(outcomes, indent=2) + '\n')
        status = 0
        print("EMERGENCY_RECOVERY_PASS " + str(session.output), flush=True)
        return 0
    finally:
        (session.output / "result.json").write_text(json.dumps(dict(case=args.case, backend=args.backend,
            exit=status, instance_uuid=session.owned.record["uuid"],
            source_identity=session.owned.record["source_freeze_identity"]), indent=2) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
