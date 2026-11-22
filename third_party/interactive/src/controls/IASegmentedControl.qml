// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQml.Models
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.Container {
    id: control
    property alias backgroundColor: appearance.backgroundColor
    property alias borderColor: appearance.borderColor
    property alias disabledBorderColor: appearance.disabledBorderColor
    property alias controlRadius: appearance.controlRadius
    property alias fontFamily: appearance.fontFamily
    property alias fontPixelSize: appearance.fontPixelSize
    property alias fontWeight: appearance.fontWeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    signal activated(int index)
    function resetAppearance(): void { appearance.resetOverrides(); }
    readonly property IA.ControlAppearance _appearance: IA.ControlAppearance {
        id: appearance
        source: IA.Appearance
        family: "tabBar"
    }
    Accessible.role: Accessible.Grouping
    implicitWidth: contentWidth + leftPadding + rightPadding
    implicitHeight: contentHeight + topPadding + bottomPadding
    horizontalPadding: appearance.horizontalPadding
    verticalPadding: appearance.verticalPadding
    spacing: IA.Appearance.metrics.spacingXS
    font: appearance.resolvedFont

    readonly property real _naturalSegmentWidth: {
        let result = 0;
        for (const item of contentChildren)
            result = Math.max(result, item.implicitWidth);
        return result;
    }
    readonly property real _naturalSegmentHeight: {
        let result = 0;
        for (const item of contentChildren)
            result = Math.max(result, item.implicitHeight);
        return result;
    }
    readonly property real _segmentWidth: Math.max(_naturalSegmentWidth,
        (availableWidth - Math.max(0, count - 1) * spacing) / Math.max(1, count))
    contentWidth: count * _naturalSegmentWidth + Math.max(0, count - 1) * spacing
    contentHeight: _naturalSegmentHeight

    property bool _complete: false
    property bool _reconciling: false
    property bool _reconcileAgain: false
    property list<QtObject> _allocations: []
    function _indexOf(item: Item): int {
        for (let i = 0; i < count; ++i) {
            if (itemAt(i) === item)
                return i;
        }
        return -1;
    }
    function _reconcile(): void {
        if (!_complete)
            return;
        if (_reconciling) {
            _reconcileAgain = true;
            return;
        }
        _reconciling = true;
        try {
            do {
                _reconcileAgain = false;
                const members = Array.from(contentChildren);
                const retained = [];
                for (const allocation of _allocations) {
                    if (members.indexOf(allocation.button) < 0) {
                        allocation.managing = false;
                        allocation.destroy();
                    } else {
                        retained.push(allocation);
                    }
                }
                _allocations = retained;
                for (const button of members) {
                    if (_indexOf(button) >= 0 && !_allocations.some(allocation => allocation.button === button)) {
                        const allocation = allocationComponent.createObject(control, {button: button});
                        if (!allocation)
                            throw new Error(allocationComponent.errorString());
                        _allocations.push(allocation);
                    }
                }
                for (const button of Array.from(group.buttons)) {
                    if (members.indexOf(button) < 0)
                        group.removeButton(button);
                }
                for (const button of members) {
                    // A consumer may mutate membership in a checkedChanged handler.
                    if (_indexOf(button) >= 0 && group.buttons.indexOf(button) < 0)
                        group.addButton(button);
                }
                group.checkedButton = currentItem as IASegmentedButton;
            } while (_reconcileAgain);
        } finally {
            _reconciling = false;
        }
    }
    function _userToggled(button: IASegmentedButton): void {
        if (_reconciling || !button || !button.checked)
            return;
        _selectForActivation(button);
    }
    function _selectForActivation(button: IASegmentedButton): void {
        const index = _indexOf(button);
        if (index < 0 || !button.enabled || !button.visible)
            return;
        setCurrentIndex(index);
        const selectedIndex = _indexOf(button);
        if (selectedIndex >= 0 && currentItem === button && button.checked && button.enabled && button.visible)
            activated(selectedIndex);
    }
    function _checkedChanged(button: IASegmentedButton): void {
        if (!_complete || _reconciling || !button || button.checked || currentItem !== button)
            return;
        // Qt unchecks the old member before updating checkedButton during an
        // exclusive transition. A checked replacement owns that transition;
        // only a genuine programmatic clear removes the current selection.
        for (const candidate of group.buttons) {
            if (candidate !== button && candidate.checked)
                return;
        }
        setCurrentIndex(-1);
    }
    function _moveSelection(direction: int): void {
        const step = mirrored ? -direction : direction;
        for (let index = currentIndex + step; index >= 0 && index < count; index += step) {
            const button = itemAt(index) as IASegmentedButton;
            if (!button || !button.enabled || !button.visible)
                continue;
            // ListView sets the new current item's focus with OtherFocusReason.
            // Establish keyboard focus first; its no-op setFocus then preserves it.
            button.forceActiveFocus(step > 0 ? Qt.TabFocusReason : Qt.BacktabFocusReason);
            _selectForActivation(button);
            return;
        }
    }
    onContentChildrenChanged: _reconcile()
    onCurrentIndexChanged: _reconcile()
    Component.onCompleted: {
        _complete = true;
        _reconcile();
    }
    Component.onDestruction: _complete = false
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            _moveSelection(event.key === Qt.Key_Right ? 1 : -1);
            event.accepted = true;
        } else {
            event.accepted = false;
        }
    }
    readonly property T.ButtonGroup _group: T.ButtonGroup {
        id: group
        exclusive: true
        onCheckedButtonChanged: {
            if (control._complete && !control._reconciling && checkedButton) {
                const index = control._indexOf(checkedButton);
                if (index >= 0)
                    control.setCurrentIndex(index);
            }
        }
    }
    Component {
        id: allocationComponent
        QtObject {
            id: allocation
            required property IASegmentedButton button
            property bool managing: true
            readonly property Binding allocatedWidth: Binding {
                target: allocation.button
                property: "width"
                when: allocation.managing && control._complete
                value: control._segmentWidth
                restoreMode: Binding.RestoreBindingOrValue
            }
            readonly property Binding allocatedHeight: Binding {
                target: allocation.button
                property: "height"
                when: allocation.managing && control._complete
                value: Math.max(control._naturalSegmentHeight, control.availableHeight)
                restoreMode: Binding.RestoreBindingOrValue
            }
        }
    }
    readonly property Instantiator _members: Instantiator {
        model: control.contentChildren
        delegate: QtObject {
            id: member
            required property var modelData
            readonly property IASegmentedButton button: modelData as IASegmentedButton
            readonly property Connections input: Connections {
                target: member.button
                function onToggled(): void { control._userToggled(member.button); }
                function onCheckedChanged(): void { control._checkedChanged(member.button); }
            }
        }
    }
    contentItem: IA.SegmentGroup {
        id: segments
        resolvedAppearance: appearance
        mirrored: control.mirrored
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: control.spacing
        keyNavigationEnabled: false
    }
    background: segments.createBackground(control)
}
