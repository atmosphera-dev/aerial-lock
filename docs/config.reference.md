# aerial-lock Configuration Reference

Sparse user overrides live at `$XDG_CONFIG_HOME/aerial-lock/config.json`
(normally `~/.config/aerial-lock/config.json`). A missing file inherits shipped
defaults without copying them into the user profile.

Bundled defaults are at `/usr/share/aerial-lock/Config/defaults.json`.
User override at `~/.config/aerial-lock/config.json`.

Each field follows strict JSON types (numbers are numbers, strings are strings,
objects are objects), validated **per field**: a field with the wrong type
falls back to its own bundle default with a startup warning naming the full
path (e.g. `panel.widthMax`) — the rest of your config is unaffected.
Unknown existing fields survive companion transactions. Invalid newly requested
settings are rejected without applying valid siblings from the same transaction.
Malformed/unreadable files are preserved and block acquisition and automatic
writes; the locker can still use local/default presentation with no remote sources.

Your config is deep-merged onto the bundle defaults: a partial `panel` or
`colors` object overrides only the fields it names, and omitted sibling
fields keep their defaults. Arrays replace completely: an explicit `sources: []`
never inherits Apple. The companion is the sole application writer. The locker
reads once per lock; aerial-idle reads once at startup. Changes made with
`aerialctl config set <dot.path> <json>` or `reset <dot.path>` use a revisioned
D-Bus transaction. Stale clients get Conflict (CLI exit 4), not last-writer-wins.
Direct external editors remain possible, but a noncooperating write can race the
final compare/rename; atomic replacement is not filesystem compare-and-swap.

## Fields

### Top-level

| Field              | Type    | Default          | Description                                    |
|--------------------|---------|------------------|------------------------------------------------|
| `language`         | string  | `"en"`           | I18n language (`en.json`, future `ru.json`)    |
| `backgroundColor`  | string  | `"#000000"`      | Solid color when no video. CSS hex.            |
| `theme`            | string  | `"Macintosh.qml"`| Whole-surface theme filename (see below).      |
| `sources`          | array   | One Apple TV descriptor | Video sources, played in list order (see below). An explicit array replaces the default. |
| `preferredResolution` | number | *(absent)*    | `1080` or `2160`. Optional and **not** in the bundle defaults — absence is the signal. See "Resolution" below. |
| `pamService`       | string  | `"aerial-lock"`  | PAM config under `/etc/pam.d/`                 |
| `fallbackQuitMs`   | number  | `3000`           | Retry the explicit quit after unlock if the first deferred quit does not complete. |
| `pamWatchdogTimeoutMs` | number | `60000`       | Abort a PAM call that shows no activity for this interval; keep the lock engaged and allow a fresh attempt. Time spent waiting for the user at a prompt never counts. |

### `sources` (array)

One list of video sources, played in order as a single looping playlist
behind the panel:

```json
"sources": [
  { "type": "file",      "name": "One clip",       "source": "~/Videos/blue.mp4" },
  { "type": "folder",    "name": "Personal",       "source": "~/Videos/locker" },
  { "type": "apple",     "name": "Apple TV",       "source": "https://sylvan.apple.com/Aerials/resources-16.tar" },
  { "type": "community", "name": "Community 2026", "source": "https://aerialscreensaver.github.io/community-2026" }
]
```

The shipped ordinary descriptor is at `Config/defaults.json.sources[0]`, with
stable ID `apple-tv`. It uses the selected tvOS catalogue URL from that file;
there is no hidden Apple source or implicit community source. Valid partial
configs lacking both `sources` and legacy video keys inherit it. Explicit empty,
custom and older Apple sources remain unchanged. Invalid `sources` is diagnosed,
not treated as permission to contact Apple.

Each entry needs `type`, `name`, and `source` (all strings). `type` is one
of `file`, `folder`, `apple`, `community`. A malformed or unknown entry is
dropped with a startup warning naming its index (`sources[2].type`); the
rest of the list is kept. `~` and `$HOME` expand in `file`/`folder` paths.
Remote sources (`apple`/`community`) may also carry `minResolution` /
`maxResolution` (`1080` or `2160`) — the hard band fetch stays within (see
"Resolution"). An invalid band value drops the source with a warning.

