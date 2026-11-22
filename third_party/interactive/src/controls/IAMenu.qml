// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.Menu {
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
    readonly property IA.ControlAppearance _appearance: IA.ControlAppearance { id: appearance; source: IA.Appearance; family: "popup" }
    implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding, IA.Appearance.metrics.menuMinWidth)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    horizontalPadding: appearance.horizontalPadding
    verticalPadding: appearance.verticalPadding
    margins: IA.Appearance.metrics.spacingS
    font: appearance.resolvedFont
    popupType: T.Popup.Item
    delegate: IAMenuItem {
        fontFamily: control.fontFamily
        fontPixelSize: control.fontPixelSize
        fontWeight: control.fontWeight
    }
    contentItem: ListView {
        implicitWidth: IA.Appearance.metrics.menuMinWidth
        implicitHeight: contentHeight
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: control.spacing
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        highlightMoveDuration: 0
        T.ScrollBar.vertical: IAScrollBar { }
    }
    background: IA.ControlBackground { resolvedAppearance: appearance }
    T.Overlay.modal: Rectangle { color: IA.Appearance.colors.scrim }
    T.Overlay.modeless: Rectangle { color: IA.Appearance.colors.scrim }
}
