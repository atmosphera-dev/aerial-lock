import QtQuick
import QtQuick.Layouts
import InterActive
import InterActive.Foundation as IA

Rectangle {
    id: root
    required property var source
    property bool busy: false
    signal toggled(bool on)
    signal editRequested()
    signal removeRequested()
    signal checkRequested()
    signal orderStarted()
    signal moveRequested(int delta)
    signal droppedBefore()
    objectName: "source-" + source.id
    implicitHeight: Math.max(56, contents.implicitHeight + 2 * IA.Appearance.metrics.spacingS)
    color: IA.Appearance.colors.content
    DropArea { anchors.fill: parent; keys: ["aerial-source"]; onDropped: root.droppedBefore() }
    RowLayout {
        id: contents
        anchors.fill: parent
        anchors.rightMargin: IA.Appearance.metrics.spacingXS
        spacing: IA.Appearance.metrics.spacingM
        Item {
            objectName: "sourceGrip"
            Layout.preferredWidth: IA.Appearance.metrics.iconSize
            Layout.preferredHeight: 32
            IA.Icon { anchors.centerIn: parent; source: Qt.resolvedUrl("icons/grip.svg"); color: IA.Appearance.colors.disabledText }
            DragHandler {
                id: drag
                target: proxy
                enabled: !root.busy
                onActiveChanged: if (active) { root.orderStarted(); proxy.Drag.active = true; }
                onGrabChanged: (transition, point) => {
                    if (transition !== PointerDevice.UngrabExclusive && transition !== PointerDevice.CancelGrabExclusive) return;
                    if (transition === PointerDevice.UngrabExclusive) proxy.Drag.drop();
                    else proxy.Drag.cancel();
                    proxy.Drag.active = false; proxy.x = 0; proxy.y = 0;
                }
            }
            Item {
                id: proxy
                width: parent.width; height: parent.height
                Drag.keys: ["aerial-source"]
                Drag.hotSpot.x: width / 2
                Drag.hotSpot.y: height / 2
                IA.Icon { anchors.centerIn: parent; visible: drag.active; source: Qt.resolvedUrl("icons/grip.svg"); color: IA.Appearance.colors.accent }
            }
        }
        Rectangle {
            objectName: "sourceKind"
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            radius: IA.Appearance.metrics.popupRadius
            color: IA.Appearance.colors.control
            IA.Icon { anchors.centerIn: parent; source: Qt.resolvedUrl("icons/" + root.source.glyph + ".svg"); color: IA.Appearance.colors.text }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 2
            IALabel { objectName: "sourceTitle"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.source.name; textFormat: Text.PlainText; wrapMode: Text.Wrap }
            IALabel {
                objectName: "sourceDetail"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.source.description
                textFormat: Text.PlainText
                wrapMode: root.source.pathDetail ? Text.NoWrap : Text.Wrap
                elide: root.source.pathDetail ? Text.ElideMiddle : Text.ElideNone
                fontPixelSize: IA.Appearance.typography.supportingSize
                foregroundColor: root.source.problem ? IA.Appearance.colors.errorText : IA.Appearance.colors.secondaryText
            }
        }
        IASwitch {
            objectName: "sourceToggle"
            enabled: !root.busy
            checked: root.source.enabled
            Accessible.name: root.source.name
            onToggled: {
                const requested = checked;
                // The service owns the applied value, not the control's click.
                checked = Qt.binding(() => root.source.enabled);
                root.toggled(requested);
            }
        }
        IAToolButton {
            id: more
            objectName: "sourceMore"
            icon.source: Qt.resolvedUrl("icons/dots.svg")
            Accessible.name: "More for " + root.source.name
            onClicked: menu.open()
            IAMenu {
                id: menu
                objectName: "sourceMenu"
                margins: Insets.edge
                y: more.height
                x: more.width - width
                onAboutToShow: root.orderStarted()
                IAMenuItem { objectName: "sourceEdit"; text: "Edit"; enabled: !root.busy; onTriggered: root.editRequested() }
                IAMenuItem { objectName: "sourceCheck"; text: root.source.canRetry ? "Try again" : "Check for new videos"; enabled: !root.busy; onTriggered: root.checkRequested() }
                IAMenuItem { objectName: "sourceMoveUp"; text: "Move up"; visible: root.source.canMoveUp; height: visible ? implicitHeight : 0; enabled: !root.busy; onTriggered: { more.forceActiveFocus(Qt.TabFocusReason); root.moveRequested(-1); } }
                IAMenuItem { objectName: "sourceMoveDown"; text: "Move down"; visible: root.source.canMoveDown; height: visible ? implicitHeight : 0; enabled: !root.busy; onTriggered: { more.forceActiveFocus(Qt.TabFocusReason); root.moveRequested(1); } }
                IAMenuSeparator { }
                IAMenuItem { objectName: "sourceRemove"; text: "Remove"; enabled: !root.busy; onTriggered: root.removeRequested() }
            }
        }
    }
}
