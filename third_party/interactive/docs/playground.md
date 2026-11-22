# Playground And Verification

The playground consumes the real InterActive library. Its first Foundations
overview combines a form, choices, tabs, slider/progress, list selection, a state
shelf, menus, and an editable dialog. Five further pages (Actions, Forms, Values, Lists, Overlays) are planned. This is not full library/gallery coverage.

## Build And Test

Use CMake 3.21+, a C++17 compiler, and Qt 6.12.0+ with Core, Gui, Qml, Quick,
QuickControls2, Svg, and ShaderTools. Test builds also need Test and QuickTest.
The commands below use a POSIX shell and Ninja, from the source directory:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DINTERACTIVE_BUILD_PLAYGROUND=ON -DINTERACTIVE_BUILD_TESTS=ON
cmake --build build --parallel 4
cmake --build build --target all_qmllint
QML_IMPORT_PATH="$PWD/build/qml${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}" \
  ctest --test-dir build -L interactive-overview --no-tests=error --output-on-failure
```

Configure with `-DCMAKE_PREFIX_PATH="$QT_PREFIX"` to select a non-default SDK.
For minimum-version verification, also pass
`-DINTERACTIVE_VERIFY_QT_VERSION=6.12.0`. CMake checks the requested exact version;
the identity test compares compiled and runtime Qt versions. Use independent
build directories and matching runtime libraries/plugins for different SDKs.

Run GUI tests in an active graphical test session. The tests use real Qt Quick
windows and an RHI renderer; an offscreen platform or Qt Quick's raster software
backend is not equivalent evidence. Popup tests require support for actual
separate popup windows and fail rather than treat an Item fallback as success.
Keep the selected SDK's environment coherent; adding the build's QML import root
must not discard existing SDK import paths or introduce another Qt installation.

Build options `INTERACTIVE_BUILD_PLAYGROUND` and `INTERACTIVE_BUILD_TESTS` default
to ON. The `interactive.playground` test is included when the playground is built.
Both options must remain ON for the complete overview test set.

The separate [hosted CI lane](../tests/README.md#hosted-build-lint-and-headless-units)
builds all targets and runs QML lint, but executes only `interactive.appearance`,
`interactive.trace` and `interactive.color-edit-state` headlessly. It does not
run the native GUI overview, capture or performance suites. Exact Qt 6.12.0
scoped native evidence does not establish full current-Qt verification or resolve
the [known native failures](../README.md#known-limits).

## Test Coverage

Tests are registered directly with CTest under the `interactive-overview` label.
Inspect discovery with `ctest --test-dir build -L interactive-overview -N` and
retain verbose output when auditing QtTest pass/fail/skip totals. A successful
CTest exit does not alone establish that no QtTest case was skipped. Missing,
skipped, blacklisted, or expected-failure cases do not satisfy a required check.

| # | Suite | Observed Outcome To Check |
|---|---|---|
| 1 | `interactive.qt-version` | Compiled/runtime Qt identity agrees with the configured SDK. |
| 2 | `interactive.appearance` | Complete profiles, validation, replacement/reset, independent engines and explicit local overrides. |
| 3 | `interactive.controls` | Consumer-visible editing, choices, tabs, value changes, and state preservation. |
| 4 | `interactive.geometry` | Shared baselines/centerlines, touch targets, scrolling and stable outer dimensions. |
| 5 | `interactive.popups` | Actual Item/Window hosting, menu/dialog interactions, focus, modality and rendered appearance. |
| 6 | `interactive.icons` | Control-owned relative sources, source fallback, tint and rendered icon pixels. |
| 7 | `interactive.states` | Consistent foreground/background pairs during pressed/selected states. |
| 8 | `interactive.playground` | The real Foundations page commits edits; live semantic palette changes reach button backgrounds/text and slider/progress fills without replacing controls or losing fixture state. A 20-row matrix checks three semantic buttons, local purple and neutral in both schemes and contrast modes, using real hover/press interactions and full-precision/RGB8 contrast. |
| 9 | `interactive.solid` | Exact default primary and resolved generated caution/danger fills, 5:1 text in light/dark normal contrast, shared semantic text, unchanged geometry, state pairs, local overrides, resets and keyboard focus indication. |
| 10 | `interactive.palette-state` | Actual global/facade/IAButton alias bindings across 1/100/1,000 ordinary consumers; relevant colors update while unchanged font, height and neutral bindings do not evaluate; nested updates and local-root reset reach existing consumers. |
| 11 | `interactive.long-document` | A test-only editable TextEdit with 10,000 deterministic paragraphs and at least one million characters preserves document/editing/scroll/undo state across accent edits; foreground and font-size changes are positive controls. |
| 12 | `interactive.playground-calibration` | Actual launcher startup, transactional correction ownership/rejection/reentrancy, full-precision append/readback, preserved fragments, contention/dead-owner recovery and injected I/O failures against the real playground writer. |
| 13 | `interactive.trace` | Appearance trace recorder: nesting, outcomes, reentrancy, threads, overflow, stop semantics, export refusal and parseable output. |
| 14 | `interactive.segmented` | Native exclusive checked/index selection, user-only activation, binding preservation, mutation/reentrancy, RTL arrows/endpoints, equal-share/natural sizing and overflow, stable track/local appearance, and trailing-switch geometry/native pointer/Space/drag. Public Qt accessible interfaces/actions are inspected; this is not AT-SPI or Orca coverage. |
| 15 | `interactive.color-well` | Native sibling targets, circle/corner routing, titled preset/Default menu, input-binding preservation, accessible names/native Space/Tab, RTL/touch and stable sample/art geometry, reversible rainbow hover and immediate reduced-motion cancellation. |
| 16 | `interactive.color-edit-state` | Same production state source: full-precision channels, user-only notifications, feedback, domains/drafts, ARGB and HSL/HSV hue memory. |
| 17 | `interactive.color-picker` | Real shared editor/dialog, live plane/rails, four formats, persistent drafts/modes, native dismissal, public accessible interfaces and square/responsive geometry. AT-SPI/Orca is not exercised. |
| 18 | `interactive.segment-marker` | Native tab/segmented consumer pixels, persistent leading cap across hover/press/focus, unchanged finished fills and complete native focus frame, LTR/RTL, light/dark normal/high, live accent/contrast/direction, touch density, resize and local radius. Run at actual DPR 1, 1.25 and 2; source coverage alone is not rendered proof. |
| 19 | `interactive.dialog-header` | Plain and color dialogs in Item/Window hosting: square corner target, title/icon centerline and body-edge alignment, elision, focus/hover corner pixels and Close click; LTR/RTL, touch and enlarged text, plus absent/replaced headers. |
| 20 | `interactive.popup-direction` | All four popup roots in Item/Window hosting: first native-frame mirroring, live overlay inheritance, custom/deferred/replaced content, modal/type changes, reopen/destruction, opposite-direction host migration and nested menus, without writes to application hosts. |
| 21 | `interactive.dialogbuttonbox` | Native model/index ownership, horizontal alignment and mirroring with narrow/custom widths, geometry publication after model mutation, native vertical invalidation, Tab/Space role signals, live RejectRole and Item/Window dialog footers. |
| 22 | `interactive.combo-box` | String/role models, exact custom values, user-only activation, native popup opening and real Item/Window hosting, keyboard/pointer selection, editable input, inherited/swatches, live appearance, RTL/density/font geometry and bottom-edge flipping. |
| 23 | `interactive.status-banner` | Optional icon/action lanes, no empty action footprint, title/action elision, message wrapping, native keyboard/pointer action outcomes, validation roles, RTL and enlarged touch text. |

The dialog-header suite also checks permanent title separators and the absence
of duplicate fold lines at the start, middle and end, with custom/absent header
fallbacks. The native button-box suite renames a completed Ok button to Add
source, repeatedly changes captions/fonts/scale, and covers dynamic standard and
custom models on natural/fixed-width, Item/Window and RTL dialogs. Footer outline
pixels are compared with identical geometry and no footer paint, including
fractional Item-host placement; pixel-aligned rows retain exact role checks.

Amber and Danger in Appearance use label-plus-`IAColorWell` rows bound to the
resolved warning/error colors. Both reuse the existing twelve Accent preset
records. Preset and live full-dialog edits enter `setSemanticCorrection`; Default
uses `resetSemanticCorrection`, restoring prior raw input or current generation.
The persistent `PaletteColorPicker` adapters keep dialog/window identity,
synchronization and tracing; `openerVisible: false` hides only their legacy Flow
and removes its implicit footprint. Accent's existing saved-palette flow is
unchanged. Consumer tests retain simultaneous dialogs, live hue/alpha/plane
updates, reset ownership, peer isolation, validation, drafts and Save outcomes.

`interactive.color-well-capture` is a separate non-overview capture suite. With
an existing absolute `IA_CAPTURE_DIR`, it captures full native frames in all four
scheme/contrast modes and seven real states: rest, sample hover/focus/press and
circle hover/focus/press. `interactive.appearance-capture` retains the ten actual
Appearance frames, including RTL. These fixtures do not alter the fixed 72-image
gallery capture driver and do not claim AT-SPI/Orca or compositor-native pointer
coverage. Native Space is observed through the existing keyboard backend.

The long-document suite observes real QQuickTextDocument state and QML font binding
evaluations. Equal document dimensions or absent size signals alone do not prove
zero layout calls; debugger-level layout tracing and paired performance evidence
are separate diagnostic runs. This fixture is not an IA TextArea or proof of Notes
integration. A pre-existing native TextEdit font-add/remove dimensions issue is
documented in the appearance API's typography boundary, not suppressed in tests.

The playground accent-picker cases add a four-row light/dark, normal/high matrix
using real pointer drags on the shared IA hue slider. Assertions inspect application
input and the rendered primary sample's fill, label and border, plus slider and
progress fills, while the pointer is still pressed and the dialog is open. One
row also drags the alpha slider. Other checks cover opening without an override,
close retaining the color, explicit-default key presence, reset pruning and sibling
preservation, pending form/hex/JSON drafts, and rendered picker synchronization
after JSON or external input changes. Implicit caution, danger and success consumers
follow the current shared palette; explicit semantic siblings and the caller-owned
purple specimen remain independent. Real saturation/lightness edits complement
hue and alpha drags. Success tests inspect its rendered swatch/message surface
and shared layout centerline at desktop, 360px and 320px widths in both modes and
schemes, retaining consumer identities. Tests inspect the real owned IA controls
and applied preview, without locating private Qt fallback children.

`interactive.color-picker-capture` is a separate capture suite, not part of the
fixed 72-image driver. It captures full native picker windows across light/dark,
normal/high, all formats, focus/error/alpha/drag, touch/font/narrow/RTL and role
titles. Provide an existing absolute `IA_CAPTURE_DIR`. Geometry and actual
rendered frames must be reviewed together; compilation is not interaction proof.

The main outputs are `build/bin/interactive-playground`,
`build/bin/interactive-appearance-tests`, `build/bin/interactive-appearance-trace-tests`, `build/bin/interactive-playground-calibration-tests`, and `build/bin/interactive-quick-tests`.
These tests establish their asserted outcomes, not completion of the remaining
control pool, installed-consumer proof, accessibility, or final visual review.

## Performance Gates

`ark-vm-build --test --no-deliver` runs G1 with the overview suite: ten versus
one hundred mixed, source-bound facades without overrides must record identical
palette/derivation/pair/font/geometry work for an accent edit and a radius-only
metrics edit (one shared palette build for the accent edit). Counts are logged as
`G1`. Height-affecting metrics still require per-control geometry work on master;
this baseline gate does not assert otherwise.

Foundation's private instrumentation API shares one inline relaxed admission
load for typed work counters and trace spans. Tests subscribe with
`AppearanceWorkRecorder` and read `AppearanceWork` counts directly; bounded
palette-recipe checks read typed per-role proposal/exhaustion records from that
same subscription. With diagnostics off, sites do not read clocks or call the
recorder. Timings use only the trace backend described below.

`ark-vm-build --perf --no-deliver` runs G3 in VM `interactive-dev` on ark.home,
using the same Debug SDK/RHI lane. The `interactive_perf` target is labeled only
`interactive-perf` (600-second timeout), outside `interactive-overview`. Its real
window contains 300 mixed controls. Five rounds report median and nearest-rank
p95 (the maximum of five) for scene creation through first swap (`create_ms`),
60 accent edits through their frames (`accent_update_ms`, total milliseconds),
300 control-center mouse moves through their frames (`hover_move_ms`, total
milliseconds), and 1,000 empty-area moves (`idle_move_us`, microseconds per event,
dispatch only). The benchmark view requests swap interval zero before creating
its native surface and retains frame-completion waits. A compositor/platform may
still pace update delivery independently of swap interval; check per-operation
wall times rather than assume the request removes that pacing.
Each metric contains `wall_ms` (or `wall_us` for idle) and `cpu_ms` distributions;
CPU uses `CLOCK_PROCESS_CPUTIME_ID` across GUI and render threads and is the total
for the batch, including all 1,000 idle moves. `IA_PERF_OUT` selects a fresh JSON
file; unset prints JSON. The wrapper retains it as `<run directory>/perf.json`.
Compare repeated runs on the same VM and mode; the benchmark adds no product
instrumentation. Earlier vsync-paced wall-only JSONs are not comparable baselines.

## Running The Playground

Use the same SDK/runtime environment as the build:

```sh
QML_IMPORT_PATH="$PWD/build/qml${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}" \
  ./build/bin/interactive-playground --page foundations --appearance default
