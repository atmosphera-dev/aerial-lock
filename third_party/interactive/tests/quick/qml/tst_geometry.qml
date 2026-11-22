// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: scene
    width: 1200
    height: 1000
    Component { id: mixedComponent; MixedFixture {} }
    Component {
        id: focusComponent
        Column {
            width: 400
            spacing: 8
            property var controls: [button, primary, tool, field, check, radio, toggle, tab, row, menu, slider]
            Item { id: entry; width: 1; height: 1; activeFocusOnTab: true }
            property alias entry: entry
            IAButton { id: button; text: "Button" }
            IAButton { id: primary; text: "Primary"; tone: IA.Presentation.Primary }
            IAToolButton { id: tool; text: "Tool" }
            IATextField { id: field; text: "Field" }
            IACheckBox { id: check; text: "Check" }
            IARadioButton { id: radio; text: "Radio" }
            IASwitch { id: toggle; text: "Switch" }
            IATabButton { id: tab; text: "Tab" }
            IAItemDelegate { id: row; text: "Row" }
            IAMenuItem { id: menu; text: "Menu" }
            IASlider { id: slider; value: 0.5 }
        }
    }
    Component {
        id: backgroundComponent
        IA.ControlBackground {
            id: background
            property string family: "button"
            width: 120
            height: 40
            resolvedAppearance: IA.ControlAppearance { source: IA.Appearance; family: background.family }
        }
    }
    Component {
        id: tabsComponent
        IATabBar {
            width: 360
            property bool rtl: false
            LayoutMirroring.enabled: rtl
            LayoutMirroring.childrenInherit: true
            IATabButton { text: "Overview" }
            IATabButton { text: "Details" }
            IATabButton { text: "History" }
        }
    }
    Component {
        id: scrollComponent
        IAScrollView {
            width: 360
            height: 200
            Flickable {
                objectName: "consumerFlickable"
                contentWidth: width
                contentHeight: 400
            }
        }
    }

    TestCase {
        name: "MixedGeometry"
        when: windowShown
        property MixedFixture fixture

        function cleanup(): void {
            IA.Appearance.input = {};
        }

        function test_sharedLanes_data(): var {
            const rows = [];
            for (const width of [360, 768, 1200]) {
                for (const scale of [1, 1.5, 2]) {
                    for (const rtl of [false, true]) {
                        rows.push({tag: width + "-scale" + scale + (rtl ? "-rtl" : "-ltr"),
                                   width: width, scale: scale, rtl: rtl});
                    }
                }
            }
            return rows;
        }

        function near(actual: real, expected: real, description: string): void {
            verify(Math.abs(actual - expected) <= 0.5,
                   description + ": " + actual + " versus " + expected);
        }

        function test_sharedLanes(data: var): void {
            scene.width = data.width;
            IA.Appearance.input = {fontScale: data.scale};
            fixture = createTemporaryObject(mixedComponent, scene,
                                             {width: data.width, height: scene.height, rtl: data.rtl}) as MixedFixture;
            verify(fixture !== null);
            verify(waitForRendering(fixture));
            verify(TestProbe.activate(fixture));
            const field = fixture.editor;
            const button = fixture.applyButton;
            const label = findChild(button, "iaLabel") as Text;
            verify(label !== null);
            near(field.height, button.height, "Composed field/button height");
            const fieldBaseline = field.mapToItem(fixture, 0, field.baselineOffset).y;
            const buttonBaseline = label.mapToItem(fixture, 0, label.baselineOffset).y;
            near(fieldBaseline, buttonBaseline, "Rendered text baselines");
            compare(label.font.pixelSize, Math.round(13 * data.scale));
            compare(field.font.pixelSize, label.font.pixelSize);
            verify(field.width > 0);
            const fieldLeft = field.mapToItem(fixture, 0, 0).x;
            const buttonLeft = button.mapToItem(fixture, 0, 0).x;
            verify(fieldLeft >= 0 && fieldLeft + field.width <= fixture.width + 0.5);
            verify(buttonLeft >= 0 && buttonLeft + button.width <= fixture.width + 0.5);
            if (data.rtl)
                verify(buttonLeft + button.width <= fieldLeft + 0.5, "RTL reorders the composed row");
            else
                verify(fieldLeft + field.width <= buttonLeft + 0.5, "LTR controls do not overlap");

            const track = findChild(fixture.slider, "iaTrack") as Item;
            const thumb = findChild(fixture.slider, "iaThumb") as Item;
            verify(track !== null);
            verify(thumb !== null);
            compare(fixture.slider.mirrored, data.rtl);
            const fill = findChild(fixture.progress, "iaFill") as Rectangle;
            verify(fill !== null);
            for (const value of [0, 37, 100]) {
                fixture.slider.value = value;
                verify(waitForRendering(fixture.slider));
                const trackCenter = track.mapToItem(fixture, track.width / 2, track.height / 2);
                const thumbCenter = thumb.mapToItem(fixture, thumb.width / 2, thumb.height / 2);
                near(thumbCenter.y, trackCenter.y, "Slider track/thumb centerline at " + value);
                verify(thumb.width > 0 && thumb.height > 0);
                const start = track.mapToItem(fixture, 0, 0).x;
                verify(thumbCenter.x >= start - 0.5);
                verify(thumbCenter.x <= start + track.width + 0.5);
                near(thumbCenter.x, start + track.width * fixture.slider.visualPosition,
                     "Thumb follows the track at " + value);
                near(fill.width, fixture.progress.contentItem.width * value / 100,
                     "Rendered progress fill at " + value);
                near(fill.x, data.rtl ? fixture.progress.contentItem.width - fill.width : 0,
                     "Progress fill origin respects layout direction");
            }
            for (const choice of [fixture.check, fixture.radioOne, fixture.radioTwo, fixture.toggle]) {
                const indicator = findChild(choice, "iaIndicator") as Item;
                const choiceLabel = findChild(choice, "iaLabel") as Text;
                verify(indicator !== null && choiceLabel !== null);
                near(indicator.mapToItem(fixture, 0, indicator.height / 2).y,
                     choiceLabel.mapToItem(fixture, 0, choiceLabel.height / 2).y,
                     "Indicator/label centerline");
                compare(choice.mirrored, data.rtl);
            }
            fixture.slider.value = 37;
            fixture.slider.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(fixture.slider, data.rtl ? Qt.Key_Left : Qt.Key_Right));
            compare(fixture.valueSummary.text, "Intensity: 38%");
            compare(fixture.progress.value, 38);

            const width = button.width;
            const height = button.height;
            verify(TestProbe.move(button, button.width / 2, button.height / 2));
            tryCompare(button, "hovered", true);
            near(button.width, width, "Hover width");
            near(button.height, height, "Hover height");
            verify(TestProbe.press(button, button.width / 2, button.height / 2));
            tryCompare(button, "down", true);
            near(button.width, width, "Pressed width");
            near(button.height, height, "Pressed height");
            verify(TestProbe.release(button, button.width / 2, button.height / 2));
            fixture.editor.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(fixture.editor, Qt.Key_Tab));
            tryCompare(button, "activeFocus", true);
            tryCompare(button, "visualFocus", true);
            near(button.width, width, "Focus width");
            near(button.height, height, "Focus height");
        }

        function test_validationAndFocusRemainDistinct_data(): var {
            return [{tag: "default", input: {}}, {tag: "red-primary", input: {tokens: {colors: {accent: "#E53935"}}}},
                {tag: "green-primary", input: {tokens: {colors: {accent: "#36A56A"}}}},
                {tag: "amber-high", input: {contrast: "high", tokens: {colors: {accent: "#D99B24"}}}},
                {tag: "light-high", input: {scheme: "light", contrast: "high"}},
                {tag: "dark-normal", input: {scheme: "dark", contrast: "normal"}},
                {tag: "dark-high", input: {scheme: "dark", contrast: "high"}}];
        }

        function test_singleFocusBorder_data(): var {
            const rows = [];
            for (const scheme of ["light", "dark"]) {
                for (const contrast of ["normal", "high"])
                    rows.push({tag: scheme + "-" + contrast, scheme: scheme, contrast: contrast});
            }
            return rows;
        }

        function rectangleGeometry(rectangle: Rectangle): var {
            return [rectangle.x, rectangle.y, rectangle.width, rectangle.height, rectangle.radius];
        }

        function test_segmentGroup_data(): var {
            const rows = [];
            for (const mode of test_singleFocusBorder_data()) {
                for (const rtl of [false, true]) {
                    for (const gap of [4, 11])
                        rows.push({tag: mode.tag + (rtl ? "-rtl-" : "-ltr-") + gap,
                            scheme: mode.scheme, contrast: mode.contrast, rtl: rtl, gap: gap});
                }
            }
            return rows;
        }

        function trackCount(item: Item): int {
            let count = item instanceof IA.ControlBackground
                && (item as IA.ControlBackground).resolvedAppearance.family === "tabBar" ? 1 : 0;
            for (const child of item.children)
                count += trackCount(child);
            return count;
        }

        function segmentGap(bar: IATabBar): real {
            const first = bar.itemAt(0) as IATabButton;
            const second = bar.itemAt(1) as IATabButton;
            const firstX = first.mapToItem(bar, 0, 0).x;
            const secondX = second.mapToItem(bar, 0, 0).x;
            return bar.mirrored ? firstX - secondX - second.width : secondX - firstX - first.width;
        }

        function test_segmentGroup(data: var): void {
            scene.width = 1200;
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast,
                tokens: data.gap === 4 ? {} : {metrics: {spacingXS: data.gap}}};
            const bar = createTemporaryObject(tabsComponent, scene, {rtl: data.rtl}) as IATabBar;
            verify(bar !== null);
            verify(TestProbe.render(bar));
            verify(TestProbe.activate(bar));
            const view = bar.contentItem as IA.SegmentGroup;
            const track = bar.background as IA.ControlBackground;
            verify(view !== null && track !== null);
            compare(bar.count, 3, "Factory track is not a native content-model member");
            compare(track.parent, bar, "Native background slot owns the track");
            compare(trackCount(bar), 1, "Exactly one effective track");
            compare(view.model, bar.contentModel);
            compare(view.mirrored, data.rtl);
            compare(view.effectiveLayoutDirection, data.rtl ? Qt.RightToLeft : Qt.LeftToRight);
            compare(bar.spacing, IA.Appearance.metrics.spacingXS);
            near(segmentGap(bar), data.gap, "Token-driven adjacent gap");
            compare([bar.leftPadding, bar.rightPadding, bar.topPadding, bar.bottomPadding],
                [data.gap, data.gap, data.gap, data.gap], "Existing track insets");
            compare([track.x, track.y, track.width, track.height], [0, 0, bar.width, bar.height]);
            near(view.width, bar.availableWidth, "Native viewport width");
            near(view.height, bar.availableHeight, "Native viewport height");
            let naturalWidth = 2 * bar.spacing + bar.leftPadding + bar.rightPadding;
            let naturalHeight = 0;
            for (let i = 0; i < bar.count; ++i) {
                const button = bar.itemAt(i) as IATabButton;
                naturalWidth += button.implicitWidth;
                naturalHeight = Math.max(naturalHeight, button.implicitHeight);
            }
            near(bar.implicitWidth, naturalWidth, "Native implicit content width plus padding");
            near(bar.implicitHeight, naturalHeight + bar.topPadding + bar.bottomPadding,
                "Native implicit height plus padding");
            const second = bar.itemAt(1) as IATabButton;
            verify(TestProbe.click(second, second.width / 2, second.height / 2));
            tryCompare(bar, "currentIndex", 1);
            compare(view.currentIndex, 1);
            compare(second.checked, true);
            second.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(second, data.rtl ? Qt.Key_Left : Qt.Key_Right));
            tryCompare(bar, "currentIndex", 2);
            compare(view.currentIndex, 2);
            compare((bar.itemAt(2) as IATabButton).checked, true);
            compare(second.checked, false);

            // Public aliases, palette replacement, hover and resizing update the
            // original background, never re-entering the factory binding.
            bar.backgroundColor = "#59616b";
            compare(track.color, "#59616b");
            bar.controlRadius = 9;
            compare(track.radius, 9);
            IA.Appearance.input = {scheme: data.scheme === "dark" ? "light" : "dark",
                contrast: data.contrast, tokens: {metrics: {spacingXS: data.gap}}};
            compare(track.color, "#59616b", "Local override remains caller-owned");
            bar.resetAppearance();
            compare(track.color, bar.backgroundColor);
            compare(track.radius, bar.controlRadius);
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast,
                tokens: {metrics: {spacingXS: 13}}};
            verify(TestProbe.render(bar));
            compare(bar.spacing, 13, "Live token replacement reaches the native gap");
            near(segmentGap(bar), 13, "Live token gap");
            bar.width = 400;
            verify(TestProbe.render(bar));
            verify(TestProbe.move(second, second.width / 2, second.height / 2));
            tryCompare(second, "hovered", true);
            compare(bar.background, track);
            compare(bar.contentItem, view);
            compare(trackCount(bar), 1);
            compare([track.width, track.height], [bar.width, bar.height]);

            bar.spacing = 7;
            verify(TestProbe.render(bar));
            near(segmentGap(bar), 7, "Writable native spacing");
            compare(view.spacing, 7);
            // Explicitly wide native buttons exercise the original overflow
            // viewport, including current-item scrolling in both directions.
            for (let i = 0; i < bar.count; ++i)
                (bar.itemAt(i) as IATabButton).width = 180;
            bar.width = 240;
            bar.setCurrentIndex(0);
            verify(TestProbe.render(bar));
            verify(view.contentWidth > view.width);
            compare(view.flickableDirection, Flickable.AutoFlickIfNeeded);
            compare(view.snapMode, ListView.SnapToItem);
            const startX = view.contentX;
            const first = bar.itemAt(0) as IATabButton;
            first.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(first, data.rtl ? Qt.Key_Left : Qt.Key_Right));
            tryCompare(bar, "currentIndex", 1);
            verify(TestProbe.keyClick(second, data.rtl ? Qt.Key_Left : Qt.Key_Right));
            tryCompare(bar, "currentIndex", 2);
            tryVerify(function() { return Math.abs(view.contentX - startX) > 1; });
            const last = bar.itemAt(2) as IATabButton;
            tryVerify(function() {
                const x = last.mapToItem(view, 0, 0).x;
                return x >= -1 && x + last.width <= view.width + 1;
            });
            compare(view.currentIndex, bar.currentIndex);
            compare(bar.background, track);
            compare(trackCount(bar), 1);
        }

        function test_choiceInnerGeometry_data(): var {
            const rows = [];
            for (const mode of test_singleFocusBorder_data()) {
                for (const rtl of [false, true])
                    rows.push({tag: mode.tag + (rtl ? "-rtl" : "-ltr"),
                        scheme: mode.scheme, contrast: mode.contrast, rtl: rtl});
            }
            return rows;
        }

        function test_choiceInnerGeometry(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            const samples = createTemporaryObject(focusComponent, scene);
            verify(samples !== null);
            verify(TestProbe.render(samples));
            verify(TestProbe.activate(samples));
            verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
            for (let index = 4; index <= 6; ++index) {
                const control = samples.controls[index];
                control.LayoutMirroring.enabled = data.rtl;
                const indicator = control.indicator as IA.ChoiceIndicator;
                verify(indicator !== null);
                const states = index === 4 ? [Qt.Unchecked, Qt.Checked, Qt.PartiallyChecked]
                    : [Qt.Unchecked, Qt.Checked];
                for (const state of states) {
                    samples.entry.forceActiveFocus(Qt.OtherFocusReason);
                    tryCompare(control, "activeFocus", false);
                    if (index === 4)
                        (control as IACheckBox).checkState = state;
                    else
                        control.checked = state === Qt.Checked;
                    verify(TestProbe.render(control));
                    const center = indicator.mark as Rectangle;
                    const thumb = findChild(indicator, "iaThumb") as Rectangle;
                    verify(center !== null);
                    const centerGeometry = rectangleGeometry(center);
                    const thumbGeometry = thumb ? rectangleGeometry(thumb) : [];
                    const inset = IA.Appearance.metrics.borderWidth + IA.Appearance.metrics.choiceGap;
                    compare(indicator.innerInset, inset);
                    fuzzyCompare(center.x, inset, 0.001);
                    fuzzyCompare(center.y, inset, 0.001);
                    fuzzyCompare(center.width, indicator.width - 2 * inset, 0.001);
                    fuzzyCompare(center.height, indicator.height - 2 * inset, 0.001);
                    control.forceActiveFocus(Qt.TabFocusReason);
                    tryCompare(control, "visualFocus", true);
                    verify(TestProbe.render(control));
                    compare(indicator.border.width, IA.Appearance.metrics.focusWidth);
                    compare(indicator.mark, center, "Focus retains the existing mark");
                    compare(rectangleGeometry(center), centerGeometry, "Focus never shrinks or moves the center");
                    if (thumb)
                        compare(rectangleGeometry(thumb), thumbGeometry, "Focus retains thumb size and endpoint");
                    samples.entry.forceActiveFocus(Qt.OtherFocusReason);
                    tryCompare(control, "activeFocus", false);
                    verify(TestProbe.render(control));
                    compare(indicator.border.width, IA.Appearance.metrics.borderWidth);
                    compare(rectangleGeometry(center), centerGeometry, "Blur retains center geometry");
                    if (thumb)
                        compare(rectangleGeometry(thumb), thumbGeometry, "Blur retains thumb geometry");
                }
            }
        }

        function test_backgroundPaintInset_data(): var {
            const families = [
                {family: "button", outlined: true, pressInset: true},
                {family: "tool", outlined: false, pressInset: true},
                {family: "tab", outlined: false, pressInset: false},
                {family: "delegate", outlined: false, pressInset: false},
                {family: "menuItem", outlined: false, pressInset: false},
                {family: "choice", outlined: false, pressInset: false},
                {family: "field", outlined: true, pressInset: false},
                {family: "slider", outlined: true, pressInset: false},
                {family: "popup", outlined: true, pressInset: false},
                {family: "surface", outlined: true, pressInset: false},
                {family: "surface", outlined: false, pressInset: false},
                {family: "tabBar", outlined: false, pressInset: false},
                {family: "tooltip", outlined: false, pressInset: false}
            ];
            const rows = [];
            for (const mode of test_singleFocusBorder_data()) {
                for (const sample of families)
                    rows.push({tag: mode.tag + "-" + sample.family + "-" + sample.outlined,
                        scheme: mode.scheme, contrast: mode.contrast, sample: sample});
            }
            return rows;
        }

        function test_backgroundPaintInset(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            const background = createTemporaryObject(backgroundComponent, scene, data.sample) as IA.ControlBackground;
            verify(background !== null);
            const inset = findChild(background, "iaPressInset") as Rectangle;
            verify(inset !== null);
            verify(TestProbe.render(background));
            const restingWidth = data.sample.outlined ? IA.Appearance.metrics.borderWidth : 0;
            const bounds = rectangleGeometry(background);
            for (const down of [false, true]) {
                background.down = down;
                for (const focused of [false, true, false]) {
                    background.focused = focused;
                    verify(TestProbe.render(background));
                    const paintedWidth = focused ? IA.Appearance.metrics.focusWidth : restingWidth;
                    const expected = [paintedWidth, paintedWidth,
                        background.width - 2 * paintedWidth, background.height - 2 * paintedWidth,
                        Math.max(0, background.radius - paintedWidth)];
                    compare(background.border.width, paintedWidth);
                    compare(rectangleGeometry(background), bounds, "Only the painted border changes");
                    compare(rectangleGeometry(inset), expected, "Decorative shading stays inside the painted border");
                    compare(inset.visible, down && data.sample.pressInset);
                }
            }
        }

        function test_singleFocusBorder(data: var): void {
            scene.width = 1200;
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            const samples = createTemporaryObject(focusComponent, scene);
            verify(samples !== null);
            verify(TestProbe.render(samples));
            verify(TestProbe.activate(samples));
            verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
            for (let index = 0; index < samples.controls.length; ++index) {
                const control = samples.controls[index];
                samples.entry.forceActiveFocus(Qt.OtherFocusReason);
                tryCompare(control, "activeFocus", false);
                // Choice focus belongs to its indicator; slider focus to its thumb.
                const border = (index >= 4 && index <= 6 ? control.indicator
                    : index === 10 ? control.handle : control instanceof IATabButton
                    ? findChild(control.background, "iaSegmentFrame") : control.background) as Rectangle;
                verify(border !== null);
                const borderless = [2, 7, 8, 9].indexOf(index) !== -1;
                compare(border.border.width, borderless ? 0 : IA.Appearance.metrics.borderWidth);
                const bounds = [border.x, border.y, border.width, border.height, border.radius,
                    control.width, control.height, control.implicitWidth, control.implicitHeight];
                control.forceActiveFocus(Qt.TabFocusReason);
                tryCompare(control, "activeFocus", true);
                if (index !== 3)
                    tryCompare(control, "visualFocus", true);
                verify(waitForRendering(control));
                compare(border.border.width, IA.Appearance.metrics.focusWidth);
                const colors = index >= 4 && index <= 6 ? (border as IA.ChoiceIndicator).colors
                    : (border as IA.ControlBackground).colors;
                compare(border.border.color, colors.focusedBorderColor);
                if (index !== 1) { // Primary retains the readable colored-fill fallback.
                    const tabFallback = control instanceof IATabButton && data.scheme === "light" && data.contrast === "high";
                    compare(border.border.color, tabFallback ? control.foregroundColor : IA.Appearance.colors.focus,
                        "Neutral focus retains the approved contrast fallback on the finished track");
                }
                compare([border.x, border.y, border.width, border.height, border.radius,
                    control.width, control.height, control.implicitWidth, control.implicitHeight], bounds,
                    "Focus grows inward without changing bounds or radius");
                for (const child of border.children) {
                    const rectangle = child as Rectangle;
                    if (rectangle !== null) {
                        if (index >= 4 && index <= 6)
                            compare(rectangle, (border as IA.ChoiceIndicator).mark, "Only the choice's mark, no ring");
                        else
                            compare(rectangle.objectName, "iaPressInset", "Only the press inset, no ring");
                    }
                }
                if (index >= 4 && index <= 6)
                    compare((control.background as Rectangle).border.width, 0, "Choice row has no second focus border");
                // Qt 6.8 grabImage crops with the item's parent-local x/y, not
                // scene coordinates. Grab the root and map nested controls.
                const image = grabImage(scene);
                verify(image.width > 0 && image.height > 0);
                const point = border.mapToItem(scene, border.width / 2, border.border.width / 2);
                const expected = border.border.color;
                fuzzyCompare(image.pixel(Math.floor(point.x * image.width / scene.width),
                    Math.floor(point.y * image.height / scene.height)), expected, 0.03,
                    "Focus color reaches the rendered top border: " + index);
            }
        }

        function test_validationAndFocusRemainDistinct(data: var): void {
            IA.Appearance.input = data.input;
            fixture = createTemporaryObject(mixedComponent, scene) as MixedFixture;
            verify(fixture !== null);
            verify(waitForRendering(fixture));
            verify(TestProbe.activate(fixture));
            const field = fixture.editor;
            const width = field.width;
            const height = field.height;
            field.validation = IA.Presentation.Invalid;
            field.forceActiveFocus(Qt.TabFocusReason);
            tryCompare(field, "activeFocus", true);
            verify(waitForRendering(field));
            const background = field.background as Rectangle;
            verify(background !== null);
            compare(background.border.color, IA.Appearance.colors.errorText);
            compare(background.border.width, IA.Appearance.metrics.focusWidth);
            for (const child of background.children) {
                const rectangle = child as Rectangle;
                if (rectangle !== null)
                    compare(rectangle.objectName, "iaPressInset", "Only the press inset, no focus outline");
            }
            const image = grabImage(fixture);
            verify(image.width > 0 && image.height > 0);
            const validationPoint = background.mapToItem(fixture, background.width / 2, background.border.width / 2);
            const scaleX = image.width / fixture.width;
            const scaleY = image.height / fixture.height;
            fuzzyCompare(image.pixel(Math.floor(validationPoint.x * scaleX), Math.floor(validationPoint.y * scaleY)),
                         IA.Appearance.colors.errorText, 0.03, "The wider focused border still renders invalid color");
            near(field.width, width, "Validation/focus width");
            near(field.height, height, "Validation/focus height");
            compare(field.text, "Maya Chen");
        }

        function test_touchTargetsDoNotOverlap(): void {
            IA.Appearance.input = {density: "touch"};
            fixture = createTemporaryObject(mixedComponent, scene, {width: 360}) as MixedFixture;
            verify(fixture !== null);
            verify(waitForRendering(fixture));
            for (const control of [fixture.applyButton, fixture.check, fixture.radioOne,
                                    fixture.radioTwo, fixture.toggle, fixture.secondTab, fixture.delegate]) {
                verify(control.width >= 44 && control.height >= 44,
                       "Touch target is at least 44 by 44: " + control);
            }
            const first = fixture.radioOne.mapToItem(fixture, 0, 0);
            const second = fixture.radioTwo.mapToItem(fixture, 0, 0);
            verify(first.x + fixture.radioOne.width <= second.x
                   || second.x + fixture.radioTwo.width <= first.x
                   || first.y + fixture.radioOne.height <= second.y
                   || second.y + fixture.radioTwo.height <= first.y);
        }

        function test_scrollViewAdoptsConsumerFlickable(): void {
            const view = createTemporaryObject(scrollComponent, scene) as IAScrollView;
            verify(view !== null);
            verify(TestProbe.render(view));
            const content = findChild(view, "consumerFlickable") as Flickable;
            verify(content !== null);
            compare(view.contentItem, content, "The supplied Flickable is the viewport, not nested content");
            near(content.width, view.availableWidth, "Consumer viewport width");
            near(content.height, view.availableHeight, "Consumer viewport height");
        }
    }
}
