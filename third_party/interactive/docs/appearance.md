<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Appearance

InterActive owns its appearance resolution. Precedence is **complete library
profile, valid application input, explicit component overrides**. It does not
read the desktop palette, discover a system scheme, change Qt's global font or
palette, or persist preferences. An application obtains and adapts any external
appearance data. No special window or parent appearance provider is required.

This document describes the initial Foundation source API, not a stable theme
ABI or a claim that the library has passed runtime or visual verification.

## Color Well Ownership

`InterActive.IAColorWell` accepts `colorValue` (default current accent), `label`
(default "Color"), `presets` (default empty list of `{name, hex}` records), and
`customColors` (default true). The sample opens a titled quick palette; Default
is available even with no presets. The optional trailing circle requests the
consumer's full picker, closing the quick palette before the callback.

`colorSelected(color)`, `defaultRequested()` and `customColorRequested()` are
requests only: the control never assigns `colorValue`. The consumer applies the
selection, resolves Default and owns the full picker. Appearance forwarding,
`resetAppearance()` and diagnostics use the ordinary shared ControlAppearance
path; no parent provider or color solver is involved.

Its natural capsule height follows the resolved button height, width is twice
that height, and the selected sample has a fixed `spacingXS` inset. The wheel
matches the sample height and logical trailing edge. Focus borders never resize
the sample or artwork. The six-color SVG keeps its original colors and circular
alpha; a stationary foreground-role rim touches it directly. Only the rainbow
rotates 0/180 on native circle hover, reversing from its current angle with
`motion.feedbackDuration`. Reduced motion cancels the owned animation and
immediately restores zero, including during a return. Plain and custom variants
have identical outer dimensions. In touch density the outer height is at least
44; the inner circle retains the approved inset rather than claiming a 44px hit
region. Native sample/custom buttons have separate focus, accessible names and
hit regions; square circle corners remain sample territory.

## Combo Box

`IAComboBox` retains Qt's model, textRole/valueRole, currentIndex/currentValue,
keyboard, accessibility and editable-input semantics. String models need no role
configuration; multi-role models specify their roles. `activated(index)` reports
interaction, not external synchronization. Custom values belong in the caller's
model; IA does not round them to a preset. The non-editable caption is plain text,
single-line and elided. Native editable text retains selection and acceptance.

The control uses the button appearance family and the ordinary local aliases,
diagnostics and `resetAppearance()`. `inherited` defaults false and gives an
enabled, backend-applied value secondary text, not disabled styling. Optional
`leadingColor` defaults transparent; a visible swatch occupies a logical leading
lane. The trailing chevron occupies a square equal to the actual control height,
independent of swatch presence and mirrored in RTL. Font/density changes grow the
same control; focus does not resize it.

The owned `IAPopup` uses real `IAItemDelegate` rows and a scrollable ListView,
matches control width and is bounded by the source window height. Qt owns
navigation, selection, dismissal and upward flipping. `popupMargins` defaults to
the live `spacingS` token; an application may provide its own edge inset without
changing IA's tokens. `popup.popupType` supports Item and Window hosting. Live
appearance changes preserve the current value, editor and open popup. Composite
typography forwards explicitly; popup/delegate color overrides remain local to
those controls rather than inheriting the button's surface.

## Status Banner

`IAStatusBanner` retains `title`, `message` and `validation`. Optional `iconName`
and `iconSource` use the ordinary IA icon lookup/source convention. Optional
`actionText` exposes one native IAButton and `actionRequested()` reports user
activation only, as in IAEmptyState; the caller owns the operation. Empty icon
and action inputs reserve no layout space. The icon, text column and action
share one centerline; titles/actions elide on one line while messages may wrap.
Message-only callers and existing validation surface/text roles are retained.

## Application Input

Import `InterActive.Foundation` for the engine-local `Appearance` singleton.
There is one instance per `QQmlEngine`; its groups retain stable QObject identity.
Each engine should have one application-level input writer. Replace the whole
map rather than mutating a nested JavaScript object in place.

```qml
import QtQuick
import InterActive.Foundation

Item {
    id: root
    property var applicationAppearance: ({
        schemaVersion: 1,
        scheme: "dark",
        density: "compact",
        fontScale: 1.0,
        contrast: "normal",
        reducedMotion: false,
        tokens: {
            colors: { accent: "#8657b8" },
            metrics: { controlRadius: 6 },
            typography: { family: "Noto Sans" }
        }
    })

    Binding {
        target: Appearance
        property: "input"
        value: root.applicationAppearance
    }
}
```

C++ is an alternative owner, not an additional competing writer:

```cpp
#include <Appearance.h>
#include <QQmlEngine>

QQmlEngine engine;
auto *appearance = engine.singletonInstance<InterActive::Appearance *>(
    "InterActive.Foundation", "Appearance");
appearance->setInput({{"scheme", "dark"}});
// Load the consuming component after supplying startup input.
```

Link the backing target `InterActive::Foundation` and make the QML module root
available to the engine. All C++ Foundation types are in namespace `InterActive`.
Build-tree public include directories contain both source headers and the
generated `interactive_foundation_export.h`. Installation/export integration is
separate from the initial Foundation target.

The Foundation module declares `DEPENDENCIES QtQuick` in `qt_add_qml_module`.
Although QColor and QFont are C++ QtGui types, their QML `color` and `font`
value-type metadata belongs to QtQuick. The generated `qmldir` dependency lets
tooling resolve Foundation's property types in their defining module; a
consumer's own `import QtQuick` does not replace that dependency. This uses Qt's
existing value-type registrations, not custom wrappers or warning suppression.

| # | Envelope Field | Type / Accepted Values | Default |
|---|---|---|---|
| 1 | `schemaVersion` | Numeric integer `1`; optional | `1` |
| 2 | `scheme` | String `light` or `dark` | `light` |
| 3 | `density` | String `compact` or `touch` | `compact` |
| 4 | `fontScale` | Finite number in `[0.5, 3]` | `1` |
| 5 | `contrast` | String `normal` or `high` | `normal` |
| 6 | `reducedMotion` | Boolean, not a string or number | `false` |
| 7 | `tokens` | Object of the five token-group objects below | Empty |

An empty input removes all application overrides. Every accepted replacement
starts again from the selected profile; omitted fields never retain old values.
Numbers must be numeric QVariant values, not convertible strings or booleans.
Integer roles accept integral JSON numbers but reject fractions. Non-finite and
out-of-range values are ignored, not clamped into apparent validity. Color inputs
are valid QColor values or Qt color strings, including `#AARRGGBB`; extended color
channels outside `[0, 1]` are rejected. Font-family strings are trimmed, nonempty,
at most 256 characters, and contain no NUL. Availability of the requested font is
left to Qt font matching.

Unknown fields/groups and invalid individual fields produce diagnostics while
valid sibling fields remain effective. Unsupported/mistyped `schemaVersion`, a
non-object `tokens`, or a non-object known group reject the entire replacement.
Rejection retains the previous `input` and resolved snapshot, replaces
`diagnostics`, and emits `inputRejected(QVariantList)`. The QVariantMap property
itself requires an object; QML rejects non-convertible top-level assignments
before the C++ setter can inspect them.

## Resolved API

`Appearance` exposes writable `input` (`QVariantMap`, C++ `input()`/`setInput()`),
read-only `scheme`, `density`, `contrast`, `fontScale`, `reducedMotion`, and
`diagnostics` (`QVariantList`). Its `colors`, `metrics`, `typography`, `motion`,
and `icons` are constant object pointers to the typed read-only `ColorTokens`,
`MetricTokens`, `TypographyTokens`, `MotionTokens`, and `IconTokens` groups.
C++ getters have exactly the same names, for example `colors()->accent()`.
QML cannot instantiate or mutate the groups.

The resolver installs all groups, metadata, input and diagnostics before emitting
`Appearance.resolvedChanged()`. It emits one such signal per changed accepted
snapshot, then each changed group's `changed()`, then `inputChanged()` and
`diagnosticsChanged()` as applicable. Identical input with identical results is a
no-op. A rejected input never emits `resolvedChanged()` or group notifications.
Supplying the accepted input again clears any intervening rejection diagnostic.
Observe `resolvedChanged()` for a complete application snapshot, not a sequence
of per-token writes. These QObjects are used on their owning GUI/QML thread.

