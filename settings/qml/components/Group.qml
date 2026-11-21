import QtQuick
import QtQuick.Layouts
Rectangle {
    id: root
    default property alias contents: column.data
    color: Style.card
    border.color: Style.line
    radius: 12
    implicitHeight: column.implicitHeight
    ColumnLayout { id: column; width: root.width; spacing: 0 }
}