- `file` / `folder` — local; played directly. Missing paths are skipped by
  mpv (error logged, playlist moves on), never a crash.
- `apple` / `community` — remote packs. The locker never downloads; it plays
  verified completed managed objects or compatible legacy clips only. Unfetched
  packs contribute no playable clip.

The legacy cache directory for a remote source is
`$XDG_CACHE_HOME/aerial-lock/<name>/` (else `~/.cache/...`), with the name
sanitized: spaces become `-`, anything outside `[A-Za-z0-9._-]` is stripped.
Two sources sanitizing to the same name → the later one is dropped with a
warning.

Sources can have an explicit stable string `id`. ID-less read-only sources get a
deterministic identity; service-managed edits persist an ID so a rename or URL
change does not change source identity. Opaque media storage names include the
representation identity, not user/provider text.

**Migration from legacy configs:** the shared reader projects old `video`,
`videoDir` and `apple` keys into sources before default inheritance; an explicit
`sources` array always wins. Only the companion persists the migration. A
disabled legacy Apple setting remains disabled. The legacy Apple cache lived
at `.../aerial-lock/apple/`; the
migrated Apple source caches at `…/aerial-lock/Apple-TV/` — rename the old
directory if you want to keep the already-downloaded clips.

### Acquisition and managed cache

All keys below are under the JSON root. Every connection and power toggle is
independent, including unknown detector states. NetworkManager and UPower are
read-only observers; their absence is reported as unknown, not asserted to be
unmetered or AC. Known metering on a VPN's underlying transport still counts.

| # | Key | Default | Meaning |
|---|---|---|---|
| 1 | `acquisition.automatic.enabled` | `true` | Automatic work; pause/resume persists this setting. |
| 2 | `acquisition.automatic.allowedResolutions` | `[1080]` | Allowed heights, 1080 and/or 2160; `[]` allows no automatic quality. |
| 3 | `acquisition.automatic.allowMetered` | `false` | Permit known metered automatic transfers. |
| 4 | `acquisition.automatic.connectionTypes` | All `true` | Individually set `ethernet`, `wifi`, `cellular`, `bluetooth`, `vpn`, `other`, `unknown`. |
| 5 | `acquisition.automatic.power` | All `true` | Individually set `ac`, `battery`, `unknown`. |
| 6 | `acquisition.checkIntervalSeconds` | `3600` | Positive integer; also reevaluate on config/policy changes. |
| 7 | `cache.maxBytes` | `10000000000` | Decimal 10 GB, about 9.31 GiB. Zero prevents new managed bytes, never means unlimited. |
| 8 | `cache.rotationDays` | `7` | Incremental older/least-recently-played replacement; zero disables scheduled rotation. |
| 9 | `cache.minFreeBytes` | `0` | Optional disk reserve; current usable space is always checked. |

Managed cache: `$XDG_CACHE_HOME/aerial-lock/managed-v1/` (default `~/.cache/...`).
It counts clips, partials, manifests, indexes and staging once, plus remaining
reservations for active work. Personal sources and unadopted legacy caches are
neither counted nor pruned. Playback pins protect an entire episode's playlist,
even across companion restarts. Lowering a cap below pinned usage stops growth
and can leave reported over-budget bytes until readers release; it never deletes
live playlist files to force numerical compliance. The last verified fallback
is protected. The cap limits storage, not lifetime/monthly download traffic.

Durable jobs, retries, rotation and playback recency use
`$XDG_STATE_HOME/aerial-lock/` (default `~/.local/state/...`); runtime coordination
uses `$XDG_RUNTIME_DIR/aerial-lock/`. Bulk staging stays in the disk cache.

### `idle` (object)

Read once when `aerial-idle` starts. Restart `aerial-idle.service` after editing
these settings. The locker accepts this object but leaves idle policy to
aerial-idle. Enabling that optional service without a user config enables suspend and
logind lock/unlock handling; the inactivity timeout remains off.

