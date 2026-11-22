// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.Popup {
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
    property alias focusColor: appearance.focusColor
    property alias controlRadius: appearance.controlRadius
    property alias fontFamily: appearance.fontFamily
    property alias fontPixelSize: appearance.fontPixelSize
    property alias fontWeight: appearance.fontWeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    readonly property IA.ControlAppearance _appearance: IA.ControlAppearance { id: appearance; source: IA.Appearance; family: "popup" }
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    horizontalPadding: appearance.horizontalPadding
    verticalPadding: appearance.verticalPadding
    spacing: appearance.spacing
    margins: IA.Appearance.metrics.spacingS
    font: appearance.resolvedFont
    popupType: T.Popup.Item
    contentItem: Item { }
    background: IA.ControlBackground { resolvedAppearance: appearance }
    T.Overlay.modal: Rectangle { color: IA.Appearance.colors.scrim }
    T.Overlay.modeless: Rectangle { color: IA.Appearance.colors.scrim }
}
