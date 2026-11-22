# InterActive

InterActive is a Qt 6.12.0+ library of explicitly imported, IA-owned Qt Quick
controls. Qt templates provide interaction semantics; IA owns the appearance
resolution and visuals. It does not install a global Qt Controls style or require
an IA window, portal, shell, or appearance root.

Status: pre-release, no stable public API yet. The playground currently has one
page, Foundations; the planned controls and pages are listed in
[control coverage](docs/control-coverage.md).

## Modules

| # | Module / Target | Purpose |
|---|---|---|
| 1 | `InterActive.Foundation` / `InterActive::Foundation` | Engine-local appearance, typed tokens, shared control resolution. |
| 2 | `InterActive` / `InterActive::Controls` | IA visual controls rooted in Qt templates. |
| 3 | `InterActive.Components` / `InterActive::Components` | Used compositions: `IAFieldRow`, `IASettingsRow`, `IAStatusBanner`, `IAEmptyState`, `IAColorEditor`, `IAColorDialog`. No speculative stubs. |
| 4 | `InterActive.Playground` / `interactive_playground_scene` | Non-installed shared QML backing target and generated plugin, real gallery fixtures and engine-local `PlaygroundState`. |

See [architecture](docs/ARCHITECTURE.md), [appearance](docs/appearance.md), and
[verification / automation](docs/playground.md) for the contracts.

## Use From Source

Use a source checkout or unpacked source archive as a subdirectory of an existing
Qt Quick application. For an existing executable target `my_app`, add this after
the application's normal Qt setup and target creation:

```cmake
set(INTERACTIVE_BUILD_PLAYGROUND OFF)
set(INTERACTIVE_BUILD_TESTS OFF)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/vendor/interactive"
                 "${CMAKE_BINARY_DIR}/interactive")
target_link_libraries(my_app PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
    InterActive::Foundation InterActive::Controls InterActive::Components)
target_compile_definitions(my_app PRIVATE
    INTERACTIVE_QML_ROOT="${CMAKE_BINARY_DIR}/interactive/qml")
```

Replace `vendor/interactive` with the source location. The library requirements
below still apply; QtTest, QuickTest and Playground are not required when both
options are OFF. With this subproject binary directory, the generated QML import
root is `<consumer-build>/interactive/qml`. Before loading application QML:

```cpp
QQmlApplicationEngine engine;
engine.addImportPath(QStringLiteral(INTERACTIVE_QML_ROOT));
// Load the application's QML after adding this path.
```

Alternatively, add that root to `QML_IMPORT_PATH` while preserving the selected
SDK's existing paths. Linking the CMake aliases alone does not supply runtime QML
import discovery. Import `InterActive`, `InterActive.Foundation` and
`InterActive.Components` as needed, using a plain Qt Quick Window or QQuickView.
Keep the matching Qt libraries, plugins and QtQuick.Shapes module available.

The recorded Qt 6.12.0 source-consumer run on 2026-11-15 built without
QtTest/QuickTest/Playground and observed `Count: 1` plus application/editor color
`#234567` on a subsequent rendered frame. That is build-tree consumption proof,
not installed-package or relocation proof. `cmake --install` integration,
exported targets and `find_package(InterActive)` support are not implemented.

## Appearance

```qml
import QtQuick
import QtQuick.Window
import InterActive

Window {
    visible: true
    width: 360
    height: 240
    IAButton {
        anchors.centerIn: parent
        text: "Publish"
        backgroundColor: "purple"
    }
}
```

No supplied input resolves to complete light defaults. Applications own
appearance acquisition and supply a replacement snapshot to the engine's
`InterActive.Foundation.Appearance.input`. Local IA appearance properties take
precedence; `resetAppearance()` removes those overrides. Native `palette` and
`font` are not a second supported appearance API.

The playground has one C++ input writer, `PlaygroundState.setAppearance(input)`.
The editor and stdin automation use that same path. It obtains the engine-local
Foundation singleton through `QQmlEngine::singletonInstance`; no competing QML
`Binding` writes the input. Rejected envelopes preserve the prior valid snapshot
and report an error. Field diagnostics remain visible in the input inspector.

## Playground

