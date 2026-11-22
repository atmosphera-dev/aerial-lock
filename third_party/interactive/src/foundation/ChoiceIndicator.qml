// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: root
    enum Kind { Check, Radio, Toggle }
    required property ControlAppearance resolvedAppearance
    property int kind: ChoiceIndicator.Check
    property bool checked: false
    property bool partial: false
    property bool controlEnabled: true
    property bool down: false
    property bool hovered: false
    property bool active: true
    property bool focused: false
    property real visualPosition: checked ? 1 : 0
    readonly property bool selected: checked || partial
    readonly property stateColors colors: resolvedAppearance.stateTable.colors(
        ControlState.of(controlEnabled, down, hovered, selected, active, false))
    readonly property color markColor: selected ? colors.choiceMarkColor : colors.foregroundColor
    // The gap is a lane between the inward outline and the center, never an
    // overlay covering a fill. Focus may thicken inward without touching it.
    readonly property real innerInset: Appearance.metrics.borderWidth + Appearance.metrics.choiceGap
    objectName: "iaIndicator"
    implicitWidth: kind === ChoiceIndicator.Toggle ? Appearance.metrics.switchWidth : Appearance.metrics.indicatorSize
    implicitHeight: kind === ChoiceIndicator.Toggle ? Appearance.metrics.switchHeight : Appearance.metrics.indicatorSize
    radius: kind === ChoiceIndicator.Check ? resolvedAppearance.controlRadius : height / 2
    color: colors.choiceGapColor
    border.color: focused ? colors.focusedBorderColor : colors.borderColor
    border.width: focused ? colors.focusedBorderWidth : Appearance.metrics.borderWidth
    property bool complete: false
    property Item mark: null
    function updateMark(): void {
        if (!complete)
            return;
        if (mark) {
            mark.visible = false;
            mark.destroy();
            mark = null;
        }
        const component = kind === ChoiceIndicator.Check ? checkComponent
            : kind === ChoiceIndicator.Radio ? radioComponent
            : kind === ChoiceIndicator.Toggle ? toggleComponent : null;
        if (component) {
            mark = component.createObject(root) as Item;
            if (!mark)
                throw new Error(component.errorString());
        }
    }
    Component.onCompleted: { complete = true; updateMark(); }
    onKindChanged: updateMark()
    Component {
        id: checkComponent
        Rectangle {
            objectName: "iaChoiceCenter"
            anchors.fill: parent
            anchors.margins: root.innerInset
            radius: Appearance.metrics.choiceCenterRadius
            color: root.selected ? root.colors.choiceCenterColor : root.colors.backgroundColor
            Icon {
                anchors.fill: parent
                visible: root.selected
                source: root.partial ? Appearance.icons.minus : Appearance.icons.check
                color: root.markColor
            }
        }
    }
    Component {
        id: radioComponent
        Rectangle {
            objectName: "iaChoiceCenter"
            anchors.centerIn: parent
            width: Math.max(0, root.width - 2 * root.innerInset)
            height: width
            radius: width / 2
            color: root.selected ? root.colors.choiceCenterColor : root.colors.backgroundColor
        }
    }
    Component {
        id: toggleComponent
        Rectangle {
            id: track
            objectName: "iaChoiceCenter"
            anchors.fill: parent
            anchors.margins: root.innerInset
            radius: height / 2
            color: root.selected ? root.colors.choiceCenterColor : root.colors.choiceTrackColor
            Rectangle {
                objectName: "iaThumb"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, Math.min(Appearance.metrics.thumbSize,
                    track.height - 2 * Appearance.metrics.choiceGap))
                height: width
                radius: width / 2
                // Equal facing-edge clearance within the inner track's end slot.
                x: (track.height - height) / 2 + root.visualPosition * Math.max(0, track.width - track.height)
                color: root.markColor
            }
        }
    }
}