```

Supported options:

```text
--page foundations
--appearance default|light|dark
--density compact|touch
--contrast normal|high
--text-scale <positive number>
--rtl
--popup item|window
--width <logical pixels> --height <logical pixels>
--scene default|menu|dialog|partial-profile
--automation
--screenshot <absolute-path>
--palette-samples-file <absolute-path>
--appearance-profile-summary <path>
--appearance-trace <path>
--appearance-profile-delay-ms <n>
--appearance-profile-duration-ms <n>
--appearance-trace-capacity-mib <n>
```

The `appearance-profile-*`/`appearance-trace` options are an opt-in diagnostic
session, off by default. `--appearance-profile-summary <path>` arms a minimal
session that counts update attempts (classified accepted/no-op/rejected), records
the accepted-color sequence, observes input delivery and frame boundaries on every
QQuickWindow including the picker, and writes a compact summary. Adding
`--appearance-trace <path>` additionally records buffered detailed spans of the
appearance pipeline (preparation, palette build, local resolution, notifications)
as a Chrome/Perfetto-compatible trace plus an integrity sidecar at `<path>.sidecar.json`.
`--appearance-profile-delay-ms` (default 5000) arms before recording;
`--appearance-profile-duration-ms` (default 24000) bounds the interval.
`--appearance-trace-capacity-mib` (1–256, default 128) is a fixed recorder budget:
on exhaustion the recorder drops records, counts the loss in the sidecar's
`incomplete`/`recordsDropped` fields, and never overwrites old events. All output
paths must be fresh; existing files are refused. Recording adds observer cost even
when idle, so measurements taken with tracing enabled are not the application's
clean runtime cost.

The default viewport is 1200x820; width and height must be at least 320. At narrow
sizes the same content reflows and scrolls. Omitted appearance/density/contrast/scale
options do not manufacture scalar overrides. The eligible **default scene launcher**
adds temporary `warning: "#FF7A18"` and `error: "#FF3434"` corrections before QML
loads, alongside the default-blue `#5C9CF5` accent. This also applies with
automation, dark scheme and high contrast. Library and constructed-state no-input
defaults remain `{}`. Menu/dialog scenes retain their original sparse inputs;
dark adds `{"scheme":"dark"}`, and the partial-profile scene supplies
`{"tokens":{"colors":{"accent":"#8657b8"}}}` unless other explicit options
add fields. Input is installed before the first visible frame.

