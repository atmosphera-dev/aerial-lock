# aerial-lock

Wayland session-lock client: PAM authentication over a looping video
background (local files or Apple/community packs) with a solid colour
until the first frame, layered JSON config and i18n. Planned future work
is listed under [Roadmap](#roadmap).

Targets `ext-session-lock-v1`. Tested on Niri and Hyprland — see
[Compositor support](#compositor-support) for coverage and limitations.
Not compatible with GNOME or KDE Plasma (their compositors do not expose
`ext-session-lock-v1` to third-party lockers).

## Installation

Build and install this tree:

```
sudo make install
```

Runtime requirements: [Quickshell 0.3.x](https://github.com/outfoxxed/quickshell)
(not tied to a specific fork),
[qt6-mpvqml ≥ 0.2.0](https://github.com/alexindigo/mpvqml), which plays the video
background, and Qt6's `Qt5Compat.GraphicalEffects` module (`qt6-5compat` on
Arch) for the frosted-clock effect and avatar masking, Qt6 Core/Gui/DBus
(`qt6-base`), PAM, `wayland`, `libxkbcommon` and keyboard/Compose data
(`xkeyboard-config`). The guarded runner also requires Bash;
the optional signal-unlock examples below use `pkill` (`procps-ng` on Arch).
With an empty
`sources` list the player is never started and the screen stays
`backgroundColor`; with sources it shows `backgroundColor` until the first
frame decodes. The headless `aerial` content service also requires
Qt6 Network, a session D-Bus provider, `libarchive` and FFmpeg libraries.
`aerialctl` and the compatible `aerial-lock-fetch` command use that service;
they do not require curl, Python or tar. Python remains test tooling.
The Aerial settings app requires Qt >= 6.12.0, including Widgets (`qt6-base`),
Quick, Controls and Shapes (`qt6-declarative`), and SVG (`qt6-svg`). Its private
InterActive runtime is built offline from the complete pinned source in this tree;
no system InterActive package or global import-path setting is needed.
Its choosers and links use the desktop's
`xdg-desktop-portal` when one is present; nothing else needs it.

Building requires a C/C++ compiler, Make, CMake, Ninja, pkg-config, Qt6
Core/Gui/DBus/Quick development files, PAM headers, wayland-client and
wayland-protocols (including `wayland-scanner`). On Arch these are provided by
`base-devel`, `cmake`, `ninja`, `qt6-base`,
`qt6-declarative`, `pam`, `wayland`,
`wayland-protocols`, `libxkbcommon`, `libarchive` and `ffmpeg`; Git is needed
to build from a clone. Arch's `qdbusxml2cpp` is supplied by `qt6-base`.
The full build also needs `qt6-svg` and `qt6-shadertools` >= 6.12.0.
The full Arch package includes the settings app and needs these Qt dependencies;
its release recipe must be updated before packaging this integration.
For an explicit core-only build on Qt 6.11, use `make BUILD_SETTINGS=0` and
`make BUILD_SETTINGS=0 install`. This excludes the settings executable, private
InterActive runtime, desktop activation, icons and tray unit, not the content
service, CLI or idle helper. The default build never silently drops settings.

With the default `PREFIX=/usr`, installation includes `aerial-lock`,
`aerial-lock-supervisor`, `aerial-unlock`, `aerial`, `aerialctl`, `aerial-lock-fetch` and
`aerial-idle` in `/usr/bin`, the shared guarded runner, `aerial-lock-auth`,
`aerial-lock-emergency` and the isolated media probe under
`/usr/libexec/aerial-lock`, its
private plugin under `/usr/lib/aerial-lock/qt-plugins`, the QML/config/theme
bundle under `/usr/share/aerial-lock`, and `/etc/pam.d/aerial-lock`. It also
installs `/usr/lib/systemd/user/aerial-idle.service` and
`/usr/share/aerial-lock/examples/idle.json`. The service is **not enabled
automatically**. The separate content service is enabled for future
user-manager startup through a vendor wants link. Installation/upgrade does
not start or restart it in running sessions. The Aerial settings app adds
`/usr/bin/aerial-settings`, its desktop entry and D-Bus activation file, icons
under `hicolor/scalable`, theme previews and `aerial-tray.service`, which is
also **not enabled**. `PREFIX` and `DESTDIR` select
the layout; Arch packaging uses `LIBEXECDIR=/usr/lib/aerial-lock`.
Settings libraries and QML modules live under
`/usr/lib/aerial-lock/interactive/{lib,qml}`. `INTERACTIVE_ROOT` overrides that
private root; `BINDIR` and `PREFIX` overrides retain relative lookup and RUNPATH.
InterActive licenses are installed under
`share/licenses/aerial-lock/interactive` relative to `PREFIX`.

### First use and automatic content

With no user config, Apple TV is the inherited video source. The content service
starts at your next login/user-manager startup, downloads the smallest eligible
clip first, then fills a 10 GB managed cache. Defaults: up to 1080p, no known
metered connections, other connection types allowed, both AC and battery allowed,
hourly checks and weekly incremental replacement. Every choice is configurable.
The cache cap is not a monthly traffic allowance: filling and later rotation
consume network traffic.

To start now, choose either:

```sh
systemctl --user start aerial
aerialctl fetch --limit 1 --wait
```

A retained/lingering user manager might not restart at the next graphical login.
D-Bus activation is a backup when the locker or CLI needs the service. The
service does not require aerial-idle, a tray, Wayland or a graphical session.
See [setup](docs/setup.md) for masks and systemd-less use.

Until a clip completes, locks show solid colour. The locker never downloads or
waits for the service: it freezes local content for each lock episode, including
respawns. New clips play from the next lock. Password authentication and emergency
recovery remain independent of content acquisition.

Existing explicit `sources` arrays, including `[]`, replace the inherited source
unchanged. A valid partial config with no `sources` or legacy video keys inherits
Apple; malformed/unreadable config or an invalid source list does not authorize
Apple downloads. Existing older Apple URLs are not rewritten.

```sh
aerialctl status --json
aerialctl config show --effective
aerialctl config set acquisition.automatic.enabled false  # pause automatic work
aerialctl config set sources '[]'                         # no video sources
aerialctl config set acquisition.automatic.allowMetered true
aerialctl config set acquisition.automatic.power.battery false
aerialctl config set acquisition.automatic.allowedResolutions '[1080,2160]'
aerialctl config set cache.maxBytes 10000000000
```

Manual `aerialctl fetch` always attempts work despite automatic pause, metering,
connection, power or automatic-quality settings. It still honors configured
sources, hard source resolution bands, cache/disk limits, TLS and media validation;
offline transport can fail. `systemctl --user mask aerial.service`
prevents service startup/activation; unlike pause, it also prevents manual fetch.

### The Aerial settings app

Launch **Aerial** from your application launcher, or run `aerial-settings`.
Starting it again raises the open window. **Overview** shows service status,
ready videos, storage and recent activity. **Sources** manages video sources;
**Downloads** controls checks, quality and storage; **Lock screen** controls
themes and locking; **About** includes the optional tray and service actions.
The window follows the desktop light/dark appearance and accent live where
the settings portal provides them, with the Qt palette as a fallback.
It is a client of the `aerial` service:
the service writes every setting and does every download, so closing the window
(or the app crashing) never stops downloads, aerial-idle or a running lock.

- **Changes save at once.** There is no Apply button. If another client
  (`aerialctl` or another API client) changed the settings first, the control
  returns to the service's value and shows a conflict instead of overwriting.
  Edits from `aerialctl` appear in the open window.
- **Sources can be switched off.** A switched-off source keeps its entry, cache
  and partial downloads, is not downloaded or refreshed, and is skipped from the
  next lock on; a lock that is already showing keeps its videos. With every
  source off, or none, locks show solid colour.
- **Service problems.** If `aerial` is unavailable, use Try again, or Start
  service when a user manager is available. A masked service must first be
  unmasked manually. If the settings file needs repair, the window offers Open
  folder and Try again; it never rewrites the broken file.
- **Folder and file choosers, Open folder and links** use the desktop's
  `xdg-desktop-portal`. Without a portal those actions report an error; local
  sources can still be added with `aerialctl config set sources ...`.
- **Tray icon (optional, off by default).** About > Show tray icon shows a tray
  icon with download status, Fetch now/Stop download, Pause/Resume and Open
  settings, and enables `aerial-tray.service` for later graphical logins. Hide
  tray icon in its menu turns that off again. The icon needs a
  StatusNotifierItem tray (Noctalia, Waybar, KDE and similar). Without one, the
  switch is hidden and About says no tray was found; an enabled preference is
  kept until a tray returns. Without systemd, start `aerial-settings --tray`
  from your compositor's autostart; the app cannot make that persistent itself.

### Apple footage and credits

Apple's aerial videos are produced by Apple. aerial-lock downloads them directly
from Apple's content delivery network onto your computer. We do not host, mirror
or redistribute them. Apple, macOS, tvOS, Apple TV and Mac are trademarks of Apple
Inc. aerial-lock is not affiliated with Apple.

This direct-download approach and disclosure are adapted from
[Aerial](https://aerialscreensaver.github.io/legal), originally started by John
Coates and maintained by Guillaume Louel and contributors. As upstream states:
"It does not host, mirror or redistribute them." No videos are included in this
repository or package. The software license does not grant rights to Apple's
footage or to separately configured community media. See
[third-party notices](docs/third-party-notices.md) for the upstream software notice.

### Build-time PAM_MAX_RESP_SIZE override

By default, aerial-lock derives `PAM_MAX_RESP_SIZE` from the system PAM headers
at build time. To force a specific value (cross-compilation, hardened distros):

```
make PAM_MAX_RESP_SIZE=1024
```

## Usage

Type and submit on any locked output; all surfaces share one draft and the panel stays pinned to its selected output.

The installed supervisor launches the shared guarded runner on every attempt,
including solid-background/no-video locks. Its private Qt generic plugin watches
the actual locker Wayland connection independently of GUI/media teardown. A
confirmed transport loss exits abnormally (70); guard monitor failure exits 71
(engagement unknown), while guard startup failure exits 72 before engagement.
None of these paths unlocks. Normal PAM unlock still flushes the unlock request
and exits 0; the supervisor retains its bounded respawn/no-automatic-unlock policy.

See the [setup guide](docs/setup.md) for compositor keybinds (niri, Hyprland
and sway), idle-policy ownership, and Atmosphera, Noctalia v5 and Omarchy setup.

One supervisor protects each Wayland session. Running `aerial-lock` again for
a session that is already being locked (a keybinding and an idle daemon, say)
starts no second locker: the new invocation asks the supervisor on duty for its
actual lock state and exits 0 only once that supervisor's current locker has
reported the compositor's lock confirmation. It waits up to 10 seconds while
the lock is still being engaged and otherwise exits 4 (see
[Recovery](#recovery-last-resort)).

### Idle, suspend and session integration

`aerial-idle` is the companion idle/session daemon. It locks on configured
inactivity, before logind-managed suspend/hibernate (including lid-close sleep),
and on `loginctl lock-session`; it forwards `loginctl unlock-session` to the
supervisor. Enable it from your graphical session:

```sh
systemctl --user enable --now aerial-idle.service
```

The user manager must have the session's `WAYLAND_DISPLAY` and
`XDG_RUNTIME_DIR` environment (and `HYPRLAND_INSTANCE_SIGNATURE` for focused-output
selection on Hyprland). The unit belongs to `graphical-session.target` and requires
`WAYLAND_DISPLAY`. With no user config, suspend and logind lock/unlock handling
are **on**, but there is **no idle timeout**:

| Key | Default | Meaning |
|---|---|---|
| `idle.timeoutSeconds` | `0` | Nonnegative integer seconds; `0` disables inactivity locking. |
| `idle.respectInhibitors` | `true` | Wayland idle inhibitors postpone the inactivity timeout. |
| `idle.lockOnSuspend` | `true` | Hold a sleep-delay inhibitor until lock confirmation, bounded by logind's delay limit minus 500 ms. |
| `idle.logindLockUnlock` | `true` | Forward logind `Lock` and `Unlock` events for the graphical session. |

For a five-minute timeout, copy the installed example **if you have no config
yet**:

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/aerial-lock"
cp -n /usr/share/aerial-lock/examples/idle.json "${XDG_CONFIG_HOME:-$HOME/.config}/aerial-lock/config.json"
```

If a config already exists, merge the example's `idle` object into it instead
of replacing the file. The example is never read automatically. The daemon
reads the same layered config once at startup, without rewriting it; apply
edits with `systemctl --user restart aerial-idle.service`. Inspect the effective
idle settings with `aerial-idle --print-config`. See the
[configuration reference](docs/config.reference.md#idle-object) for validation
and protocol fallback behavior.

Each lock runs independently: in its own transient user unit when the systemd
user manager is reachable, otherwise as a detached process. Restarting or
stopping aerial-idle leaves a running lock intact. Locks started by keybinds or
other callers are discovered through the same session-scoped supervisor;
aerial-idle does not need to have started them.

### Alternatives without aerial-idle

Use one idle/session policy owner. With swayidle or hypridle handling these
events, leave aerial-idle disabled. For shell-specific locker settings and
which automatic lock paths to disable, see the [setup guide](docs/setup.md).

For **swayidle** built with logind support:

```sh
swayidle -w \
    timeout 300 'aerial-lock' \
    before-sleep 'aerial-lock' \
    lock 'aerial-lock' \
    unlock "pkill -USR1 -f '^aerial-lock-supervisor'"
```

`aerial-lock` stays in the foreground for the lock's lifetime, so swayidle's
`-w` before-sleep command waits until unlock or logind's maximum delay expires;
it is not a lock-readiness-only wait. aerial-idle instead releases its delay
as soon as the supervisor confirms the lock.

For **hypridle**:

```conf
general {
    lock_cmd = aerial-lock
    unlock_cmd = pkill -USR1 -f '^aerial-lock-supervisor'
    before_sleep_cmd = loginctl lock-session
    inhibit_sleep = 3   # wait until locked; needs hyprland-lock-notify-v1 (Hyprland)
}

listener {
    timeout = 300
    on-timeout = loginctl lock-session
}
```

Both examples wire logind's unlock event explicitly. `pkill -f` matches the
command line: `pkill -x aerial-lock-supervisor` cannot match the kernel's
15-character process-name limit. The expression above targets supervisors
for all this user's Wayland sessions; the session-scoped D-Bus `Release()`
method can target one session (see [Recovery](#recovery-last-resort)).

### Systemd-less sessions

Start `aerial-idle` from compositor autostart or your user service manager
with the Wayland/session-bus environment. For example, Niri accepts:

```kdl
spawn-at-startup "aerial-idle"
```

Without a systemd user manager, locks use the detached launch path. elogind
exposes the same login1 API for suspend and lock/unlock events, but **a real
elogind setup is unverified**. With no logind/elogind, aerial-idle warns and
only inactivity locking is available; use your distribution's sleep hooks to
run `aerial-lock` in the graphical user's session before sleep. Those hooks
must supply that session's environment and coordinate sleep with confirmed
locking; a root hook with no Wayland/session-bus environment is insufficient.

### Development

Enable the pre-commit hook with `git config core.hooksPath .githooks`; it needs
Podman and a user systemd session, and builds a resource-limited Arch lint image
automatically when `.devcontainer/` changes. GitHub CI builds the same container
and runs the same QML lint offline with the repository mounted read-only;
the hook additionally checks staged whitespace.

Run from the repo during development:

```
make DEBUG_DISMISS=0 DEBUG_VERBOSE=0
bash Scripts/dev/run.sh
```

`AERIAL_LOCK_SHELL` selects the shell directory for `aerial-lock` and the bundled
`Config/defaults.json` read by `aerial-idle`. Set it to the source tree for both
processes when testing a development bundle. Unset or empty, it uses the compiled
`DATADIR` (normally `/usr/share/aerial-lock`). Paths are made absolute and cleaned
at startup; a relative override is resolved against the caller's working
directory. A nonempty override is passed as that absolute path to locks started
by aerial-idle, including transient systemd units and detached processes.

Before `make install` is run, `/etc/pam.d/aerial-lock` won't exist yet.
Use the env-var override to test with your system's login PAM stack:

```
AERIAL_LOCK_PAM_SERVICE=login bash Scripts/dev/run.sh
```

### Testing

Use a disposable test VM for builds, automated tests and nested-compositor
experiments. See [`Tests/README.md`](Tests/README.md) for the portable suites and
integration drivers, and [`docs/nested-testing.md`](docs/nested-testing.md) for
the interactive workflow. Inside that VM, a development build can be launched
with the debug flags:

```
make dev
```

`make dev` builds with `DEBUG_DISMISS=1` (a "Dismiss (debug)" button that
unlocks without authentication) and `DEBUG_VERBOSE=1`, then launches from the
repo. Both flags land in the build-generated, root-owned
`Config/build-flags.json` — a dev build prints a startup warning when the
bypass is enabled. Release builds (`make`) always set both flags `false`;
**user configuration cannot enable the bypass.** For a chatty build without
the bypass: `make DEBUG_VERBOSE=1`.

### Lock-state logs

Operational transitions are logged at info level, even without
`AERIAL_LOCK_VERBOSE=1`. Filter the launcher's captured output with
`grep 'lock-state:'`, or use `journalctl --user -g 'lock-state:'` for locks
started by the user manager. Locker lines show `old -> new` and a fixed reason:
lock request, compositor confirmation, authentication, unlock request, release
and exit. A watchdog timeout is `auth ... -> error reason=timed-out` in the
existing auth model. Supervisor lines include spawn/retry attempt numbers,
state changes, release source (`D-Bus` or `SIGUSR1`) and child exit codes.
The locker reports the shared channel as `external-release`; the supervisor
identifies which external caller route requested it.

State logs never include passwords, response lengths, prompt answers or key
events. Verbose logging is separate and retains the existing PAM-stack message
diagnostics. The supervisor forwards locker stdout and stderr directly to its
own sinks; it does not retain an unread log buffer during long locks.

Release completion reads the native lock state as well as observing its
notifications, including on Quickshell 0.3.2. The exit backstop starts with the
unlock request. Confirmed release exits normally; a release still unconfirmed
at the deadline exits abnormally so the supervisor can retry, never as success.

## Configuration

Bundled defaults are read without creating a full user config. Sparse overrides
live at `$XDG_CONFIG_HOME/aerial-lock/config.json` (normally `~/.config/...`).
The companion alone writes application settings, through revisioned CLI/D-Bus
transactions; the locker and idle daemon only read them.
See [`docs/config.reference.md`](docs/config.reference.md) for available options.

Override individual translated strings in
`$XDG_CONFIG_HOME/aerial-lock/i18n/<lang>.json` (default `~/.config/...`), where
`<lang>` is the configured `language`. Missing keys fall back to the bundled
language, then English. See the [i18n reference](docs/config.reference.md#i18n).

### Themes

The lock surface is a full-surface QML theme, selected by filename with the
`theme` key (default `"Macintosh.qml"`). Five ship in the bundle —
Macintosh, Windows7, MaterialYou, Enfield, Clockwork — and you can drop
your own into `~/.config/aerial-lock/themes/` (user files win over
same-named bundled ones). Enfield prefers mountain videos; the other themes
play your configured pool as-is. The authoring contract, the shared
`LockContext` object, and the topic-matching semantics are documented in
[`docs/theming.md`](docs/theming.md).

### Lock-screen power controls

Windows7, MaterialYou, Enfield and Clockwork show only power actions the system
allows without additional authorization. Restart and shut down ask "Are you
sure?" inline, with Cancel focused first and no password gate. Suspend acts at
once; custom themes can also offer hibernate when the device supports it.
Application blockers and failures appear on screen. Actions never unlock the
session, bypass inhibitors or open a polkit prompt.

Set `"powerActions": { "enabled": false }` to hide all power controls. Existing
configs inherit `true`; nothing needs migration. User/session switching is not
implemented, so those controls are not drawn. See the
[configuration reference](docs/config.reference.md#poweractions-object) and
[theme API](docs/theming.md#contextpower).

### Video backgrounds

The `sources` list drives the background — local files and folders plus
Apple Aerial and community packs, played in list order as one looping
playlist:

```json
"sources": [
  { "type": "apple",  "name": "Apple TV", "source": "https://sylvan.apple.com/Aerials/resources-16.tar" },
  { "type": "folder", "name": "Personal", "source": "~/Videos/locker" }
]
```

The content service automatically acquires configured remote packs. To fetch
manually and wait for a verified clip:

```
aerialctl fetch --limit 1 --wait
aerial-lock-fetch --limit 1     # compatible frontend, same per-source limit
```

New clips live under `~/.cache/aerial-lock/managed-v1/`, with validated immutable
objects and local indexes. Compatible old name-based caches remain readable,
but are not adopted, counted against the managed cap or pruned. Personal files
and folders remain unmanaged. Source min/max are hard limits; the optional
`preferredResolution` selects within that band and, for automatic work, within
the allowed heights. A secure lock's display hint can save an absent preference;
an existing value is never overwritten. See the
[configuration reference](docs/config.reference.md) for source IDs, policy,
resolution, cache accounting and legacy migration.

### Environment overrides

| Variable                    | Overrides      | Purpose                        |
|-----------------------------|----------------|--------------------------------|
| `AERIAL_LOCK_PAM_SERVICE`   | `pamService`   | Dev testing without installing PAM file |
| `AERIAL_LOCK_SHELL` | Shell/bundle directory | Shared by the locker, idle, companion and CLI; unset or empty uses compiled `DATADIR`. Custom profiles require a matching companion on a private bus. |

## Compositor support

Two separate questions: does the compositor implement
`ext-session-lock-v1`, and has aerial-lock been run on it?

| Compositor       | Protocol  | aerial-lock |
|------------------|-----------|-------------|
| Niri             | Yes       | Tested      |
| Hyprland         | Yes       | Tested      |
| Sway >= 1.9      | Yes       | Untested    |
| Cosmic           | Yes       | Untested    |
| river / Wayfire  | Probably  | Untested    |
| KDE KWin / Plasma| No        | —           |
| GNOME / Mutter   | No        | —           |

On niri the password panel appears on the default monitor (the first usable
lock surface), with no compositor library required. On Hyprland it uses the
focused output when that name matches a usable lock surface
(`HYPRLAND_INSTANCE_SIGNATURE`), otherwise the first usable lock surface.
The selection stays pinned while that surface exists. If it is
removed, the same theme and editor move to a surviving output without clearing
the typed text or selection and without restarting PAM; if no lock surface is
left, the editor waits until one returns. Hyprland's own `FALLBACK` output is a
real lock surface and receives the editor.

Recovery from a crashed locker is compositor **policy**, not a protocol
guarantee: `ext-session-lock-v1` says a compositor *may* let a new client
take over a dead lock. Niri does; other compositors still need verifying —
see the per-compositor notes under [Recovery](#recovery-last-resort).

## Recovery (last resort)

The supervisor (`aerial-lock-supervisor`, what `aerial-lock` actually execs)
owns the locker lifecycle: it retries the QML locker within three total
attempts, then switches to its native emergency password locker. A known
startup failure (FailedToStart or exit 72/126/127) switches immediately.
Emergency process crashes get their own three-attempt budget; wrong passwords
do not count. It never returns to Qt during that episode. If emergency recovery
also fails, use a TTY. Crash recovery never unlocks automatically. Normal interactive
unlock goes through PAM. An explicit D-Bus `Release()` call on
`org.aeriallock.Supervisor` at `/org/aeriallock/Supervisor` (on its
session-scoped `org.aeriallock.Supervisor.s<hash>` bus name), or SIGUSR1 to
the supervisor on duty, releases its locker gracefully without PAM:

```sh
pkill -USR1 -f '^aerial-lock-supervisor'
```

Any caller on the user's session bus may call `Release()`, and any process
running as the user can request this external unlock, as with
swaylock/hyprlock. A release requested while the locker is still engaging is
delivered once it confirms the lock. Release disables further respawns;
the locker takes its normal unlock/teardown path and exits 0.

Killing the locker or supervisor (SIGTERM, `pkill`, `systemctl --user stop`) never unlocks.
The compositor keeps the session locked; the supervisor retries a killed locker within
its attempt limit. Killing the supervisor leaves the lock without respawns. Use SIGUSR1
or `Release()` on the supervisor to unlock without a password, as with swaylock and hyprlock.

**Emergency UI.** Every output shows only a centered input box, masking dots
and a geometric arrow (left in RTL locales). Return submits a nonempty draft;
Backspace removes one Unicode scalar; Ctrl-U/Escape clears. Checking pauses typing and pulses the
arrow; rejection clears the dots and briefly flashes the border red. The client
uses the configured PAM service through the same exec helper, not a weaker
fallback stack. XKB layouts, AltGr and Compose work; platform IME, clipboard,
visible/extra PAM prompts and screen-reader support are not provided. Unsupported
PAM interactions retain the lock and require TTY recovery.

The emergency binary uses only libc, Wayland and XKB, not Qt, mpv, PAM libraries,
GL or fonts. It is always installed beside the helper; `LIBEXECDIR=/usr/lib/aerial-lock`
relocates both. Secret memory is wiped and excluded from dumps; if mlock fails,
recovery remains usable with a warning and residual swap exposure. Missing helper
or PAM policy leaves the emergency lock engaged with nontext error feedback.

**Automatic.** Locker exit `0` retains its existing clean unlock / PAM refusal /
external invalidation meaning. Explicit Release prevents retries or escalation.
Compositor death is checked first. Emergency `locked` confirms protection, not
that its first frame has appeared; phase and surface logs distinguish those.
An unconfirmed normal attempt was never proof that the desktop was protected.
If all recovery fails, inspect the outcome before assuming protection:

| Supervisor exit | Meaning |
|---|---|
| 0 | Clean completion or compositor gone; for the supervisor on duty this is not a lock-readiness signal. A duplicate invocation for a session that is already protected exits 0 only after the supervisor on duty confirmed that its current locker holds the lock. |
| 1 | The supervisor could not start: the session to protect, the session bus or its per-session name is unavailable. Nothing was launched. |
| 2 | Other abnormal failures exhausted retries, or the locker exited abnormally after an explicit release request (without respawn); no automatic recovery unlock. After release the screen state is **unknown**. Otherwise the log says the session stays locked only if an attempt confirmed the lock; without confirmation the current screen state is **unknown**. Recover manually below. |
| 3 | Emergency refused/unavailable, or ultimate failure after guard/startup failure. Logs record phase and whether any attempt confirmed protection. Without confirmation the current screen state is **unknown** and may be unlocked; a previous episode's dead lock may also remain. Check the screen and recover from a TTY if needed. |
| 4 | Duplicate invocation, lock not confirmed: another supervisor is on duty for this session, but it did not report a confirmed lock within 10 seconds (still engaging, failing or gone), or the name's owner gave no valid state. The screen **may be unlocked**; this invocation launched nothing. |

**From a TTY (or SSH), as the same user — manual recovery / diagnostics:**

```
export XDG_RUNTIME_DIR=/run/user/$(id -u)
# find the stuck session's socket — usually the older wayland-N
ls -la $XDG_RUNTIME_DIR/wayland-*
WAYLAND_DISPLAY=wayland-N aerial-unlock
```

Then switch back to that session's TTY. If takeover is refused and this
session's supervisor confirms its child holds the lock, the tool reports the
child PID. Prefer the supervisor's graceful SIGUSR1 release above. If the
locker cannot handle release, use:

```
WAYLAND_DISPLAY=wayland-N aerial-unlock --purge
```

This stops only the confirmed locker for that socket, suppresses supervisor
respawns, and tries to take over and release the compositor lock. If the
supervisor is unavailable or cannot confirm ownership (including older or
third-party lockers), it will not guess which process to stop.

| Exit | Meaning |
|---|---|
| 0 | recovered — lock taken over and released |
| 1 | no Wayland socket / environment (guidance printed) |
| 2 | inconclusive — compositor didn't answer within 5 s |
| 3 | takeover refused (PID + `--purge` hint only for confirmed Aerial holder) |

**Compositor notes.** Dead-locker recovery is compositor policy:

**Hyprland visual limitation:** the owned 0.57.2 virtual-output acceptance run
captured desktop pixels during crash replacement, despite no lock-time desktop
input and no unlock request. The emergency prompt does not eliminate this
compositor flash. Do not equate successful takeover with continuous visual
privacy; sampled captures also cannot exclude flashes between their samples.

- **Niri / Sway (wlroots)** — takeover of a dead lock is unconditional; a
  respawned locker replaces the dead one with no flag. On Niri, a dead locker
  shows a dark-maroon screen (that's the compositor's clientless-lock colour,
  not aerial-lock).
- **Hyprland ≥ 0.56.1** — a dead locker shows the "lockdead" fallback. A
  respawned locker is refused without the client-side flag
  `misc { allow_session_lock_restore = true }`; the supervisor sets it
  just-in-time on Hyprland, respawns, and unsets it when the episode ends.
  (Arch's legacy-config-manager build has no `hl.clear_crashed_lockscreen()`,
  so the flag path is the only one that works here.)
- **Hyprland < 0.56.1** — same flag path; no other command exists.
- **GNOME** — the protocol isn't exposed to third-party clients; not
  applicable.
- **KDE Plasma (KWin)** — KWin does not implement `ext-session-lock-v1`;
  it is its own lock daemon (KScreenLocker in-process) and only offers the
  private `kde-lockscreen-overlay-v1`. Quickshell correctly reports
  "compositor does not support the ext-session-lock-v1 protocol". This is
  a compositor gap, not a Quickshell or aerial-lock one; every third-party
  session-lock client is affected the same way. Not applicable.

`loginctl unlock-session` works through aerial-idle (with
`idle.logindLockUnlock=true`) or an alternative daemon's explicitly wired
unlock event. Without such a listener, use `Release()` or SIGUSR1 directly;
logind does not itself release the compositor lock. "Correct password always
fails" is a different symptom (`pam_faillock`); fix with `sudo faillock --reset`.

If the locker cannot start, fix the installation from a TTY. When a previous
attempt already engaged the lock, manual recovery may also be needed; the
supervisor never performs takeover-and-unlock automatically.

## Known limitations

- **Stock Fcitx visible-echo prompts.** With a CJK engine active, a PAM prompt
  requesting visible input can trigger a candidate popup whose parent is not
  valid for a session-lock surface. The compositor may disconnect the locker;
  the connection guard exits 70 and bounded retries may end with a clientless
  lock requiring TTY recovery. Ordinary hidden-password prompts use Fcitx's
  plain-keyboard path by default. Keep `AllowInputMethodForPassword` disabled
  and switch to the plain keyboard input method for visible-echo prompts.
- **Memory during long video locks.** Continuous track changes have shown
  post-warm-up RSS growth of about **19 MB/hour**. Attribution remains open;
  this alpha does not claim flat memory use over arbitrarily long locks.
- **Nested Hyprland input.** Synthetic keyboard/pointer input into nested
  Wayland-backed Hyprland is not reliable in every setup. The automated
  multi-output lane uses render-backed virtual outputs; this does not establish
  physical-output or real platform-IME coverage. See the nested testing guide.
- **Authentication and secret lifetime.** PAM account-management flows are not
  implemented, and editable response strings live on the QML heap. See the
  [configuration reference](docs/config.reference.md#known-limitations).

## Roadmap

The optional idle daemon, headless content service and Aerial settings app have
separate roles. Later work includes catalogue browsing and advanced rotation
controls over the companion API, plus POI/location presentation.
Local sources and configured Apple/community packs remain first-class sources;
community content is never added automatically.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
