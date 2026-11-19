# Testing aerial-lock

Build and test in a disposable VM with the runtime/build dependencies from the
README, including installed **qt6-mpvqml ≥ 0.2.0**. Use a disk-backed workspace
and temporary directory. The commands below run inside that guest from the
repository root:

```sh
mkdir -p "$HOME/aerial-test/tmp"
export TMPDIR="$HOME/aerial-test/tmp"
export LIBGL_ALWAYS_SOFTWARE=1 QSG_RHI_BACKEND=opengl
make DEBUG_DISMISS=0 DEBUG_VERBOSE=0
```

## Portable checks

The `Checks` GitHub workflow runs on pushes and PRs to master, using Blacksmith
`blacksmith-8vcpu-ubuntu-2404` x64 VMs (8 vCPU, 32 GB RAM, 160 GB disk).
The image-build and test container caps remain 4 GB RAM and two CPUs;
runner capacity does not override those limits. Its separate
`build-and-test` job uses the same Arch `.devcontainer/Dockerfile` as lint:
it builds the production targets with `make`, builds `Tests/CMakeLists.txt`,
runs the portable CTest/Python cases offscreen, the Node suites, and every
`tst_*.qml` suite with the whole-log zero-QWARN gate below. The container has
no network and bounded memory, processes, CPU and run time. The checkout is
read-only; `.devcontainer/test.sh` copies it to the container's writable layer.

### Real-lock CI smoke

The same job then runs `.devcontainer/lock-smoke.sh` in a fresh offline container
with the same resource caps and a seven-minute timeout. It builds and installs
the production targets (debug dismiss and verbose logging off), starts Sway with
its headless backend/pixman renderer, and launches the real installed `aerial-lock`
as a new unprivileged throwaway user on a private D-Bus session. No KVM, privileged
container, host display socket or graphics device is needed.
The image removes Arch Sway's scheduler file capability; the smoke uses the
container engine's default capabilities, not an expanded privilege set.

`ci-lock-smoke.py` reuses the remediation driver's filtered process observer.
Like that driver's Sway setup, it keeps a virtual keyboard alive and checks the
headless seat's keyboard readiness before starting the locker.
It requires the actual session-lock protocol confirmation, injects one wrong
password with wtype, observes `lock-state: auth ... -> failed` while the same
locker retains the lock, then enters the generated correct password through
the same native keyboard path and real `pam_unix`. It requires auth success,
`unlock_and_destroy`, confirmed release, natural locker exit 0 and supervisor
exit 0 with no respawn, and no warnings from the unlock request through process
exit (including QML context and deferred-call teardown warnings). The warning
check reads the complete drained log, not a timed observation or recent-line
buffer. Every wait/subprocess is bounded. The fixture password
is generated per container, passed only on stdin and never written to a file
or printed; protocol keyboard/IME traffic is discarded before persistence.
The image builds checksum-pinned wtype v0.4 with the same TMPDIR-only tempfile
patch described below, so its keymap files stay on the container's writable
disk, not a shared temporary directory. Failure never uses debug dismiss or
an external release to turn a failed authentication cycle into a pass.

`ci-emergency-smoke.py` then creates a separate unprivileged PAM fixture and
fresh Sway session for each installed-supervisor recovery case: missing runner
and three confirmed normal-locker crashes. Both must render the actual C client,
reject a wrong answer without replacing it, authenticate through pam_unix,
confirm unlock/sync and exit naturally. A desktop witness receives no injected
F11 keys during crash gaps and receives F12 after authentication. No test-only
product auth override or debug dismissal is used.

These are short platform smokes, not niri/Hyprland acceptance, media playback,
physical display, multi-output, IME or suspend coverage. The full VM drivers
remain separate. To reproduce, use the exact image build and smoke container
commands from `.github/workflows/qmllint.yml` inside a disposable test VM.

CI still excludes `media-fallback-render` (needs a video-rendering
platform), `owned-instance` (requires an operator-prepared VM manifest), and
the real-session drivers below: remediation, staged/nested launch, surface
input, and Quickshell lifecycle/password-limit/release/startup fixtures.
Those need real session-lock/PAM, niri/Hyprland, native input, display hotplug,
or an owned VM session; offscreen tests do not prove those outcomes.

```sh
bash Scripts/dev/qmllint.sh .
node --test Tests/*.test.cjs
```