Envelope metadata and every typed token use a property-specific NOTIFY signal:
`fontScaleChanged`, `accentChanged`, `controlRadiusChanged`, and the corresponding
`<property>Changed` names. Only changed effective values notify. The aggregate
signals retain the ordering described above; specific notifications follow complete
installation and do not trigger a second resolution.

The finished color palette is immutable shared state owned by this Appearance.
Normalized colors/presence, scheme/contrast and color-blend inputs determine when
the coupled palette calculation runs. Font scale, unrelated metrics, icon URLs
and motion durations retain the existing palette. Accepted replacement input is
prepared from profile defaults, never from the previous resolved output. Rejection
retains accepted state, and clearing rejection diagnostics need not rebuild colors.
`TokenSchema::resolve` remains the stateless entry point using the same recipe;
ordinary controls forward completed color roles instead of calling it themselves.
`TokenSchema` is a compatibility facade: private `TokenValidation` owns envelope
parsing, schema and diagnostics, `PaletteBuilder` owns profile preparation and
palette/non-color construction, and `ColorMath` owns the pure color helpers.

Completed updates carry color-value/presence masks and independent metrics,
typography, motion and icon group flags. Facades resolve only affected color or
font/geometry inputs. Nested publication compares against the latest source and
the facade's actual installed snapshot rather than applying an obsolete mask.
These updates travel through a private, Appearance-owned `AppearanceSource`;
facades obtain immutable state and observe completion there, without friend
access or private update types in Appearance's public meta-object. Completion
still follows `resolvedChanged()` and precedes specific/group notifications.

Nested setters still install synchronously. Property-notification obligations are
drained on the same stack against the newest installed values, so an earlier
dispatch cannot leave font/layout bindings stale after a nested color update.
Complete state is guaranteed within each Appearance or ControlAppearance, not a
simultaneous transaction across all sibling controls and arbitrary QML bindings.
Nested signal order beyond that snapshot guarantee is not fixed.

Diagnostics have `path`, `code`, `severity`, and `message`. Codes are
`unknown-field`, `invalid-value`, `rejected-input`, `low-contrast`,
`translucent-canvas`, `palette-collision`, `palette-generation-exhausted`, and
`low-feedback` (on control state-table diagnostics).
Rejections have severity `error`; other diagnostics have
severity `warning`. Contrast entries also contain numeric `ratio` and `target`.
Diagnostics describe the latest attempt rather than accumulating indefinitely.
Palette collision entries contain the two compared role `paths`, measured
CAM16-UCS `distance`, and numeric `target`. Equivalent alias/state findings are
deduplicated. These warnings concern palette distinction, not text contrast.

## Color Tokens

All properties in `colors` are QColor. The profiles are Airmail-inspired starting
points. Primary uses the exact default blue `#5C9CF5`, or the exact accepted
application accent including its precision and alpha. Missing danger/error,
caution/warning and success companions derive from that accent's visible tone and
intensity, with the profile's rose, amber and green colors used as **hue seeds**.
Their resolved RGB outputs are not fixed profile literals. Neutral chrome,
ordinary text, disabled treatment and noncolor groups do not follow accent edits.
Derived labels target 7:1 in both modes, using the strongest available result when
7:1 is impossible. The readability floor is 5:1 in normal mode and 7:1 in high
mode; high retains its existing neutral adjustments. This is a bounded visual
trial, not a finalized whole-palette design.

| # | Role | Light | Dark |
|---|---|---|---|
| 1 | `canvas` | `#F4F5F7` | `#202124` |
| 2 | `content` | `#FFFFFF` | `#27292D` |
| 3 | `popup` | `#FFFFFF` | `#303237` |
| 4 | `text` | `#25282D` | `#ECEEF2` |
| 5 | `secondaryText`, `placeholderText` | `#616873` | `#B0B6C0` |
| 6 | `disabledText` | `#858C96` | `#777F8B` |
| 7 | `disabledSurface` | `#F0F1F3` | `#2B2D31` |
| 8 | `control` | `#F0F2F4` | `#35373B` |
| 9 | `controlHover` | `#E7EAEE` | `#3E4146` |
| 10 | `controlPressed` | `#DDE2E7` | `#484C52` |
| 11 | `selection` | `#E3E8ED` | `#41464E` |
| 12 | `inactiveSelection` | `#EDF0F3` | `#32353A` |
| 13 | `outline` | `#7D8591` | `#868F9B` |
| 14 | `separator` | `#DFE3E8` | `#41454C` |
| 15 | `accent` | `#5C9CF5` | `#5C9CF5` |
| 16 | `onAccent` | HCT-derived text across accent states | Same recipe |
| 17 | `accentText` | HCT-derived accent text on content | Same recipe |
| 18 | `error` | Accent-derived; hue seed `#E06C75` | Same hue seed |
| 19 | `onError` | HCT-derived text across error states | Same recipe |
| 20 | `errorText` | HCT-derived error text on content and error surface | Same recipe |
| 21 | `warning` | Accent-derived; hue seed `#F5A742` | Same hue seed |
| 22 | `onWarning` | HCT-derived text across warning states | Same recipe |
| 23 | `warningText` | HCT-derived warning text on content and warning surface | Same recipe |
| 24 | `success` | Accent-derived; hue seed `#2C704F` | Accent-derived; hue seed `#84CCA1` |
| 25 | `successText` | HCT-derived success text on content and success surface | Same recipe |
| 26 | `tooltipBackground` | `#282B30` | `#E5E9EE` |
| 27 | `tooltipText` | `#F4F5F7` | `#202124` |
| 28 | `inverseSurface` | `#1F2022` | `#1F2022` |
| 29 | `inverseText` | `#EEF0F4` | `#EEF0F4` |
| 30 | `inverseSecondary` | `#AEB5BF` | `#AEB5BF` |
| 31 | `scrim` | `#3D000000` (24% black) | `#7A000000` (48% black) |
| 32 | `shadow` | `#29000000` (16% black) | `#5C000000` (36% black) |
| 33 | `focus` | HCT-derived from accent for 3:1 across neutral focus surfaces | Same recipe |
| 34 | `link` | Effective `accentText` | Effective `accentText` |
| 35 | `transparent` | Transparent black | Transparent black |
| 36 | `accentHover`, `accentPressed` | Accent blended away from its paired text | Same recipe |
| 37 | `errorHover`, `errorPressed` | Error blended away from its paired text | Same recipe |
| 38 | `warningHover`, `warningPressed` | Warning blended away from its paired text | Same recipe |
| 39 | `errorSurface`, `warningSurface`, `successSurface` | Semantic color blended into content | Same recipe |
| 40 | `primarySurface`, `primaryHover`, `primaryPressed` | `accent`, `accentHover`, `accentPressed` | Same roles |
| 41 | `primaryText`, `primaryOutline` | HCT-derived from accent | Same recipe |
| 42 | `destructiveSurface`, `destructiveHover`, `destructivePressed` | `error`, `errorHover`, `errorPressed` | Same roles |
| 43 | `destructiveText`, `destructiveOutline` | HCT-derived from error | Same recipe |
| 44 | `cautionSurface`, `cautionHover`, `cautionPressed` | `warning`, `warningHover`, `warningPressed` | Same roles |
| 45 | `cautionText`, `cautionOutline` | HCT-derived from warning | Same recipe |

Missing accent/error/warning hover and pressed colors blend the effective fill
toward white for dark labels or black for light labels, selecting the endpoint
with greater foreground contrast and using the existing `hoverBlend` and
`pressedBlend` strengths. This feedback depends on the paired label, not the
page's main `text`. Their missing `onAccent`, `onError` and `onWarning` foregrounds
are then HCT-derived against all three effective states, including explicit state
overrides. An explicitly supplied `control` likewise derives missing
`controlHover` and `controlPressed`; otherwise their profile seeds are retained.

