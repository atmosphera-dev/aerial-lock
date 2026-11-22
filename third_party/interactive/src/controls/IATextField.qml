// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.TextField {
    id: control
    property int validation: IA.Presentation.None
    property alias backgroundColor: appearance.backgroundColor
    property alias foregroundColor: appearance.foregroundColor
    property alias disabledBackgroundColor: appearance.disabledBackgroundColor
    property alias disabledForegroundColor: appearance.disabledForegroundColor
    property alias borderColor: appearance.borderColor
    property alias disabledBorderColor: appearance.disabledBorderColor
    property alias focusColor: appearance.focusColor
    property alias controlRadius: appearance.controlRadius
    property alias fontFamily: appearance.fontFamily
    property alias fontPixelSize: appearance.fontPixelSize
    property alias fontWeight: appearance.fontWeight
    property alias minimumHeight: appearance.minimumHeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    IA.ControlAppearance {
        id: appearance
        source: IA.Appearance
        family: "field"
        validation: control.validation
        readonly property IA.stateColors colors: stateTable.colors(
            IA.ControlState.of(control.enabled, false, false, false, true, false))
    }
    implicitWidth: Math.max(contentWidth, placeholder.implicitWidth, IA.Appearance.metrics.fieldWidth)
                   + leftPadding + rightPadding
    implicitHeight: Math.max(contentHeight + topPadding + bottomPadding, minimumHeight)
    leftPadding: appearance.horizontalPadding
    rightPadding: appearance.horizontalPadding
    topPadding: appearance.verticalPadding
    bottomPadding: appearance.verticalPadding
    font: appearance.resolvedFont
    color: appearance.colors.foregroundColor
    placeholderTextColor: appearance.colors.placeholderTextColor
    selectionColor: appearance.stateTable.selectionColor
    selectedTextColor: appearance.stateTable.selectedTextColor
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true
    Text {
        id: placeholder
        objectName: "iaPlaceholder"
        anchors.fill: parent
        anchors.leftMargin: control.leftPadding
        anchors.rightMargin: control.rightPadding
        anchors.topMargin: control.topPadding
        anchors.bottomMargin: control.bottomPadding
        visible: control.length === 0 && control.preeditText.length === 0
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        textFormat: Text.PlainText
        elide: Text.ElideRight
        horizontalAlignment: control.horizontalAlignment
        verticalAlignment: control.verticalAlignment
    }
    background: IA.ControlBackground {
        resolvedAppearance: appearance
        controlEnabled: control.enabled
        focused: control.activeFocus
    }
}
