# InterActive Architecture

This document describes the approved architectural direction, not a claim of
implemented control coverage, stable API spellings, or completed verification.
InterActive (IA) targets Qt 6.12.0 or newer and provides explicitly imported,
IA-owned Qt Quick controls and reusable compositions.

This supported minimum includes Qt's native explicit popup sizing fix on both
axes ([QTBUG-147847](https://bugreports.qt.io/browse/QTBUG-147847)); IA does not
replace that ownership with a sizing workaround.

## Module Layers

The layers build upward: `Foundation -> Controls -> Components -> Playground`.
Higher layers consume lower layers; lower layers never import their consumers.

| # | Layer / QML module | Responsibility |
|---|---|---|
| 1 | Foundation / `InterActive.Foundation` | Engine-local appearance input and finished shared palette, complete profiles, validation, sparse per-control overrides, typed output groups, and reusable rendering helpers. |
| 2 | Controls / `InterActive` | IA-prefixed controls rooted in `QtQuick.Templates`, combining Qt interaction semantics with IA-owned appearance and visuals. |
| 3 | Components / `InterActive.Components` | Generic compositions of IA controls, receiving content, values, and semantic callbacks or signals. |
| 4 | Playground / `InterActive.Playground` | Non-installed example application consuming the real library; owns fixtures, specimen pages, appearance editing, and review automation. |

The playground is a consumer, not a second control implementation. Library
modules must not depend on its fixtures, launch arguments, or automation.

## Appearance Resolution

The shared path is engine-local `Appearance`, then property forwarding and genuine
component-local override branches, then typed resolved values consumed by visuals. Precedence is
**library defaults -> valid application-supplied appearance -> explicit local
overrides**. Foundation owns this resolution once; controls do not implement
independent fallback or precedence logic.

`Appearance` has one instance per `QQmlEngine`, with no mutable appearance state
shared across engines. One application-level writer supplies replacement input
snapshots. Complete, replaceable Airmail-inspired profiles provide a light
no-input default and a complete dark alternative. Missing values use the selected
profile and shared derivations; omitted fields must not retain stale overrides.
The library does not discover the system's preferred scheme.

Build a complete resolved candidate before notifying consumers. Invalid or
unknown individual fields produce diagnostics without discarding valid fields;
malformed envelopes or unsupported schema versions reject the update and retain
the previous valid snapshot. Colors, typography, metrics, motion, and icon
recipes belong behind this common input and typed-output boundary, not in
scattered string-key lookups or per-control aesthetic constants.

`Appearance.colors` is the stable public object backed by an immutable finished
palette. Accepted replacements rebuild that palette only when its normalized color,
ownership, contrast or blend inputs change. Ordinary controls share its state and
select roles rather than reconstruct palettes or perform contrast mathematics.
Typography, geometry, motion and icon outputs are resolved independently.

`ControlAppearance` consumes `Appearance` and owns selectors, sparse local overrides
and only their dependent local results, plus separate typography/geometry records. It must distinguish
unset values from explicit assignments, including assignments equal to the
current default. Local getters expose resolved values; setters alter override
state. Clearing overrides resumes the current application/default values without
clearing global input, semantic presentation, or interaction state. Derive
unspecified dependent values after overrides, respect explicit paired values,
and retain meaningful disabled, inactive, focus, and validation treatments.

Properties notify through specific signals, such as `accentChanged` and
`resolvedFontChanged`, only for changed effective values. Aggregate
`resolvedChanged`/token-group `changed` signals remain complete-snapshot observers.
Each appearance/facade installs complete state before notifying; nested updates
retain pending notification obligations and owned immutable source references.
This is a per-object guarantee, not a graph-wide transaction across sibling
controls and arbitrary bindings. Setters remain synchronous.

The supported customization boundary is IA-owned properties. Inherited native
Qt `palette` and `font` members are not an alternative theming API or a promised
compatibility layer. Where templates need them, populate them internally from
resolved IA values as output adapters; IA visuals consume the typed IA output
directly. Do not mutate the process-global Qt palette, font, or style. Live
appearance updates must preserve text, selection, scroll position, focus, and
already-open popups rather than recreate controls or the engine.

## Controls And Ownership

Qt templates retain keyboard, pointer, touch, checked/value, and accessibility
semantics. IA supplies the visual implementation and appearance contract, not
replacement interaction machinery built from rectangles and mouse areas. This
is not a named QQC2 style: consumers explicitly import `InterActive` and use
IA types. Ordinary Qt controls are neither automatically replaced nor restyled,
and there is no native or Basic style fallback that counts as IA coverage.

Controls obtain appearance internally without looking up a parent/window theme
provider. A compatible plain Qt Quick window is sufficient, subject to each
template's normal scene/window requirements. `IAApplicationWindow` is an optional
convenience, never a prerequisite or caller-installed appearance root.

`IAComboBox` keeps the template's model and selection owner, with a native text
input content item for editable semantics and an elided label for ordinary
selection. Its indicator is a trailing square lane; the optional swatch uses the
editor's leading lane. The owned IAPopup/ListView/IAItemDelegate composition does
not replace template keyboard, activation or popup-flipping behavior.

A component knows its children, not its parents. Consumers pass data, content,
and callbacks and use public methods or signals; neither side reaches into the
other's internal items or state. Shared helpers receive explicit inputs. The
application supplies validation state and decides what interactions do; IA does
not inspect application data to infer validity or business behavior.

Composite controls must provide their IA visual dependencies, including editors,
delegates, indicators, popup content, and standard dialog buttons. Popups consume
engine-local appearance even in separate windows; composites pass any intended
local overrides explicitly rather than rely on visual-parent palette inheritance.
Item- and Window-hosted QML popups belong to the design; native platform-rendered
popups do not establish IA styling coverage. Alignment belongs in shared layout
lanes, grids, or anchors, not unrelated pixel offsets.

The color picker has one narrow specialized interaction: `IAColorPlane` uses a
public template Control, pointer handler and Keys for its two physical HSL axes.
There is no public Qt color-plane template. Its Grouping accessibility role
describes both axes; native fields provide equivalent channel editing. Native
sliders, fields, segments, popup and dialog buttons keep their Qt semantics.
Foundation's stateless `ColorPlaneSurface` caches an opaque QColor HSL image on
the GUI thread; only hue, physical size and DPR invalidate it. The marker and
opacity are separate consumers, never renderer-owned state. The plane's shared
ControlBackground owns its complete inside-bounds focus frame. Constant resolved
maximum normal/focused border padding keeps data geometry stable on focus. A live
ShaderEffectSource combines the existing surface and separate QML marker into
one offscreen texture; QtQuick.Shapes ShapePath.fillItem/PathRectangle clips that
texture to the resolved inner radius. The component-local CurveRenderer supplies
antialiased path edges without global MSAA. Marker movement updates the offscreen
composition, not the cached CPU color image. Endpoint centers retain physical
coordinates and the plane owns their clipping, never its containing viewport.

Components' internal `ColorEditState` is the sole full-precision color/draft/hue
owner for each persistent `IAColorEditor`. `colorValue` synchronizes external
authority without emitting an edit; `currentColor` is installed before user-only
`colorEdited`. Display formatting, mode changes and same-color feedback do not
roundtrip through RGB8 text. Drafts and mode objects persist across reflow/hiding.
After the synchronous edit callback, currentColor reconciles to authoritative
colorValue without an edit echo, including ignored or application-corrected
requests. A clean field's native focus loss is not a text edit.
`IAColorDialog` composes this editor under the shared IADialog header and a direct
native Close button box. Its measured caption uses public background/padding
slots, with natural width independent of narrow reflow. The explicit source
`parentWindow` accepts any compatible QQuickWindow, including QQuickView; no
application-window subtype or parent theme lookup is needed. Playground's thin
adapter alone owns role writing, synchronization trace scopes and persistence.
The dialog explicitly closes only through Escape, its shared header or native
Close button, not on main-window/peer clicks. Hue/opacity artwork mirrors with
native slider visualPosition; the plane's physical HSL axes do not.

Qt reparents Window-hosted popup content outside the owner's visual tree. The
four popup roots share Foundation's internal nonvisual `PopupDirection` adapter.
Its explicit inputs are Qt's public source overlay and the public content item.
It reads the exact implicit-child inheritance metadata along that supplied
overlay's Item parent chain and binds only the popup-exclusive native Window.
Native target classification occurs on content-window change, content replacement
and completion, not on the earlier logical-source change during host migration.
The adapter records source-host provenance separately and excludes every observed
application host from native targets, including an old Item host retained during
enter when replacement content arrives after logical migration.
Each popup root supplies its content input only after its own QML completion,
after Qt finishes parser-status callbacks and deferred content. The binding stays
live for later replacement; the adapter cannot eagerly execute deferred content.
At a Window's content root it reads both public mirroring aliases: they share
values, but Qt can notify either attachment. Other source Items are unchanged.
All four popup roots own nonvisual appearance metadata through explicit readonly
properties, never public default contentData. A helper cannot begin deferred
visual-content construction before a consumer's custom content is recorded.
No visual witness is added to Overlay, no host is written, and no theme provider,
locale/global direction, private popup item or application direction API is added.

The dialog button box owns one native model-backed ListView. A small internal
QQuickItem adapter snapshots only its new horizontal geometry during public
polish; native vertical measurement, sizing and interaction remain Qt's. The
view reports the committed actual button extent as its natural implicit width,
separately from the allocated lane and inline spacer widths. This preserves
Qt's natural-size invalidation without entering layout during a partial model
mutation. Consumers neither install the adapter nor force its polish phase.

## Scope Boundaries

Applications own appearance acquisition, adaptation, and persistence. InterActive
has no D-Bus transport, `dbusqml` dependency, portal reader, shell detection, or
preference storage. It has no Aerogram controllers, mail concepts, or Atmosphera
imports. Airmail is visual inspiration, not copied branding, assets, or code.

Brief is a separate workstream after the library is established and proven.
Aerogram migration, external appearance protocols, and complete Qt control
catalogue coverage are not part of this architecture document's implementation
claims. Coverage must be recorded explicitly and verified at the consuming
visuals and interactions; successful startup or an updated singleton alone does
not prove the appearance contract.
