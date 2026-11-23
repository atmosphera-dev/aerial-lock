import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import InterActive
import InterActive.Foundation as IA

// Only the vertical viewport clips. The column keeps horizontal room for focus
// frames; the bar has a separate lane centred in the shared edge gutter.
Item {
    id: root
    property string title: ""
    property alias actions: actionLane.data
    default property alias sections: sections.data
    readonly property alias viewport: flick
    readonly property alias contentColumn: page
    readonly property alias scrollBar: bar

    IAScrollView {
        anchors.fill: parent
        topPadding: Insets.edge
        bottomPadding: Insets.edge
        leftPadding: 0
        rightPadding: 0
        background: null
        T.ScrollBar.vertical: IAScrollBar {
            id: bar
            objectName: "pageScrollBar"
            parent: lane
            height: lane.height
        }
        Flickable {
            id: flick
            objectName: "pageViewport"
            clip: true
            contentWidth: width
            contentHeight: page.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: page
                objectName: "pageColumn"
                x: Insets.edge
                width: Math.min(620, Math.max(0, flick.width - 2 * Insets.edge))
                spacing: 28
                RowLayout {
                    objectName: "pageBand"
                    Layout.fillWidth: true
                    Layout.preferredHeight: IA.Appearance.metrics.controlHeight
                    spacing: IA.Appearance.metrics.spacingS
                    IALabel {
                        objectName: "pageTitle"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: root.title
                        fontPixelSize: 20
                        fontWeight: IA.Appearance.typography.emphasisWeight
                    }
                    RowLayout { id: actionLane; spacing: IA.Appearance.metrics.spacingS }
                }
                ColumnLayout {
                    id: sections
                    objectName: "pageSections"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 28
                }
            }
        }
    }
    Item {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: Insets.edge
        anchors.bottomMargin: Insets.edge
        width: Insets.edge
        Item {
            id: lane
            anchors.horizontalCenter: parent.horizontalCenter
            width: IA.Appearance.metrics.scrollBarWidth
            height: parent.height
            clip: true
        }
    }
}
