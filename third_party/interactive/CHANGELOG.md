# Changelog

## [0.1.1] - 2026-11-22

Add the native-template IAComboBox with model/role selection, editable input,
inherited-value text, optional swatches and Item/Window popups. IAStatusBanner
gains optional icons and an actionRequested callback. Checkboxes and radios
align their leading indicator edge with surrounding content; switches retain
their existing insets. The dialog heading owns a permanent separator, and all
dialog dividers preserve the native side outline.

Existing accent on-state colors, square dialog Close target, owned scrolling,
RejectRole handling and native button-box sizing are retained, not new APIs.
Completed Ok -> Add source renaming, repeated caption/font changes and dynamic
button models now have explicit regression coverage. Footer tests compare exact
uncovered background pixels at fractional bounds before checking pixel-aligned
role colors; no product geometry or pixel tolerance was changed.

Qt 6.12.0 remains the minimum. Source/build-tree consumption is supported;
installation, exported packages, stable API, full control/gallery coverage and
AT-SPI/Orca qualification remain outside this source preview. Known native
baseline findings and historical unresolved issues are recorded in
[Known limits](README.md#known-limits); this is not an all-tests-green release.

## [0.1.0] - 2026-11-15

Initial source preview of the Foundation, Controls and Components modules and
Foundations playground. Source/build-tree consumption is supported; this is not
an installed or relocated package, stable API, complete gallery or accessibility
certification. Install/export/package configuration, remaining controls/pages,
full current-Qt verification and AT-SPI/Orca proof remain outstanding.

Known native findings include two accepted Item-dialog footer-bottom pixel
failures, older high-DPR icon fixture/oracle failures, an unresolved Playground
SIGSEGV and unresolved dialog/window input-readiness failures. Later passing
scoped runs do not establish their cause or correction; unresolved findings are
not waived. Hosted build/lint/three-unit-suite success is not all-tests-green or
native GUI acceptance. See [coverage](docs/control-coverage.md) and
[verification](docs/playground.md) for the scope and remaining obligations.

History below groups the original non-merge content commits by local calendar
date (America/Los_Angeles), newest date first. Temporary trials are recorded as
historical steps, not current features; merge-only commits add no unique content.

### 2026-11-15

#### Fix

Group-track derivation now distinguishes the surrounding canvas as well as the
selected segment, preserving explicit colors. Shared ownership corrections cover
segmented selection/reentrancy, HSV hue edits, custom dialog sizing and popup
host migration, with corresponding regression assertions.

- Distinguish group tracks from segment and canvas (`cffadc7`).
- Reconcile control state and popup/color/dialog ownership (`e5860fd`).

#### CI

Add a hosted build, QML lint and headless unit lane to record an explicit
verification scope. It builds the full project but runs only appearance, trace
and color-edit-state units; native GUI acceptance remains separate.

- Add hosted build, lint and three headless unit suites (`341f1e2`).

### 2026-11-14

#### Feature

Replace the playground's external color picker with shared IA color editing and
dialog compositions, including a specialized HSL plane, persistent drafts and
full-precision application authority. Tabs and segmented choices gain a shared
leading accent marker that persists through hover, press and focus.

- Add IA color plane/editor/dialog and shared dialog-header coverage (`3115552`).
- Mark current tabs and segmented choices on their leading edge (`b1cd0c9`).

### 2026-11-12

#### Breaking

Dialogs now own their ordinary body viewport, height cap and vertical scrolling,
while footer layout publishes horizontal geometry during Qt's polish phase.
Ordinary bodies move from a `contentItem` replacement to dialog children; custom
`contentItem` replacement remains an explicit replacement of that viewport.

- Give dialogs body sizing/scrolling and native footer layout ownership (`4218a69`).

#### Fix

Native popup direction follows public source-overlay inheritance without writing
application hosts or prematurely constructing custom content. The playground
uses the shared segmented/switch controls while retaining editor disclosure and
drafts; tests cover popup lifecycle and direction propagation.

- Preserve popup direction and integrate shared appearance choices (`414d57a`).

#### Feature

Add native-template segmented choices and trailing switch rows for reusable
settings, plus capsule color wells with preset, Default and custom-color requests.
Applications retain selection/color authority; the controls supply their own
layout, appearance and public accessible interfaces.

- Add segmented choices and trailing switch placement (`a6c78e8`).
- Add capsule color wells to palette settings (`cd034d4`).

### 2026-11-11

#### Breaking

Raise the supported minimum to Qt 6.12.0 for Qt's explicit native popup sizing
fix on both axes. Native Wayland popup tests now require observed compositor
input rather than treating in-process key dispatch as equivalent proof.

- Require Qt 6.12.0 and verify native popup input (`6b6427e`).

#### Fix

Menu navigation highlights now use the exact effective accent/on-accent pair,
with an inward conditional outline when needed against the popup surface.
Explicit paired colors remain caller-owned, and check state stays independent
of navigation highlight.

- Use exact accent menu highlights and conditional borders (`3a07829`).

#### Feature

Tabs share a Foundation segment-layout helper while preserving Qt's native
container behavior. A default inter-segment gap improves separation without
recreating the group track during appearance or selection changes.

- Share tab layout and add configurable segment spacing (`28b7f4d`).

### 2026-11-10

#### Fix

Choice content uses resting-border geometry so keyboard focus thickens the
outline inward without moving or shrinking the center, track or thumb.
Geometry and rendered-state assertions cover that stable layout.

- Keep choice indicator geometry stable across focus (`95e40ce`).

### 2026-11-06

#### Feature

Unify focus, hover/selection feedback, choice rows, tab/menu structure and value
tracks through shared Foundation outputs. Dialogs gain principal-action and
heading/inset roles, while the playground moves appearance editing into a
persistent dialog behind a compact toolbar; explicit local colors retain ownership.

- Paint keyboard focus inside each control's own border (`a77067b`).
- Derive distinct hover/selection fills with measured diagnostics (`7648c83`).
- Share outlined choice rows with accent centers (`d4054de`).
- Add segmented tab tracks, fixed menu slots and sparse separators (`cfa3b05`).
- Add quiet scrollbar thumbs and independent value-track roles (`8dc6259`).
- Add dialog principal-action, heading and inset roles (`514d576`).
- Simplify the toolbar and preserve appearance-editor state in a dialog (`5fd1b7b`).

### 2026-11-04

#### Feature

Expose shared immutable state tables keyed by control family, tone and validation
so ordinary consumers reuse completed interaction colors. The additive state
lookup API retains snapshot ownership and effective-value notifications.

- Add palette-shared interaction state tables (`287fe17`).

#### Refactor

Replace repeated facade bookkeeping with typed records, generated notifications
and indexed QML state selection. Split schema validation, palette building and
color mathematics behind a private appearance-source boundary, and share one
instrumentation facility without changing the public control aliases.

- Use typed facade records and generated notifications (`4c4dc22`).
- Select completed state colors in QML (`bfe323f`).
- Split token responsibilities and seal the public appearance header (`1215335`).
- Unify work counters and trace spans (`f26cb50`).

#### Performance

Share observation of application font/screen changes and skip unrelated facade
work using completed-update masks and cached diagnostics. Engine input and local
overrides remain separately owned while unchanged work is reused.

- Share the font environment and skip unaffected appearance work (`c6ea86e`).

### 2026-11-03

#### Breaking

Buttons and tool buttons switch to shared inset press feedback with a visual
content shift instead of a pressed fill. Their local `pressedBackgroundColor`
alias is removed; other controls and the appearance facade retain that property.

- Replace button press fills and the temporary trial with a shared inset (`6e21448`).

#### Feature

Add explicit rendered capture tooling and work-count/CPU-time performance gates
to separate artifact production from visual acceptance and performance evidence.
The playground can save its main window after readiness; separate popup windows
still require whole-scene capture.

- Add state captures and the playground screenshot option (`e9cad37`).
- Add work-count and CPU-time performance gates (`1d34b83`).

### 2026-11-02

#### Fix

Align initial controls and playground content with shared IA appearance and
layout behavior. Derived labels aim for 7:1 with strongest tested fallback,
while normal/high diagnostic floors and explicit caller colors remain distinct.

- Align control aliases, separator colors and playground presentation (`aa8c722`).
- Derive labels at 7:1 and remove the temporary contrast trial (`0ba29e9`).

#### Feature

Import the initial engine-local appearance library, Qt-template controls,
compositions, Foundations playground and native test sources. Temporary contrast
and pressed-look specimens support the subsequent visual choices; both are
removed by later commits rather than shipped as separate current controls.

- Import the library, playground and tests (`ae01d17`).
- Add the temporary label-contrast comparison (`22a4cf7`).
- Add the temporary pressed-look comparison (`66b62b8`).

#### Refactor

Consolidate CMake requirements/output paths, test dependencies and trace setup,
and remove redundant code/build leftovers. ShaderTools is declared with the
top-level Qt requirements instead of rediscovered inside Foundation.

- Clean up source, CMake and test leftovers (`10d8e38`).

#### Docs

Establish the project README, LGPL-3.0-or-later terms and third-party attribution.
Then describe current functionality and planned coverage neutrally rather than
using worktree-specific review instructions as product documentation.

- Add the README and license texts (`a81105b`).
- Make documentation neutral and current (`76cc6ee`).