| Field | Type | Default | Description |
|---|---|---|---|
| `timeoutSeconds` | integer ≥ 0 | `0` | Lock after this much inactivity; `0` disables the timeout. Very large values saturate at the Wayland protocol's maximum millisecond timeout. |
| `respectInhibitors` | boolean | `true` | Visible video/presentation idle inhibitors postpone the timeout. If false, input alone resets the timer (requires ext-idle-notify v2; v1 warns and falls back to respecting inhibitors). |
| `lockOnSuspend` | boolean | `true` | Hold a logind sleep-delay inhibitor until the supervisor confirms the lock, bounded by logind's delay limit minus 500 ms. |
| `logindLockUnlock` | boolean | `true` | Honour the graphical session's logind `Lock` and `Unlock` events. |

The installed `/usr/share/aerial-lock/examples/idle.json` sets a five-minute
timeout. It is an example only and is never loaded automatically. Copy its
`idle` object into your config to opt in. Partial objects merge recursively;
wrong-typed fields (including negative or fractional timeouts) fall back to
their individual defaults with a warning. The idle daemon never rewrites the file.

### `powerActions` (object)

| Field | Type | Default | Description |
|---|---|---|---|
| `enabled` | boolean | `true` | Show supported theme power controls while securely locked; false hides all power controls and prevents their requests. |

Read once per lock. Existing configs inherit the default without a migration
or rewrite. Partial objects merge normally; invalid types fall back per field.
To hide every power control:

```json
{ "powerActions": { "enabled": false } }
```

Only logind's exact `Can*` reply `yes` permits an action, checked again before
execution. Additional administrator authorization is never requested. Restart
and shutdown ask for inline confirmation with Cancel focused first; suspend
and hibernate act immediately. Hibernate availability depends on the device.
Matching application block inhibitors are respected, not overridden. Actions
never unlock the session; sleep/resume retains the lock and editable draft.
This setting does not change hardware keys, logind policy or `aerial-idle`.

### `panel` (object)

| Field         | Type   | Default | Description                     |
|---------------|--------|---------|---------------------------------|
| `widthMax`    | number | `360`   | Max panel width in px           |
| `fieldHeight` | number | `40`    | Password field height in px     |
| `radius`      | number | `12`    | Panel corner radius in px       |
| `fieldRadius` | number | `8`     | Field corner radius in px       |
| `outerMargin` | number | `20`    | Content margin inside panel     |
| `spacing`     | number | `12`    | Space between panel elements    |
| `fontSize`    | number | `16`    | Text pixel size                 |

### `colors` (object)

| Field                 | Type   | Default        | Description                |
|-----------------------|--------|----------------|----------------------------|
| `text`                | string | `"#dddddd"`    | Normal text color          |
| `textError`           | string | `"#ff5555"`    | Error text color           |
| `input`               | string | `"#ffffff"`    | Input text color           |
| `panelFill`           | string | `"#14ffffff"`  | Panel background (with alpha) |
| `panelBorder`         | string | `"#1fffffff"`  | Panel border (with alpha)  |
| `fieldFill`           | string | `"#0fffffff"`  | Field background (with alpha) |
| `fieldBorderFocused`  | string | `"#40ffffff"`  | Field border when focused  |
| `fieldBorder`         | string | `"#1affffff"`  | Field border (unfocused)   |

## I18n

`Config/i18n/en.json` ships with English strings. Set `"language"` to choose
`<lang>.json`. Each key is resolved from
`$XDG_CONFIG_HOME/aerial-lock/i18n/<lang>.json` (default
`~/.config/aerial-lock/i18n/<lang>.json`), then the bundled
`Config/i18n/<lang>.json`, then bundled `en.json`.

Override only the strings you want to change, for example in `i18n/en.json`:

```json
{ "prompt": "Your password, please" }
```

Files must contain a JSON object of nonempty strings. A missing, unreadable
or broken file logs one warning and leaves the lower layers intact. Invalid
or blank values are ignored with one warning per file; valid siblings still
apply. Files are read once at startup, so edits apply on the next lock.

