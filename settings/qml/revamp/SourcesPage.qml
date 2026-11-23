import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Foundation as IA

PageScaffold {
    id: root
    required property var sourceModel
    property bool busy: false
    property string error: ""
    signal addRequested()
    signal toggled(string id, bool on)
    signal editRequested(string id)
    signal removeRequested(string id)
    signal checkRequested(string id)
    signal orderStarted(string id)
    signal moveRequested(string id, int delta)
    signal droppedBefore(string id)
    title: "Sources"
    actions: IAButton {
        objectName: "addSource"
        text: "Add source"
        icon.source: Qt.resolvedUrl("icons/plus.svg")
        enabled: !root.busy
        onClicked: root.addRequested()
    }
    SettingsSection {
        Layout.fillWidth: true
        title: "Play order"
        description: "Videos play from the top source down. Drag a source to move it."
        Repeater {
            id: rows
            model: root.sourceModel
            SourceRow {
                required property var sourceRow
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                source: sourceRow
                busy: root.busy
                onToggled: on => root.toggled(sourceRow.id, on)
                onEditRequested: root.editRequested(sourceRow.id)
                onRemoveRequested: root.removeRequested(sourceRow.id)
                onCheckRequested: root.checkRequested(sourceRow.id)
                onOrderStarted: root.orderStarted(sourceRow.id)
                onMoveRequested: delta => root.moveRequested(sourceRow.id, delta)
                onDroppedBefore: root.droppedBefore(sourceRow.id)
            }
        }
        SettingRow {
            visible: rows.count === 0
            label: "No video sources"
            description: "Add an online pack, folder or video file."
        }
    }
    DropArea {
        Layout.fillWidth: true
        implicitHeight: IA.Appearance.metrics.spacingS
        keys: ["aerial-source"]
        onDropped: root.droppedBefore("")
    }
    IALabel {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        visible: text.length > 0
        text: root.error
        textFormat: Text.PlainText
        foregroundColor: IA.Appearance.colors.errorText
        wrapMode: Text.Wrap
    }
}
