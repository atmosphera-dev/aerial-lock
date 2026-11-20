import QtQuick
import QtQuick.Layouts
import AerialLock.Settings
import "components" as UI
UI.Sheet {
    id: root
    required property CacheController cache
    readonly property var cleanup: cache.cleanup
    title: cleanup.state === "result" ? "Clean up result" : "Clean up cache"
    visible: cleanup.visible
    onClosed: cache.dismissCleanup()
    UI.Label { Layout.fillWidth: true; text: root.cleanup.state === "pending" ? "Waiting for the service. You can close this sheet; accepted work continues in Recent activity." : root.cleanup.state === "error" ? root.cleanup.error : root.cleanup.message; wrapMode: Text.WordWrap }
    UI.Label { Layout.fillWidth: true; visible: root.cleanup.state === "confirm"; text: "Storage may change before the confirmed run. Aerial will report what it actually removes."; wrapMode: Text.WordWrap; font.pixelSize: 13 }
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        UI.Button { text: root.cleanup.state === "confirm" ? "Cancel" : "Close"; onClicked: root.close() }
        UI.Button { visible: root.cleanup.state === "confirm"; text: "Clean up"; primary: true; onClicked: root.cache.confirmCleanup() }
    }
}
