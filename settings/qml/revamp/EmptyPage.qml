import QtQuick
import InterActive.Components

Item {
    id: root
    property alias title: state.title
    property alias description: state.description
    property alias actionText: state.actionText
    signal actionRequested()
    IAEmptyState {
        id: state
        objectName: "emptyState"
        anchors.centerIn: parent
        width: Math.min(420, Math.max(0, parent.width - 2 * Insets.edge))
        onActionRequested: root.actionRequested()
    }
}
