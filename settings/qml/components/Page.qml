import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
C.ScrollView {
    id: root
    property string title
    property Component headingAction
    default property alias contents: column.data
    clip: true
    contentWidth: availableWidth
    C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
    ColumnLayout {
        id: column
        width: root.availableWidth
        spacing: 22
        RowLayout {
            objectName: "pageHeading"
            Layout.fillWidth: true
            Layout.minimumHeight: 40
            Label { objectName: "pageTitle"; Layout.fillWidth: true; text: root.title; font.pixelSize: 22; font.bold: true; verticalAlignment: Text.AlignVCenter }
            Loader { sourceComponent: root.headingAction }
        }
    }
}
