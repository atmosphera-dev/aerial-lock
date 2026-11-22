// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.Switch {
    id: control
    property int indicatorPosition: IA.Presentation.Leading
    property alias backgroundColor: appearance.backgroundColor
    property alias foregroundColor: appearance.foregroundColor
    property alias hoverBackgroundColor: appearance.hoverBackgroundColor
    property alias pressedBackgroundColor: appearance.pressedBackgroundColor
    property alias disabledBackgroundColor: appearance.disabledBackgroundColor
    property alias disabledForegroundColor: appearance.disabledForegroundColor
    property alias inactiveBackgroundColor: appearance.inactiveBackgroundColor
    property alias inactiveForegroundColor: appearance.inactiveForegroundColor
    property alias selectedBackgroundColor: appearance.selectedBackgroundColor
    property alias selectedForegroundColor: appearance.selectedForegroundColor
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
    IA.ControlAppearance { id: appearance; source: IA.Appearance; family: "choice" }
    implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding, minimumHeight)
    implicitHeight: Math.max(Math.max(implicitContentHeight, implicitIndicatorHeight) + topPadding + bottomPadding, minimumHeight)
    horizontalPadding: appearance.horizontalPadding
    verticalPadding: appearance.verticalPadding
    spacing: appearance.spacing
    font: appearance.resolvedFont
    hoverEnabled: true
    indicator: choiceRow.indicator
    contentItem: IA.ChoiceRow {
        id: choiceRow
        kind: IA.ChoiceIndicator.Toggle
        resolvedAppearance: appearance
        checked: control.checked
        visualPosition: control.visualPosition
        controlEnabled: control.enabled
        down: control.down
        hovered: control.hovered
        active: control.Window.active
        focused: control.visualFocus
        mirrored: control.mirrored
        indicatorPosition: control.indicatorPosition
        spacing: control.spacing
        text: control.text
        font: control.font
    }
    background: IA.ControlBackground {
        resolvedAppearance: appearance
        color: colors.choiceGapColor
        outlined: false
    }
}
