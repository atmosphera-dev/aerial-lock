import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
import AerialLock.Settings
import "components" as UI
C.ApplicationWindow {
    id: root
    required property SettingsController controller
    property int page: 0
    readonly property string intervalText: downloads.intervalText
    width: 960
    height: 900
    minimumWidth: 720
    minimumHeight: 540
    title: "Aerial"
    visible: true
    color: UI.Style.window
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Item {
            Layout.preferredWidth: 224
            Layout.fillHeight: true
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: UI.Style.line }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                anchors.topMargin: 16
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.topMargin: 4
                    Layout.bottomMargin: 18
                    spacing: 10
                    UI.AppIcon {}
                    UI.Label { text: "Aerial"; font.pixelSize: 17; font.bold: true }
                    Item { Layout.fillWidth: true }
                }
                Repeater {
                    model: root.controller.panel === "settings" ? [{ title: "Downloads", icon: "download" }, { title: "Sources", icon: "stack" }, { title: "Cache & status", icon: "database" }, { title: "Lock screen", icon: "lock" }, { title: "About", icon: "info" }] : []
                    C.AbstractButton {
                        id: navigation
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 38
                        padding: 12
                        text: modelData.title
                        background: Rectangle {
                            radius: 10
                            color: root.page === navigation.index ? UI.Style.selection : "transparent"
                            border.width: navigation.visualFocus ? 2 : 0
                            border.color: UI.Style.accent
                        }
                        contentItem: RowLayout {
                            spacing: 12
                            UI.Glyph { name: navigation.modelData.icon; color: root.page === navigation.index ? UI.Style.selectionText : UI.Style.text }
                            UI.Label { Layout.fillWidth: true; text: navigation.text; font.weight: Font.Medium; color: root.page === navigation.index ? UI.Style.selectionText : UI.Style.text }
                        }
                        onClicked: root.page = index
                    }
                }
                Item { Layout.fillHeight: true; Layout.minimumHeight: 24 }
                UI.StatusSummary { Layout.fillWidth: true; text: root.controller.status; onClicked: root.page = 2 }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 32
                anchors.rightMargin: 32
                anchors.topMargin: 22
                anchors.bottomMargin: 32
                Downloads { id: downloads; Layout.fillWidth: true; Layout.fillHeight: true; visible: root.controller.panel === "settings" && root.page === 0; controller: root.controller }
                UI.Page {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.controller.panel === "settings" && root.page !== 0
                    title: ["Downloads", "Sources", "Cache & status", "Lock screen", "About"][root.page]
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.controller.panel !== "settings"
                    spacing: 10
                    Item { Layout.fillHeight: true }
                    UI.Glyph { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 28; Layout.preferredHeight: 28; name: "alert" }
                    UI.Label { Layout.fillWidth: true; text: root.controller.panel === "diagnostic" ? "Settings need attention" : "The aerial service isn't running"; font.pixelSize: 19; font.bold: true; horizontalAlignment: Text.AlignHCenter }
                    UI.Label { Layout.fillWidth: true; text: root.controller.panel === "diagnostic" ? "Fix the settings file, then try again. The file is left unchanged." : "Settings and downloads need it. Starting it doesn't change any settings."; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
                    UI.Label { Layout.fillWidth: true; text: root.controller.diagnostic; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
                    UI.Label { Layout.fillWidth: true; visible: root.controller.panel === "unavailable"; text: "If it is masked, run systemctl --user unmask aerial, then try again."; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 13 }
                    UI.Button { Layout.alignment: Qt.AlignHCenter; primary: true; text: root.controller.panel === "diagnostic" ? "Try again" : "Start service"; onClicked: root.controller.retry() }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