The everyday toolbar contains three icon-only tools on one centerline: the current
Light/Dark scheme (sun/moon, click to switch), an accent swatch opening the preset
menu, and sliders opening **Appearance**. One trailing single-line status reports
the saved-accent count. The neutral sidebar consumes IA's ordinary selected-row
appearance, rather than an inverse local palette.

**Appearance** is a non-native, Window-hosted `IADialog` containing the persistent
appearance editor. Closing and reopening it retains manual hex/JSON drafts and
the Input editor disclosure, without replacing gallery consumers. Defaults,
expanded palette readouts/reset actions, Amber/Danger live pickers, Save and its
destination/status, Touch targets, 7:1 preview, and the JSON/input tooling live
there. A Light/Dark segmented choice shares its first row with Defaults; the
palette pickers, resets and Save wrap in a separate group below. Four independent
trailing switch rows control Touch targets, 7:1 preview, Input editor and Reduced
motion. Native user toggles, including dragging, write through the existing
appearance writer; external input updates checked/selected state without echo.
Input editor is local disclosure state, not appearance input. Hiding it, changing
scheme and Defaults preserve its objects, disclosure and pending hex/JSON drafts.

Use `--contrast high` or the Appearance window's **7:1 preview** toggle for the text-contrast
visual trial. `--contrast normal` explicitly selects the normal 5:1 design target;
omitting the flag leaves the input unchanged and the library default normal.
Other contrast values are rejected. Light/Dark preserves the contrast selection,
and Defaults replaces the input with `{}`, returning to the normal light profile.
The normal target gives margin above WCAG AA's 4.5:1 requirement for normal-sized
text; the optional 7:1 target matches AAA's normal-text contrast requirement, not
full WCAG AAA compliance or a guarantee for arbitrary application/local colors.