`i18n.test.cjs` executes ConfigStore's actual methods with a fake FileView
boundary. It checks per-key user/bundle/English precedence, partial overrides,
user-only languages, and one-warning fallback for missing or broken user files.
Invalid or blank leaves cannot erase usable lower-layer labels.

### QML suites

```sh
mkdir -p "$TMPDIR/qml-gate"
log="$TMPDIR/qml-gate/qmltestrunner.log"
status=0
QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=opengl LIBGL_ALWAYS_SOFTWARE=1 \
  /usr/lib/qt6/bin/qmltestrunner -import Tests/imports -input Tests >"$log" 2>&1 || status=$?
cat "$log"
qwarnings=$(awk '/^QWARN[[:space:]]*:/ { count++ } END { print count+0 }' "$log")
printf 'Whole-log QWARN count: %s\n' "$qwarnings"
test "$status" -eq 0 && test "$qwarnings" -eq 0
```

The gate requires both a successful test exit and **zero QWARNs in the whole
log**, for every suite. Keep the same log check when selecting a single suite:
construction and teardown warnings occur outside test functions and can escape
`failOnWarning()`. Every `Connections` declares its target at construction; bind
to a nullable context while it is unavailable instead of relying on the implicit
parent target or assigning `target` later in `init()`.

Locate component internals by a unique, component-scoped `objectName` (using
QtTest's `findChild` or a name-only traversal when counting instances). Assert
visibility, geometry, text and interface properties only after selecting the
named object; those mutable properties must not decide which object is tested.

Use `-input Tests/tst_theme_host.qml` to select just one suite. The test-only
`imports/Quickshell` modules let standalone QtTest load ThemeHost's types
without Quickshell's executable-linked plugins. They supply only its inactive
I/O boundary; any environment or file access throws. The handoff suite loads
the actual ThemeHost, Loader, timers, AuthInputState and candidate QML, while
driving a synthetic writer's composition and explicit release. Real native
IME handling belongs to the native input probe; real theme discovery and
Quickshell loading belong to the nested-shell camera gate.

The accent-sampler suite writes its frame files into `TMPDIR`; point it at a
disposable directory. Auth-input tests fail on warnings, and destroy consumers
before their context. Theme fidelity includes a markup-bearing PAM message on
all five themes and the fallback; glyph layout must match plain-text rendering.

Response-limit coverage spans `tst_auth_input.qml` (typing and exact-fit
replacement), `native-input-probe` (whole-edit paste/IME rejection, exact-fit
paste and safe submission), and `lock-lifecycle-shell.qml` (the actual status
projection, clearing and PAM precedence). The editor reserves one native input
unit beyond the build-derived cap only to detect overflow; that edit is restored
before publication. Unknown PAM limits leave Qt's default widget ceiling alone
and never produce a response-limit hint.
`lock-password-limit-shell.qml` additionally drives native typing, navigation
and Backspace through the actual editor/controller, checking the projected hint
and its clearing. Like the lifecycle fixture, run it through the real Quickshell
runner in the disposable VM, with its shell-root loader and bundled imports.

### Native tests

`power-actions` runs the actual native service against an independent login1
peer on a caller-created private session bus, selected only through the C++
constructor seam. It covers strict capability mapping, confirmation and
retirement, inhibitors, wire flags/noninteractive headers, failures, service
replacement and lifecycle ordering. No portable test sends a real OS action.
`tst_power_actions.qml` drives all four actual themes at wide/narrow sizes,
including Cancel-first keyboard flow, hidden controls, literal error text,
progress and measured geometry. Config/i18n Node tests execute ConfigStore's
shipped methods, not a second merge implementation.

Inside the owned VM's Wayland session, after building the tests:

```sh
python3 Tests/test-power-controller.py --build "$ROOT/build/debug/aerial-tests"
```

This cold-loads the real Quickshell controller/context and native service using
a test-only QML plugin whose constructor selects the private peer bus. Only PAM
and the compositor boundary are synthetic; no native lock is acquired. It
checks abort-before-synchronous-success, retained draft/selection, blocked
submission, literal inhibitor projection, hibernate dispatch/status/resume and
external Release retiring an unsent transaction. It is not real-logind policy,
multi-output or physical sleep/shutdown proof.

`power-actions-vm.py --root "$ROOT" --vm-record "$VM_RECORD" --backend niri
--case <case>` adds installed-lock and real-login1 acceptance. Launch it through
the recorded active graphical session's spawn path, not an SSH/root subject.
Cases are `themes`, `controller`, `outputs`, `policy`, `inhibited`, `error` and
`supported-{suspend,hibernate,reboot,poweroff}`. The theme wrapper observes the
public context and measured named controls; it never replaces the system bus.
Secure coverage, absence of power-triggered release, and ordinary independent
PAM cleanup are recorded in filtered logs. It does not capture lock surfaces.
Visual evidence is a separate normal-window render of actual themes with a
scripted context, not a capture of the live lock.

Prepare the operator manifest and a TMPDIR-only workspace-local wtype at
`$ROOT/tools/bin/wtype`, then prepare credentials inside that disposable guest:

```sh
sudo python3 Tests/power-actions-vm-fixtures.py --root "$ROOT" --vm-record "$VM_RECORD" prepare
python3 Tests/power-actions-vm.py --root "$ROOT" --vm-record "$VM_RECORD" --backend niri --prepare
```

The root fixture helper validates the recorded DMI UUID and caller/workspace,
saves the original account line root-only, and owns only its temporary PAM,
other-user session and tester/action-scoped policy files. Policy checks include
systemd's `set-reboot-*` implication paths; the original installed policy is
never edited. `policy restore` removes those fixtures; after all lanes, run
the helper's `restore` operation to restore the original account line exactly
and remove the credential file. Never copy credentials into evidence.

Terminal cells publish pre-dispatch records before the OS can terminate the
driver. An external UUID-bound VM operator must independently attest changed
boot ID, shut off, or kernel PM entry/exit with the same boot/lock after resume.
SSH loss alone is not success. Unsupported hibernate is `UNAVAILABLE`. Masked
targets may make Can* refuse before dispatch: that is unavailable/denied, not
a passed real dispatch-error cell. Persistent live-lock failures use BLOCKED;
there is no automatic locker kill or external Release to manufacture success.

`auth-helper` runs the actual exec worker against a test-only PAM module in a
private config directory. It covers batched info/error/hidden/visible prompts,
empty and newline responses, rejection, max tries, errors, abort/EOF, malformed
and oversized input, crashes and killing a stalled module without account edits.
`auth-context` drives the production native owner against a process fixture:
fragmented/malformed/truncated/extra output, success followed by crash, prompt
and echo state, abort/restart, byte limits (including UTF-8), exec failure and
child reaping. Neither test substitutes for real pam_unix and installed-locker
acceptance. See `native/auth-protocol.md` for the transport and security boundary.

`auth-timezone-shell.qml` is the real-session old/new deadlock comparison, not
a portable/offscreen test. In the owned VM, prepare an auth-only pam_unix service
named `aerial-auth-timezone` without account lockout, and load the fixture through
the production runner (the same shell-root Loader pattern as `test-lock-state.py`).
Preload the test build's `libauth-timezone-hammer.so`. Its four threads hammer
`localtime_r` only in qs, never in the exec helper. Set
`AERIAL_TEST_AUTH_BACKEND=old` for PamContext, or `exec` for AuthContext;
`AERIAL_TEST_AUTH_ITERATIONS` defaults to 100. A rejection must reach QML within
10 seconds. The fixture aborts its own conversation and exits 3 on a stall;
all timely rejections exit 0. Preserve the old red run and every new latency,
then restore the operator's authentication fixture. No lock is acquired.

`python3 Tests/test-lock-state.py` runs the real Quickshell Lock controller
with synthetic PAM in an owned VM's Wayland session, without engaging a lock.
Build with `make` first and set `TMPDIR` to the disk-backed workspace. It checks
the exact prompt/failure/success transition sequence and absence of synthetic
passwords from the complete output, with verbose logging both off and on.
Like the lifecycle fixture, this is separate from the portable offscreen gate;
real PAM and compositor release still need a real-lock cycle.

`python3 Tests/test-lock-release-state.py` loads the actual production shell and
Lock controller with an injected native-state boundary. No native lock is
acquired. Zero, one and duplicate release notifications must each yield one
unlocked event and natural exit 0. Late state-only release must complete on the
backstop's direct observation. A still-locked or still-secure boundary must emit
no unlocked event and exit 1 through the request-armed backstop. The fixture uses
the guest's login service only for startup validation; it never starts a PAM
conversation. It also requires the Lock's context to remain live until the
release call returns, and warning-free release/exit for each notification mode;
the unconfirmed cases still require the backstop's operational warning.

One layout serves these tests and the VM drivers below: a workspace `$ROOT`
with the tree under test at `$ROOT/src/aerial-lock` and the test build at
`$ROOT/build/debug/aerial-tests`.

```sh
make -C "$ROOT/src/aerial-lock"
cmake -S "$ROOT/src/aerial-lock/Tests" -B "$ROOT/build/debug/aerial-tests" -G Ninja \
  -DAERIAL_LOCK_SOURCE="$ROOT/src/aerial-lock"
cmake --build "$ROOT/build/debug/aerial-tests"
ctest --test-dir "$ROOT/build/debug/aerial-tests" --output-on-failure
```

`supervisor-hyprland` runs the actual supervisor with a fake hyprctl and stub
locker on a private bus. It covers Lua/legacy provider selection (including old
versions without status), exit-0 semantic rejection, malformed status/read-back,
wrong effective values, failed cleanup and re-ensuring after failure or reload.
No compositor, authentication or unlock request is used. Real crash/relock and
PAM acceptance still require the owned Hyprland VM lanes.

`make` comes first: `supervisor-session` uses the built supervisor. The portable
set covers native input and editor migration, the BLOCKED latch, supervisor
session scoping and engagement-confirmed duplicates (waiting while engaging, a
dead locker's lost confirmation, ignored foreign reports, never-engaging and
failing supervisors, and `dbus-test-tool` name owners that do not answer or
answer malformed; it also uses `busctl`), pre-engagement/monitor/mixed and
engaged-then-abnormal supervisor outcomes, literal shell
operands, incremental build configuration, driver wait ordering, media-failure
fallback (an injected core failure; `media-fallback-render` needs a platform
that renders video, e.g. inside a nested compositor: it is reported as skipped
only on the `offscreen`/`minimal` Qt platforms or a non-OpenGL Qt Quick
backend, and fails anywhere else if video does not start) and output-selection
policy. `-DAERIAL_LOCK_VM_RECORD=<manifest>` adds the `owned-instance`
regression for a prepared VM (below).

`build-config` also rebuilds every native target with LTO. On Arch it sources
`CFLAGS`, `CXXFLAGS`, `LDFLAGS` and `LTOFLAGS` from `/etc/makepkg.conf`; elsewhere
it uses a minimal optimized LTO build. This exercises Qt's protected-data-symbol
contract at final link, as well as compilation, and checks the rebuilt idle
executable's effective config. Existing path-change, no-op and compiler-flag
invalidation checks still run first. The linked auth consumer must select the
checkout helper for `make dev`, remain unchanged on another dev build, then
rebuild with the installed helper path on `make install`. This also exercises
a checkout path containing spaces; only the copied dev launcher is stubbed in
the portable fixture. Staged installs check the unit generated
from `systemd/aerial-idle.service.in` with the default prefix, `/usr/local`,
and an explicit `BINDIR` override: `ExecStart` must name the installed binary
without `DESTDIR`, the unit must have mode 644, and uninstall must remove the
payload in each layout.

`supervisor-release` uses a private bus and stub direct-child runners to
observe exactly `release\n`, pending engagement, D-Bus/SIGUSR1 parity,
idempotence, no respawn after clean or abnormal release, a waiting duplicate's
SIGUSR1 immunity (exit 0/4 unchanged), and the child's unblocked SIGUSR1 mask.
Its terminal probe pumps the production Supervisor after normal episode
completion, without entering `app.exec()`, to check the actual
`org.aeriallock.Supervisor.Error.NotLocked` reply for ended/failed states;
there is no product test switch. Offscreen `tst_engagement_report.qml` checks
the exact command and one-warning policy at the inactive transport boundary.

`supervisor-output` uses a private bus and command-synchronized child to check
stdout and stderr visibility while the child is alive, direct file-descriptor
inheritance and supervisor RSS after finite output volumes. The child waits for
the next command after acknowledging each completed write; neither liveness nor
the memory assertion depends on a timed output burst. Both checks also run
through a Python interpreter symlink whose path contains spaces, exercising
the fixture wrapper's argument quoting.

`idle-config` compares the native reader's `--print-config` output with the
actual ConfigStore merge/schema methods in QML for shared fixtures: missing,
partial, full and invalid objects/fields, extra keys and timeout bounds.
Both idle fixtures set `AERIAL_LOCK_SHELL` to the source tree, so no installed
`/usr/share/aerial-lock` is needed. This variable selects both the supervisor's
shell directory and aerial-idle's bundled `Config/defaults.json`; unset or empty
uses the compiled `DATADIR`. `idle-shell-dir` builds with a private compiled
bundle, observes a different timeout from a scratch shell override, checks unset
and empty fallback, reads the source bundle and rejects an unavailable override.
`idle-logind` uses private system/session buses, fake login1/systemd peers and
the real supervisor with a stub runner. It checks lock deduplication, unlock
forwarding, real inhibitor-FD lifetime through confirmation/deadline/resume,
login1 replacement (including replacement without an old resume and failed
state snapshots), bypass-started locks, typed transient-unit arguments and
detached process ancestry. The fake systemd merges its explicit manager
environment with the unit's `Environment`, then applies `UnsetEnvironment`
after parsing all properties. A stale manager shell override is replaced by a
nonempty daemon override or removed when the daemon's override is unset/empty;
the unset property must have D-Bus type `as`. Both launch paths
check the shell override seen by the launched process and the supervisor's
actual shell operand, including unset/empty compiled fallback without `/usr`.
Relative `.` and paths containing `..` are resolved against the daemon's cwd
once, then reach both child launch paths as the same absolute, cleaned path.
The relative-bundle print-config probe observes its distinct timeout, and a
direct supervisor caller also checks relative operand normalization.
Daemon termination must leave the independent
lock alive. Each policy switch and compositor-disconnect exit are covered.
Private runtime sockets use a shared short namespace under `TMPDIR`, with
encoded-path length preflight before launching a fixture.

Cross-process file receipts and runner markers use `fixture_files.publish`:
write and close a dot-prefixed temporary file beside the target, then rename.
Existence and marker globs therefore expose only complete snapshots. Journal
appends serialize replacement snapshots with a separate lock, preserving every
record even if runner generations overlap. Empty control markers carry no payload;
streaming logs are read for content, never treated as complete on existence.
The C++ idle peer uses flushed newline events, not file receipts. The native
BLOCKED probe already publishes its JSON with `QSaveFile::commit()`.
`fixture-publication` pauses the actual generated idle runner after open and
before write, observes target absence, then verifies exact release bytes. It
also checks journal replacement visibility and failed-publication preservation.

## Real-session integration drivers

The optional [KVM CI platform](vm/README.md) rebuilds a pinned official Arch
cloud image and runs one nested niri real-PAM unlock group on demand. It uses
configurable QEMU/SSH identities and the owned manifest below, not Ark-specific
settings. The manual `VM niri smoke` workflow leaves existing PR checks alone;
it is not full acceptance, IME or multi-output proof.

Stock wtype 0.4 writes its keymap to a hardcoded shared temporary path, ignoring
`TMPDIR`. For disk-backed-only guest testing, put a workspace-local build with
the TMPDIR-only tempfile patch first in `PATH`; preserve its source, patch and
binary hash with the operator evidence. No system wtype replacement is needed.

`run-remediation.sh` (→ `remediation-driver.py`), `launch-integration.py` and
`nested-launch-integration.py` run real locks with real PAM inside owned nested
compositors (or a recorded native DRM session) of a disposable test VM. They
never run on a desktop session. Every run takes `--root <workspace>` and
`--vm-record <manifest>`.

Workspace layout under the root: `src/aerial-lock` (the tree under test),
`build/debug/aerial-tests` (built exactly as in "Native tests" above), `stage`
(`make -C "$ROOT/src/aerial-lock" install DESTDIR="$ROOT/stage"`), `run`,
`results`, `media`. `ROOT` below names that disk-backed guest workspace and
`VM_RECORD` its operator-produced manifest.

```sh
bash Tests/run-remediation.sh --root "$ROOT" --lane debug --vm-record "$VM_RECORD" \
  --backend niri --case unlock --cycles 50
```

Cases: `cold-load-and-media-fallback`, `prompt-state`, `negative-auth` (run it
once; it counts against the account's lockout), `unlock`, `early-unlock`,
`screenshot-overlap`, `connection-loss` (`--exit-budget-ms 250`),
`sampling-scale` (`--resolutions 240,1080,2160 --transitions 100
--sample-seconds 5`), `sampling-soak`, `panel-failover` (Hyprland,
`--outputs 3 --hyprland-virtual-outputs`) and, on `--backend niri-drm`,
`desktop-input-control` and `panel-zero-outputs`.

The Hyprland virtual-output lane uses stock render-backed headless outputs; it
proves lock behavior on virtual outputs, not physical-display presentation.
`virtual-pointer-probe` uses the version-2 wlr protocol in `virtual-pointer.xml`
(upstream MIT notice retained). Native input is distinct from Qt synthetic
events and from real platform IME composition. Passwords reach the locker only
through stdin of the input tool; protocol traffic is filtered before anything
is written to disk.

Stock Fcitx visible-echo candidate popups and nested Hyprland synthetic input
remain limitations (README). A portable native-input probe exercises injected
Qt IME events, not a real platform candidate popup. Do not conflate those results.

A failed locker that is still running is never killed: the driver writes a
`run/BLOCKED.json` / `run/blocked-qs-*.json` latch, keeps its compositor and
refuses later runs until someone inspects it. `--recover-owned-failures` allows
recovery only when the manifest also carries a matching
`recovery_authorization`; a failed attempt stays failed.

`connection-loss` runs the guard cases through a byte-preserving relay that
cuts only the locker's first Wayland connection: blocked-GUI, logger,
video, retirement and exit-trap cases must exit 70 within 250 ms; arming and
monitor fault injections exit 72 and 71 respectively. The supervisor's
`supervisor-failures` regression checks pre-engagement, monitor and mixed
attempt outcomes and every pre-exec runner failure (exit 72): exhausted guard
failures exit 3 and never claim a screen state the supervisor cannot know. All fault hooks live in test-only
preload libraries; the product has no fault or debug-exit setting.

`sampling-scale` checks the original budgets (RSS ≤ 1 GiB, FDs < 75 % of the
limit, MemAvailable ≥ 25 %), that the private frame directory never holds more
files than there are outputs, and that the shared `/tmp` is unchanged.

`launch-integration.py --root "$ROOT" --vm-record "$VM_RECORD"` checks the
staged installed supervisor, same-PID runner, real-PAM unlock and failed starts
(missing runner/plugin, arming and monitor faults). A duplicate supervisor for
the same session runs beside each on a private bus: beside the real lock it
must report waiting while engaging and exit 0 only after the locker's
confirmation; beside each failed start it must exit 4. `--stage` can select a second
install prefix inside the workspace. `nested-launch-integration.py` checks the
development template with `--backend niri --vm-key` (emulated guest keyboard)
or `--backend hyprland --ipc-control` (compositor spawn path, not native keys).

`surface-input-integration.py --root "$ROOT" --vm-record "$VM_RECORD" --case <case>`
requires a recorded native niri DRM session with two mapped outputs. Niri's
nested winit backend has only one output and cannot reproduce output hotplug.
Run each of `non-panel`, `hotplug`, `fast` and `compose`. These cover ordinary
non-panel input, output removal/re-addition, native typing across at least ten
focus transfers and stock Fcitx inline dead-key composition at focus-out. The
inline engine avoids the documented unsupported CJK candidate-popup path. Each case observes
the pinned panel's exact synthetic draft before stopping diagnostics, then real
PAM unlock on the non-panel surface, natural exit 0 and desktop input. The QML surface
suite covers selection, masking, consume/clear, busy/limit/tuning behavior,
immediate first-key authority, gesture cancellation, rapid focus roundtrips and
both theme-handoff orders; the native-input probe adds two real QQuickWindows
and the native commit-before-focus-in ordering, including a key delivered
before native focus-in. `surface-input-filter` separately checks scope, FIFO,
exactly-once replay, key metadata, the 64-event/250-ms bounds, count-only logging,
destruction cleanup and non-interception of input-method events. Real platform composition and
fast cross-output input are exercised in the real-session driver.

`native-authority-natural`, `native-authority-empty-state`,
`native-authority-same-window` and `native-authority-focus-out` exercise the
actual editor-to-shared-draft path
with two QQuickWindows. They cover normal focus-out commit, the empty residual
composing state from the stock Mozc type/Escape reduction, immediate incoming
authority and exactly-once first-key publication, and same-window replacement
waiting for natural completion. The focus-out case observes current activity
before an incoming editor exists, while leaving Qt's residual flag untouched.
These are public Qt event surrogates, not real
platform-IME proof.

`native-input-sink-panel` injects public `QInputMethodEvent` preedit/commit
events into the actual editor. It attaches the panel to a composing sink's
native window and checks retained native focus, writer/generation and preedit,
continued composition, no submission on composing Return, one commit revision,
then panel authority. The QML suites check fallback/theme declarative focus,
pending-focus cancellation, writer retirement and commit-with-new-preedit ordering. These are synthetic Qt
editor-contract checks. Real Fcitx/Mozc sink-to-panel composition is the IME
experiment's M3 regression after rebasing onto this fix; master cannot provide
that proof while its stock candidate-popup limitation remains.

The staged launch driver also cold-loads a real locker for each D-Bus
`Release()` and documented `pkill -USR1 -f '^aerial-lock-supervisor'` route:
it observes `unlock_and_destroy`, supervisor exit 0 without respawn and desktop
input afterwards. `lock-release-shell.qml` adds a real QML lock with a
synthetic in-flight PAM endpoint: release must abort PAM before synchronous
completion, clear pending state, emit `unlocked()` once and destroy the Loader
tree through the normal path. This is the Lock.release() QML regression;
offscreen shims do not substitute for the real session-lock lifecycle.

The same staged driver exercises five-second inactivity locking and a real
visible-surface idle inhibitor: no lock for 15 seconds while inhibited, then
lock within 10 seconds after the probe exits. Each leg uses an independent
owned compositor cycle, observes real lock confirmation, authenticates through
PAM, observes clean exit and checks desktop input afterwards. These nested
checks do not replace real-session service restart, logind or suspend checks
in the disposable guest's own graphical session.

Prepare authentication fixtures only in the disposable guest. Save original
credentials root-only before changing them, pass fixture secrets through stdin,
and restore the original credentials byte-for-byte afterwards. Do not persist
passwords or unfiltered protocol traces in evidence. Capture full screenshots
from the nested compositor into the guest's disk-backed artifact directory.

## Owned manifest

A mode-0600 JSON file under `<root>/run/`, owned by the caller, written by the
operator who prepared the VM. `owned_instance.py` validates it before any
launch or control and refuses every mismatch; there is no fallback.

| # | Field | Content |
|---|---|---|
| 1 | `version` | `1` |
| 2 | `host`, `domain`, `base_domain` | VM host, the disposable test domain, and the prepared base it was cloned from (which is refused as a target) |
| 3 | `uuid`, `mac` | Domain UUID and MAC; the MAC must be present in the guest |
| 4 | `host_identity` | `{uuid, mac}` as observed from the host's hypervisor |
| 5 | `guest` | `{user, uid, boot_id}` of the current boot |
| 6 | `paths` | Absolute `root`, `source`, `build`, `stage`, `run`, `results`, `media`, all inside `root`, caller-owned, no symlinks |
| 7 | `ownership` | `{task_created: [root, …], pre_existing: []}` |
| 8 | `source_freeze_identity` | SHA-256 identifying the delivered source snapshot |
| 9 | `source_files` | `{relative path: {sha256, mode}}` of the tree under `paths.source` |
| 10 | `current_source_identity` | SHA-256 of `json.dumps(source_files, sort_keys=True)` |
| 11 | `binary_hashes`, `binary_source_identity` | SHA-256 of each built/staged binary the drivers execute, and the source identity they were built from |
| 12 | `session` | The outer compositor: `process {pid, uid, starttime, executable}`, `backend`, `runtime`, `wayland` and `ipc` listener identities (from `owned_instance.session_identity`) |
| 13 | `environment` | `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`, `NIRI_SOCKET` of that session |
| 14 | `initial_outputs` | Mapped output names at preparation time |
| 15 | `pam_fixture` | `{prepared, uuid, uid, operator_session, service, secret_file}`; the service file is exactly `auth required pam_unix.so`, the secret is `<run>/pam-fixture-secret` (0600) |
| 16 | `recovery_authorization` | `null`, or `{uuid, mode: "recover-owned-failures", user_approval_reference}` |

Any source transfer invalidates the binary hashes: rebuild and re-record them
before a product run.