## Fetching remote sources

The headless companion acquires configured remote sources automatically after
user-manager startup. It chooses the smallest eligible known-size clip first,
then fills under the cap. Unknown sizes come after known sizes and remain bounded
during transfer. The locker sends one nonblocking scheduling hint after secure
engagement, but never downloads, waits for a service reply or changes its frozen
playlist mid-lock. New completions appear from the next lock, including respawns.

```
aerialctl fetch [source-id] [--limit N] [--wait]
aerial-lock-fetch [--limit N]
```

`--limit N` is a ready-clip limit **per source**, with existing verified eligible
clips counting; zero means no item limit, still under the managed byte cap.
Partials resume only with compatible representation/validator/range metadata.
`aerial-lock-fetch` is a thin `aerialctl fetch --wait` frontend, with exits 0
(at least one eligible verified clip ready), 1 (operation/no ready clip), 2
(bad arguments). No independent downloader or config writer remains.

Manual fetch bypasses automatic pause, connection, metering, power and quality
allowance, not source bands, source selection, disk/cap, TLS or validation. It
does not add Apple to `sources: []` or promise success offline. CLI status/jobs
report counts, policy reasons and partial-source errors without routine token
URLs. Settings inspection can reveal configured URLs; treat that output as private.

| # | Command | Purpose |
|---|---|---|
| 1 | `aerialctl status --json` | Actual profile, paths, readiness, bytes, policy and jobs. |
| 2 | `aerialctl config show [--effective] [--json]` | Raw sparse overrides or normalized effective view. |
| 3 | `aerialctl config set <dot.path> <json>` / `reset <dot.path>` | Revisioned sparse changes; reset removes an override. |
| 4 | `aerialctl content ensure` / `refresh [source-id]` | Normal-policy planning or metadata refresh. |
| 5 | `aerialctl content pause` / `resume` | Persist automatic enabled false/true; playback and manual fetch remain available. |
| 6 | `aerialctl jobs [--watch]` / `cancel <job-path>` | Observe durable work or cancel, retaining resumable partials. |
| 7 | `aerialctl cache status` / `prune [--dry-run]` | Managed accounting and safe reclamation only. |

CLI exits: 0 accepted/success (`--wait` requires the requested ready outcome),
1 failure, 2 usage, 3 automatic-policy deferred, 4 settings conflict. An accepted
service job survives its CLI exiting. Manual fetch never exits 3 for automation
settings. A masked service cannot be activated; unmask before manual use.

### Theme

`theme` names the whole-surface lock UI. The default is `Macintosh.qml`.
The value is a plain QML **filename** (no path, no URL, no traversal) —
anything else is a config bug, warned about and replaced by the built-in
panel. Lookup order: `~/.config/aerial-lock/themes/<name>` first, then the
bundle's `/usr/share/aerial-lock/Themes/<name>`; user files win. The file
is read once per lock — editing it mid-lock never reloads away a partial
response.

Bundled: `Macintosh.qml`, `Windows7.qml`, `MaterialYou.qml`, `Enfield.qml`,
`Clockwork.qml`. See `docs/theming.md` for the authoring contract, the
context object, and the topic-preference semantics that let a theme bias
which of your configured clips play (Enfield prefers mountains).

New managed clips play only after successful transfer, size/checksum checks when
available, isolated initial-frame decoding, immutable publication and indexing.
A partial filename never means completion. Legacy nonempty `.mp4` plus `.done`
continues to be readable without automatic adoption or pruning.

### Resolution

Fetch picks one URL per asset from a **hard band** and a **soft preference**.
Automatic work first intersects the band with `allowedResolutions` (default
`[1080]`); manual fetch ignores only that automatic allowance.

- The band is the source's optional `minResolution`/`maxResolution`
  (`1080` or `2160`). Nothing outside it is downloaded — a 4K-only pack
  with `maxResolution: 1080` contributes nothing (warned and skipped).
  Omitted `min` means 0; omitted `max` means no ceiling.
