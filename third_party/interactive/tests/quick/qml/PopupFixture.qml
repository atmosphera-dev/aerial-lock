// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import InterActive
import InterActive.Foundation as IA

Item {
    id: fixture
    width: 800
    height: 640
    property int popupMode: T.Popup.Item
    property string menuResult: "No action selected"
    property string committedName: "Maya Chen"
    property string backgroundResult: "Background unchanged"
    property alias menuOpener: menuOpener
    property alias dialogOpener: dialogOpener
    property alias backgroundButton: backgroundButton
    property alias menu: menu
    property alias submenu: submenu
    property alias firstAction: firstAction
    property alias disabledAction: disabledAction
    property alias nestedAction: nestedAction
    property alias dialog: dialog
    property alias dialogEditor: dialogEditor
    property alias popup: popup
    property alias popupEditor: popupEditor
    property alias popupOpener: popupOpener
    property alias menuSummary: menuSummary
    property alias committedSummary: committedSummary
    property alias backgroundSummary: backgroundSummary
    property alias tooltip: tooltip

    ColumnLayout {
        x: 24
        y: 24
        spacing: IA.Appearance.metrics.spacingS
        IAButton { id: menuOpener; text: "Actions"; onClicked: menu.open() }
        IAButton {
            id: dialogOpener
            text: "Edit preferences"
            onClicked: {
                dialogEditor.text = fixture.committedName;
                dialog.open();
            }
        }
        IAButton { id: popupOpener; text: "Filter"; onClicked: popup.open() }
        IAButton {
            id: backgroundButton
            text: "Background action"
            onClicked: fixture.backgroundResult = "Background activated"
        }
        IALabel { id: menuSummary; text: fixture.menuResult }
        IALabel { id: committedSummary; text: "Saved: " + fixture.committedName }
        IALabel { id: backgroundSummary; text: fixture.backgroundResult }
    }

    IAMenu {
        id: menu
        parent: fixture
        x: menuOpener.mapToItem(fixture, 0, menuOpener.height).x
        y: menuOpener.mapToItem(fixture, 0, menuOpener.height).y
        popupType: fixture.popupMode
        focus: true
        IAMenuItem {
            id: firstAction
            text: "Use compact preview"
            shortcutText: "Ctrl+K"
            onTriggered: fixture.menuResult = "Compact preview selected"
        }
        IAMenuItem {
            id: disabledAction
            text: "Unavailable action"
            enabled: false
            onTriggered: fixture.menuResult = "Disabled action activated"
        }
        IAMenu {
            id: submenu
            title: "Export"
            popupType: fixture.popupMode
            focus: true
            IAMenuItem {
                id: nestedAction
                text: "Export summary"
                onTriggered: fixture.menuResult = "Summary exported"
            }
            IAMenuItem { text: "Export selection"; onTriggered: fixture.menuResult = "Selection exported" }
        }
        IAMenuSeparator {}
        IAMenuItem {
            text: "Show details"
            checkable: true
            onTriggered: fixture.menuResult = checked ? "Details shown" : "Details hidden"
        }
    }

    IADialog {
        id: dialog
        parent: fixture
        x: (fixture.width - width) / 2
        y: (fixture.height - height) / 2
        width: 380
        title: "Edit preferences"
        popupType: fixture.popupMode
        modal: true
        focus: true
        standardButtons: T.Dialog.Apply | T.Dialog.Cancel
        closePolicy: T.Popup.CloseOnEscape
        onApplied: fixture.committedName = dialogEditor.text
        onOpened: dialogEditor.forceActiveFocus(Qt.TabFocusReason)
        ColumnLayout {
            width: parent.width
            IALabel { text: "Display name" }
            IATextField {
                id: dialogEditor
                Layout.fillWidth: true
                Accessible.name: "Dialog display name"
            }
        }
    }

    IAPopup {
        id: popup
        parent: fixture
        x: (fixture.width - width) / 2
        y: (fixture.height - height) / 2
        width: 320
        popupType: fixture.popupMode
        focus: true
        onOpened: popupEditor.forceActiveFocus(Qt.TabFocusReason)
        contentItem: IATextField { id: popupEditor; text: "Initial filter" }
    }

    IAToolTip {
        id: tooltip
        parent: menuOpener
        popupType: fixture.popupMode
        text: "Available preview actions"
        delay: 0
        timeout: -1
    }
}
