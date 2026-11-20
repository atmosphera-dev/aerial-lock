import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
C.Button {
    id: root
    property bool primary: false
    property string glyph
    // An external link: accent text with a trailing external-link glyph.
    property bool link: false
    readonly property color foreground: root.link ? Style.accent : root.primary ? Style.accentText : Style.text
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    implicitHeight: 34
    leftPadding: root.link ? 4 : 14
    rightPadding: root.link ? 4 : 14
    contentItem: RowLayout {
        spacing: root.link ? 4 : 8
        Glyph { visible: root.glyph.length > 0 && !root.link; name: root.glyph; color: root.foreground }
        Label {
            Layout.fillWidth: true
            text: root.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.weight: Font.Medium
            color: root.foreground
        }
        Glyph { visible: root.link; name: "external"; color: root.foreground; implicitWidth: 14; implicitHeight: 14 }
    }
    background: Rectangle {
        color: root.primary ? Style.accent : "transparent"
        border.color: root.visualFocus ? Style.selection : root.link ? "transparent" : root.primary ? Style.accent : Style.line
        border.width: root.visualFocus ? 2 : 1
        radius: 8
    }
}
