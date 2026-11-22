// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.Label {
    id: control
    property int tone: IA.Presentation.Auto
    property alias backgroundColor: appearance.backgroundColor
    property alias foregroundColor: appearance.foregroundColor
    property alias disabledForegroundColor: appearance.disabledForegroundColor
    property alias controlRadius: appearance.controlRadius
    property alias fontFamily: appearance.fontFamily
    property alias fontPixelSize: appearance.fontPixelSize
    property alias fontWeight: appearance.fontWeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    IA.ControlAppearance {
        id: appearance
        source: IA.Appearance
        family: "label"
        tone: control.tone
    }
    color: appearance.stateTable.colors(IA.ControlState.of(enabled, false, false, false, true, false)).foregroundColor
    linkColor: appearance.stateTable.linkColor
    font: appearance.resolvedFont
    elide: Text.ElideRight
    background: Rectangle {
        objectName: "iaBackground"
        color: control.backgroundColor
        radius: control.controlRadius
    }
}