### Live Palette Calibration

The Appearance window's **Accent**, **Amber** (warning/caution) and **Danger** (error) buttons
show applied root colors as swatches and hex readouts. Accent opens an anchored
menu of twelve suggested calibration accents plus **Custom…**, which opens its
existing live color dialog. Each preset shows its original swatch, name and hex.
Saved presets have grey name/hex text and a saved accessibility description, but
remain selectable by pointer or keyboard. The closed button names the matching
preset and uses the same saved grey; unmatched or translucent accents say Custom
with the actual color readout. Amber and Danger still open their live pickers.
All use thin playground adapters over the shared `IAColorEditor`/`IAColorDialog`.
Opening or synchronizing does not create an override. Color
and alpha changes apply immediately through the same appearance-input writer as the JSON
editor and external input, so the real library consumers update while the picker
is open. There is no Apply/Cancel transaction: closing keeps the current live
color, with no rollback.

Amber/Danger picker corrections belong to the **current effective accent**. A
different accent restores their underlying explicit input keys (including their
original raw values), or removes them so generation resumes, in the same snapshot
as the accent edit. Equal full-precision RGBA assignments keep corrections;
precision/alpha changes count as different. Returning to an old accent does not
revive its corrections. Scheme, contrast and unrelated token patches keep them.

