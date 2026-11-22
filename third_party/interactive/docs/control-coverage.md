# Control Coverage

Controls implemented so far.
All listed controls use Qt templates and the shared IA appearance resolver.
Ordinary Qt Controls are not an IA fallback.

| # | Family | Implemented For The Overview |
|---|---|---|
| 1 | Actions and navigation | IAButton, IAToolButton, IAToolBar, IAToolSeparator, IATabBar, IATabButton |
| 2 | Text and forms | IALabel, IATextField, IAComboBox |
| 3 | Choices | IACheckBox, IARadioButton, IASwitch (leading/trailing), IASegmentedControl, IASegmentedButton |
| 4 | Lists and scrolling | IAItemDelegate, IAScrollView, IAScrollBar |
| 5 | Values | IASlider, IAProgressBar, IAColorWell, IAColorPlane |
| 6 | Surfaces and overlays | IAPane, IAFrame, IAMenu, IAMenuItem, IAMenuSeparator, IAPopup, IADialog, IADialogButtonBox, standalone IAToolTip |
| 7 | Compositions | IAFieldRow, IASettingsRow, IAStatusBanner, IAEmptyState, IAColorEditor, IAColorDialog |

Menu delegates, submenu arrows/checkmarks, dialog footer buttons, scroll bars,
focus treatment, and source/name icons are supplied by the library rather than
page-local replacements. Foundation owns the neutral SVG fixtures and rendering
helpers. See [test coverage](playground.md#test-coverage) for observed outcomes:
implementation presence is not runtime proof.

`IAColorWell` owns native sample/custom buttons and a quick IAMenu, not a full
color dialog. Its focused suite and separate native state captures cover its
capsule geometry, circle containment, preset/default signals and reversible
rainbow motion. The playground supplies the existing live dialog adapter.

`IAColorPlane` is the specialized two-axis HSL control: it uses the public Qt
template Control, pointer handler and Keys; Qt has no public color-plane template.
Its stable data geometry and inside-bounds focus frame surround a
cached color surface and separate marker, rounded together through QtQuick.Shapes.
`IAColorEditor` owns persistent formats/drafts through its internal color state;
`IAColorDialog` composes it with the shared IA dialog header and native Close
button box. Applications retain color authority through `colorValue` and the
user-only `colorEdited` callback. Public Qt accessible-interface checks are not
AT-SPI/Orca proof.

Planned controls: IARoundButton, IATextArea, IASpinBox,
IACheckDelegate, IARadioDelegate, IASwitchDelegate,
IAScrollIndicator, IARangeSlider, IABusyIndicator, IAApplicationWindow,
IAPage, IAGroupBox, IAMenuBar, IAMenuBarItem and the attached-tooltip helper.
Also planned: the five further gallery pages (Actions, Forms, Values, Lists,
Overlays), a relocated installed-consumer proof, full verification on a current
Qt release, and accessibility checks. The newer exact Qt 6.12.0 lane supplies
scoped native evidence, not that outstanding broad verification. Hosted CI is
limited to build, QML lint and three headless unit suites, as described in
[the test environment](../tests/README.md#hosted-build-lint-and-headless-units).
The current suites do not establish full library or gallery completion; existing
native findings remain listed in [Known limits](../README.md#known-limits).
