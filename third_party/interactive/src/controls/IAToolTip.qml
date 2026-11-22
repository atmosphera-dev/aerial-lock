// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.ToolTip {
    id: control
    property bool _popupDirectionReady: false
    Component.onCompleted: control._popupDirectionReady = true
    readonly property IA.PopupDirection _popupDirection: IA.PopupDirection {
        source: control.T.Overlay.overlay
        contentItem: control._popupDirectionReady ? control.contentItem : null
    }
    property alias backgroundColor: appearance.backgroundColor
    property alias foregroundColor: appearance.foregroundColor
    property alias borderColor: appearance.borderColor
    property alias disabledBorderColor: appearance.disabledBorderColor
    property alias controlRadius: appearance.controlRadius
    property alias fontFamily: appearance.fontFamily
    property alias fontPixelSize: appearance.fontPixelSize
    property alias fontWeight: appearance.fontWeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    readonly property IA.ControlAppearance _appearance: IA.ControlAppearance { id: appearance; source: IA.Appearance; family: "tooltip" }
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    horizontalPadding: appearance.horizontalPadding
    verticalPadding: appearance.verticalPadding
    margins: IA.Appearance.metrics.spacingS
    y: -implicitHeight - IA.Appearance.metrics.spacingXS
    font: appearance.resolvedFont
    delay: IA.Appearance.motion.tooltipDelay
    timeout: IA.Appearance.motion.tooltipTimeout
    focus: false
    popupType: T.Popup.Item
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent
    contentItem: Text {
        objectName: "iaLabel"
        text: control.text
        font: control.font
        color: control.foregroundColor
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
    }
    background: IA.ControlBackground {
        resolvedAppearance: appearance
        outlined: false
    }
}