**Reset amber / Reset danger** restores the picker-owned baseline, or removes an
ordinary explicit semantic root when no picker owns it. **Reset accent** removes
the accent and clears both picker-owned corrections together, even at default blue.
Empty containers are pruned; no remembered generated color is inserted. Explicit
key presence remains resettable even when its value equals generation.
Scheme, contrast, text scale, typography, metrics, other color overrides and form
drafts remain intact. The separate **Defaults** action still replaces the whole
input with `{}`. Accepted JSON/automation replacements and complete `tokens` subtree
replacements take ownership, including identical accepted input; rejection retains
current correction records. Generic warning/error token writes take that role's
ownership. No operation prompts or autosaves calibration corrections. This
disposable behavior does not discard form, dialog, manual-hex or JSON drafts.

The input editor retains its manual Accent hex field and **Set** action. Pending
hex text survives picker edits and external appearance changes until applied or
explicitly cleared with **Reset accent**; an untouched field follows the applied
accent. Reset accent clears only that hex draft, not the pending replacement JSON
draft. JSON and external changes synchronize the already-visible persistent
editor without closing/reopening its native Window or emitting a role write.

The shared picker is a nonmodal IA dialog with Hex/RGB/HSV/HSL fields, alpha,
physical HSL plane and native hue/opacity sliders. Hex uses Qt #RRGGBB or
#AARRGGBB (ARGB) notation. Valid native field editingFinished commits live;
invalid text stays unapplied and visibly identified. Modes and drafts persist.
Normal native focus loss can finish a field before a Close click; dismissal
handlers themselves never apply pending drafts, save or undo. Close, header and
Escape use native dialog behavior. Shared appearance resolution and explicit
local overrides remain authoritative; arbitrary accents promise no contrast target.

For generic consumers, `IAColorEditor` and `IAColorDialog` take `colorValue`, expose
readonly `currentColor` and emit user-only `colorEdited(color)`. Install an accepted
value synchronously in that callback; ignored or transformed requests reconcile
the preview to the actual input without an edit echo. `IAColorDialog.parentWindow`
is the explicit compatible QQuickWindow source (QQuickView is supported), with
native Window hosting and CloseOnEscape. Main-window and peer clicks do not
dismiss it. `IAColorPlane` is the narrow public-handler two-axis control beneath
the editor, not an application palette writer.
The plane uses QtQuick.Shapes with a component-local CurveRenderer and a live
offscreen texture for its existing cached surface plus separate marker. This
preserves rounded clipping and antialiased inner edges without global MSAA;
marker moves update that texture, not the CPU color image. QtQuick.Shapes must
be available at runtime alongside the other Qt6.12 QML modules.

### Save Calibration Samples

**Save** appends one complete snapshot of the three applied root tokens—`accent`,
`warning` (Amber), `error` (Danger)—with resolved scheme/contrast, UTC timestamp and
temporary correction flags. It saves applied values, never pending dialog text,
manual-hex/JSON drafts, historical selection captions, local purple, or form data.
Explicit `primarySurface`/`cautionSurface`/`destructiveSurface` overrides remain
independent and are explained inline; saving the root tuple is not saving every
visual role. The success family stays generator-owned with no fourth picker.

The default destination is the configured Qt **DocumentsLocation** plus
`InterActive/palette-samples.txt`. The path is displayed in the Appearance window.
`--palette-samples-file <absolute-path>` selects an intentional alternate destination.
Launch, opening, dragging, closing, resets and Defaults create no sample data;
only Save creates the parent/file. Unusable Documents locations produce an inline
Save error rather than a working-directory fallback. Temporary initial suggestions
are never reapplied by Defaults, resets or returning to blue.

The file is UTF-8 JSON-lines text: one compact version-1 object plus LF per Save.
Every color contains rounded human `hexArgb` (`#AARRGGBB`) and exact normalized
`rgba16` `[R,G,B,A]` channels in 0–65535. For example:

```json
{"version":1,"utc":"2026-10-04T20:00:00.000Z","scheme":"light","contrast":"normal","accent":{"hexArgb":"#ff5c9cf5","rgba16":[23644,40092,62965,65535]},"amber":{"hexArgb":"#ffff7a18","rgba16":[65535,31354,6168,65535]},"danger":{"hexArgb":"#ffff3434","rgba16":[65535,13364,13364,65535]},"corrections":{"amber":true,"danger":true}}
```