Primary, Caution and Destructive use solid semantic surfaces, not blends into
`content`. Each presentation's `Surface`, `Hover`, `Pressed`, `Text` and `Outline`
roles is independently replaceable. Missing text is derived across its normal,
hover and pressed surfaces; missing outlines use that effective text color,
including explicitly supplied text. They do not run a separate contrast solver
against the surrounding content. The resolver prefers the original resting fill
and derives a hue-related label before considering a fill adjustment; it does
not force white labels. Slider/progress fills continue to use solid `accent`,
independent of action-surface overrides.

Fill colors and text colors have separate roles. `accentText` targets content;
`errorText`, `warningText` and `successText` target both content and their matching
message surfaces. Those message surfaces still blend the semantic color into
content using `semanticBlend`; they are distinct from solid action surfaces.
Missing `link` follows effective `accentText`, while `focus` derives separately
from accent for its 3:1 target across content (the field background), canvas,
popup, control, controlHover, controlPressed, selection and inactiveSelection.
Surfaces are composited against their content/canvas backdrop before the one
palette-time derivation. Explicit `focus` stays exact; colored and local fills
retain the per-state foreground fallback. Labels and validation use the readable `Text`
roles, not the bright fill tokens.

### HCT And Contrast

#### Accent-derived semantic companions

Foundation analyzes the accent composited over effective content, canvas and
finally white. It never writes generated colors back into `Appearance.input`.
Generated bases are opaque; an explicit semantic RGBA input remains exact. Hidden
RGB at zero accent alpha does not seed a colorful palette, and exact RGB gray
requests zero intended chroma rather than CAM16's small residual neutral chroma.

For each missing family, the bounded recipe requests the profile anchor hue at
the analyzed accent tone and `max(C,12)` chroma as one direct HCT proposal, where
C is the accent's intended chroma. The chroma floor retains semantic identity
with gray or weakly chromatic seeds; it is not a claim of exact chroma matching.
If the finalized direct proposal fails the acceptance checks below, at most two
tone repairs follow: from the direct proposal's measured separation blockers,
`clamp(min(blocker tones) − 16, 0, 100)` and `clamp(max(blocker tones) + 16, 0, 100)`,
or `T ∓ 16`-style fixed steps when there are no blockers. Duplicated request
tones and generated RGB values buy no further attempts. Each request starts from
the original anchor hue and `max(C,12)`, never from an earlier attempt's
gamut-reduced object. Actual gamut-mapped chroma must be at least 8 and hue must
be within 22 degrees of the anchor. Among feasible repairs, the one whose final
base tone is closest to the accent tone wins, breaking exact ties by larger
actual base chroma, then lower RGB integer. This bounds a role to three base
proposals per branch, not a claim that all downstream HCT conversions are bounded
by it.

Candidates use the shared text-first fill/state/label finalization before
acceptance. Their visible RGB8 resting fills must remain at least 12 apart in raw
Euclidean CAM16-UCS distance; interactive normal/hover/pressed combinations use
8. Readability checks also inspect final full-precision and RGB8 colors. A
direct proposal passing every applicable check is returned immediately; otherwise
both distinct repairs are finalized and checked against all reservations. These
numbers are trial design measurements, not WCAG color-distance criteria or
color-vision deficiency guarantees; labels and icons remain important.

Primary and explicitly supplied semantic groups are finalized and reserved first,
including explicit state-only inputs on later groups. Missing families resolve in
error, warning, success order. A red-family accent with chroma at least 12 and hue
within 20 degrees of the danger anchor keeps danger true neutral at a related
brightness, with no return to red. Other families first prefer recognizable
chromatic identities, with
true-gray candidates as a last resort. Different generated companions cannot
simply collapse onto one gray. No drag history, cache or timer changes the recipe.

Every explicit base, state, foreground or outline remains caller-owned, even if
unusual, equal to a generated value, translucent or impossible. Immutable pair
failures are diagnosed without rejecting unrelated candidates. A derived shared
foreground remains candidate-dependent even on fixed surfaces. An exhausted
bounded search retains a deterministic best-effort result, adds a
`palette-generation-exhausted` warning with the role, tried/maximum proposal
counts and the failed gate names (reporting full-precision and RGB8 readability
separately), and keeps the ordinary measured `palette-collision` and/or
`low-contrast` findings; red-primary danger remains neutral. Bounded exhaustion
is not a claim that no feasible palette exists. The library cannot
promise readability or separation for every arbitrary supplied combination.
Omission/reset resumes current derivation, preserving explicit siblings and local
overrides. An explicit `primarySurface` constrains the visible actions but does
not replace the accent analysis seed or slider/progress fill.

Foundation uses the native Material Color Utilities HCT (hue, chroma, tone) and
contrast helpers for foreground derivation, not a Material theme engine. The
original seed's hue and chroma express intent: candidates vary tone, and the HCT
solver may reduce chroma to fit the sRGB gamut. Exact hue/chroma preservation is
not promised. Neutral seeds request zero chroma rather than acquiring a tint from
CAM16's residual neutral chroma.

The shared derivation tries cheap direct proposals before any interval probing:
the flattened seed itself when it already meets every actual pair, MCU's
algebraic dark and light exterior solutions at the original hue/chroma against
extrema across full-precision and 8-bit-rounded background representations, and
the literal black/white endpoints. Every proposal is measured as its final
quantized sRGB color against every flattened background at both precisions; a
requested tone is never assumed to be the output, and MCU's −1 safety margin is
never clamped into a supposedly successful tone. Among readable fast proposals
the one whose actual tone is closest to the seed's tone wins, breaking exact ties
by lower RGB integer; this is the closest fast proposal, not a global optimum or
a promise of the same value an interval search would return. When no fast
proposal passes, the general fallback intersects feasible luminance ranges across
every relevant background, including middle-tone solutions between dark and light
surfaces, probes interval interiors, and judges final quantized colors at both
precisions. If none meets the target, it returns the tested candidate with the
highest minimum contrast across the backgrounds and retains `low-contrast`
diagnostics. All valid explicit
colors, including assignments equal to defaults, win; the resolver does not
rewrite them to hide a contrast failure.

For an opaque, library-owned resting semantic fill that still cannot meet the
readability floor (5:1 normal / 7:1 high), the same solver finds a nearby fill
against the best derived text. This adjustment requests the floor plus 0.1,
leaving room to re-derive a hue-related label rather than stopping at the
pure-black limit. It is not a fixed
tone or blanket lightening, and never rewrites an explicit application color or
alpha input. State feedback is derived from that resting pair, then the common
label is resolved across all actual state surfaces.

Derived labels (`onAccent`/`onError`/`onWarning`, action `Text`, semantic message
`Text`, `accentText` and local-override foregrounds) aim for **7:1 in both contrast
modes**. Among passing proposals the label closest to the seed's actual tone wins;
when none passes, the strongest tested result is used. This target does not move
fills: fill adjustment and companion acceptance keep their existing 5:1 normal /
7:1 high readability floor, and explicit caller colors remain exact.

Enabled text diagnostics use that same floor: 5:1 normal gives margin above
WCAG AA's 4.5:1 requirement for normal-sized text; 7:1 high corresponds to the AAA
normal-text contrast requirement, not a whole-application conformance claim. A
best-effort label below 7:1 but at least 5:1 therefore does not produce a normal-mode
`low-contrast` warning. The generic `contrastingText` and `checkContrast` helpers
retain their optional 4.5 default for direct callers; appearance resolvers pass
the label target or diagnostic floor explicitly. Checks cover
main text on normal, hovered, pressed and selected surfaces; secondary,
placeholder, link and semantic text on content; semantic text on message surfaces;
accent/error/warning paired text through hover and press; action text through all
three action surfaces; and tooltip/inverse pairs. Outline/focus against content
and action outlines against their normal surfaces have a 3:1 target. Disabled
text and decorative separators are separately classified and are not treated as
enabled-text failures.

Alpha is composited before deriving or checking contrast. Action foreground
derivation and local control checks use `content` over `canvas` as the background
reference; content/message text derivation and global diagnostic pairs use
`canvas`. If even `canvas` is translucent, the final reference is white and a
`translucent-canvas` diagnostic asks the consumer to verify its actual host
background. The resolver does not know arbitrary host backdrops. These checks
and best-effort derivations are not a universal contrast guarantee for custom
palettes, compositions or rendered pixels.

