import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
C.ScrollView {
    id: root
    property string title
    property Component headingAction
    property real headingSpacing: 22
    default property alias contents: column.data
    clip: true
    contentWidth: availableWidth
    C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
    ColumnLayout {
        width: root.availableWidth
        spacing: root.headingSpacing
        RowLayout {
            objectName: "pageHeading"
            Layout.fillWidth: true
            Layout.minimumHeight: 40
            Label { objectName: "pageTitle"; Layout.fillWidth: true; text: root.title; font.pixelSize: 22; font.bold: true; verticalAlignment: Text.AlignVCenter }
            Loader { sourceComponent: root.headingAction }
        }
        ColumnLayout { id: column; Layout.fillWidth: true; spacing: 22 }
    }
}
