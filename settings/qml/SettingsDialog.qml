import QtQuick
import QtQuick.Controls as C
import QtQuick.Templates as T
import InterActive

IADialog {
    id: root
    modal: true
    focus: true
    parent: C.Overlay.overlay
    margins: Insets.edge
    width: Math.min(440, parent ? parent.width - 2 * Insets.edge : 440)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    closePolicy: T.Popup.CloseOnEscape
}
