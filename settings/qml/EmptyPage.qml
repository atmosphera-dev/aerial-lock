import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Components

Item {
    id: root
    property alias title: state.title
    property alias description: state.description
    property string actionText
    property string secondaryText
    property string error
    signal actionRequested()
    signal secondaryRequested()
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(420, Math.max(0, parent.width - 2 * Insets.edge))
        IAEmptyState {
            id: state
            objectName: "emptyState"
            Layout.fillWidth: true
            // The recovery column owns the horizontal inset for both copy and actions.
            horizontalPadding: 0
        }
        IAButton {
            objectName: "connectionRetry"
            visible: text.length > 0
            text: root.actionText
            onClicked: root.actionRequested()
        }
        IAButton {
            objectName: "recoverySecondary"
            visible: text.length > 0
            text: root.secondaryText
            onClicked: root.secondaryRequested()
        }
        IALabel {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.error
            wrapMode: Text.Wrap
        }
    }
}
