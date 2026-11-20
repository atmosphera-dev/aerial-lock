import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
Item {
    id: root
    required property string sourceId
    required property string title
    property string description
    property string glyph: "stack"
    property bool on: true
    property bool canRetry: false
    property bool separator: false
    required property var toggle
    required property var edit
    required property var remove
    required property var retry
    required property var beginDrag
    property var dropBefore: () => {}
    objectName: "source-" + sourceId
    implicitHeight: Math.max(64, line.implicitHeight + 20)
    Rectangle { width: parent.width; height: 1; color: Style.line; visible: root.separator }
    DropArea { anchors.fill: parent; keys: ["aerial-source"]; onDropped: root.dropBefore(root.sourceId) }
    RowLayout {
        id: line
        anchors.fill: parent
        anchors.margins: 10
        spacing: 12
        Item {
            objectName: "sourceGrip"
            Layout.preferredWidth: 24
            Layout.preferredHeight: 38
            Glyph { anchors.centerIn: parent; name: "grip"; color: Style.muted }
            DragHandler {
                id: drag
                target: dragProxy
                onActiveChanged: {
                    if (active) { root.beginDrag(root.sourceId); dragProxy.Drag.active = true; }
                    else { dragProxy.Drag.drop(); dragProxy.Drag.active = false; dragProxy.x = 0; dragProxy.y = 0; }
                }
            }
            Item {
                id: dragProxy
                width: parent.width
                height: parent.height
                Drag.keys: ["aerial-source"]
                Drag.hotSpot.x: width / 2
                Drag.hotSpot.y: height / 2
                Glyph { anchors.centerIn: parent; visible: drag.active; name: "grip"; color: Style.accent }
            }
        }
        Rectangle {
            implicitWidth: 38
            implicitHeight: 38
            color: Style.window
            radius: 10
            Glyph { anchors.centerIn: parent; name: root.glyph }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label { Layout.fillWidth: true; text: root.title; font.weight: Font.Medium }
            Label { Layout.fillWidth: true; text: root.description; font.pixelSize: 13; wrapMode: Text.WordWrap }
        }
        Button { visible: root.canRetry; text: "Retry"; onClicked: root.retry(root.sourceId) }
        Toggle { objectName: "sourceToggle"; text: root.title; observed: root.on; onRequested: value => root.toggle(root.sourceId, value) }
        IconButton {
            text: "Actions for " + root.title
            glyph: "dots"
            onClicked: menu.open()
            C.Menu {
                id: menu
                C.MenuItem { text: "Edit"; onTriggered: root.edit(root.sourceId) }
                C.MenuItem { text: "Remove"; onTriggered: root.remove(root.sourceId) }
            }
        }
    }
}