## Metrics

All `metrics` properties are finite qreal logical-pixel values. Values are
unscaled by `fontScale`; controls grow from actual font metrics instead.

| # | Role | Compact Default | Input Bounds |
|---|---|---|---|
| 1 | `spacingXS`, `spacingS`, `spacingM` | 4, 8, 12 | `[0, 128]` |
| 2 | `spacingL`, `spacingXL` | 16, 24 | `[0, 256]` |
| 3 | `controlHeight`, `toolHeight`, `rowHeight`, `targetSize` | 30, 28, 30, 28 | `[0, 512]` |
| 4 | `controlRadius`, `popupRadius` | 4, 8 | `[0, 128]` |
| 5 | `iconSize`, `indicatorSize` | 16, 16 | `[1, 256]` |
| 6 | `switchWidth`, `switchHeight`, `thumbSize` | 30, 18, 14 | `[1, 256]` |
| 7 | `trackHeight` | 4 | `[1, 128]` |
| 8 | `sliderThumbSize` | 16 | `[1, 256]` |
| 9 | `borderWidth`, `focusWidth` | 1, 2 | `[0, 16]` |
| 10 | `popupPadding` | 8 | `[0, 128]` |
| 11 | `menuMinWidth` | 180 | `[0, 2048]` |
| 12 | `fieldWidth`, `sliderLength`, `progressWidth` | 180, 180, 180 | `[0, 2048]` |
| 13 | `horizontalPadding`, `verticalPadding`, `spacing` | 12, 6, 8 | `[0, 128]` |
| 14 | `scrollBarWidth`, `scrollBarMinimumLength` | 8, 24 | `[1, 128]`, `[1, 512]` |
| 15 | `progressHeight`, `busySize` | 4, 20 | `[1, 128]`, `[1, 256]` |
| 16 | `shadowRadius`, `shadowOffset` | 12, 2 | `[0, 128]` |
| 17 | `hairlineWidth` | 1 | `[0, 8]` physical pixels |
| 18 | `pressInsetDepth` | 8 | `[0, 64]` |
| 19 | `pressContentShift` | 1 | `[0, 8]` |
| 20 | `choiceGap` | 2 | `[0, 128]` |
| 21 | `choiceCenterRadius` | 2 | `[0, 128]` |
| 22 | `dialogMinWidth` | 180 | `[0, 2048]` |

`hairlineWidth` is the one exception to logical-pixel dimensions: a decorative
separator divides it by the effective DPR. Structural alignment and slider-value
math do not belong in tokens. Switch/slider geometry still needs layout checks
when callers supply unusual independent dimensions.

Touch density imposes a floor of 44 on `controlHeight`, `toolHeight`, `rowHeight`,
and `targetSize`, even if a smaller valid token is supplied. Controls must use
these dimensions in real layout, including width and spacing; Foundation alone
cannot ensure non-overlapping hit targets.

## Typography

| # | Role | Type / Default | Input Bounds |
|---|---|---|---|
| 1 | `family` | QString; initial application UI font family | Nonempty, at most 256 characters |
| 2 | `controlSize`, `supportingSize`, `headingSize` | qreal; 13, 12, 16 logical px | `[6, 128]` before `fontScale` |
| 3 | `controlWeight`, `emphasisWeight` | int; 400, 600 | `[1, 1000]` |

The application font family is captured when an Appearance or source-null
ControlAppearance is constructed; later process-global font changes are not an
appearance input. If no GUI application font is available, the family seed is
`Sans Serif`. No proprietary font is bundled. `fontScale` multiplies all three
sizes, including supplied sizes. Local `fontPixelSize` is already effective and
is not scaled a second time. `resolvedFont` rounds pixel size to the nearest
integer for Qt's QFont API and sets the effective family and weight.

Effective typography and height calculation are separate from colors. An exposed
size change such as 13.1→13.2 notifies that size without another measurement when
the rounded QFont request is unchanged. Height uses the requested floor and the
measured font height plus twice vertical padding; padding/floor/density changes
reuse the measurement. Radius, horizontal padding and spacing do not measure fonts.

The existing QFont request also inherits other application-font attributes. Public
Qt font-database/application-font, primary-screen, logical-DPI and relevant DPR
notifications invalidate measured-height reuse while retaining the captured family.
IA uses the original one-argument QFontMetricsF measurement, without a mandatory
window or paint-device-specific protocol. Unsignaled changes to runtime font
substitution/fallback tables are outside this observation boundary.

One application-owned font environment observes these process-wide notifications
for all facades and QML engines: one application event filter and one set of screen
connections. Only observation/generation is shared; appearance input, captured font
families and generation-keyed measurement reuse remain independently owned.

Qt's own font-database refresh updates existing rendered text on the next scene-
graph synchronization; IA does not force equal-font notifications or schedule idle
frames. Qt 6.8.0 and 6.11.2 have a reproduced TextEdit limitation: after a font is
added under an unchanged request, glyphs/document layout refresh but exposed
content dimensions can remain stale. This pre-existing font-add/remove behavior
is separate from color-only updates; IA does not introduce a workaround for it.

## Motion And Icons

| # | Group / Role | Default | Input Bounds |
|---|---|---|---|
| 1 | `motion.feedbackDuration`, `popupDuration`, `busyDuration` | 120, 150, 1000 ms | Integer `[0, 10000]` |
| 2 | `motion.tooltipDelay`, `tooltipTimeout` | 500, 5000 ms | Integer `[0, 60000]` |
| 3 | `motion.easing` | `6` (`QEasingCurve::OutCubic`) | Integer `[0, 40]`, built-in curves only |
| 4 | `motion.hoverBlend`, `pressedBlend` | 0.08, 0.16 | qreal `[0, 1]` |
| 5 | `motion.semanticBlend` | 0.08 light / 0.12 dark | qreal `[0, 1]` |
| 6 | `motion.disabledOpacity` | 1 | qreal `[0, 1]` |
| 7 | `motion.pressInsetOpacity` | 0.28 | qreal `[0, 1]` |
| 8 | `icons.strokeWidth` | 1.5 logical px | qreal `[0.25, 16]` |
| 9 | `icons.check`, `minus`, `chevronDown`, `chevronRight`, `close`, `search` | QUrl values below | Absolute asset URL |

Icon URLs default to `qrc:/qt/qml/InterActive/Foundation/icons/` followed by
`check.svg`, `minus.svg`, `chevron-down.svg`, `chevron-right.svg`, `close.svg`,
and `search.svg`, respectively. Resource authors must register these exact paths.
Foundation's C++ resolver does not load or verify assets. Overrides accept QUrl
or string values using `qrc`, `file`, `https`, `http`, or `data`; relative URLs are
rejected to avoid caller-dependent resolution. Token presence is not proof of
asset rendering, tinting, or theme-icon lookup.

Reduced motion forces `feedbackDuration`, `popupDuration`, and `busyDuration` to
zero after overrides. It does not erase static hover/press color distinction or
tooltip delays. Busy/progress consumers must retain a meaningful static state
and must not interpret zero as a request for a zero-duration infinite loop.

High contrast starts from the selected profile, strengthens main/secondary/
placeholder text and outlines to black (light) or white (dark), makes selection
opaque, and imposes border/focus width floors of 2/3. Its accent remains exact;
missing semantic companions use the same accent-derived recipe and the selected
readability floor. Labels target 7:1 in both modes. Shared foreground derivation
covers normal, hover and pressed states, and semantic borders still follow text.
Explicit colors win in both contrast modes.

This profile is a text-contrast preview, not a whole-application WCAG AAA claim.
Explicit application and local colors still win: if their fill/state combinations
cannot meet the selected 5:1 or 7:1 diagnostic floor, they remain unchanged and
produce `low-contrast` diagnostics.
Applications must supply accessible override colors rather than rely on the
library to silently rewrite their inputs.

## Press Feedback

