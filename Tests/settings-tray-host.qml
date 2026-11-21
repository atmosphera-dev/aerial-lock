//@ pragma UseQApplication
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Services.SystemTray

// Independent real Quickshell SNI/DBusMenu consumer. No production app types,
// fake desktop services or product backdoor. Native platform menu rendering.
ShellRoot {
    id: root
    // Untyped views keep this fixture independent of Quickshell's metadata version.
    property var trayItems: SystemTray.items
    property var entries: opener.children
    property var panelScreen: panel.screen
    property var panelContent: panel.contentItem
    property var anchorRef: menuAnchor
    property var item: trayItems.values.find(value => value.id === "org.aeriallock.AerialSettings") || null
    property bool showTooltip: false
    SystemPalette { id: colors }
    QsMenuOpener { id: opener; menu: root.item ? root.item.menu : null }
    PanelWindow {
        id: panel
        screen: Quickshell.screens[0]
        anchors { top: true; left: true; right: true }
        implicitHeight: 38
        color: colors.window
        RowLayout {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            Image {
                id: icon
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                source: root.item ? root.item.icon : ""
                sourceSize.width: 24
                sourceSize.height: 24
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: event => { if (event.button === Qt.LeftButton) root.item.activate(); else root.anchorRef.open(); }
                }
            }
            Text { text: "Aerial tray qualification"; color: colors.windowText; font.pixelSize: 14 }
        }
    }
    QsMenuAnchor { id: menuAnchor; menu: root.item ? root.item.menu : null; anchor.item: icon }
    // A second layer surface below the bar's exclusive zone, at its right edge.
    PanelWindow {
        visible: root.showTooltip && root.item !== null
        screen: Quickshell.screens[0]
        anchors { top: true; right: true }
        implicitWidth: 280
        implicitHeight: 100
        color: colors.window
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            Text { text: root.item ? root.item.tooltipTitle : ""; font.bold: true; color: colors.windowText; font.pixelSize: 14 }
            Text { text: root.item ? root.item.tooltipDescription : ""; color: colors.windowText; font.pixelSize: 13 }
        }
    }
    IpcHandler {
        target: "proof"
        function info(): string {
            return JSON.stringify({ count: root.trayItems.values.length, title: root.item ? root.item.title : "", status: root.item ? root.item.status : -1,
                tooltipTitle: root.item ? root.item.tooltipTitle : "", tooltip: root.item ? root.item.tooltipDescription : "", iconReady: icon.status === Image.Ready,
                menuVisible: root.anchorRef.visible, screen: root.panelScreen.name, panelWidth: panel.width,
                iconX: icon.mapToItem(root.panelContent, icon.width / 2, icon.height / 2).x, iconY: icon.mapToItem(root.panelContent, icon.width / 2, icon.height / 2).y,
                entries: root.entries.values.map(entry => ({ text: entry.text, enabled: entry.enabled, separator: entry.isSeparator })) });
        }
        function trigger(index: int): void { root.entries.values[index].triggered(); }
        function activate(): void { root.item.activate(); }
        function showMenu(): void { root.showTooltip = false; root.anchorRef.open(); }
        function tooltip(): void { if (root.anchorRef.visible) root.anchorRef.close(); root.showTooltip = true; }
        function dismiss(): void { if (root.anchorRef.visible) root.anchorRef.close(); root.showTooltip = false; }
        function quit(): void { if (root.anchorRef.visible) root.anchorRef.close(); Qt.quit(); }
    }
    // Natural task-only shutdown even if a test assertion or IPC fails. Never
    // kill/restart the user's shell (there is no host shell in this guest).
    Timer { interval: 90000; running: true; onTriggered: Qt.quit() }
}
