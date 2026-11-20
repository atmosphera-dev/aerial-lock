import QtQuick
import QtQuick.Shapes
Item {
    id: root
    implicitWidth: 30
    implicitHeight: 30
    Item {
        width: 60
        height: 60
        scale: root.width / 60
        transformOrigin: Item.TopLeft
        Rectangle {
            anchors.fill: parent
            radius: 15
            gradient: Gradient {
                GradientStop { position: 0; color: "#2b3f74" }
                GradientStop { position: 0.58; color: "#9a7aa6" }
                GradientStop { position: 1; color: "#f2b27f" }
            }
        }
        Rectangle { x: 34; y: 16; width: 12; height: 12; radius: 6; color: "#fff4cf" }
        Shape {
            anchors.fill: parent
            ShapePath {
                fillColor: "#e61d2550"
                strokeColor: "transparent"
                PathSvg { path: "M0 46l14-13 9 8 9-10 12 10 16-9v13a15 15 0 0 1-15 15h-30a15 15 0 0 1-15-14z" }
            }
            ShapePath {
                fillColor: "#0e1430"
                strokeColor: "transparent"
                PathSvg { path: "M2 52l16-9 12 5 14-6 16 6a15 15 0 0 1-15 12h-30a15 15 0 0 1-13-8z" }
            }
        }
    }
}