`IAButton` and `IAToolButton` signal press with a shared inner shadow along the
inside top edge and a downward content shift, keeping their resting/hover fill
(or selected/inactive fill when checked). `metrics.pressInsetDepth`,
`metrics.pressContentShift`, and `motion.pressInsetOpacity` size this inset;
their defaults are the same in light/dark schemes and compact/touch densities.
The shift is visual only: implicit size, baseline and layout do not change.
Release removes the inset and shift, and disabled controls never show them.
There is no press animation, including with reduced motion.

Other controls retain their pressed fills. The `*Pressed` palette roles and
`motion.pressedBlend` remain available for those controls, selection and shared
label derivation. `IAButton` and `IAToolButton` no longer expose a local
`pressedBackgroundColor` override; the property remains on `ControlAppearance`
and controls that consume pressed fills.

## Local Appearance

`ControlAppearance` is a creatable QObject. Each IA control owns one internally,
sets `source: Appearance`, selects its `family`, and forwards its supported
appearance properties to it. It never owns a duplicate writable application
input. An initially null source has a complete light/compact profile; destroying
the source returns to those defaults without losing explicit local overrides.
Changing source disconnects the previous source and installs the new shared state.
Source-null color data is shared immutable library data; each facade retains its
own construction-time font family and local overrides.

Internally, a single data-only family descriptor table selects color roles and
metric/typography fields. The public family string is validated and converted to
an internal enum by its setter; existing family strings and invalid-input diagnostics
are unchanged, with the menu-specific `menuItem` family added. Accepted source snapshots decode metric and typography records
once. Facades use X-macro-generated indexed arrays and explicit presence bits for
local values/overrides, with typed font, geometry and color-branch cache keys.
Facade notifications compare typed last-notified values and drain pending bits in
the existing property order, re-reading effective values after nested callbacks;
they do not read properties reflectively. The application/token-group publication
path remains separate. Visual flags and the decision of which interaction state
is active remain in the controls.

Each immutable palette lazily memoizes one immutable state table per
`(family, tone, validation)` combination (`Auto` shares `Neutral`). Facades without
color overrides retain that shared table; local color overrides produce a private
copy through the same builder, reusing it until its effective colors/findings
change. Font/geometry edits do not rebuild the table. Shared pair diagnostics are
reused from the palette; locally affected pairs retain their dependency caches.

The additive read-only value API is `appearance.stateTable.colors(state)`, returning
`backgroundColor`, `foregroundColor`, `borderColor`, `borderWidth`, `focusColor`, `focusedBorderColor`,
`focusedBorderWidth`, `fillColor`,
`trackColor`, `thumbColor`, and `placeholderTextColor`. Fill uses the ordinary fill or disabled
foreground. The `slider` family supplies a neutral `controlPressed`-seeded remainder,
independent of its outline, and a circular thumb's rest/hover/pressed surface.
The passive `progress` family uses that same remainder seed; its existing local
`backgroundColor` override owns the remainder. Both controls consume Foundation's
small `ValueTrack` capsule renderer, with scalar amount/orientation/RTL inputs.
Disabled value tracks retain the disabled background and foreground roles.
Field placeholders use the shared placeholder or local disabled foreground.
The table also holds field `selectionColor`/`selectedTextColor` and label
`linkColor`. These retain their shared application roles, independent of local
fill/foreground overrides, exactly as before. `stateTable`
is a small owning value around shared immutable storage, not another QObject or
an eagerly boxed list per control. Its `stateTableChanged` signal tracks changed
effective state colors, conditional border width or focus width, so reading the property establishes a QML binding
dependency before the indexed lookup. Stored value copies retain their snapshot.
`Presentation.State` exposes `Rest`, `Hover`, `Pressed`, `Selected`, `SelectedHover`,
`SelectedPressed`, `Inactive`, `InactiveHover`, and `Disabled`. These entries encode
the family state recipe: hover retains ordinary foreground; selected hover has a
distinct fill with the selected foreground; selected pressed uses the ordinary
pressed pair; inactive hover retains the inactive pair; disabled uses the disabled
pair/border. Menu navigation uses its highlighted pair for selected, selected
hover/press and inactive-highlight entries, independently of its checked mark.
Each entry's focused border uses `focusColor` when it has at least 3:1 contrast
against that entry's fill (composited over content/canvas), otherwise that entry's
foreground. This choice is computed only by the shared/private table builder.
An invalid field keeps its resolved invalid border color while focused. The small
owning table value also snapshots `metrics.focusWidth`, exposed on each returned
entry as `focusedBorderWidth`; width-only changes notify without rebuilding colors.
Press-inset controls still select their resting,
hover or selected fill rather than the pressed fill. One pure Foundation QML
function, `ControlState.of(enabled, down, hovered, selected, active, insetPress)`,
maps the template flags to this index; it performs no C++ resolution or color
math. Backgrounds and indicators read one indexed entry. Choice labels continue
to use only enabled/disabled text, not the indicator's selected pair. The passive
`scrollBar` family supplies an always-available neutral capsule thumb (`controlPressed`
at rest, `outline` on hover, `secondaryText` while pressed) through `thumbColor`.
Its `trackColor` stays transparent, including hover/press: no full-height wash,
fade timer or animation. Overflow/policy controls visibility independently of activity.
The neutral seed intentionally does not follow the high-contrast separator/outline
promotion, so value remainders and resting thumbs never become text-colored rails.
Explicit foreground overrides own the thumb in every enabled state; explicit
background/hover/pressed overrides still own the corresponding track state.
Thumbs consume disabled foreground while tracks retain their enabled-state
selection. These decorative thumb colors are not diagnosed as text pairs.
All role selection/derivation happens in the memoized palette table (or cached
local override table), never on pointer movement. ScrollView's existing viewport,
bar positioning and local override propagation contract are unchanged by this stage.

Interactive table construction derives Hover and Pressed fills from Rest,
targeting raw Euclidean CAM16-UCS distance **4** after
backdrop compositing and RGB8 rounding. It tries nine fixed strengths from the
corresponding `hoverBlend`/`pressedBlend` token through 3× that strength, first
toward the paired label, then away toward its higher-contrast black/white
endpoint. Candidates must retain the 5:1 normal / 7:1 high readability floor at
full and RGB8 precision. If a generated label leaves no feedback headroom, the
builder may finalize that unowned pair jointly against a stronger bounded
proposal; caller-owned labels and surfaces are never rewritten. Exhaustion
retains deterministic best effort and reports `low-feedback` with measured
`distance` and `target`. These are IA trial distinction measurements, not WCAG
criteria. Passive families, including tooltips, do not acquire hover behavior.
After the original 1x-3x search exhausts, a finite fallback examines weaker
strengths on the same two rays, strongest first. It enumerates the union of
actual Qt RGB8 channel transitions (at most 765 boundaries / 766 intervals per
ray), measures each distinct color once, and checks full/RGB8 readability at
both interval ends. The joint-label headroom probe retains only the original
stronger search, so weaker feedback cannot suppress its label repair. Old
successful results stay exact; if weaker proposals also fail, the original
best effort and warning stay exact. Interval-end checks are bounded best effort
for unusual full-precision colored inputs, not a continuous-space optimizer.

Button, tool, delegate, tab and choice tables reserve pairwise distance 4 among
Rest, Hover, Selected and SelectedHover. An already-distinct selected seed is
retained; otherwise the same bounded search starts from it with `pressedBlend`,
reserving Rest and Hover. SelectedHover derives from that selected fill with
`hoverBlend`, reserving all three siblings. Ordinary row/tab fills remain neutral;
no accent tint is added. Inset press behavior is unchanged. A transparent resting
background keeps a transparent disabled background unless explicitly overridden.
Only table construction performs these searches: unmodified controls share the
palette's memoized table, genuine local branches cache their own finished table,
and hover/press still perform only the indexed lookup.

