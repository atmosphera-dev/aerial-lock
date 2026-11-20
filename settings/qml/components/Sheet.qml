import QtQuick
import QtQuick.Shapes
import QtQuick.Controls as C
import QtQuick.Layouts
C.Popup {
    id: root
    property string title
    property bool closable: true
    default property alias contents: body.data
    modal: true
    focus: true
    C.Overlay.modal: Rectangle { color: "#88000000" }
    anchors.centerIn: C.Overlay.overlay
    width: Math.min(500, parent.width - 32)
    implicitWidth: contentItem.implicitWidth
    implicitHeight: contentItem.implicitHeight
    padding: 0
    closePolicy: closable ? C.Popup.CloseOnEscape : C.Popup.NoAutoClose
    background: Shape {
        id: surface
        implicitWidth: 0
        implicitHeight: 0
        readonly property real edge: 0.5
        readonly property real corner: 16
        readonly property real rightEdge: width - edge
        readonly property real bottomEdge: height - edge
        readonly property string topOutline: "M " + edge + " " + heading.height + " V " + corner + " Q " + edge + " " + edge + " " + corner + " " + edge + " H " + (width - corner) + " Q " + rightEdge + " " + edge + " " + rightEdge + " " + corner + " V " + heading.height
        readonly property string lowerOutline: " H " + rightEdge + " V " + (height - corner) + " Q " + rightEdge + " " + bottomEdge + " " + (width - corner) + " " + bottomEdge + " H " + corner + " Q " + edge + " " + bottomEdge + " " + edge + " " + (height - corner) + " V " + heading.height
        ShapePath {
            fillColor: Style.card
            strokeColor: "transparent"
            PathSvg { path: surface.topOutline + " Z" }
        }
        ShapePath {
            fillColor: Style.window
            strokeColor: "transparent"
            PathSvg { path: "M " + surface.edge + " " + heading.height + surface.lowerOutline + " Z" }
        }
        ShapePath {
            fillColor: "transparent"
            strokeColor: Style.line
            strokeWidth: 1
            PathSvg { path: surface.topOutline + surface.lowerOutline + " Z M " + surface.edge + " " + heading.height + " H " + surface.rightEdge }
        }
    }
    contentItem: ColumnLayout {
        spacing: 0
        Item {
            id: heading
            Layout.fillWidth: true
            implicitHeight: 54
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 10
                Label { Layout.fillWidth: true; text: root.title; font.pixelSize: 17; font.bold: true }
                IconButton { visible: root.closable; glyph: "x"; text: "Close"; onClicked: root.close() }
            }
        }
        ColumnLayout { id: body; Layout.fillWidth: true; Layout.margins: 20; spacing: 14 }
    }
}
