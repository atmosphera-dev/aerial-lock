import QtQuick
import QtQuick.Layouts
import InterActive.Foundation as IA

// The window owns navigation and supplies completed pages, never placeholders.
Rectangle {
    id: root
    property alias pages: sidebar.pages
    property alias currentPage: sidebar.current
    default property alias content: pane.data
    signal selected(string key)
    color: IA.Appearance.colors.content
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Sidebar {
            id: sidebar
            objectName: "sidebar"
            Layout.preferredWidth: 212
            Layout.fillHeight: true
            onSelected: key => root.selected(key)
        }
        Rectangle {
            Layout.preferredWidth: IA.Appearance.metrics.hairlineWidth
            Layout.fillHeight: true
            color: IA.Appearance.colors.separator
        }
        Item {
            id: pane
            objectName: "pagePane"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.fillHeight: true
        }
    }
}