`IAMenuItem` selects the Foundation `menuItem` family. Its navigation highlight uses
the exact `accent` fill and finished `onAccent` label in every highlighted state,
including hover, press and inactive navigation. Explicit selected/inactive backgrounds
own their sparse pairs; an unspecified local partner is derived for readability.
Explicit paired text, including equal-valued assignments, stays exact and incompatible
pairs produce the usual contrast diagnostic. Reset resumes the current shared pair.
Unrelated local overrides do not change the highlight. The separate Choice center
recipe is unchanged. Disabled entries never highlight, and checking alone changes
only the mark. Destructive commands keep the ordinary transparent row with the
`errorText` danger role and use the same accent/onAccent highlight, never a red fill.
Foundation compares the effective highlighted fill against the composited palette
`popup` surface once during shared/private table construction. Below 5:1 normal or
7:1 high contrast it supplies a conditional inward border, colored from the finished
resting Primary Button table. A local `borderColor` overrides that color exactly.
The immutable entry retains the conditional flag; `StateTable` snapshots the current
`metrics.borderWidth` as `StateColors.borderWidth` (zero when absent), independently
of color caching. Metric changes notify without rebuilding palettes or color tables.
The native row Rectangle draws the border without changing content geometry, and
its existing focus border still takes precedence. The existing table-time feedback
recipe may finalize an unowned danger foreground for hover headroom; explicit
`errorText` remains exact. Other action tones are unchanged.
The native menu's shared `textPadding` reserves one leading lane across rows. Inside
it, fixed check/icon slots collapse when absent; label, optional caller-supplied
shortcut text and a row-height-square submenu slot share one structural row. The
arrow is centered with equal facing-edge clearance, including RTL and touch density.

Only the selected kind's check/mixed icon, radio dot or switch thumb is created.
The three choice templates share Foundation's `ChoiceRow` text/indicator lane,
including intrinsic sizing, centerline, spacing, elision and explicit RTL placement.
The choice family's horizontal inset resolves from `verticalPadding`. Checkboxes
and radios remove the logical leading inset so their indicator edge aligns with
a preceding field/group label, mirrored in RTL; their trailing padding, vertical
hit target and indicator/text gap are unchanged. Standalone switches retain both
horizontal insets. Explicit native leftPadding/rightPadding remain caller-owned.
Each choice has an unchanged outer outline and an inset center positioned by
the resting `metrics.borderWidth` plus `metrics.choiceGap`. Focus thickens the
border inward into that gap without moving or shrinking the center, track or
thumb. Native antialiasing can partially cover a narrow gap's pixels.
Checkboxes use a rounded square (`metrics.choiceCenterRadius`, independent of
outline thickness) with a check/mixed icon, radios a dot, and switches an inner capsule with
a thumb constrained to its end slot. The state table supplies `choiceGapColor`,
`choiceCenterColor`, `choiceMarkColor` and `choiceTrackColor`. The default on pair
uses the accent hue/chroma with its tone resolved for a light mark at the 7:1
target; explicit selected/inactive pairs remain exact. Hover and press compose
with the on center without erasing its meaning. Off switches use a neutral track
and contrasting ordinary foreground thumb; disabled-on retains a neutral center
and paired mark. Explicit ordinary off/hover/pressed/disabled backgrounds still
own the switch's inner off track; the default neutral track is used only when
that state is unowned. The ordinary state tuple retains its existing precedence;
the choice center/mark roles carry the persistent on pair independently.
Colors are built once per shared palette table (or cached local
override table); QML performs only state/role selection. Focus contrast is measured
against the outline's gap/backdrop, not the separate accent center; its fallback
is the outline role on that same backdrop, not the inner center's light mark.
Inset backgrounds create their Gradient and stops only when `pressInset` is used;
their decorative shading stays clipped inside the current painted border,
including while focused, rather than using the fixed indicator-content inset.
The named empty inset Rectangle remains available on other backgrounds. Changing
the kind or inset trait replaces only that owned visual, not the control.
Focus changes the one border's color/width inward, without a second ring or any
change to the rectangle's bounds/radius. Borderless backgrounds draw that same
border only while focused. Choices focus their indicator, not the whole label row.
Existing control focus predicates are retained (visual focus, active focus for fields).
No public control property/alias is changed. A fill-only edit now
emits `stateTableChanged` because fill is represented in the table; unrelated
font/geometry changes still do not.

`IATabBar` uses the passive `tabBar` family: one quiet track, unchanged `spacingXS`
insets and a `spacingXS` gap between adjacent segments (4 logical pixels by
default), with no resting border. Its native writable `spacing` can override
the gap. Foundation's `SegmentGroup` shares the horizontal ListView construction
and track Component, exposing `createBackground(parentItem)` for the native
template's background slot. The track is created once, not recreated by palette,
selection, hover or size changes. Qt retains content ownership, implicit sizing,
mirroring and overflow scrolling; Foundation imports no Controls.
The `tab` family selects a raised
`content` segment in light mode and `controlHover` in dark mode. The track is
finalized once by the same bounded feedback recipe, reserving both the raised
segment and effective canvas at distance 4. Only this track search evaluates on
canvas; ordinary text diagnostics retain their content/canvas backdrop. Explicit
application and local backgrounds remain exact, including equal-valued and
translucent assignments. Sparse local branches carry the same surround input,
so live canvas changes rederive unowned tracks. Hover reserves the raised segment before deriving its own fill, so
feedback cannot consume the current segment's color. Tab feedback and focus are evaluated against that finished track,
not the content surface behind the group. Explicit local colors remain exact;
Qt still owns current index, keyboard navigation and scrolling.
The existing 3:1 focused-border rule can therefore choose the label color for
an unselected light-high tab, or a hovered light tab, rather than the accent.
The selected segment's focus frame and leading marker are unchanged.

Current enabled tabs and segmented choices share Foundation's `SegmentBackground`,
a shared owner of two full-size native rounded `ControlBackground` frames. A
complementary hard clip assigns each pixel to exactly one frame; the leading
frame uses the exact effective accent (left LTR, right RTL). Both frames keep
identical geometry and the original native border, including antialiasing.
The cap extent is the finished focused border width plus two logical
pixels: four normally, five in high contrast. Focus paints the unchanged native
inward frame over the cap, leaving two pixels inside; focus never resizes it.
Hover, press and inactive selection retain the cap and existing finished state
fills. Unselected and disabled members have no cap. Live accent/direction/contrast
changes preserve the native control and background. Local fill, focus and radius
overrides keep their existing ownership; a consumer's custom background replaces
this artwork. The group track and spacing are unchanged.

### Segmented choices and switch rows

`IASegmentedControl` selects a value, rather than a page. Its entries are
`IASegmentedButton` children. It shares the tab track, spacingXS gap, tab state
palette and appearance aliases; no separate tab-versus-segmented visual treatment
is defined yet. The container manages equal member widths/heights through public
bindings. Its natural size comes from member implicit dimensions, and a narrow
allocation scrolls rather than overlapping labels. Taking a member out restores
its previous width/height values or bindings.

```qml
IASegmentedControl {
    width: 300
    Accessible.name: "Appearance"
    onActivated: index => application.setDarkMode(index === 1)
    IASegmentedButton { text: "Light" }
    IASegmentedButton { text: "Dark" }
}
IASwitch {
    width: 300
    text: "Touch targets"
    indicatorPosition: IA.Presentation.Trailing
}
```

`currentIndex`/`currentItem` and add/insert/move/take/remove remain native Container
APIs. Use `setCurrentIndex(index)` to retain an existing index binding; `-1` clears
selection and an out-of-range index has no current item. First nonempty construction
selects the native first index. Checking another member programmatically updates
the index without `activated`. Clear through the container, not by unchecking an
individual member. Pointer, Space and accessible activation use native button
semantics. `activated(index)` reports a changed interactive selection only, not
programmatic updates, structural changes or activating an already checked member.
Native `click()`/`animateClick()` also simulate interaction; the group does not call
them internally. Left/Right selects and focuses the next enabled, visible member,
mirrored in RTL, without wrapping. Disabling/hiding the selected member preserves
selection. Group and member accessibility roles are Grouping and RadioButton.

`IASwitch.indicatorPosition` defaults to `IA.Presentation.Leading`, preserving
existing rows. `Trailing` puts the label in the leading content lane and the switch
in the trailing lane, mirrored once in RTL. Labels stay single-line and elide;
outer control padding, native drag/Space behavior and focus geometry are unchanged.
The shared choice lane keeps one logical horizontal anchor and mirrors its edge.
Live direction and Leading/Trailing changes never transiently set opposing anchors,
so check, radio and switch indicators retain natural dimensions. Switch thumb
direction remains Qt's `visualPosition`, without a second mirror.

