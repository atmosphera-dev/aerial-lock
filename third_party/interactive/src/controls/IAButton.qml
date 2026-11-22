// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.Button {
    id: control
    property int tone: IA.Presentation.Auto
    property alias backgroundColor: appearance.backgroundColor
    property alias foregroundColor: appearance.foregroundColor
    property alias hoverBackgroundColor: appearance.hoverBackgroundColor
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

    IA.ControlAppearance {
        id: appearance
        source: IA.Appearance
        family: "button"
        tone: control.tone === IA.Presentation.Auto && control.highlighted
              ? IA.Presentation.Primary : control.tone
    }

    implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding, minimumHeight)
    implicitHeight: Math.max(implicitContentHeight + topPadding + bottomPadding, minimumHeight)
    horizontalPadding: appearance.horizontalPadding
    verticalPadding: appearance.verticalPadding
    spacing: appearance.spacing
    font: appearance.resolvedFont
    hoverEnabled: true
    icon.width: IA.Appearance.metrics.iconSize
    icon.height: IA.Appearance.metrics.iconSize
    icon.color: surface.foregroundColor

    contentItem: IA.IconLabel {
        contentShift: surface.contentShift
        text: control.text
        font: control.font
        color: surface.foregroundColor
        iconName: control.icon.name
        iconSource: IA.IconSource.resolve(control, control.icon.source)
        iconWidth: control.icon.width
        iconHeight: control.icon.height
        iconColor: control.icon.color
        iconCache: control.icon.cache
        display: control.display
        spacing: control.spacing
        mirrored: control.mirrored
    }
    background: IA.ControlBackground {
        id: surface
        pressInset: true
        resolvedAppearance: appearance
        controlEnabled: control.enabled
        down: control.down
        hovered: control.hovered
        selected: control.checked
        active: control.Window.active
        focused: control.visualFocus
    }
}
