// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: scene
    width: 800
    height: 700
    Component {
        id: groupComponent
        IASegmentedControl {
            width: 480
            property bool rtl: false
            property int consumerClicks: 0
            property var activations: []
            property var activatedCallback: null
            LayoutMirroring.enabled: rtl
            LayoutMirroring.childrenInherit: true
            Accessible.name: "Appearance"
            onActivated: index => {
                activations = activations.concat([index]);
                if (activatedCallback)
                    activatedCallback(index);
            }
            IASegmentedButton { text: "Light"; onClicked: ++consumerClicks }
            IASegmentedButton { text: "Dark"; onClicked: ++consumerClicks }
            IASegmentedButton { text: "System appearance"; onClicked: ++consumerClicks }
        }
    }
    Component { id: emptyComponent; IASegmentedControl { width: 480 } }
    Component {
        id: memberComponent
        IASegmentedButton { text: "Added" }
    }
    Component {
        id: boundComponent
        Item {
            id: boundFixture
            width: 600; height: 80
            property int selection: 1
            property real memberWidth: 73
            property real memberHeight: 39
            property alias group: group
            IASegmentedControl {
                id: group
                width: 480
                currentIndex: boundFixture.selection
                IASegmentedButton { text: "One" }
                IASegmentedButton { text: "Two" }
                IASegmentedButton { text: "Three"; width: boundFixture.memberWidth; height: boundFixture.memberHeight }
            }
        }
    }
    Component {
        id: switchComponent
        IASwitch {
            width: 360
            text: "Enable alerts"
            property bool rtl: false
            LayoutMirroring.enabled: rtl
            LayoutMirroring.childrenInherit: true
        }
    }
    Component {
        id: checkComponent
        IACheckBox {
            width: 360; text: "Include notes"
            property bool rtl: false
            LayoutMirroring.enabled: rtl
            LayoutMirroring.childrenInherit: true
        }
    }
    Component {
        id: radioComponent
        IARadioButton {
            width: 360; text: "Daily updates"
            property bool rtl: false
            LayoutMirroring.enabled: rtl
            LayoutMirroring.childrenInherit: true
        }
    }
    TestCase {
        name: "SegmentedChoice"
        when: windowShown

        function init(): void {
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop).*/);
        }
        function cleanup(): void {
            TestProbe.move(scene, scene.width - 2, scene.height - 2);
            TestProbe.release(scene, scene.width - 2, scene.height - 2);
            IA.Appearance.input = {};
        }
        function ready(item: Item): void {
            verify(TestProbe.render(item));
            verify(TestProbe.activateForInput(item));
        }
        function makeGroup(properties: var): IASegmentedControl {
            const group = createTemporaryObject(groupComponent, scene, properties || {}) as IASegmentedControl;
            verify(group !== null);
            ready(group);
            return group;
        }
        function near(actual: real, expected: real, description: string): void {
            verify(Math.abs(actual - expected) <= 1,
                description + ": " + actual + " versus " + expected);
        }
        function selection(group: IASegmentedControl, expected: int): void {
            compare(group.currentIndex, expected);
            compare(group.currentItem, group.itemAt(expected));
            for (let i = 0; i < group.count; ++i)
                compare(group.itemAt(i).checked, i === expected);
        }
        function click(item: Item): void {
            verify(TestProbe.click(item, item.width / 2, item.height / 2));
        }
        function test_nativePointerSpaceAndConsumerHandlers(): void {
            const group = makeGroup({});
            selection(group, 0);
            click(group.itemAt(1));
            selection(group, 1);
            compare(group.activations, [1]);
            compare(group.consumerClicks, 1);
            click(group.itemAt(1));
            selection(group, 1);
            compare(group.activations, [1]);
            compare(group.consumerClicks, 2);
            group.itemAt(2).forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(group, Qt.Key_Space));
            selection(group, 2);
            compare(group.activations, [1, 2]);
            compare(group.consumerClicks, 3);
            verify(TestProbe.keyClick(group, Qt.Key_Space));
            compare(group.activations, [1, 2]);
        }
        function test_programmaticAndClear(): void {
            const group = makeGroup({});
            for (const index of [2, 0, -1, 9, 1]) {
                group.setCurrentIndex(index);
                selection(group, index);
            }
            group.itemAt(2).checked = true;
            selection(group, 2);
            compare(group.activations, []);
            group.setCurrentIndex(-1);
            selection(group, -1);
            click(group.itemAt(1));
            selection(group, 1);
            compare(group.activations, [1]);
        }
        function test_bindingPreservationAndDeparture(): void {
            const fixture = createTemporaryObject(boundComponent, scene);
            verify(fixture !== null);
            const group = fixture.group as IASegmentedControl;
            ready(group);
            selection(group, 1);
            group.setCurrentIndex(0);
            selection(group, 0);
            fixture.selection = 2;
            selection(group, 2);
            click(group.itemAt(1));
            selection(group, 1);
            fixture.selection = 0;
            selection(group, 0);
            const departed = group.takeItem(2) as IASegmentedButton;
            verify(departed !== null);
            departed.parent = scene;
            tryCompare(departed, "width", 73);
            tryCompare(departed, "height", 39);
            fixture.memberWidth = 91;
            fixture.memberHeight = 43;
            compare(departed.width, 91);
            compare(departed.height, 43);
            departed.destroy();
        }
        function test_programmaticUncheck(): void {
            const group = makeGroup({});
            group.itemAt(0).checked = false;
            selection(group, -1);
            compare(group.activations, []);
            group.itemAt(1).checked = true;
            selection(group, 1);
            group.itemAt(2).checked = true;
            selection(group, 2);
            group.setCurrentIndex(0);
            selection(group, 0);
            click(group.itemAt(1));
            selection(group, 1);
            compare(group.activations, [1]);
        }
        function test_focusMutation_data(): var {
            return [{tag: "reorder", action: "reorder"}, {tag: "remove", action: "remove"},
                {tag: "disable", action: "disable"}, {tag: "hide", action: "hide"}];
        }
        function test_selectionCallbackReorderActivation(): void {
            const group = makeGroup({});
            const destination = group.itemAt(1);
            let handled = false;
            const mutate = function() {
                if (group.currentItem === destination && !handled) {
                    handled = true;
                    group.moveItem(group.currentIndex, 2);
                }
            };
            group.currentIndexChanged.connect(mutate);
            try {
                click(destination);
                verify(handled);
                selection(group, 2);
                compare(group.activations, [2]);
            } finally {
                group.currentIndexChanged.disconnect(mutate);
            }
        }
        function test_focusMutation(data: var): void {
            const group = makeGroup({});
            const destination = group.itemAt(1);
            let handled = false;
            const mutate = function() {
                if (!destination.activeFocus || handled) return;
                handled = true;
                if (data.action === "reorder") group.moveItem(1, 2);
                else if (data.action === "remove") group.takeItem(1);
                else if (data.action === "disable") destination.enabled = false;
                else destination.visible = false;
            };
            destination.activeFocusChanged.connect(mutate);
            try {
                group.itemAt(0).forceActiveFocus(Qt.TabFocusReason);
                verify(TestProbe.keyClick(group, Qt.Key_Right));
                verify(handled);
                selection(group, data.action === "reorder" ? 2 : 0);
                compare(group.activations, data.action === "reorder" ? [2] : []);
            } finally {
                destination.activeFocusChanged.disconnect(mutate);
                if (data.action === "remove") destination.destroy();
            }
        }
        function test_dynamicNativeIndices(): void {
            const group = makeGroup({});
            const added = createTemporaryObject(memberComponent, scene) as IASegmentedButton;
            verify(added !== null);
            group.insertItem(1, added);
            compare(group.count, 4);
            selection(group, 0);
            group.setCurrentIndex(2);
            group.moveItem(2, 3);
            selection(group, 3);
            const taken = group.takeItem(1);
            compare(taken, added);
            selection(group, 2);
            taken.parent = scene;
            group.removeItem(group.currentItem);
            selection(group, 1);
            group.currentItem.destroy();
            tryCompare(group, "count", 1);
            selection(group, 0);
            compare(group.activations, []);
        }
        function test_emptyAndLastRemoval(): void {
            const group = createTemporaryObject(emptyComponent, scene) as IASegmentedControl;
            verify(group !== null);
            selection(group, -1);
            const added = createTemporaryObject(memberComponent, scene) as IASegmentedButton;
            group.addItem(added);
            selection(group, 0);
            group.takeItem(0);
            selection(group, -1);
            compare(group.count, 0);
            added.parent = scene;
        }
        function test_addedMemberInteractiveAndDefaultDeparture(): void {
            const group = makeGroup({});
            const added = createTemporaryObject(memberComponent, scene) as IASegmentedButton;
            verify(added !== null);
            const naturalWidth = added.implicitWidth;
            const naturalHeight = added.implicitHeight;
            group.insertItem(1, added);
            ready(group);
            click(added);
            selection(group, 1);
            compare(group.activations, [1]);
            compare(group.takeItem(1), added);
            near(added.width, naturalWidth, "Departure restores default natural width");
            near(added.height, naturalHeight, "Departure restores default natural height");
        }
        function test_reentrantActivationMutation(): void {
            const group = makeGroup({});
            group.activatedCallback = function(index) {
                compare(index, 1);
                group.removeItem(group.currentItem);
                group.setCurrentIndex(0);
            };
            click(group.itemAt(1));
            compare(group.count, 2);
            selection(group, 0);
            compare(group.activations, [1]);
            group.activatedCallback = null;
            click(group.itemAt(1));
            selection(group, 1);
            compare(group.activations, [1, 1]);
        }
        function test_arrows_data(): var {
            return [{tag: "ltr", rtl: false}, {tag: "rtl", rtl: true}];
        }
        function test_boundedArrowsWithNoCurrentItem(): void {
            const group = makeGroup({});
            for (const index of [-1, -5, 9]) {
                group.setCurrentIndex(index);
                group.forceActiveFocus(Qt.TabFocusReason);
                verify(TestProbe.keyClick(group, Qt.Key_Left));
                selection(group, index);
            }
            group.setCurrentIndex(-1);
            group.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(group, Qt.Key_Right));
            selection(group, 0);
            compare(group.activations, [0]);
        }
        function test_reentrantCheckedMembership(): void {
            const group = makeGroup({});
            const member = group.itemAt(1) as IASegmentedButton;
            const mutate = function() {
                if (member.checked) {
                    compare(group.takeItem(1), member);
                    group.setCurrentIndex(0);
                }
            };
            member.checkedChanged.connect(mutate);
            group.setCurrentIndex(1);
            compare(group.count, 2);
            selection(group, 0);
            compare(group.activations, []);
            member.checkedChanged.disconnect(mutate);
            member.destroy();
            // Keep the taken member visually detached; reparenting it onto the
            // test surface would overlap the remaining members until destruction.
            ready(group);
            click(group.itemAt(1));
            selection(group, 1);
            compare(group.activations, [1]);
        }
        function test_disabledAndHiddenDestinations(): void {
            const group = makeGroup({});
            const middle = group.itemAt(1);
            middle.enabled = false;
            group.itemAt(0).forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(group, Qt.Key_Right));
            selection(group, 2);
            middle.enabled = true;
            middle.visible = false;
            verify(TestProbe.keyClick(group, Qt.Key_Left));
            selection(group, 0);
            compare(group.activations, [2, 0]);
        }
        function test_arrows(data: var): void {
            const group = makeGroup({rtl: data.rtl});
            const next = data.rtl ? Qt.Key_Left : Qt.Key_Right;
            const previous = data.rtl ? Qt.Key_Right : Qt.Key_Left;
            group.itemAt(0).forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(group, previous));
            selection(group, 0);
            compare(group.activations, []);
            verify(TestProbe.keyClick(group, next));
            selection(group, 1);
            compare(group.itemAt(1).activeFocus, true);
            compare(group.itemAt(1).visualFocus, true);
            group.itemAt(1).enabled = false;
            group.itemAt(1).visible = false;
            selection(group, 1);
            compare(group.activations, [1]);
            group.itemAt(0).forceActiveFocus(Qt.TabFocusReason);
            group.setCurrentIndex(0);
            verify(TestProbe.keyClick(group, next));
            selection(group, 2);
            verify(TestProbe.keyClick(group, next));
            selection(group, 2);
            compare(group.activations, [1, 2]);
            verify(TestProbe.keyClick(group, previous));
            selection(group, 0);
            compare(group.activations, [1, 2, 0]);
            verify(TestProbe.keyClick(group, Qt.Key_Up));
            selection(group, 0);
            compare(group.activations, [1, 2, 0]);
        }
        function test_independentGroups(): void {
            const first = makeGroup({});
            const second = makeGroup({y: 100});
            click(first.itemAt(1));
            click(second.itemAt(2));
            selection(first, 1);
            selection(second, 2);
            compare(first.activations, [1]);
            compare(second.activations, [2]);
        }
        function test_nativeAccessibleInterface(): void {
            const group = makeGroup({});
            compare(TestProbe.accessibleState(group).role, Accessible.Grouping);
            compare(TestProbe.accessibleState(group).name, "Appearance");
            const button = group.itemAt(1) as IASegmentedButton;
            const state = TestProbe.accessibleState(button);
            compare(state.role, Accessible.RadioButton);
            compare(state.name, "Dark");
            compare(state.checkable, true);
            compare(state.checked, false);
            verify(state.actions.indexOf(state.pressAction) >= 0);
            verify(TestProbe.accessibleAction(button, state.pressAction));
            selection(group, 1);
            compare(TestProbe.accessibleState(button).checked, true);
            compare(group.activations, [1]);
            verify(TestProbe.accessibleAction(button, state.toggleAction));
            selection(group, 1);
            compare(group.activations, [1]);
            button.enabled = false;
            compare(TestProbe.accessibleState(button).disabled, true);
            verify(TestProbe.accessibleAction(button, state.pressAction));
            compare(group.activations, [1]);
        }
        function modes(): var {
            const rows = [];
            for (const scheme of ["light", "dark"]) {
                for (const contrast of ["normal", "high"])
                    rows.push({tag: scheme + "-" + contrast, scheme: scheme, contrast: contrast});
            }
            return rows;
        }
        function test_layoutAppearanceAndStableTrack_data(): var { return modes(); }
        function test_layoutAppearanceAndStableTrack(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            const group = makeGroup({});
            const track = group.background;
            compare(track.color, data.scheme === "dark" ? "#35373b"
                : data.contrast === "high" ? "#dddfe0" : "#e0e2e4");
            const natural = group.implicitWidth;
            const view = group.contentItem as ListView;
            verify(view !== null);
            compare(view.keyNavigationEnabled, false);
            view.forceLayout();
            near(group.itemAt(0).width, group.itemAt(2).width, "Equal share");
            near(group.itemAt(1).x - group.itemAt(0).x - group.itemAt(0).width,
                IA.Appearance.metrics.spacingXS, "Token gap");
            group.width = 900;
            near(group.implicitWidth, natural, "Natural width independent of allocation");
            group.width = 160;
            view.forceLayout();
            verify(view.contentWidth > view.width, "Narrow lane scrolls");
            for (let i = 0; i < group.count; ++i)
                verify(group.itemAt(i).width >= group.itemAt(i).implicitWidth);
            group.width = 480;
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast,
                tokens: {metrics: {spacingXS: 11}, colors: {accent: "#a040c0"}}};
            view.forceLayout();
            near(group.itemAt(1).x - group.itemAt(0).x - group.itemAt(0).width, 11, "Live gap");
            group.setCurrentIndex(2);
            compare(group.background, track);
            group.itemAt(2).selectedBackgroundColor = "#743185";
            const surface = findChild(group.itemAt(2).background, "iaSegmentFrame");
            compare(surface.color, "#743185");
            group.itemAt(2).resetAppearance();
            verify(surface.color !== "#743185");
            compare(group.background, track);
            selection(group, 2);
            compare(group.activations, []);
        }
        function test_liveCanvasAndOwnedTrack(): void {
            const group = makeGroup({});
            const track = group.background;
            const original = String(group.backgroundColor);
            group.backgroundColor = original;
            IA.Appearance.input = {tokens: {colors: {canvas: original}}};
            verify(TestProbe.render(group));
            compare(track.color, original, "Equal assignment remains owned");
            group.resetAppearance();
            verify(TestProbe.render(group));
            verify(String(track.color) !== original);
            compare(track.color, group.backgroundColor);
            group.backgroundColor = "#80445566";
            compare(track.color, "#80445566");
            group.backgroundColor = "transparent";
            compare(track.color.a, 0);
            group.resetAppearance();
            compare(group.background, track);
            compare(group.currentIndex, 0);
        }
        function test_switchGeometry_data(): var {
            const rows = [];
            for (const rtl of [false, true]) {
                for (const position of [IA.Presentation.Leading, IA.Presentation.Trailing]) {
                    for (const density of ["compact", "touch"]) {
                        for (const text of ["", "Alerts", "A very long setting label which must elide without overlapping the switch"]) {
                            rows.push({tag: rtl + "-" + position + "-" + density + "-" + text.length,
                                rtl: rtl, position: position, density: density, text: text});
                        }
                    }
                }
            }
            return rows;
        }
        function test_switchGeometry(data: var): void {
            IA.Appearance.input = {density: data.density, fontScale: 1.5};
            const toggle = createTemporaryObject(switchComponent, scene,
                {rtl: data.rtl, indicatorPosition: data.position, text: data.text}) as IASwitch;
            verify(toggle !== null);
            ready(toggle);
            const row = toggle.contentItem as Text;
            const indicator = toggle.indicator;
            compare(indicator.parent, row);
            const left = (data.position === IA.Presentation.Leading) !== data.rtl;
            for (const width of [260, 440]) {
                toggle.width = width;
                near(indicator.mapToItem(toggle, 0, 0).x,
                    left ? toggle.leftPadding : toggle.width - toggle.rightPadding - indicator.width,
                    "Indicator lane edge");
                near(indicator.mapToItem(toggle, 0, indicator.height / 2).y,
                    row.mapToItem(toggle, 0, row.height / 2).y, "Shared vertical center");
                compare(row.leftPadding > 0, left);
                compare(row.rightPadding > 0, !left);
                compare(row.horizontalAlignment, data.rtl ? Text.AlignRight : Text.AlignLeft);
            }
            const thumb = findChild(toggle, "iaThumb") as Item;
            verify(thumb !== null);
            const thumbWidth = thumb.width;
            const indicatorWidth = indicator.width;
            toggle.forceActiveFocus(Qt.TabFocusReason);
            compare(toggle.visualFocus, true);
            near(thumb.width, thumbWidth, "Focus preserves thumb size");
            near(indicator.width, indicatorWidth, "Focus preserves indicator size");
        }
        function test_trailingSwitchInteraction_data(): var {
            return [{tag: "ltr", rtl: false}, {tag: "rtl", rtl: true}];
        }
        function test_liveChoiceDirection_data(): var {
            const rows = [];
            for (const kind of ["check", "radio", "switch"])
                for (const scheme of ["light", "dark"])
                    for (const contrast of ["normal", "high"])
                        for (const density of ["compact", "touch"])
                            for (const scale of [1, 1.5])
                                rows.push({tag: kind + scheme + contrast + density + scale, kind: kind,
                                    scheme: scheme, contrast: contrast, density: density, scale: scale});
            return rows;
        }
        function test_liveChoiceDirection(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast, density: data.density, fontScale: data.scale};
            const component = data.kind === "check" ? checkComponent : data.kind === "radio" ? radioComponent : switchComponent;
            const control = createTemporaryObject(component, scene);
            verify(control !== null);
            ready(control);
            click(control);
            compare(control.checked, true);
            control.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(control, Qt.Key_Space));
            compare(control.checked, data.kind === "radio");
            const checked = control.checked;
            const center = findChild(control, "iaChoiceCenter") as Item;
            verify(center !== null);
            const natural = Qt.size(control.indicator.implicitWidth, control.indicator.implicitHeight);
            const centerSize = Qt.size(center.width, center.height);
            const thumb = findChild(control, "iaThumb") as Item;
            const thumbSize = thumb !== null ? Qt.size(thumb.width, thumb.height) : Qt.size(0, 0);
            const positions = data.kind === "switch"
                ? [IA.Presentation.Leading, IA.Presentation.Trailing, IA.Presentation.Leading] : [IA.Presentation.Leading];
            for (const position of positions) {
                if (data.kind === "switch") control.indicatorPosition = position;
                for (const rtl of [false, true, false]) {
                    control.rtl = rtl;
                    verify(TestProbe.render(control));
                    const row = control.contentItem as Text;
                    const left = (position === IA.Presentation.Leading) !== rtl;
                    near(control.indicator.width, natural.width, "Live direction retains natural indicator width");
                    near(control.indicator.height, natural.height, "Live direction retains natural indicator height");
                    near(control.indicator.mapToItem(control, 0, 0).x,
                        left ? control.leftPadding : control.width - control.rightPadding - control.indicator.width,
                        "One logical anchor selects the physical edge");
                    near(control.indicator.mapToItem(control, 0, control.indicator.height / 2).y,
                        row.mapToItem(control, 0, row.height / 2).y, "Live centerline");
                    compare(row.leftPadding > 0, left);
                    compare(row.rightPadding > 0, !left);
                    compare(Qt.size(center.width, center.height), centerSize);
                    compare(control.checked, checked);
                    compare(control.activeFocus, true);
                    if (thumb !== null) {
                        compare(Qt.size(thumb.width, thumb.height), thumbSize);
                        const gap = (center.height - thumb.height) / 2;
                        near(thumb.x, control.visualPosition === 0 ? gap : center.width - gap - thumb.width,
                            "Native visualPosition is not mirrored twice");
                    }
                }
            }
            if (data.kind === "switch") {
                const indicator = control.indicator;
                verify(TestProbe.press(indicator, indicator.width * 0.2, indicator.height / 2));
                verify(TestProbe.move(indicator, indicator.width * 0.95, indicator.height / 2));
                verify(TestProbe.release(indicator, indicator.width * 0.95, indicator.height / 2));
                compare(control.checked, true, "Native drag still reaches its on endpoint after live flips");
            }
        }
        function test_trailingSwitchInteraction(data: var): void {
            const toggle = createTemporaryObject(switchComponent, scene,
                {rtl: data.rtl, indicatorPosition: IA.Presentation.Trailing}) as IASwitch;
            ready(toggle);
            verify(TestProbe.click(toggle, data.rtl ? toggle.width - toggle.rightPadding - 10 : toggle.leftPadding + 10,
                toggle.height / 2));
            compare(toggle.checked, true);
            click(toggle.indicator);
            compare(toggle.checked, false);
            toggle.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(toggle, Qt.Key_Space));
            compare(toggle.checked, true);
            const indicator = toggle.indicator;
            verify(TestProbe.press(indicator, data.rtl ? indicator.width * 0.2 : indicator.width * 0.8, indicator.height / 2));
            verify(TestProbe.move(indicator, data.rtl ? indicator.width * 0.95 : indicator.width * 0.05, indicator.height / 2));
            verify(TestProbe.release(indicator, data.rtl ? indicator.width * 0.95 : indicator.width * 0.05, indicator.height / 2));
            compare(toggle.checked, false);
            toggle.enabled = false;
            click(toggle);
            verify(TestProbe.keyClick(toggle, Qt.Key_Space));
            compare(toggle.checked, false);
        }
    }
}