`IAMenuSeparator` and `IAToolSeparator` retain `foregroundColor`,
`appearanceDiagnostics` and `resetAppearance()`, but use the sparse Foundation
`SeparatorAppearance` color facade instead of a text/font facade. Their default
and reset value is the live `separator` role and their override feeds the actual
Foundation `Separator` line. Equal-valued assignments remain locally owned;
`undefined`/RESET resumes the live role. Decorative lines do not acquire text
contrast diagnostics, font measurement or control state tables.

Its semantic inputs are `source` (`Appearance*`), `family` (QString), `tone`
(`Presentation::Tone`), and `validation` (`Presentation::Validation`). Enums are
`Presentation.Auto`, `Neutral`, `Primary`, `Destructive`, `Caution`, and
`Presentation.None`, `Warning`, `Invalid`. `Caution` is an action tone, distinct
from validation `Warning`. `Auto` resolves neutral here: action controls map their
template `highlighted` input to Primary before passing it to this resolver;
delegates/menu items do not reinterpret navigation highlight as Primary.

| # | Family | Neutral Base / Differences |
|---|---|---|
| 1 | `button` (default) | `control` / `text`; `controlHeight` |
| 2 | `tool` | Transparent / text; `toolHeight` |
| 3 | `field` | `content` / text; `controlHeight` |
| 4 | `surface` | `content` / text; content-derived height |
| 5 | `popup` | `popup` / text; `popupRadius`, `popupPadding`, content-derived height |
| 6 | `label` | Transparent / text; content-derived height |
| 7 | `delegate`, `tab` | Transparent / text; `rowHeight` |
| 8 | `choice` | Transparent / text; `controlHeight` |
| 9 | `tooltip` | Tooltip background/text; `supportingSize`; no hover/press recoloring |
| 10 | `menuItem` | Delegate geometry/resting mapping; exact accent/onAccent navigation highlight with conditional inward outline |
| 11 | `tabBar` | Quiet `control`-seeded group track; `spacingXS` insets; no interaction recoloring |
| 12 | `slider` | Neutral thumb with interaction feedback; independent neutral value remainder and accent fill |
| 13 | `progress` | Passive neutral value remainder and accent fill |
| 14 | `scrollBar` | Transparent track; quiet neutral position/extent thumb with stronger hover/press roles |

Primary/Caution/Destructive choose their solid surface, text and outline roles.
Hover and pressed use the matching semantic states; selection uses the pressed
surface, and inactive selection uses the normal surface with the same paired text
for colored non-label controls. Colored labels retain the neutral selected/inactive
pairs. These pairs remain separate from local ordinary background/pressed overrides.
Disabled controls retain neutral disabled colors and outlines. A local border
override still applies while disabled unless a distinct `disabledBorderColor`
is supplied; validation outlines also retain their existing behavior. Labels
instead use `accentText`, `warningText` or `errorText` on their transparent base.
Validation selects `warningText`/`errorText` for the outline. Invalid fields retain
that outline color at the focus width when focused; other focus colors follow
the per-state readability selection above.
An unspecified semantic border follows the final local `foregroundColor`, including
foreground derivation after local background overrides. An explicit application
`primaryOutline`/`cautionOutline`/`destructiveOutline`, validation border, or local
`borderColor` takes precedence over this relationship; local border overrides win
last. RESET resumes the current applicable default, not a captured color.
Validation does not inspect application data. Families do not find parents or
make interaction-state decisions.

All properties in the following table are typed, writable, and have Q_PROPERTY
RESET handlers. Getters return effective values; setters store only local
overrides. In C++, `backgroundColor()` has `setBackgroundColor(const QColor&)`
and `resetBackgroundColor()`; the same capitalization pattern applies to every
property. In QML, assignment to `undefined` invokes RESET on this object. An IA
control's forwarding API must preserve that reset behavior rather than maintain
another override map.

| # | Property | Type | Default / Meaning |
|---|---|---|---|
| 1 | `backgroundColor` | QColor | Family/tone's normal base only |
| 2 | `foregroundColor` | QColor | Family/tone's normal, hover and pressed text |
| 3 | `hoverBackgroundColor`, `pressedBackgroundColor` | QColor | Neutral or semantic state recipes |
| 4 | `disabledBackgroundColor`, `disabledForegroundColor` | QColor | `disabledSurface`, `disabledText` |
| 5 | `inactiveBackgroundColor`, `inactiveForegroundColor` | QColor | Neutral `inactiveSelection`/`text`, or semantic normal surface/text |
| 6 | `selectedBackgroundColor`, `selectedForegroundColor` | QColor | `selection`/`text`, or semantic pressed/paired text |
| 7 | `borderColor`, `focusColor` | QColor | Functional/validation outline, independent focus |
| 8 | `controlRadius` | qreal | Control or popup radius |
| 9 | `fontFamily`, `fontPixelSize`, `fontWeight` | QString, qreal, int | Effective typography |
| 10 | `minimumHeight` | qreal | Maximum of requested floor and font height plus twice vertical padding |
| 11 | `horizontalPadding`, `verticalPadding`, `spacing` | qreal | Matching metric tokens, or popup padding |
| 12 | `fillColor` | QColor | Solid `accent`, independent of action-surface overrides; used by slider/progress fills |
| 13 | `disabledBorderColor` | QColor | Neutral `outline`, or explicit local/validation border; independently overridable |

The disabled, inactive and selected backgrounds have their own paired foregrounds;
visuals must consume the corresponding pair instead of placing on-accent text on
a neutral inactive surface. A local `backgroundColor` override roots its missing
ordinary states and label locally. The completed table applies the bounded
feedback recipe above; it does not use the page's main text as a blend endpoint.
Explicit state backgrounds remain unchanged. Selected, inactive and disabled
backgrounds remain separate from that base override.

After applying all local background and state overrides and deriving missing
states, the resolver HCT-derives an unspecified `foregroundColor` from the
effective base across normal, hover and pressed backgrounds together. This also
happens for a hover-only or pressed-only background override, not just a base
override. Ordinary hover/pressed pairs have no separate text input; they share
`foregroundColor`. A local selected/inactive background independently derives its
unspecified paired foreground from that surface. Explicit foregrounds and state
colors are retained, even when assigned the current default, and unresolved
contrast is reported. Disabled pairs retain their separate treatment rather than
being included in enabled-text derivation.

These local results are sparse, component-owned branches cached by their actual
surfaces, explicit presence, backdrop, target and applicable blend inputs. A local
normal background roots its missing ordinary text/hover/pressed colors locally,
so an unrelated accent edit does not recalculate that branch. Actually inherited
focus, fill, selected/inactive or validation roles still follow the shared state.
A hover-only or pressed-only override legitimately retains inherited dependencies.
Ordinary diagnostic pairs are measured once in shared state and projected to local
paths; locally changed pairs are measured locally, independently of font/geometry.

Projected ordinary pair findings are shared by palette and role mapping, rather
than copied into each unmodified control. A control flattens its public diagnostic
list only when read, caching it until its effective findings or input errors change.
Diagnostic notifications compare those components without forcing that list to be
built and retain the same changed-effective-value and nested-publication contract.

Local validation reuses TokenSchema. Local pixel sizes allow `[3, 384]`, the full
effective global size range; local height floors allow `[0, 4096]` to accommodate
font-derived defaults. Other local bounds match their corresponding tokens.
Typed QML properties undergo Qt's normal type conversion before their C++ setter;
strict original-QVariant input typing applies at the application-map boundary.
Invalid local assignments preserve the last valid local override and publish a
diagnostic. A valid assignment or reset clears that property's input diagnostic.

Explicit presence is tracked even when an assigned value equals the current
default. Global updates change only unspecified values. Resets resume the **live**
source/default, not a value captured when the control was created. Height remains
a floor: it cannot clip the chosen font and padding, and interactive families in
touch density also honor `targetSize`. Local font/metric overrides remain local
through source changes; they are not overwritten by a theme-change handler.

