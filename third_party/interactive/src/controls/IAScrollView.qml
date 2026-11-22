// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.ScrollView {
    id: control
    property alias backgroundColor: appearance.backgroundColor
    property alias borderColor: appearance.borderColor
    property alias disabledBorderColor: appearance.disabledBorderColor
    property alias controlRadius: appearance.controlRadius
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    readonly property IA.ControlAppearance _appearance: IA.ControlAppearance { id: appearance; source: IA.Appearance; family: "surface" }
    implicitWidth: contentWidth + leftPadding + rightPadding
    implicitHeight: contentHeight + topPadding + bottomPadding
    clip: true
    T.ScrollBar.vertical: IAScrollBar {
        parent: control
        x: control.mirrored ? 0 : control.width - width
        y: control.topPadding
        height: control.availableHeight
        active: control.T.ScrollBar.horizontal.active
    }
    T.ScrollBar.horizontal: IAScrollBar {
        parent: control
        x: control.leftPadding
        y: control.height - height
        width: control.availableWidth
        active: control.T.ScrollBar.vertical.active
    }
    background: IA.ControlBackground {
        resolvedAppearance: appearance
        outlined: false
    }
}