The executable target is `interactive_playground`, output
`<build>/bin/interactive-playground`. The options are listed in
[playground options](docs/playground.md#running-the-playground).

The initial viewport is 1200x820. At narrow widths the same controls reflow and
scroll; no second widget tree is created. Width and height must be at least 320.
Omitted scalar options do not populate input fields. Eligible default-scene launcher
startup adds temporary Amber `#FF7A18` / Danger `#FF3434` corrections beside default
Accent `#5C9CF5`, consistently with automation, dark and high contrast. Library
no-input defaults and a later **Defaults** action remain `{}`. Menu/dialog inputs
remain sparse; dark adds `{"scheme":"dark"}`, and `--scene partial-profile` is
`{"tokens":{"colors":{"accent":"#8657b8"}}}` unless explicit appearance,
density, or scale arguments add fields. Startup input is set before loading QML.
The desktop application ID is `interactive-playground`.

On Wayland, non-automated `--scene menu --popup window` prints a stderr prompt
and waits for **Left Shift in the focused playground window** before opening.
Supported Qt versions require native Wayland input and its grab serial to create
an anchored menu popup; activation alone is not sufficient. The default scene's
ordinary Collection actions button needs no bootstrap because its real click
supplies that input.

Edit Maya Chen's local `example.test` profile, Apply to update the separate
committed summary, or Reset to restore the committed draft. The dialog has its
own transaction: Apply updates the form and commits; Cancel preserves both the
pre-dialog form draft and the committed profile. Choices, tabs, collection
selection/pinning/removal, slider readout, and progress have visible fixture
outcomes. These fixtures never access the network or persist personal data.

The appearance editor supports default/light/dark, compact/touch, scale, accent,
font family, radius, spacing, contrast, reduced motion, partial input, and JSON
replacement. Controls and popups are persistent objects during input changes.
The state shelf includes disabled, primary, destructive, read-only, invalid,
and an explicit purple/font/radius override with a local reset action.

Three live **Accent / Amber / Danger** pickers use the shared IA color editor/dialog
and the same input writer. The editor retains full-precision color, alpha,
Hex/RGB/HSV/HSL fields and live HSL plane/rails. Amber/Danger corrections are temporary for the current accent;
a different effective accent restores their underlying inputs/default generation
without a prompt or autosave. Closing keeps live edits. **Save** appends the three
applied root colors to DocumentsLocation + `InterActive/palette-samples.txt`, or
the supplied absolute CLI path. Files are created only on Save. Version-1 JSON
lines retain readable ARGB hex and precise RGBA16 channels, scheme/contrast/time
and correction flags. Duplicates are allowed; samples are never automatically
loaded or promoted. See [calibration details](docs/playground.md#live-palette-calibration)
for reset/ownership, overrides, preserved drafts and honest append/readback failures.

Missing danger, caution and success colors now derive centrally from the effective
accent's visible brightness and intensity, while neutral light/dark chrome stays
neutral. The default primary remains exactly `#5C9CF5`; explicit semantic colors
and local purple remain caller-owned. The Foundations page presents this bounded
visual trial through the live picker, semantic buttons and a success message.
Measured `palette-collision` warnings report unresolved distinction separately
from text-contrast warnings; neither measurement is an accessibility guarantee.

## Test Integration

Parent test targets link `interactive_playground_scene` and import
`InterActive.Playground`. The generated `PlaygroundState` singleton factory obtains
the test engine's Foundation source automatically; no application root, context
property, or process-wide singleton pointer is needed. Tests can instantiate
`FoundationsPage { width: 1200; height: 820 }` inside their normal QuickTest host.

Before each case, call `PlaygroundState.resetFixtures()` and
`PlaygroundState.setAppearance({})`. Close existing popups or destroy the page
between cases. Fixture reset deliberately does not reset appearance or close
popups. `Main` is a plain Window fixture for full-launch tests; its public
`initialScene`, `windowPopups`, and `rightToLeft` inputs match the CLI.

| # | Public State Surface | Meaning |
|---|---|---|
| 1 | `draftName`, `draftEmail`, `optIn`, `notifications`, `delivery` | Writable form draft; delivery accepts `daily` or `weekly`. |
| 2 | `dirty`, `validDraft`, `committedSummary` | Observable draft/commit outcomes. |
| 3 | `applyDraft()`, `resetDraft()` | Commit valid draft; restore last committed values. |
| 4 | `beginDialog()`, `dialogName`, `dialogEmail`, `validDialog`, `applyDialog()`, `cancelDialog()` | Separate dialog transaction. |
| 5 | `previewValue`, `selectedTab`, `rowSelected`, `pinned`, `collectionVisible` | Public scenario setup: 0..100, 0..1, and booleans. |
| 6 | `setAppearance()`, `setAppearanceJson()`, `setAppearanceOption()`, `setAppearanceToken()` | Shared application input path, returning acceptance. |
| 7 | `appearanceInput`, `appearanceError`, `appearanceRejected(message)` | Accepted snapshot and explicit rejection observation. |
| 8 | `resetFixtures()` | Deterministic scenario reset without changing appearance. |
| 9 | `colorReference` | Selected reference-color caption; does not change appearance or assign semantic roles. |
| 10 | `setSemanticCorrection()`, `resetSemanticCorrection()`, `resetAccentForCalibration()` | Playground-owned warning/error correction lifecycle through the single writer. |
| 11 | `hasAmberCorrection`, `hasDangerCorrection`, `hasAccentOverride`, `hasAmberOverride`, `hasDangerOverride` | Ownership and explicit key presence, independently of color inequality. |
| 12 | `savePaletteSample()`, `paletteSamplePath`, `paletteSaveStatus`, `paletteSaveFailed` | Explicit applied-root append and separate inline Save result; tests inject temporary paths in C++ before UI use. |

`Main` and `FoundationsPage` expose semantic `openMenu()` / `openDialog()` methods.
Normal user buttons and CLI scenes call those same methods. Read-only
`activePopupContent` is an application observation surface for the launcher's
public `QQuickItem::window()` check; `Main.sceneReady` indicates the requested
scene is open, not that it rendered. The launcher separately waits for exposure
and synchronized render acknowledgements from the main window and every host in
the public `openPopupContents` list, including a cascading submenu. Closed menus'
hidden windows are not included. Tests
may inspect public control output; production consumers do not query child DOM
or mutate another component's internal state.

Stable fixture `objectName` values:

| # | Names | Observation |
|---|---|---|
| 1 | `nameField`, `emailField`, `applyButton`, `resetButton`, `committedSummary` | Text edit and Apply/Reset. Summary is an `IAStatusBanner`; inspect `message`. |
| 2 | `choiceCheck`, `choiceSwitch`, `dailyRadio`, `weeklyRadio` | Draft choices. |
| 3 | `valueSlider`, `valueReadout`, `progressPreview` | Slider value, label `text`, progress `value`. |
| 4 | `previewTabs`, `listRow`, `selectionInspector`, `overviewScroll` | Selection, tab content, and scroll. |
| 5 | `menuButton`, `dialogButton`, `mixedMenu`, `deliverySubmenu`, `editDialog`, `dialogNameField`, `dialogEmailField` | Real overlays and editable transaction. Dialog standard buttons use the template's `standardButton()` API. |
| 6 | `menuInspectCollection`, `menuEditProfile`, `menuPinCollection`, `menuRemoveCollection` | Semantic menu actions; Ctrl+E opens the same edit dialog. |
| 7 | `appearanceDark`, `appearanceLight`, `appearanceReset`, `appearanceTouch`, `appearanceEditorToggle`, `appearanceJson`, `appearanceInspector` | Input editing and resolved inspector. |
| 8 | `customPurpleButton`, `resetLocalAppearance` | Local appearance precedence/reset. |
| 9 | `opencode-blue`, `opencode-amber`, `opencode-rose`, `opencodeSelection` | Primary, Caution and Danger semantic samples and their historical selection caption. |
| 10 | `semanticSuccessPreview`, `semanticSuccessText`, `semanticSuccessSwatch`, `semanticSuccessReadout` | Existing success message surface/text and live fill swatch/readout. |
| 11 | `{accent,amber,danger}PickerButton`, `{accent,amber,danger}Swatch`, `{accent,amber,danger}Readout`, `{accent,amber,danger}ColorDialog`, `reset{Accent,Amber,Danger}Button` | Shared live picker composition with stable names; editor public `openAccentPicker()` retained. |
| 12 | `savePaletteButton`, `paletteSaveStatus`, `paletteSamplePath`, `paletteToolbarScroll` | Explicit Save, immutable last-saved/error readout, destination and scrolling toolbar. |

## Automation

With `--automation`, stdin accepts nonblocking, newline-delimited JSON. Only these
commands are supported:

```json
{"command":"setAppearance","input":{"scheme":"dark"}}
{"command":"close"}
```

Stdout contains only `IA_EVENT ` plus compact JSON records (plus `IA_PROFILE` lines when an `--appearance-profile-*` option is set); Qt logs go to stderr.
`ready` and `appearanceChanged` contain `protocolVersion:1`, `event`,
`page:"foundations"`, `scene`, `input`, `resolvedScheme`,
`viewport:{width,height}`, `dpr`, `qtVersion`, `renderer:"opengl"`, and
`popupWindow`. The renderer is inspected, not inferred from the environment.
`popupWindow` means the actual content item belongs to a distinct exposed window,
not just that `Popup.Window` was requested. Old in-flight frames cannot
acknowledge a newer snapshot. There are no readiness sleeps.

Wayland Window-menu startup emits
`IA_EVENT {"protocolVersion":1,"event":"inputRequired","key":"Shift_L"}`
after the native parent is active, exposed and has submitted a frame. Opening
then waits for that native key event in the focused window. Supplying input is
the caller's responsibility; the application does not manage input-injection
tools or the surrounding desktop. See the
[protocol documentation](docs/playground.md#native-input-bootstrap).

Close and EOF produce exactly one
`IA_EVENT {"protocolVersion":1,"event":"closed"}` and normal exit 0. Malformed
commands and rejected appearance envelopes produce an `error` event and failure
exit, not a silent fallback. Input is bounded to 1 MiB and 256 queued commands.
Drivers wait for each appearance acknowledgement before sending the next command.
Close/EOF cancel outstanding observations and queued input, so abandoning a wait
does not depend on an obscured window rendering again.
The launcher has a 15-second render deadline.

## Build And Test

Requirements: CMake 3.21+, a C++17 compiler, and Qt 6.12.0+ with Core, Gui, Qml,
Quick, QuickControls2, Svg, and ShaderTools. Optional test builds also require
Test and QuickTest. Native GUI suites need a working graphical session; the
[hosted CI selection](tests/README.md#hosted-build-lint-and-headless-units) runs
only three headless unit suites after the full build and QML lint.

Qt 6.12.0 is the supported minimum because it includes the native popup sizing
fix on both axes ([QTBUG-147847](https://bugreports.qt.io/browse/QTBUG-147847)),
preserving explicit popup dimensions during content changes.

Run these commands in your test environment:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DINTERACTIVE_BUILD_PLAYGROUND=ON -DINTERACTIVE_BUILD_TESTS=ON
cmake --build build --parallel 4
cmake --build build --target all_qmllint
QML_IMPORT_PATH="$PWD/build/qml${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}" \
  ctest --test-dir build -L interactive-overview --no-tests=error --output-on-failure
```

To select a non-default Qt SDK, pass `-DCMAKE_PREFIX_PATH="$QT_PREFIX"` at
configure time. For an exact Qt 6.12.0 SDK verification lane:

```sh
cmake -S . -B build/qt612-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DINTERACTIVE_VERIFY_QT_VERSION=6.12.0 \
  -DINTERACTIVE_BUILD_PLAYGROUND=ON -DINTERACTIVE_BUILD_TESTS=ON
```

Set `QT_PREFIX` to that SDK's installation prefix. Keep each SDK in a separate
build directory and use its matching
runtime libraries and plugins, including the QtQuick.Shapes QML module used by
the color plane's rounded texture fill. QtTest and QML tests are registered directly with
CTest; no separate orchestration tool is required.

See [the verification contract](docs/playground.md) for suite coverage, runtime
requirements, and the distinction between automated results and visual review.

## Known Limits

This 0.1.1 source preview has no stable API, complete control/gallery coverage,
install/export/package configuration, or AT-SPI/Orca proof. See
[control coverage](docs/control-coverage.md) for the outstanding work and
[CHANGELOG](CHANGELOG.md) for the history-derived release notes.

The Qt 6.12.0 native overview retains these pre-existing v0.1.0 baseline findings:

| # | Case | Observation |
|---|---|---|
| 1 | SegmentMarker/currentMarker(tabs-light-normal-false) | Pixel 326,16: actual #dcdfe0 versus #dddfe0. |
| 2 | SegmentMarker/currentMarker(segments-light-normal-false) | Same pixel and colors. |
| 3 | PopupConsumers/dialogApplyCancelAndModalBlock(item) | Saved: Maya Chen versus Saved: Avery Stone; intermittent original Item Apply outcome. |

The former two Item-dialog footer-bottom failures were fractional-boundary test
oracle defects: identical pixels remain with footer paint absent. Exact uncovered
background comparison and pixel-aligned role assertions now cover that contract.
No product rounding or tolerance relaxation was introduced. Older high-DPR icon
fixture/oracle findings, a Playground SIGSEGV and dialog/window input-readiness
findings (including ColorPicker/dismissalOrdering(header)) remain unresolved;
later passing runs do not identify their cause or establish a correction.
Headless CI and passing scoped native checks do not establish full current-Qt
verification, native pointer injection, AT-SPI/Orca or an all-tests-green result.

## License

LGPL-3.0-or-later. See [LICENSE.md](LICENSE.md), [COPYING.LESSER](COPYING.LESSER),
and [COPYING](COPYING).