Repeated Saves intentionally append duplicates. A cooperating `QLockFile` excludes
interleaving, without waiting or application-forced removal of live locks. Qt's
built-in dead-owner/reboot recovery remains available; age-only expiration is
disabled. Appending a bounded line never loads/replaces history. A missing trailing
LF receives a separator before the new record, preserving its orphan fragment.
Short writes and uncertain flush/close/readback failures preserve whatever reached
disk and warn that retrying can duplicate data; there is no replay/repair/rollback.
Success appears as **Last saved …** only after exact filesystem-visible readback,
with that immutable saved tuple/time. This is not a power-loss durability guarantee
or protection against unrelated noncooperating editors. Save errors are separate
from appearance rejection errors and retain all applied values and drafts.

The menu's read-only history index reads exactly the displayed destination when
it is installed, immediately before opening the menu, and after every Save attempt.
It matches fully opaque accents at QColor's displayed RGB8 precision, while saved
RGBA16 precision and duplicate records remain unchanged. Any valid matching record
counts, regardless of scheme, contrast or correction flags. Selection does not
replay saved Amber/Danger companions and preserves pending hex, JSON and form edits.
External additions appear on the next refresh; there is no polling or watcher.
Missing history is known empty and creates nothing. Malformed/oversized records
and incomplete tails are skipped with a count; unavailable history clears the
index and has its own visible status, independently of Save and appearance errors.
Reading never locks, repairs or rewrites the file; only explicit Save appends it.

Samples are not replayed, promoted into defaults, or used for automatic learning.
The starting tangerine/red inputs are calibration suggestions, not aesthetically
approved defaults or guaranteed 5:1/7:1 pairs. Existing truthful diagnostics remain.
The Appearance window wraps picker/reset pairs and scrolls when enlarged text/touch settings
need more height, leaving the gallery reachable.

Trace scopes remain opt-in/inert. Picker handlers use fixed `qml-color-handler`
and `qml-sync-picker` scope IDs with `args.aux` role values **0 Accent / 1 Amber /
2 Danger**. Legacy Accent bridge methods keep their original mapping. Serialization
and sample I/O happen only on Save, never on a drag.

Quick tests inject fresh `QTemporaryDir` destinations before loading any fixture;
`QStandardPaths` test mode does not sandbox Documents. Real hue/intensity/alpha
drags, simultaneous role dialogs, resets, applied disk tuples, preserved drafts/
identities and 1200/360/320px geometry with enlarged text/touch settings are checked
by `interactive.playground`, using the actual owned IA controls and generic
currentColor contract rather than private Qt fallback probes.

### Gallery Consumers

The Profile preferences specimen includes a real `IAComboBox` for Delivery
schedule, synchronized with the same daily/weekly draft as the radio examples.
External draft changes do not activate it; interactive selection updates the
draft and Apply commits through the existing fixture owner.

The desktop application ID is `interactive-playground`. All fixture data is
local and deterministic. Apply updates a separate committed summary; the profile
editing dialog has its own Apply/Cancel transaction. Appearance changes do not
recreate controls or clear drafts. See the README's test-integration section for
public fixture methods and stable object names.

The solid semantic button specimen compares `Presentation.Primary`,
`Presentation.Caution` and `Presentation.Destructive` on three otherwise identical
buttons. Captions are Primary, Caution and Danger. Primary retains the exact default
blue `#5C9CF5`; missing caution/danger companions derive from its visible tone and
intensity using the shared amber/rose hue seeds. Foundation derives readable,
hue-related text and interaction colors; the specimen supplies only tones, with
no local fill, foreground, border or interaction-color recipes. Caution is an
action tone, distinct from the `Warning` validation state.

The color readouts show the effective shared button surfaces, including application
overrides. Live edits to `accent`, `warning` and `error` update the existing sample
controls; the slider/progress fills continue to use `accent`, independently of an
explicit `primarySurface` override. The sample model contains only stable names
and tones, so palette changes do not rebuild its delegates. Clicking a sample
records its name and current effective color in the selection caption without
changing application appearance. Later palette changes leave that recorded
selection intact; fixture reset clears it. Existing `opencode-*` object names
remain available to consumers.

The compact **Saved successfully** message consumes existing `successSurface` and
`successText` roles, with a `success` swatch and live readout. Success has no new
button tone. The generated palette is a visual trial for review, and arbitrary
explicit inputs can produce measured `palette-collision` or `low-contrast`
warnings. Neither perceptual distance nor these specimens establish whole-app
accessibility or danger recognition.

