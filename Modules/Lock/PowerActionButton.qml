import QtQuick

// Decoration-free control: pointer, keyboard and accessible activation have
// exactly one path. Theme-specific styling lives in the supplied children.
FocusScope {
    id: root
    required property string label
    property bool trapNavigation: false
    property bool armed: false
    readonly property bool hovered: pointer.containsMouse
    signal activated()
    signal navigate(int direction)
    // Qt already excludes hidden/disabled items from traversal. Changing this
    // flag while an item owns focus warns when an inline state replaces it.
    activeFocusOnTab: true
    function focusControl() { if (visible && enabled) forceActiveFocus(Qt.TabFocusReason) }
    onActiveFocusChanged: { if (!activeFocus) armed = false }
    onVisibleChanged: { if (!visible) armed = false }
    Keys.onPressed: function(event) {
        if (!root.visible || !root.enabled) return
        if (trapNavigation && (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)) {
            navigate(event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier) ? -1 : 1)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            if (!event.isAutoRepeat) armed = true
            event.accepted = true
        }
    }
    Keys.onReleased: function(event) {
        if (!root.visible || !root.enabled) return
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            if (!event.isAutoRepeat && armed) { armed = false; activated() }
            event.accepted = true
        }
    }
    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        onClicked: { root.forceActiveFocus(Qt.MouseFocusReason); root.activated() }
    }
    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.onPressAction: { if (root.visible && root.enabled) root.activated() }
}
