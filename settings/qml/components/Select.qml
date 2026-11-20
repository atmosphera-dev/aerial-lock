import QtQuick
import QtQuick.Controls as C
import QtQuick.Layouts
C.ComboBox {
    id: root
    property bool inherited: false
    implicitWidth: Math.max(156, valueLabel.implicitWidth + 44)
    implicitHeight: 34
    padding: 1
    textRole: "label"
    valueRole: "token"
    indicator: Item { visible: false }
    contentItem: RowLayout {
        spacing: 0
        Label {
            id: valueLabel
            Layout.fillWidth: true
            Layout.leftMargin: 11
            text: root.displayText
            color: root.inherited ? Style.muted : Style.text
        }
        Item {
            objectName: "chevronSlot"
            Layout.preferredWidth: root.availableHeight
            Layout.fillHeight: true
            Glyph { objectName: "chevron"; anchors.centerIn: parent; width: 16; height: 16; name: "chevron" }
        }
    }
    background: Rectangle {
        color: Style.window
        radius: 8
        border.color: root.visualFocus ? Style.selection : Style.line
        border.width: root.visualFocus ? 2 : 1
    }
    delegate: C.ItemDelegate {
        id: option
        required property var modelData
        width: root.width
        text: modelData.label
        contentItem: Label { text: option.text }
        highlighted: root.highlightedIndex === index
        required property int index
    }
    popup: C.Popup {
        y: root.height
        width: root.width
        padding: 4
        implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
        }
        background: Rectangle { color: Style.card; border.color: Style.line; radius: 8 }
    }
}