Semantic labels use `accentText`, `warningText` and `errorText`, not the bright
fill seeds. Both contrast modes derive hue-related button text first and keep
semantic borders matched to it. Default blue stays exact in both schemes/modes;
missing companions are finalized for the selected contrast target, rather than
retaining fixed historical amber/rose outputs or applying blanket lightening.
Semantic and local-derived hover/pressed fills blend toward white for dark labels
or black for light labels, whichever gives greater label contrast, independently
of page-main text. A common label is then derived over the actual three surfaces,
including explicit state overrides, to target 5:1 normal or 7:1 high contrast at
both full precision and rounded RGB8.

The existing local-purple specimen explicitly chooses deeper `#6A3D99` in high
mode and retains `#8657B8` in normal mode; its light label remains centrally
derived. That fill choice belongs to this caller, not a silent library rewrite
of explicit colors. Explicit application/local colors and state overrides remain
unchanged when their combinations cannot meet the selected target, with
`low-contrast` warnings rather than a claim of compliance. Border, focus and
disabled-state policies are unchanged.

## Automation Protocol

`--automation` enables nonblocking line-delimited JSON on stdin. The supported
commands replace the complete application appearance input or close normally:

```json
{"command":"setAppearance","input":{"scheme":"dark"}}
{"command":"close"}
```

Stdout contains `IA_EVENT ` followed by compact JSON; normal Qt logs go to stderr.
Rendered observations have this shape:

```json
{
  "protocolVersion": 1,
  "event": "ready",
  "page": "foundations",
  "scene": "menu",
  "input": {},
  "resolvedScheme": "light",
  "viewport": {"width": 1200, "height": 820},
  "dpr": 1,
  "qtVersion": "6.12.0",
  "renderer": "opengl",
  "popupWindow": true
}
```

`ready` and `appearanceChanged` wait for an active expected host and submitted
frames from the main scene and open popup hosts. Old in-flight frames cannot
acknowledge a newer appearance snapshot. This automation mode currently requires
OpenGL RHI; the renderer is inspected, not inferred from an environment variable.
`popupWindow` reports a distinct, exposed content host, not merely a requested
`Popup.Window` enum. It does not establish compositor placement.

`setAppearance` uses the same application input path as the editor. Close and EOF
emit exactly one `{"protocolVersion":1,"event":"closed"}` record and exit
normally. Invalid commands or rejected input envelopes emit `event:"error"`
with a diagnostic message and exit unsuccessfully. Input is bounded to 1 MiB and
256 queued commands. Close/EOF can cancel a pending observation. Render readiness
has a 15-second deadline; external clients should also bound waits and normal
process exit rather than infer success from an open process.

### Native-Input Bootstrap

On Wayland, supported Qt versions need native input and its grab serial before
opening an anchored Window menu; activation alone is insufficient.
For startup `--scene menu --popup window`, the application waits until its parent
is active, exposed and has submitted a frame, then emits:

```text
IA_EVENT {"protocolVersion":1,"event":"inputRequired","key":"Shift_L"}
```

The caller supplies that native key event to the focused application through its
test environment. No input-injection utility, virtual machine, compositor
management, or machine identity is embedded in the application. A synthetic
in-process Qt event is not equivalent to native Wayland input. The request and
the subsequent rendering share the original readiness deadline.

Without automation, this startup scene prompts on stderr for Left Shift in the
focused window. The default scene's ordinary Collection actions button needs no
bootstrap: a real click supplies the input. Item menus and dialog scenes do not
request the modifier. This describes existing playground behavior, not a library
initialization requirement.

## Review Capture Tooling

`--screenshot <absolute-path>` waits for the existing automation `ready` condition
(active/exposed hosts and submitted frames), grabs the main `QQuickWindow`, saves
a PNG and exits successfully. A missing/relative path, readiness timeout, empty
grab or save failure is reported on stderr with a nonzero exit. The destination's
parent directory must already exist. Screenshot mode does not read stdin, even
with `--automation`; EOF cannot cancel the one-shot capture. It uses the same
OpenGL RHI requirement and 15-second readiness deadline as automation.
Use `--popup item`: main-window grabs do **not** include separate Window popups.

The opt-in `interactive.capture` CTest suite is labeled `interactive-capture`
only, with a 600-second timeout; it is not part of `interactive-overview`.
Set `IA_CAPTURE_DIR` to an **absolute, existing, writable directory** in the
disk-backed guest workspace. Missing or unusable destinations fail clearly.
For example, inside the project's authorized graphical test VM, with the same
SDK/import environment as the other suites and disk-backed `TMPDIR`:

