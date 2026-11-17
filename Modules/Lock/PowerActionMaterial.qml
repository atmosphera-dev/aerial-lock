import QtQuick
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

// Material presentation only. Data and callbacks are supplied by its consumer.
Rectangle {
    id: root
    required property real unit
    required property bool compact
    required property string fontFamily
    required property string message
    required property var controls
    required property string selectedIcon
    required property string actionLabel
    required property bool confirming
    required property bool waiting
    required property bool failed
    required property color inkDark
    required property color inkMid
    required property color accentSurface
    required property color cardSurface
    required property color fieldSurface
    required property color errorText
    signal activate(string action)
    signal navigate(int direction)
    function focusControl(index) {
        var button = buttons.itemAt(index) as PowerActionButton
        if (button) button.focusControl()
    }
    readonly property real padding: (compact ? 8 : 16) * unit
    implicitHeight: contents.implicitHeight + padding * 2
    radius: 38 * unit
    color: cardSurface
    ColumnLayout {
        id: contents
        anchors.fill: parent
        anchors.margins: root.padding
        anchors.topMargin: (root.compact ? 8 : 14) * root.unit
        spacing: (root.compact ? 2 : 12) * root.unit
        Item { Layout.fillHeight: true; visible: root.waiting }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12 * root.unit
            Rectangle {
                Layout.preferredWidth: (root.compact ? 32 : 48) * root.unit
                Layout.preferredHeight: width
                radius: width / 2
                color: root.failed ? "#F3D3DD" : root.inkDark
                Image {
                    id: icon
                    source: root.selectedIcon
                    visible: false
                    anchors.centerIn: parent
                    width: 20 * root.unit; height: width
                    sourceSize.width: width * 2; sourceSize.height: height * 2
                }
                ColorOverlay { anchors.fill: icon; source: icon; color: root.failed ? root.errorText : root.accentSurface }
                Canvas {
                    anchors.fill: parent
                    visible: root.waiting
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        ctx.lineWidth = 3 * root.unit
                        ctx.strokeStyle = root.accentSurface
                        ctx.beginPath()
                        ctx.arc(width / 2, height / 2, width / 2 - ctx.lineWidth, -Math.PI / 2, 0)
                        ctx.stroke()
                    }
                    RotationAnimator on rotation { running: root.visible && root.waiting; from: 0; to: 360; duration: 1200; loops: Animation.Infinite }
                }
            }
            Column {
                Layout.fillWidth: true
                spacing: 2 * root.unit
                Text {
                    objectName: "powerMessage"
                    width: parent.width
                    text: root.failed ? root.message : root.message.toUpperCase()
                    textFormat: Text.PlainText
                    wrapMode: root.failed ? Text.Wrap : Text.NoWrap
                    elide: root.failed ? Text.ElideNone : Text.ElideRight
                    font.family: root.fontFamily
                    font.pixelSize: (root.failed ? 11 : 12) * root.unit
                    font.letterSpacing: root.failed ? 1.5 * root.unit : 0
                    color: root.failed ? root.errorText : root.inkDark
                }
                Text {
                    visible: root.confirming
                    text: root.actionLabel.toUpperCase()
                    textFormat: Text.PlainText
                    font.family: root.fontFamily
                    font.pixelSize: 9 * root.unit
                    color: root.inkMid
                }
            }
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12 * root.unit
            visible: root.controls.length > 0
            Item { Layout.fillWidth: true; visible: root.failed }
            Repeater {
                id: buttons
                model: root.controls
                PowerActionButton {
                    id: pill
                    required property var modelData
                    label: modelData.label.toUpperCase()
                    objectName: modelData.name
                    Layout.fillWidth: modelData.action === "confirm"
                    Layout.preferredWidth: labelText.implicitWidth + 32 * root.unit
                    Layout.preferredHeight: (root.compact ? 26 : 38) * root.unit
                    trapNavigation: root.confirming
                    onActivated: root.activate(modelData.action)
                    onNavigate: function(direction) { root.navigate(direction) }
                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: pill.modelData.action === "confirm" ? root.inkDark : root.fieldSurface
                    }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4 * root.unit
                        radius: height / 2
                        color: "transparent"
                        border.width: 2 * root.unit
                        border.color: root.inkDark
                        visible: pill.activeFocus
                    }
                    Text {
                        id: labelText
                        anchors.centerIn: parent
                        text: pill.label
                        textFormat: Text.PlainText
                        font.family: root.fontFamily
                        font.pixelSize: 10 * root.unit
                        font.letterSpacing: 1 * root.unit
                        color: pill.modelData.action === "confirm" ? root.accentSurface : root.inkDark
                    }
                }
            }
        }
    }
}
