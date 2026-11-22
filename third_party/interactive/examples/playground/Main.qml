// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Templates as T
import InterActive
import InterActive.Foundation as IA

Window {
    id: window
    objectName: "playgroundWindow"
    width: 1200
    height: 820
    minimumWidth: 320
    minimumHeight: 320
    visible: true
    title: "InterActive / Foundations"
    color: IA.Appearance.colors.canvas
    LayoutMirroring.enabled: window.rightToLeft
    LayoutMirroring.childrenInherit: true

    property string initialScene: "default"
    property bool windowPopups: false
    property bool rightToLeft: false
    property bool startupDispatched: false
    readonly property Item activePopupContent: gallery.activePopupContent
    readonly property list<Item> openPopupContents: gallery.openPopupContents
    readonly property bool sceneReady: startupDispatched
        && (initialScene === "menu" ? gallery.menuOpen
            : initialScene === "dialog" ? gallery.dialogOpen : true)
    signal observationChanged()
    onActivePopupContentChanged: observationChanged()
    onOpenPopupContentsChanged: observationChanged()
    onSceneReadyChanged: observationChanged()

    function openMenu(): void { gallery.openMenu(); }
    function openDialog(): void { gallery.openDialog(); }

    // The launcher calls this only after the native parent is active and exposed.
    function startScene(): void {
        if (!startupDispatched && visible && active) {
            startupDispatched = true;
            if (initialScene === "menu")
                openMenu();
            else if (initialScene === "dialog")
                openDialog();
        }
    }

    Item {
        anchors.fill: parent
        RowLayout {
            anchors.fill: parent
            spacing: 0
            IAPane {
                objectName: "navigationRail"
                visible: window.width >= 1000
                Layout.preferredWidth: 176
                Layout.fillHeight: true
                backgroundColor: IA.Appearance.colors.control
                controlRadius: 0
                contentItem: ColumnLayout {
                    spacing: IA.Appearance.metrics.spacingS
                    IALabel {
                        objectName: "navigationHeading"
                        Layout.fillWidth: true
                        leftPadding: IA.Appearance.metrics.horizontalPadding
                        rightPadding: IA.Appearance.metrics.horizontalPadding
                        text: "InterActive"
                        fontPixelSize: IA.Appearance.typography.headingSize
                        fontWeight: IA.Appearance.typography.emphasisWeight
                    }
                    IALabel {
                        objectName: "navigationSubtitle"
                        Layout.fillWidth: true
                        leftPadding: IA.Appearance.metrics.horizontalPadding
                        rightPadding: IA.Appearance.metrics.horizontalPadding
                        text: "Control workbench"
                        foregroundColor: IA.Appearance.colors.secondaryText
                        fontPixelSize: IA.Appearance.typography.supportingSize
                    }
                    IAItemDelegate {
                        objectName: "navigationCurrentPage"
                        Layout.fillWidth: true
                        text: "Foundations"
                        highlighted: true
                        fontWeight: IA.Appearance.typography.emphasisWeight
                    }
                    Item { Layout.fillHeight: true }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                AppearanceToolbar {
                    Layout.fillWidth: true
                    onAccentRequested: anchor => appearanceEditor.openAccentMenu(anchor)
                    onAppearanceRequested: appearanceWindow.open()
                }
                FoundationsPage {
                    id: gallery
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    windowPopups: window.windowPopups
                }
            }
        }
    }

    IADialog {
        id: appearanceWindow
        objectName: "appearanceWindow"
        title: "Appearance"
        parent: T.Overlay.overlay
        popupType: T.Popup.Window
        modal: false
        closePolicy: T.Popup.CloseOnEscape
        width: Math.min(980, window.width - 2 * margins)
        x: (window.width - width) / 2
        y: (window.height - height) / 2
        standardButtons: T.Dialog.Close
        AppearanceEditor {
            id: appearanceEditor
            objectName: "appearanceEditor"
            width: parent.width
            pickerParentWindow: window
        }
    }
}
