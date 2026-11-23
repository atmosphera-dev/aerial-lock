import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import AerialLock.Settings
import InterActive
import InterActive.Foundation as IA

SettingsDialog {
    id: root
    required property CacheController cache
    readonly property var cleanup: cache.cleanup
    readonly property bool confirming: cleanup.state === "confirm"
    title: cleanup.state === "result" ? "Clean up result" : "Clean up cache"
    standardButtons: T.Dialog.Apply | T.Dialog.Cancel
    onApplied: cache.confirmCleanup()
    onClosed: cache.dismissCleanup()
    function sync() {
        if (cleanup.visible && !visible) open();
        else if (!cleanup.visible && visible) close();
    }
    Component.onCompleted: sync()
    onCleanupChanged: sync()
    onOpened: {
        const apply = standardButton(T.Dialog.Apply);
        apply.text = "Clean up";
        apply.visible = Qt.binding(() => root.confirming);
        standardButton(T.Dialog.Cancel).text = Qt.binding(() => root.confirming ? "Cancel" : "Close");
    }
    ColumnLayout {
        width: parent.width
        spacing: IA.Appearance.metrics.spacingS
        IALabel {
            Layout.fillWidth: true
            text: root.cleanup.state === "pending" ? "Waiting for the service. You can close this dialog; accepted work continues in Recent activity."
                : root.cleanup.state === "error" ? root.cleanup.error : root.cleanup.message
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        IALabel {
            Layout.fillWidth: true
            visible: root.confirming
            text: "Storage may change before the confirmed run. Aerial will report what it actually removes."
            foregroundColor: IA.Appearance.colors.secondaryText
            fontPixelSize: IA.Appearance.typography.supportingSize
            wrapMode: Text.Wrap
        }
    }
}
