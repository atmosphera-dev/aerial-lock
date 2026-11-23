import QtQuick
import InterActive

IAComboBox {
    id: root
    required property var descriptor
    signal requested(var token)
    popupMargins: Insets.edge
    textRole: "label"
    valueRole: "token"
    model: descriptor.choices || []
    currentIndex: descriptor.selected ?? -1
    inherited: descriptor.inherited ?? false
    onActivated: {
        const token = currentValue;
        currentIndex = Qt.binding(() => root.descriptor.selected ?? -1);
        root.requested(token);
    }
}