```sh
IA_CAPTURE_DIR="$PWD/build/captures/review/states" \
  ./build/bin/interactive-quick-tests -input tests/quick/qml/tst_capture.qml
# Or: IA_CAPTURE_DIR=... ctest --test-dir build -L interactive-capture --output-on-failure
./build/bin/interactive-playground --page foundations --popup item --scene menu \
  --appearance dark --contrast high --screenshot "$PWD/build/captures/review/window/menu-dark-high.png"
```

Each light/dark × normal/high mode produces 11 labeled family sheets: four button
tones; neutral checkable buttons; text/icon-only tool buttons; tabs in a tab bar;
delegates in a list; items in an open item-hosted menu; check boxes, radio buttons,
switches, slider and text field. Rows are the relevant states (including pointer
hover/held press, checked/selected, Tab keyboard focus, disabled and field
validation/read-only states). Control regions include `2 × focusWidth` on every
side; real parent clipping is preserved, not repaired by the capture tooling.
Four further PNGs per mode show a focused last field in a scrollable dialog,
focused first/last delegates in a clipped list, and a focused tab in its bar.
The 60 PNGs live under `<scheme>-<contrast>/`; `manifest.txt` records each PNG's
family, scheme, contrast and included states/columns. The suite checks capture
setup and successful writes, not visual correctness or golden-image equality.

The external laptop wrapper `ark-vm-build --capture` builds in VM
`arch-niri-interactive-devbase-interactive-palette-cpu` on ark, then runs this suite
and 12 real playground window shots (default/menu/dialog × light/dark ×
normal/high, item-hosted popups), after tests when `--test` is also supplied.
Guest outputs stay under `/home/tester/interactive/build/captures/<run>/`, never
RAM-backed temporary storage. It prints `CAPTURE_OK` only for the complete
72-PNG run, copies captures and the manifest to the laptop run directory's
`captures/`, and logs their count/path in `run.log` (`capture.log` has guest
details). Capture or copy failures are reported without blocking preview refresh.
These are agent review artifacts under `~/TemporaryFiles/InterActive/ark-vm-build/`,
not final screenshot delivery to the user. No image processing runs on the laptop.

## Visual Review

`interactive.appearance-capture` has its own `interactive-appearance-capture`
label, separate from overview and the fixed 72-frame capture run. In the same
graphical guest environment, set `IA_CAPTURE_DIR` to an existing directory outside
that run and execute `interactive-quick-tests -input
tests/quick/qml/tst_appearance_capture.qml`. The fixture imports real `Main`, opens
Appearance through its toolbar action, proves distinct native hosting and bounded
height, then grabs the entire host content extent. Ten full frames cover
light/dark, normal/high and closed/expanded Input editor, plus two light-normal
RTL frames driven only through Main's application direction. Its manifest records
actual window bounds, region boxes and the unmodified sample destination text.
That destination includes the test's temporary-directory suffix and is not a
deterministic image region; it is not hidden or replaced for comparison.

The consumer suite also checks scheme pointer/RTL arrows/Space endpoints, resolved
consumer colors, external synchronization without user activation, switch peer
independence/drag/reduced-motion endpoints, and 320/360/desktop row geometry with
RTL and enlarged touch text. Existing picker, saving, draft, focus and reopen
checks remain in the same suite.

Main's Window is the single application mirroring owner, including its overlay
and, through the shared native-popup adapter, separate Appearance popup.
Actual-Main tests set only `rightToLeft` for
initial LTR/RTL and live changes while Appearance stays open; they check native
header/footer and row edges, scheme selection, object/draft/focus preservation
and close/reopen without injecting mirroring into the dialog or editor.

`interactive.popup-direction` exercises all four real popup roots in Item/native
hosting, public overlay inheritance flags, first native-frame state, live changes,
reopen, content/type/modal changes, destruction, opposite-direction host migration
and a nested native menu. Both hosts' public mirror notifications are checked
throughout, including preserved input bindings and idle-overlay pass-through.

Capture the whole composited scene when inspecting Window popups; grabbing only
the main QQuickWindow omits separate surfaces. Check for obstructions, native
focus, effective dimensions and actual DPR before interpreting an image. Qt
global positions are not compositor-placement evidence on Wayland.

Review light, dark and partial appearance in the default, menu and dialog scenes,
using both popup hosting modes. Inspect alignment, typography, contrast, state
distinctions, and popup anchoring. Images demonstrate only their depicted state;
pair them with interaction and geometry assertions. A rendered acknowledgement
does not itself establish that a screenshot is unobstructed or visually correct.
Also inspect the accent toolbar and open picker at desktop and narrow widths,
including live updates and reset. Review the shared IA picker against its native
controls, square plane, measured lane centerlines and responsive layout; default
reference dimensions are not fixed universal geometry.

Desktop orchestration, image collection and environment-specific execution
records are external to the project. The repository provides the playground and
native tests, not a machine-management layer.
