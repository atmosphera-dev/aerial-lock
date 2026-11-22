// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: scene
    width: 600
    height: 440
    component CheckReference: Rectangle {
        id: referenceRoot
        color: IA.Appearance.colors.canvas
        required property real inset
        required property real centerRadius
        required property color centerColor
        required property color markColor
        required property bool partial
        required property bool selected
        Rectangle {
            anchors.fill: parent
            anchors.margins: referenceRoot.inset
            radius: referenceRoot.centerRadius
            color: referenceRoot.centerColor
            IA.Icon {
                anchors.fill: parent
                visible: referenceRoot.selected
                color: referenceRoot.markColor
                source: referenceRoot.partial ? IA.Appearance.icons.minus : IA.Appearance.icons.check
            }
        }
    }
    Component { id: checkReferenceComponent; CheckReference {} }
    Component {
        id: sampleComponent
        Rectangle {
            width: 480
            height: 400
            color: IA.Appearance.colors.canvas
            property bool rtl: false
            property alias heading: heading
            property var choices: [check, radio, toggle]
            Item { id: entry; width: 1; height: 1; activeFocusOnTab: true }
            property alias entry: entry
            Column {
                x: 24
                y: 24
                width: parent.width - 48
                spacing: IA.Appearance.metrics.spacingS
                LayoutMirroring.enabled: parent.rtl
                LayoutMirroring.childrenInherit: true
                IALabel { id: heading; width: parent.width; text: "Delivery preferences" }
                IATextField { width: parent.width; text: "One shared content edge" }
                IACheckBox { id: check; width: parent.width; text: "Enable notifications"; tristate: true; hoverEnabled: false }
                IARadioButton { id: radio; width: parent.width; text: "Daily delivery"; autoExclusive: false; hoverEnabled: false }
                IASwitch { id: toggle; width: parent.width; text: "Standalone switch"; hoverEnabled: false }
            }
        }
    }
    TestCase {
        name: "ChoiceAlignment"
        when: windowShown
        function cleanup(): void { IA.Appearance.input = {}; }

        function test_alignment_data(): var {
            const rows = [];
            for (const scheme of ["light", "dark"])
                for (const density of ["compact", "touch"])
                    for (const fontScale of [1, 2])
                        for (const rtl of [false, true])
                            rows.push({tag: scheme + "-" + density + "-" + fontScale + "-" + rtl,
                                scheme: scheme, density: density, fontScale: fontScale, rtl: rtl});
            return rows;
        }
        function geometry(item: Item, relativeTo: Item): var {
            const p = item.mapToItem(relativeTo, 0, 0);
            return [p.x, p.y, item.width, item.height];
        }
        function pixelAt(image: var, item: Item, x: real, y: real): color {
            const point = item.mapToItem(scene, x, y);
            return image.pixel(Math.floor(point.x * image.width / scene.width),
                Math.floor(point.y * image.height / scene.height));
        }
        function test_alignment(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, density: data.density, fontScale: data.fontScale};
            const sample = createTemporaryObject(sampleComponent, scene, {rtl: data.rtl});
            verify(sample !== null);
            verify(TestProbe.render(sample));
            verify(TestProbe.activateForInput(sample));
            for (let index = 0; index < sample.choices.length; ++index) {
                const control = sample.choices[index] as T.AbstractButton;
                const row = control.contentItem as IA.ChoiceRow;
                const indicator = control.indicator as IA.ChoiceIndicator;
                compare(control.mirrored, data.rtl);
                const edge = indicator.mapToItem(sample, data.rtl ? indicator.width : 0, 0).x;
                const headingEdge = sample.heading.mapToItem(sample, data.rtl ? sample.heading.width : 0, 0).x;
                const inset = index === 2 ? IA.Appearance.metrics.verticalPadding : 0;
                fuzzyCompare(edge, headingEdge + (data.rtl ? -inset : inset), 0.001,
                    "Check/radio share the group edge; standalone switch keeps its released inset");
                compare(data.rtl ? row.rightPadding : row.leftPadding, indicator.width + control.spacing);
                fuzzyCompare(indicator.mapToItem(control, 0, indicator.height / 2).y,
                    row.mapToItem(control, 0, row.height / 2).y, 0.001);
                verify(control.height >= control.minimumHeight);
                compare(control.topPadding, IA.Appearance.metrics.verticalPadding);
                compare(control.bottomPadding, IA.Appearance.metrics.verticalPadding);
                if (data.density === "touch") verify(control.height >= 44);
                for (const state of index === 0 ? [Qt.Unchecked, Qt.Checked, Qt.PartiallyChecked] : [Qt.Unchecked, Qt.Checked]) {
                    sample.entry.forceActiveFocus(Qt.OtherFocusReason);
                    if (index === 0) (control as IACheckBox).checkState = state;
                    else control.checked = state === Qt.Checked;
                    verify(TestProbe.render(control));
                    const before = geometry(indicator, sample);
                    const center = indicator.mark;
                    const centerBefore = geometry(center, sample);
                    const thumb = findChild(indicator, "iaThumb") as Item;
                    const thumbBefore = thumb ? geometry(thumb, sample) : [];
                    control.forceActiveFocus(Qt.TabFocusReason);
                    tryCompare(control, "visualFocus", true);
                    verify(TestProbe.render(control));
                    compare(geometry(indicator, sample), before);
                    compare(geometry(center, sample), centerBefore);
                    if (thumb) compare(geometry(thumb, sample), thumbBefore);
                    sample.entry.forceActiveFocus(Qt.OtherFocusReason);
                    verify(TestProbe.render(control));
                    compare(geometry(indicator, sample), before);
                }
                // Explicit template padding remains caller-owned, including RTL.
                control.leftPadding = 11;
                control.rightPadding = 17;
                verify(TestProbe.render(control));
                const customEdge = indicator.mapToItem(control, data.rtl ? indicator.width : 0, 0).x;
                fuzzyCompare(customEdge, data.rtl ? control.width - 17 : 11, 0.001);
                control.leftPadding = undefined;
                control.rightPadding = undefined;
            }
        }

        function test_nativeSpace_data(): var {
            const rows = [];
            for (const scheme of ["light", "dark"])
                for (const rtl of [false, true])
                    rows.push({tag: scheme + "-" + rtl, scheme: scheme, rtl: rtl});
            return rows;
        }
        function test_nativeSpace(data: var): void {
            IA.Appearance.input = {scheme: data.scheme};
            const sample = createTemporaryObject(sampleComponent, scene, {rtl: data.rtl});
            verify(sample !== null);
            for (let index = 0; index < sample.choices.length; ++index) {
                const control = sample.choices[index] as T.AbstractButton;
                verify(TestProbe.activateForInput(control));
                control.forceActiveFocus(Qt.TabFocusReason);
                tryCompare(control, "activeFocus", true);
                const outcomes = index === 0 ? [Qt.PartiallyChecked, Qt.Checked, Qt.Unchecked]
                    : index === 1 ? [true] : [true, false];
                for (const outcome of outcomes) {
                    verify(TestProbe.nativeKeyHostReady(control));
                    verify(TestProbe.nativeKeyClick(control, Qt.Key_Space));
                    if (index === 0) compare((control as IACheckBox).checkState, outcome);
                    else compare(control.checked, outcome);
                    compare((control.indicator as IA.ChoiceIndicator).selected,
                        index === 0 ? outcome !== Qt.Unchecked : outcome);
                }
            }
        }

        function test_renderedStatePairs_data(): var { return test_nativeSpace_data(); }
        function test_renderedStatePairs(data: var): void {
            IA.Appearance.input = {scheme: data.scheme};
            const sample = createTemporaryObject(sampleComponent, scene, {rtl: data.rtl});
            verify(sample !== null);
            for (let index = 0; index < sample.choices.length; ++index) {
                const control = sample.choices[index] as T.AbstractButton;
                const indicator = control.indicator as IA.ChoiceIndicator;
                const reference = index === 0 ? createTemporaryObject(checkReferenceComponent, scene, {
                    width: indicator.width, height: indicator.height,
                    inset: indicator.innerInset, centerRadius: IA.Appearance.metrics.choiceCenterRadius,
                    centerColor: "transparent", markColor: "transparent", partial: false, selected: false
                }) as CheckReference : null;
                for (const state of ["off", "on", "hover", "pressed", "inactive", "disabled", "focus", "partial"]) {
                    control.checked = state !== "off";
                    if (index === 0 && state === "partial") (control as IACheckBox).checkState = Qt.PartiallyChecked;
                    indicator.hovered = state === "hover";
                    indicator.down = state === "pressed";
                    indicator.active = state !== "inactive";
                    indicator.controlEnabled = state !== "disabled";
                    indicator.focused = state === "focus";
                    const expected = indicator.selected ? indicator.colors.choiceCenterColor
                        : index === 2 ? indicator.colors.choiceTrackColor : indicator.colors.backgroundColor;
                    if (reference) {
                        const position = indicator.mapToItem(scene, 0, 0);
                        reference.x = 500 + position.x % 1;
                        reference.y = position.y;
                        reference.centerColor = expected;
                        reference.markColor = indicator.markColor;
                        reference.partial = indicator.partial;
                        reference.selected = indicator.selected;
                    }
                    verify(TestProbe.render(control));
                    const image = grabImage(scene);
                    const center = indicator.mark as Rectangle;
                    compare(center.color, expected);
                    let centerPixels = 0;
                    let markPixels = 0;
                    for (let y = 0; y < indicator.height; ++y)
                        for (let x = 0; x < indicator.width; ++x) {
                            const pixel = pixelAt(image, indicator, x, y);
                            // Capture pixels are RGB8; finished palette roles can
                            // retain higher precision than their displayed value.
                            if (Qt.colorEqual(pixel, expected.toString())) ++centerPixels;
                            if (Qt.colorEqual(pixel, indicator.markColor.toString())) ++markPixels;
                        }
                    if (reference) {
                        // Compare exact AA-composited glyph pixels to an independent
                        // same-scale icon, rather than require a fully opaque SVG pixel.
                        for (let row = 0; row < center.height; ++row)
                            for (let column = 0; column < center.width; ++column) {
                                const x = center.x + column;
                                const y = center.y + row;
                                compare(pixelAt(image, indicator, x, y), pixelAt(image, reference, x, y), state);
                            }
                    }
                    if (expected.a === 1) verify(centerPixels > 0, "Actual center pixels use the finished state role: " + state);
                    if (indicator.selected && index === 2) verify(markPixels > 0, "Actual thumb uses its paired mark: " + state);
                }
            }
        }
    }
}