- `preferredResolution` (`1080` or `2160`, global) picks inside the band:
  the same height when available, else the next height **up** — never past
  `maxResolution`. A 4K-only pack with preference 1080 downloads its 4K
  clips.

`preferredResolution` is **existence-written, never overwritten**: when the
key is absent from your `config.json` and a secure lock supplies an observed
display hint, the companion stamps it from the largest
connected output's logical height (`>= 2160` → 2160, else 1080). A key
already in the file is never touched — even an invalid one (an out-of-band
value is ignored with a warning, and left in the file). If the key is
absent and no outputs are visible, that run picks the highest in-band URL
and writes nothing.

## Known limitations

- Stock Fcitx candidate popups on visible-echo PAM prompts can disconnect the
  locker; keep password input methods disabled and use the plain keyboard
  method for those prompts. Long video locks have shown approximately 19 MB/h
  RSS growth under continuous track changes. Nested Hyprland synthetic input
  also has coverage limits. See [README known limitations](../README.md#known-limitations)
  for symptoms and workarounds.
- **No PAM account-management phase.** The exec-isolated `aerial-lock-auth` runs
  `pam_authenticate` only, so interactive account-management flows (e.g. an
  expired password's "new password/retype" prompt) are unreachable — identical
  to swaylock and hyprlock, which are also auth-only. A plain expired password
  still authenticates correctly.
- **Plaintext password lifetime.** The response being edited is held as a
  QML string in the shared input state (`context.auth.input`): it lives on
  the JS heap for the whole editing session, cannot be zeroed, and a
  cleared value survives until garbage collection. On accepted submission
  the draft is consumed and cleared (all field mirrors follow), and an
  accepted response queued for PAM's first prompt is cleared *before*
  `respond()` is called, on failure, abort and teardown. A native undo
  cannot resurrect consumed text (the authoritative clear is programmatic,
  which resets the field's undo history). The exposure is narrowed, not
  eliminated; a real fix needs an `mlock`ed buffer behind a C++ helper.

## Recovery

The supervisor retries the QML locker within three total attempts, then uses
the installed native emergency locker. FailedToStart and exit 72/126/127 switch
immediately. Emergency crashes have a separate three-attempt budget; wrong PAM
answers keep the same client and never consume that budget. There is no
automatic unlock or return to Qt in an emergency episode. Normal exit `0` keeps
its clean unlock / PAM refusal / external invalidation semantics. Final failures
exit `2`, or `3` for emergency refusal/unavailability and guard/startup failure.
Logs distinguish the phase and previous confirmation from an unknown screen
state. Manual last resort remains `aerial-unlock` from a TTY.

Emergency authentication uses the same `pamService` and computation-only
`pamWatchdogTimeoutMs`. A nonempty `AERIAL_LOCK_PAM_SERVICE` wins; otherwise a
valid user field wins over the selected bundle, per field. Empty service uses
`aerial-lock` only where the normal configuration does. A missing/broken bundle
with no derivable service disables emergency authentication, never substitutes
another PAM policy. Invalid/out-of-range watchdogs use 60000 ms with a warning.
Neither client changes PAM throttling or writes configuration during recovery.
The emergency UI supports only the first hidden prompt; other interactions fail
closed with nontext feedback. Its keyboard/Compose path has no platform IME,
clipboard or accessibility support. See README for the complete UI limitations.

An explicit supervisor D-Bus `Release()` or SIGUSR1 request releases gracefully
without PAM and disables further respawns for that episode. aerial-idle forwards
logind `Unlock` when `idle.logindLockUnlock` is true. After a release request,
clean locker exit gives supervisor exit `0`; abnormal exit gives `2` without
respawn and the screen state is unknown. This is an explicit external-unlock
path, not automatic crash recovery.

One supervisor protects each Wayland session. Its state follows the actual
lock: each locker attempt reports the compositor's ext-session-lock `locked`
confirmation over a private channel the supervisor accepts only from that
attempt's process, and a locker that exits loses the confirmation until the
next attempt reports. A second `aerial-lock` for the same session launches
nothing; it asks the supervisor on duty for that state and exits `0` only once
it is confirmed, waiting up to 10 s while the lock is still being engaged.
Otherwise — not confirmed in time, a failing or vanished supervisor, or a name
owner without a valid state — it exits `4`: engagement not confirmed, the
screen may be unlocked. Behaviour is fixed — the respawn limit and timeouts
are build constants, not configuration.

## Rate limiting and lockout

Throttling is delegated to the PAM stack. `aerial-lock` adds no client-side
attempt counter: on `AuthContext.MaxTries` the UI shows a message but leaves
the field enabled, and a fresh attempt starts a **new** PAM transaction with
a reset counter — retries are effectively unlimited from the client's
perspective.

This is deliberate, and matches swaylock and hyprlock. A client-side counter
is bypassed by restarting the locker, so it buys no security against anyone
who can spawn a process — and a lockout counter in the locker is a new way
to fail closed: a bug in it locks out the legitimate user permanently.
Cost with no benefit.

The shipped `/etc/pam.d/aerial-lock` is `auth include login`, so whatever
policy the system login stack enforces applies here automatically — which is
what makes delegation sound rather than lazy. Hardening belongs in
`/etc/pam.d/login` via `pam_faildelay` or `pam_faillock`, not in the locker.

## PAM_MAX_RESP_SIZE

This value is derived at build time from `/usr/include/security/_pam_types.h`
(`gen-pam-limits.c`). It is NOT configurable at runtime — it reflects the
actual limit of the PAM library compiled on your system.

To force a specific value at build time:
```
make PAM_MAX_RESP_SIZE=1024
```

The value is logged on every startup. If it differs from your expectation,
rebuild the package.

## Compositor

Not user configuration. At lock time, aerial-lock reads the focused output
from a compositor backend if one is present:

- On niri (`NIRI_SOCKET` set), use the default monitor (the first usable lock
  surface), without a compositor backend or optional library.
- Otherwise, `HYPRLAND_INSTANCE_SIGNATURE` selects Hyprland
  (`Hyprland.focusedMonitor.name`).
- Without a backend, if it fails to load, or if its name has no usable lock
  surface, use the first usable lock surface, with a warning in the last case.

There is no config key to pick a screen. A wrong name must not hide the
panel. The selected surface stays pinned until it disappears; the same
theme/editor then moves to a survivor, preserving the draft, selection and PAM
state. With no usable lock surface it waits and recovers when a surface returns.
Hyprland's real `FALLBACK` output is usable; Qt's placeholder screen is not a
lock surface by itself.

## Debug / development

Debug behaviour is a **build-time** property, not user configuration: `make`
writes `Config/build-flags.json` (root-owned when installed) with
`debugAllowDismiss` and `verbose`, both `false` in release builds.
`make dev` sets both `true` — the dismiss button bypasses authentication, so a
dev build prints a startup warning. A user config cannot enable either flag.

### Environment overrides

| Variable                    | Overrides      | Purpose                        |
|-----------------------------|----------------|--------------------------------|
| `AERIAL_LOCK_PAM_SERVICE`   | `pamService`   | Dev testing without installing PAM file |

Example:
```
AERIAL_LOCK_PAM_SERVICE=login bash Scripts/dev/run.sh
```

## Example

```json
{
  "language": "en",
  "backgroundColor": "#1a1a2e",
  "sources": [
    { "type": "apple", "name": "Apple TV", "source": "https://sylvan.apple.com/Aerials/resources-16.tar" }
  ],
  "pamService": "login",
  "fallbackQuitMs": 3000,
  "panel": {
    "widthMax": 400,
    "fieldHeight": 44,
    "radius": 14,
    "fieldRadius": 10,
    "outerMargin": 20,
    "spacing": 12,
    "fontSize": 16
  },
  "colors": {
    "text": "#eeeeee",
    "textError": "#ff6666",
    "input": "#ffffff",
    "panelFill": "#14ffffff",
    "panelBorder": "#1fffffff",
    "fieldFill": "#0fffffff",
    "fieldBorderFocused": "#40ffffff",
    "fieldBorder": "#1affffff"
  }
}
```

Additional fields will be added in future phases for overlays.
