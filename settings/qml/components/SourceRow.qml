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
    required property var toggle
    required property var edit
    required property var remove
    required property var retry
    required property var beginDrag
    implicitHeight: Math.max(64, line.implicitHeight + 20)
    RowLayout {
        id: line
        anchors.fill: parent
        anchors.margins: 10
        spacing: 12
        IconButton { text: "Reorder " + root.title; glyph: "grip"; onPressed: root.beginDrag(root.sourceId) }
        Glyph { name: root.glyph; implicitWidth: 38; implicitHeight: 38 }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label { Layout.fillWidth: true; text: root.title; font.weight: Font.Medium }
            Label { Layout.fillWidth: true; text: root.description; font.pixelSize: 13; wrapMode: Text.WordWrap }
        }
        Button { visible: root.canRetry; text: "Retry"; onClicked: root.retry(root.sourceId) }
        Toggle { text: root.title; checked: root.on; onToggled: root.toggle(root.sourceId, checked) }
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
