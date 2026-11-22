// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.ToolBar {
    id: control
    property alias backgroundColor: appearance.backgroundColor
    property alias foregroundColor: appearance.foregroundColor
    property alias borderColor: appearance.borderColor
    property alias fontFamily: appearance.fontFamily
    property alias fontPixelSize: appearance.fontPixelSize
    property alias fontWeight: appearance.fontWeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    readonly property IA.ControlAppearance _appearance: IA.ControlAppearance { id: appearance; source: IA.Appearance; family: "surface" }
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: Math.max(implicitContentHeight, IA.Appearance.metrics.toolHeight) + topPadding + bottomPadding
    horizontalPadding: appearance.horizontalPadding
    verticalPadding: appearance.verticalPadding
    font: appearance.resolvedFont
    contentItem: Item { }
    background: Rectangle {
        objectName: "iaBackground"
        color: control.backgroundColor
        IA.Separator {
            color: control.borderColor
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: control.position === T.ToolBar.Footer ? parent.top : undefined
            anchors.bottom: control.position === T.ToolBar.Header ? parent.bottom : undefined
        }
    }
}
