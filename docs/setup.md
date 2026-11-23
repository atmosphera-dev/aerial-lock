# Session setup

Install aerial-lock first (see [installation](../README.md#installation)). Run
`aerial-lock` from a terminal in your Wayland session and unlock with your
password before changing automatic locking. Keep a TTY or SSH recovery route
available; see [recovery](../README.md#recovery-last-resort).

## Settings runtime layout

The default build includes settings and requires a coherent Qt >= 6.12.0
installation. Core-only Qt 6.11 installs must explicitly use `BUILD_SETTINGS=0`
for both build and install. Do not mix plugins from different Qt versions.

The settings executable resolves only its private InterActive module root,
relative to its installed location. `PREFIX`, `BINDIR`, `INTERACTIVE_ROOT` and
`DESTDIR` are supported without `QML_IMPORT_PATH`, `LD_LIBRARY_PATH`, ldconfig
or installing IA in Qt's own directories. Keep the private root together when
relocating the package. Source-only descriptors in `settings/lint` are not
runtime modules and are never installed.

## Compositor keybinds

Replace any existing binding for the chosen key rather than keeping two lock
commands. The executable must be on the compositor's `PATH`.

### niri

Add this line inside the existing `binds` block in
`~/.config/niri/config.kdl` (do not add a second block):

```kdl
binds {
    Mod+L allow-when-locked=true { spawn "aerial-lock"; }
}
```

`Mod` is niri's configured modifier (normally Super on a native session).
`allow-when-locked` permits repeat requests while locked; it does not unlock.
Run `niri validate`, then press Mod+L after niri reloads the file.

Source: niri's [key bindings and spawn documentation][niri-binds].

### Hyprland

For the legacy `~/.config/hypr/hyprland.conf` format:

```conf
bind = SUPER, L, exec, aerial-lock
```

For Lua-based `~/.config/hypr/hyprland.lua` configurations instead:

```lua
hl.bind("SUPER + L", hl.dsp.exec_cmd("aerial-lock"))
```

Use the format your installation already uses, not both. Run `hyprctl reload`
and `hyprctl configerrors`; the latter must report no errors. Press Super+L.

Sources: Hyprland's [legacy binding parser][hypr-legacy] and
[Lua binding documentation][hypr-binds].

#### Keep recovery frames opaque

Hyprland can briefly show the desktop when replacing a crashed locker, even
while desktop input remains blocked. To mitigate this visual disclosure, set
these compositor-wide options. In a Lua configuration (`hyprland.lua`, the only
format since Hyprland 0.57):

```lua
hl.config({
    misc = {
        lockdead_screen_delay = 0,
        session_lock_xray = false,
    },
})
```

Or in a legacy `hyprland.conf` `misc` block (Hyprland 0.50 to 0.56; on older
versions omit `session_lock_xray`, which they lack and which defaults to off):

```conf
misc {
    lockdead_screen_delay = 0
    session_lock_xray = false
}
```

Reload and check `hyprctl configerrors`. Zero delay suppressed the captured
recovery flash in a Hyprland 0.56.2 VM test; it is a mitigation, not a guarantee
of an atomic handoff on every version or system. The tradeoff is that Hyprland
briefly shows its "lockscreen app died" fallback during healthy normal lock
startup too, before aerial-lock's first frame. It disappears when the locker
renders; it does not by itself mean aerial-lock crashed. These settings also
affect other session-lock clients. Don't set `allow_session_lock_restore`
yourself: the supervisor turns it on before each locker launch and off again
when the lock ends, with either config format.

Sources: Hyprland's [lock settings][hypr-lock-options] and
[lock-screen rendering checks][hypr-lock-render].

### sway

Add to `~/.config/sway/config` (Sway 1.9 or newer):

```conf
bindsym Mod4+l exec aerial-lock
```

`Mod4` is Super. Run `sway --validate`, then `swaymsg reload` and press Super+L.
Source: sway's [`bindsym` and `exec` reference][sway-config].

## Choose one idle/session owner

A keybind locks immediately; it does not arrange inactivity or suspend locking.
`aerial-idle` handles inactivity, lock-before-sleep, and logind lock/unlock events.
Enable it from the graphical session:

```sh
systemctl --user enable --now aerial-idle.service
```

Follow [idle/session integration](../README.md#idle-suspend-and-session-integration)
for the session environment and timeout configuration. The default inactivity
timeout is **zero** (off); check `aerial-idle --print-config`.

Disable the other shell/daemon's automatic lock and lock-before-sleep paths
before enabling aerial-idle. If you keep swayidle or hypridle as the owner,
leave aerial-idle disabled and use the existing
[alternatives without aerial-idle](../README.md#alternatives-without-aerial-idle).
Those examples also explain foreground waiting and explicit unlock handling.

After setup, test the key, `loginctl lock-session`, the configured idle timeout,
and a suspend/resume cycle separately. Confirm the aerial-lock screen appears
and accepts your password on every output; do not infer suspend protection from
a working keybind alone.

## Content companion startup

The headless `aerial` owns settings transactions and remote content
downloads. It is separate from optional `aerial-idle`: installing the content
service neither enables inactivity locking nor changes suspend policy.

Installation provides a vendor `default.target.wants` link for future user-manager
startups. It does not start or restart a companion in an existing session. Log out
and back in, or start now:

```sh
systemctl --user start aerial.service
```

With lingering or another still-running session, a login can reuse the existing
user manager; it need not trigger a new manager startup. The package never enables
linger. `aerialctl` and the normal locker also have session D-Bus activation as
a backup. No Wayland display, tray or aerial-idle process is needed for downloads.

To prevent future automatic service startup and D-Bus activation:

```sh
systemctl --user mask aerial.service
```

Masking does not stop an already running owner. Stop it explicitly if desired;
`systemctl --user unmask aerial.service` restores startup eligibility.
To pause downloads instead while retaining settings/CLI access:

```sh
aerialctl config set acquisition.automatic.enabled false
```

On a systemd-less session, start the installed `aerial` from your
compositor's autostart (default: `/usr/bin/aerial`). Conventional dbus-daemon
activation also uses that installed executable. Do not run a second downloader.

Custom XDG roots or `AERIAL_LOCK_SHELL` development bundles do not activate a
missing production owner. Start a matching companion explicitly on a private
bus, with the same bundle and XDG roots for every client. The API rejects a
different profile rather than importing the caller's environment or paths.

## Aerial settings app and tray

The Aerial window opens on Overview. Sources, Downloads, Lock screen and About
remain separate pages; storage options and cleanup are under Downloads. The
window follows desktop appearance live. Closing it has no effect on the
background service, automatic locking or the optional tray.

The Aerial app changes user units only when you click. Neither the tray nor
aerial-idle is enabled by installation or by opening the window; the content
service keeps the startup behavior described above.

| # | Click | User-manager change |
|---|---|---|
| 1 | About > Show tray icon on | Shows the icon now, then `enable` + `start` of `aerial-tray.service` for later graphical logins. If the window is already open, that unit run hands the tray to the window's process and exits. |
| 2 | Show tray icon off, or Hide tray icon in the tray menu | `disable` of `aerial-tray.service`; the icon goes away, an open window stays open |
| 3 | Overview notice or Lock screen > Turn on, then confirm | `enable --now aerial-idle.service` |
| 4 | Lock screen > Restart aerial-idle | `restart aerial-idle.service`, when saved idle settings need applying or startup freshness is uncertain |
| 5 | Start service (service unavailable panel) | `start aerial.service`; a masked unit stays masked until you run `systemctl --user unmask aerial.service` |

aerial-idle reads its settings when it starts, so saving an idle setting never
restarts it: the page offers Restart instead. Keep one idle owner; turn off
swayidle, hypridle or a shell's own idle locking yourself before Turn on (see
[Choose one idle/session owner](#choose-one-idlesession-owner)). The app never
edits other daemons or desktop configuration.

Without a systemd user manager the app still saves settings and controls
downloads, but cannot arrange autostart. Start these from your
compositor's autostart instead, for example on niri:

```kdl
spawn-at-startup "aerial"
spawn-at-startup "aerial-settings" "--tray"
```

Add `aerial-idle` only if it is your single idle owner.

## Atmosphera (Noctalia v4-derived)

In **Settings > Lock Screen > Appearance**, choose the external lock style and
set its command to `aerial-lock`. The corresponding settings are:

```json
{
  "general": {
    "lockScreenEnabled": true,
    "lockScreenPlugin": "external",
    "externalLockCommand": "aerial-lock",
    "lockOnSuspend": false
  },
  "idle": { "enabled": false }
}
```

This is a fragment, not a replacement for your settings. Prefer the Settings UI
so existing values are preserved. With aerial-idle as owner, turn off
**Settings > Idle** and **Lock Screen > Behavior > Lock on suspend** as above.
Keep `lockScreenEnabled` on so manual shell lock actions still work.

Do not use the old `Settings.data.idle.lockCommand` for locker selection. It is
still declared, but idle locking now calls `CompositorService.lock()`.
`resumeLockCommand` runs on activity during a pending idle fade, not after a
successful PAM unlock; leave it empty unless you have a separate need.

Check the compositor keybind and Atmosphera's session-menu Lock action, then
test aerial-idle using the checks above.

Source (Atmosphera revision `523f773`):
[`Commons/Settings.qml:831-835`][atmo-settings] declares the external command;
[`AppearanceSubTab.qml:31-75`][atmo-ui] exposes it;
[`CompositorService.qml:876-897`][atmo-lock] selects it.
[`IdleService.qml:215-218,276-284,410-412`][atmo-idle] and
[`LogindService.qml:32-43`][atmo-logind] show the idle and suspend paths.

## Noctalia v5

v5 is no longer the v4 QML settings model. In v5.5.0 there is **no global external
locker command** replacing the built-in session-lock implementation. Use the
compositor keybind and aerial-idle; disable Noctalia's own lock screen in
**Settings > Lock Screen**:

```toml
# ~/.config/noctalia/config.toml
[lockscreen]
enabled = false
lock_before_suspend = false
```

GUI overrides in `~/.local/state/noctalia/settings.toml` win over this file, so
remove conflicting overrides through Settings. Disable every enabled idle
behavior that locks or runs a locker command. Noctalia's session-menu/IPC Lock
action becomes unavailable or a no-op; it is not redirected to aerial-lock.

For idle-only external invocation, v5 does support a custom behavior:

```toml
[idle.behavior.lock]
enabled = true
timeout = 300
action = "command"
command = "aerial-lock"
```

Use that only instead of aerial-idle's inactivity timer
(`idle.timeoutSeconds = 0`); aerial-idle can still own suspend and logind events.
Do not set `resume_command` to unlock on ordinary activity.

Run `noctalia config validate` and inspect `noctalia config export` for effective
settings. Check the compositor keybind, then idle and suspend separately.

Sources (v5.5.0): [lock-screen settings][noctalia-lock],
[lock-screen schema][noctalia-schema], [idle behaviors][noctalia-idle], and
[config locations/override precedence][noctalia-config].

## Omarchy

### Current shell-based Omarchy

Upstream revision `74de104` no longer uses hypridle/hyprlock: its own shell
owns idle and the lock screen. Super+Ctrl+L invokes `omarchy-system-lock`, whose
first action is `omarchy-shell lock lock`. A separate
`omarchy-sleep-lock.service` locks before sleep. There is no external-locker
setting; switching requires a local script customization:

| # | Change | Where |
|---|---|---|
| 1 | Set `idle.lock` to `0`; add `"omarchy.lock"` to `disabledPlugins`, preserving other entries. Do this while unlocked. | `~/.config/omarchy/shell.json` |
| 2 | Back up the installed script, then replace only `omarchy-shell lock lock >/dev/null` with `aerial-lock >/dev/null 2>&1 &`. Keep the remaining keyboard-layout, 1Password and screensaver cleanup code. | File reported by `command -v omarchy-system-lock` |
| 3 | Run `systemctl --user mask --now omarchy-sleep-lock.service`, then enable aerial-idle as above. | Graphical session |

The existing key and menu still call `omarchy-system-lock`, now launching
aerial-lock. Disabling the built-in lock plugin also prevents direct shell IPC
from starting a competing locker; its lock-status/preview features no longer
describe the external locker. Review the script change after Omarchy upgrades,
which can replace it. To remove this customization later, restore the saved
script and shell settings, disable aerial-idle, and unmask/re-enable Omarchy's
sleep-lock unit.

Check Super+Ctrl+L, the menu's Lock action, inactivity and suspend/resume.

1Password: `omarchy-system-lock` also locks 1Password, but the idle and suspend
locks from aerial-idle start `aerial-lock` directly and don't run that script.
If you rely on it, turn on 1Password's own automatic locking in its settings;
aerial-lock itself does not lock 1Password.

Sources: [`default/hypr/bindings/utilities.lua:144`][omarchy-key],
[`bin/omarchy-system-lock:8-28`][omarchy-lock],
[`shell/plugins/services/idle/Service.qml:21-25,78-79`][omarchy-idle],
[`shell/services/PluginRegistry.qml:148-168`][omarchy-plugins], and
[`default/systemd/user/omarchy-sleep-lock.service`][omarchy-sleep].

### Older Omarchy with hypridle/hyprlock

If your installation still has `~/.config/hypr/hypridle.conf` launching
`omarchy-system-lock`/hyprlock, keep hypridle as the owner and leave aerial-idle
disabled. Apply the [hypridle alternative](../README.md#alternatives-without-aerial-idle):
replace `lock_cmd` and `before_sleep_cmd`, and change the lock listener's
`on-timeout` to `loginctl lock-session`. Keep the existing `after_sleep_cmd`,
and set `inhibit_sleep = 3`: hypridle's automatic mode only waits for the lock
before sleeping when the command contains `hyprlock`. Disable the separate screensaver listener if replacing
it with aerial-lock's video background.

Replace Super+Ctrl+L's command with `aerial-lock` in your personal bindings.
For a Lua setup, `~/.config/hypr/bindings.lua` accepts:

```lua
hl.unbind("SUPER + CTRL + L")
o.bind("SUPER + CTRL + L", "Lock system", "aerial-lock")
```

For a legacy `.conf` setup, use:

```conf
unbind = SUPER CTRL, L
bind = SUPER CTRL, L, exec, aerial-lock
```

Reload Hyprland and restart hypridle using `omarchy-restart-hypridle`. Check the
key, `loginctl lock-session`, idle and suspend/resume. Bypassing the old Omarchy
lock script also bypasses its 1Password/layout/brightness helpers; retain any
you need in your own wrapper and point every lock entry point at that wrapper.
Update menu entries that still launch the old locker.

Historical source (revision `b9ddccf`):
[`config/hypr/hypridle.conf`][omarchy-old-idle],
[`bin/omarchy-system-lock`][omarchy-old-lock], and
[`config/hypr/bindings.lua`][omarchy-old-bindings].

[niri-binds]: https://github.com/niri-wm/niri/blob/main/docs/wiki/Configuration%3A-Key-Bindings.md#spawn
[hypr-legacy]: https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/config/legacy/ConfigManager.cpp#L1492-L1495
[hypr-binds]: https://github.com/hyprwm/hyprland-wiki/blob/3d598abaa15b9dfc4834b3e45fe0b734707a9adb/content/configuring/core/binds/_index.md
[hypr-lock-options]: https://github.com/hyprwm/Hyprland/blob/11cbc6c577b84ffb1edf4df3fc5b6128c3ae6947/src/config/values/ConfigValues.cpp#L586-L603
[hypr-lock-render]: https://github.com/hyprwm/Hyprland/blob/11cbc6c577b84ffb1edf4df3fc5b6128c3ae6947/src/render/Renderer.cpp#L1781-L1803
[sway-config]: https://github.com/swaywm/sway/blob/master/sway/sway.5.scd
[atmo-settings]: https://github.com/alexindigo/atmosphera/blob/523f773b86e1182f4615df3669cd332c76457110/Commons/Settings.qml#L831-L835
[atmo-ui]: https://github.com/alexindigo/atmosphera/blob/523f773b86e1182f4615df3669cd332c76457110/Modules/Panels/Settings/Tabs/LockScreen/AppearanceSubTab.qml#L31-L75
[atmo-lock]: https://github.com/alexindigo/atmosphera/blob/523f773b86e1182f4615df3669cd332c76457110/Services/Compositor/CompositorService.qml#L876-L897
[atmo-idle]: https://github.com/alexindigo/atmosphera/blob/523f773b86e1182f4615df3669cd332c76457110/Services/Power/IdleService.qml#L215-L218
[atmo-logind]: https://github.com/alexindigo/atmosphera/blob/523f773b86e1182f4615df3669cd332c76457110/Services/Power/LogindService.qml#L32-L43
[noctalia-lock]: https://github.com/noctalia-dev/noctalia/blob/v5.5.0/docs/user/configuration/shell.mdx#lock-screen
[noctalia-schema]: https://github.com/noctalia-dev/noctalia/blob/v5.5.0/src/config/schema/config_schema_desktop.cpp
[noctalia-idle]: https://github.com/noctalia-dev/noctalia/blob/v5.5.0/docs/user/services/idle.mdx
[noctalia-config]: https://github.com/noctalia-dev/noctalia/blob/v5.5.0/docs/user/configuration/index.mdx#file-locations
[omarchy-key]: https://github.com/basecamp/omarchy/blob/74de10479d3bf7aa7865408e33219064cfe45432/default/hypr/bindings/utilities.lua#L144
[omarchy-lock]: https://github.com/basecamp/omarchy/blob/74de10479d3bf7aa7865408e33219064cfe45432/bin/omarchy-system-lock#L8-L28
[omarchy-idle]: https://github.com/basecamp/omarchy/blob/74de10479d3bf7aa7865408e33219064cfe45432/shell/plugins/services/idle/Service.qml#L21-L25
[omarchy-plugins]: https://github.com/basecamp/omarchy/blob/74de10479d3bf7aa7865408e33219064cfe45432/shell/services/PluginRegistry.qml#L148-L168
[omarchy-sleep]: https://github.com/basecamp/omarchy/blob/74de10479d3bf7aa7865408e33219064cfe45432/default/systemd/user/omarchy-sleep-lock.service
[omarchy-old-idle]: https://github.com/basecamp/omarchy/blob/b9ddccfc377abe0b8fc3ff1ee5b31a86bf202d4a/config/hypr/hypridle.conf
[omarchy-old-lock]: https://github.com/basecamp/omarchy/blob/b9ddccfc377abe0b8fc3ff1ee5b31a86bf202d4a/bin/omarchy-system-lock
[omarchy-old-bindings]: https://github.com/basecamp/omarchy/blob/b9ddccfc377abe0b8fc3ff1ee5b31a86bf202d4a/config/hypr/bindings.lua
