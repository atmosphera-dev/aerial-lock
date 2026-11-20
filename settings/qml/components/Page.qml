import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
C.ScrollView {
    id: root
    property string title
    default property alias contents: column.data
    clip: true
    contentWidth: availableWidth
    C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
    ColumnLayout {
        id: column
        width: root.availableWidth
        spacing: 22
        Label { Layout.fillWidth: true; text: root.title; font.pixelSize: 22; font.bold: true; Layout.minimumHeight: 40; verticalAlignment: Text.AlignVCenter }
    }
}