Read-only `resolvedFont` is a QFont; read-only `diagnostics` is a QVariantList of
local validation/contrast findings. Global input findings remain on Appearance.
Every property and semantic input has its own `<property>Changed` notification,
including `sourceChanged`, `resolvedFontChanged`, `minimumHeightChanged` and
`diagnosticsChanged`. These notify only changed effective values after the complete
facade state is installed. `resolvedChanged()` remains an aggregate snapshot signal
for source changes, override ownership/reset and diagnostic operations, including
equal-valued assignments/replacements with no specific value notification.
`resetOverrides()` clears all local values/errors in one aggregate operation,
without changing source, family, tone, validation, global input or interaction data.
IA controls expose this as `resetAppearance()`.

## Popup Direction

Direction is Qt layout inheritance, not appearance input. `IAPopup`, `IADialog`,
`IAMenu` and `IAToolTip` share an internal nonvisual Foundation adapter for Qt's
separate popup-window boundary. It receives the public overlay/content objects;
the inherited value is the first source ancestor with
`LayoutMirroring.childrenInherit`, using its effective `enabled` value. Thus an
overlay's own enabled bit without child inheritance does not override its
ancestor's child value. No helper Item is inserted into the overlay.
Each root gates the adapter's content input until its own component completion,
so reading that input cannot prematurely execute Qt's deferred custom content.
Later content replacement remains declaratively observed.
Nonvisual appearance helpers belong to explicit root properties, not the public
content/model route, so they do not prematurely begin deferred custom content.

Only the popup-exclusive native Window receives enabled/childrenInherit bindings.
Item hosting and host migration never write the owner's Window properties. The
target is reclassified on content-window changes, replacement and completion,
not on the earlier source-overlay notification. This is an implementation detail,
not a new popup option, locale mutation or application direction provider.

## Dialog Composition

`IADialogButtonBox.principalAction` controls visual prominence of its standard
buttons, independently of Qt's roles, signals, ordering and keyboard behavior.
The default `-1` selects exactly one completion: the first present button in
Ok/Open/Save/SaveAll/Retry/Ignore order, then Yes/YesToAll, then Apply. Set a
standard-button enum to choose another completion, or `NoButton` for none.
Missing or non-completion choices produce no Primary button; Cancel/Close stay
neutral and Discard stays Destructive. Custom buttons retain their own tones.

Horizontal footer layout publishes a component-local snapshot during Qt Quick's
normal polish phase. `contentWidth` is the allocated lane (at least the available
width or actual button extent), not the natural button extent; the ListView's
content width includes its inline alignment spacers. `implicitContentWidth`
retains Qt's native implicit minimum. Horizontal alignment uses actual button
widths, including explicit custom widths; fill-mode `implicitWidth` remains a
natural minimum instead of referring back to stretched allocation. Native
vertical sizing and model/index/focus/input behavior remain owned by Qt.
The Controls-local layout helper and underscored bindings are implementation
details, not an application API or a way to synchronously force polishing.

`IADialog` uses `dialogMinWidth`, not the menu width floor. Its popup padding
defines the header, content and footer edges; the owned footer takes those same
four insets explicitly, without offsets. `ControlAppearance.headingPixelSize`
and `headingWeight` expose the shared heading/emphasis roles for owned title
text. Explicit local `fontPixelSize`/`fontWeight`, including equal-valued
assignments, win for both ordinary text and the heading; reset resumes the live
roles. The dialog forwards ordinary typography to its standard buttons and
heading typography to its title. Surface/radius overrides remain on the dialog
surface, not its buttons; application content remains application-owned.

Put an ordinary body layout among the dialog's children, not in `contentItem`:

```qml
import QtQuick.Layouts
import QtQuick.Templates as T
import InterActive

IADialog {
    width: 420
    title: "Preferences"
    standardButtons: T.Dialog.Ok | T.Dialog.Cancel
    ColumnLayout {
        width: parent.width
        IATextField { Layout.fillWidth: true }
    }
}
```

The default `bodyData` list forwards visual and nonvisual objects to the owned
natural-size host. A single body's implicit size supplies the natural size;
multiple visual children need an application-owned layout. Bind that layout's
width to `parent.width`, and expose its natural `implicitHeight` independently
of viewport height. The body host always receives the actual viewport width.
Do not anchor natural body height to the viewport or add a redundant outer
ScrollView. A deliberately bounded nested editor may still own its own scroll.

The dialog's implicit height includes body, header, footer, padding, spacing and
constant edge gaps. Its default actual height grows to fit, capped by the item
host window's height or the Window popup's host-screen available height, less margins.
Explicit width and height remain consumer-owned overrides; `dialogMinWidth` is
an implicit-width floor. Window sizing never uses the popup's own window height.
`ScreenGeometry` is a public-Qt data adapter with an explicit `QQuickWindow`
input and read-only per-screen `availableGeometry`; it observes screen changes,
available-geometry updates and lifetime. It never substitutes virtual-desktop
geometry. Without a pre-show host, the dialog falls back to its independent
natural height until the real host is available.

Only the body scrolls vertically. A stable trailing lane, mirrored in RTL,
reserves the actual vertical scrollbar width plus `spacingXS`, even when the
bar is hidden. This dialog-local composition does not change general scrolling
controls or add a global scrollbar setting. Hidden edges alone show a 1px
separator and a 16px inward surface fade; `spacingS` gaps remain when hidden.
Reduced motion removes fades, not lines or spacing. The default heading owns a
permanent quiet title/body separator. Its presence suppresses the conditional
top fold line, not the hidden-content fade. Custom or absent headings retain
the conditional fold line; replacing the header still replaces its artwork.
All three dividers span the actual background frame interior, so they never
paint over its side outline, including a thicker high-contrast border.

The structural title row elides its title and provides a square, accessible
Close icon when its `IADialogButtonBox.hasRejectAction` query is true. That live
query observes standard and custom RejectRole buttons, insertion/removal and
role changes. The icon calls the native dialog `reject()` path. Qt continues
to own standard-button actions, modality, keyboard behavior and popup focus.

Migration: replace `contentItem: MyBody { ... }` with `MyBody { width:
parent.width; ... }`, remove the former body's outer scrolling and height cap,
and retain any distinct bounded editor inside it. Replacing `contentItem`
deliberately replaces the owned viewport and is not the ordinary-body API.

## Verification Boundary

The unit target is `interactive_appearance_tests`, output
`${PROJECT_BINARY_DIR}/bin/interactive-appearance-tests`, linked to the real
`InterActive::Foundation` backing library. CTest name is `interactive.appearance`
and label is `interactive-overview`. Its source covers schema/profile/type-surface
completeness, replacement/reset, strict validation and rejection, derivations,
contrast, motion/density/scaling, notification snapshots, independent engines,
QML creation/RESET, every local reset property, source lifetime, fonts and local
precedence.
The semantic palette matrix independently measures final HCT/CAM16-UCS values,
full/RGB8 contrast, neutral invariance, intensity response, exact input precedence,
state-only reservations, immutable collisions, alpha compositing, determinism and
bounded impossible-input behavior. Its color-space math linkage is test-only;
production selection is not the test oracle.
The QML creation case allows up to 10 seconds for pending component loading,
processes events while waiting, and requires `Ready` before calling `create()`.
It stops waiting immediately on an error and reports the initial/current status,
URL, import paths and component errors on failure; it does not retry creation or
discard import errors.

After configuring with the selected Qt SDK, run the focused tests in a graphical
test environment. CTest inherits that environment without a test-local import
path override. Add the IA build's QML root without discarding any SDK import
paths already supplied by the environment; Qt also includes its installation's
default QML import locations.

```bash
cmake --build build --target interactive_appearance_tests --parallel 4
QML_IMPORT_PATH="$PWD/build/qml${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}" \
    ctest --test-dir build -R '^interactive\.appearance$' \
    --no-tests=error --output-on-failure
```

Consumer propagation, real fonts/layout, non-overlapping touch targets, icon
resources and rendering, open popups, contrast against arbitrary host content,
and actual reduced-motion busy feedback still require control-level and rendered
GUI checks. No Foundation unit result can by itself prove those outcomes.
