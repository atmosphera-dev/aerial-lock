// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: scene
    width: 400
    height: 200
    Component { id: buttonComponent; IAButton {} }
    Component { id: toolComponent; IAToolButton {} }
    Component { id: tabComponent; IATabButton {} }
    Component { id: delegateComponent; IAItemDelegate {} }
    Component { id: checkComponent; IACheckBox {} }
    Component { id: radioComponent; IARadioButton {} }
    Component { id: switchComponent; IASwitch {} }
    Component {
        id: radioReferenceComponent
        Rectangle {
            id: reference
            required property color referenceBorderColor
            required property color referenceCenterColor
            property bool focused: false
            property bool paintsOutline: true
            property bool paintsCenter: true
            readonly property real centerInset: IA.Appearance.metrics.borderWidth + IA.Appearance.metrics.choiceGap
            width: IA.Appearance.metrics.indicatorSize
            height: width
            radius: height / 2
            color: "transparent"
            border.color: referenceBorderColor
            border.width: !paintsOutline ? 0 : focused ? IA.Appearance.metrics.focusWidth : IA.Appearance.metrics.borderWidth
            Rectangle {
                x: reference.centerInset
                y: x
                width: reference.width - 2 * reference.centerInset
                height: width
                radius: width / 2
                color: reference.referenceCenterColor
                visible: reference.paintsCenter
            }
        }
    }
    Component {
        id: radioRasterComponent
        Rectangle {
            width: 400
            height: 80
            property alias radio: sample
            IARadioButton { id: sample; x: 20; y: 20; width: 120; text: "Radio"; hoverEnabled: false }
        }
    }
    Component {
        id: indicatorComponent
        IA.ChoiceIndicator {
            resolvedAppearance: IA.ControlAppearance { source: IA.Appearance; family: "choice" }
        }
    }
    Component { id: fieldComponent; IATextField {} }
    Component { id: labelComponent; IALabel {} }
    Component {
        id: progressComponent
        IAProgressBar {
            property bool rtl: false
            LayoutMirroring.enabled: rtl
        }
    }
    Component {
        id: sliderComponent
        IASlider {
            property bool rtl: false
            LayoutMirroring.enabled: rtl
        }
    }
    Component { id: scrollComponent; IAScrollBar {} }
    Component { id: menuItemComponent; IAMenuItem {} }
    Component { id: menuSeparatorComponent; IAMenuSeparator { width: 200 } }
    Component { id: toolSeparatorComponent; IAToolSeparator {} }
    Component {
        id: tabBarComponent
        IATabBar {
            width: 300
            IATabButton { text: "Overview"; hoverEnabled: false }
            IATabButton { text: "Details"; hoverEnabled: false }
        }
    }
    Component {
        id: slotMenuComponent
        IAMenu {
            width: 300
            IAMenuItem { text: "Plain command"; hoverEnabled: false }
            IAMenuItem { text: "Checked command"; checkable: true; checked: true; hoverEnabled: false }
            IAMenuItem { text: "Icon command"; icon.source: IA.Appearance.icons.search; hoverEnabled: false }
            IAMenuItem {
                text: "Both slots"; checkable: true; checked: true
                icon.source: IA.Appearance.icons.search; hoverEnabled: false
            }
            IAMenu {
                title: "Submenu"
                IAMenuItem { text: "Nested command" }
            }
        }
    }
    TestCase {
        name: "PressedStatePairs"
        when: windowShown

        function init(): void { IA.Appearance.input = {}; }

        function cleanup(): void {
            TestProbe.move(scene, scene.width - 2, scene.height - 2);
            TestProbe.release(scene, scene.width - 2, scene.height - 2);
            IA.Appearance.input = {};
        }

        function test_onlyUsedVisuals(): void {
            const indicator = createTemporaryObject(indicatorComponent, scene, {checked: true}) as IA.ChoiceIndicator;
            verify(indicator !== null);
            compare(indicator.children.length, 1, "Only the check mark is instantiated");
            verify(indicator.children[0] instanceof Rectangle);
            verify(indicator.mark.children[0] instanceof IA.Icon);
            compare(visualItemCount(indicator), 5);
            indicator.partial = true;
            compare((indicator.mark.children[0] as IA.Icon).source, IA.Appearance.icons.minus);
            indicator.kind = IA.ChoiceIndicator.Radio;
            tryVerify(function() { return indicator.children.length === 1; });
            verify(indicator.children[0] instanceof Rectangle);
            compare(visualItemCount(indicator), 1);
            verify(findChild(indicator, "iaThumb") === null);
            indicator.kind = IA.ChoiceIndicator.Toggle;
            tryVerify(function() { return indicator.children.length === 1; });
            compare(indicator.mark.children[0].objectName, "iaThumb");
            compare(visualItemCount(indicator), 2);
            indicator.kind = IA.ChoiceIndicator.Check;
            tryVerify(function() { return indicator.children.length === 1; });
            verify(indicator.mark.children[0] instanceof IA.Icon);
            compare(visualItemCount(indicator), 5);

            for (const component of [buttonComponent, toolComponent, tabComponent, delegateComponent,
                                     checkComponent, radioComponent, switchComponent]) {
                const control = createTemporaryObject(component, scene) as T.AbstractButton;
                const inset = findChild(control.background, "iaPressInset") as Rectangle;
                verify(inset !== null);
                const usesInset = component === buttonComponent || component === toolComponent;
                compare(inset.gradient instanceof Gradient, usesInset,
                        "No Gradient or stops are created for a non-inset family");
            }
        }

        function test_supplementaryRoleConsumers(): void {
            const field = createTemporaryObject(fieldComponent, scene, {placeholderText: "Placeholder"}) as IATextField;
            const label = createTemporaryObject(labelComponent, scene) as IALabel;
            const progress = createTemporaryObject(progressComponent, scene) as IAProgressBar;
            const slider = createTemporaryObject(sliderComponent, scene) as IASlider;
            const scroll = createTemporaryObject(scrollComponent, scene) as IAScrollBar;
            verify(field !== null && label !== null && progress !== null && slider !== null && scroll !== null);
            IA.Appearance.input = {tokens: {colors: {placeholderText: "green", onAccent: "yellow", link: "red"}}};
            compare(field.placeholderTextColor, "#008000");
            compare(field.selectedTextColor, "#ffff00");
            compare(label.linkColor, "#ff0000");
            for (const control of [progress, slider]) {
                control.fillColor = "purple";
                const fill = findChild(control, "iaFill") as Rectangle;
                verify(fill !== null);
                compare(fill.color, "#800080", "A fill-only edit updates the actual consumer");
                control.enabled = false;
                compare(fill.color, control.disabledForegroundColor);
            }
            field.enabled = false;
            compare(field.color, field.disabledForegroundColor);
            compare(field.placeholderTextColor, field.disabledForegroundColor);
            const track = findChild(slider, "iaTrack") as Rectangle;
            compare(track.color, slider.disabledBackgroundColor);
            scroll.enabled = false;
            compare((scroll.contentItem as Rectangle).color, scroll.disabledForegroundColor);
            compare((scroll.background as Rectangle).color, scroll.backgroundColor,
                    "The scrollbar track still ignores enabled");
        }

        function test_valueAndScrollConsumers_data(): var { return test_choiceLocalOffTracks_data(); }

        function test_valueAndScrollConsumers(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            const slider = createTemporaryObject(sliderComponent, scene,
                {width: 180, value: 0.25}) as IASlider;
            const progress = createTemporaryObject(progressComponent, scene,
                {y: 60, width: 180, value: 0.25}) as IAProgressBar;
            const scroll = createTemporaryObject(scrollComponent, scene,
                {x: 240, width: 8, height: 160, size: 0.35, active: false}) as IAScrollBar;
            verify(slider !== null && progress !== null && scroll !== null);
            verify(TestProbe.render(scene));
            verify(TestProbe.activate(scene));
            const sliderTrack = findChild(slider, "iaTrack") as IA.ValueTrack;
            const progressTrack = progress.contentItem as IA.ValueTrack;
            verify(sliderTrack !== null && progressTrack !== null, "One shared track construction");
            for (const rtl of [false, true]) {
                slider.rtl = rtl;
                progress.rtl = rtl;
                for (const value of [0, 0.25, 1]) {
                    slider.value = value;
                    progress.value = value;
                    for (const track of [sliderTrack, progressTrack]) {
                        const fill = findChild(track, "iaFill") as Rectangle;
                        fuzzyCompare(fill.width, track.width * value, 0.001);
                        fuzzyCompare(fill.x, rtl ? track.width - fill.width : 0, 0.001);
                        compare(track.color, IA.Appearance.colors.controlPressed);
                        compare(fill.color, IA.Appearance.colors.accent);
                        verify(track.color !== IA.Appearance.colors.outline);
                        verify(track.color !== IA.Appearance.colors.text);
                    }
                }
            }
            slider.orientation = Qt.Vertical;
            slider.width = 30;
            slider.height = 140;
            slider.value = 0.25;
            verify(TestProbe.render(slider));
            const verticalFill = findChild(sliderTrack, "iaFill") as Rectangle;
            fuzzyCompare(verticalFill.height, sliderTrack.height * 0.25, 0.001);
            fuzzyCompare(verticalFill.y, sliderTrack.height - verticalFill.height, 0.001);
            const knob = slider.handle as IA.ControlBackground;
            fuzzyCompare(knob.radius, knob.width / 2, 0.001);
            const endpoint = sliderTrack.mapToItem(slider, sliderTrack.width / 2, verticalFill.y);
            fuzzyCompare(knob.y + knob.height / 2, endpoint.y, 0.001);

            const thumb = scroll.contentItem as Rectangle;
            const rail = scroll.background as Rectangle;
            verify(scroll.visible, "Overflow thumb remains visible with no activity");
            compare(thumb.color, IA.Appearance.colors.controlPressed);
            compare(rail.color.a, 0);
            fuzzyCompare(thumb.radius, thumb.width / 2, 0.001);
            verify(TestProbe.move(scroll, scroll.width / 2, thumb.y + thumb.height / 2));
            tryCompare(scroll, "hovered", true);
            compare(thumb.color, IA.Appearance.colors.outline);
            compare(rail.color.a, 0, "Hover never paints a full-height wash");
            verify(TestProbe.press(scroll, scroll.width / 2, thumb.y + thumb.height / 2));
            tryCompare(scroll, "pressed", true);
            compare(thumb.color, IA.Appearance.colors.secondaryText);
            compare(rail.color.a, 0);
            verify(TestProbe.release(scroll, scroll.width / 2, thumb.y + thumb.height / 2));
            verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
            tryCompare(scroll, "hovered", false);
            compare(thumb.color, IA.Appearance.colors.controlPressed);
            scroll.size = 1;
            compare(scroll.visible, false, "Availability still follows overflow/policy");
            scroll.policy = T.ScrollBar.AlwaysOn;
            compare(scroll.visible, true);
            scroll.policy = T.ScrollBar.AlwaysOff;
            compare(scroll.visible, false);
        }

        function test_choiceCenters_data(): var {
            const rows = [];
            for (const scheme of ["light", "dark"]) {
                for (const contrast of ["normal", "high"]) {
                    for (const component of [checkComponent, radioComponent, switchComponent]) {
                        for (const rtl of [false, true])
                            rows.push({tag: scheme + "-" + contrast + "-" + rows.length,
                                scheme: scheme, contrast: contrast, component: component, rtl: rtl});
                    }
                }
            }
            return rows;
        }

        function test_segmentedTabs_data(): var { return test_choiceLocalOffTracks_data(); }

        function test_segmentedTabs(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            const bar = createTemporaryObject(tabBarComponent, scene) as IATabBar;
            verify(bar !== null);
            verify(TestProbe.render(bar));
            verify(TestProbe.activate(bar));
            const track = bar.background as IA.ControlBackground;
            const first = bar.itemAt(0) as IATabButton;
            const second = bar.itemAt(1) as IATabButton;
            const segment = findChild(first.background, "iaSegmentFrame") as IA.ControlBackground;
            compare(track.resolvedAppearance.family, "tabBar");
            compare(track.color, bar.backgroundColor);
            compare(track.color, data.scheme === "dark" ? "#35373b"
                : data.contrast === "high" ? "#dddfe0" : "#e0e2e4");
            compare(track.border.width, 0);
            compare(bar.leftPadding, IA.Appearance.metrics.spacingXS);
            compare(bar.topPadding, bar.leftPadding);
            compare(bar.spacing, IA.Appearance.metrics.spacingXS);
            compare(segment.color, data.scheme === "dark" ? IA.Appearance.colors.controlHover : IA.Appearance.colors.content);
            compare(segment.border.width, 0);
            compare((findChild(second.background, "iaSegmentFrame") as Rectangle).color.a, 0);
            verify(segment.color !== track.color);
            fuzzyCompare(first.mapToItem(bar, 0, 0).x, bar.leftPadding, 0.001);
            fuzzyCompare(first.mapToItem(bar, 0, 0).y, bar.topPadding, 0.001);
            first.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(first, Qt.Key_Right));
            tryCompare(bar, "currentIndex", 1);
            compare((findChild(first.background, "iaSegmentFrame") as Rectangle).color.a, 0);
            compare((findChild(second.background, "iaSegmentFrame") as Rectangle).color, second.selectedBackgroundColor);
            // Already-focused items keep their native focus reason. Enter with
            // a real Tab event, as the existing capture/focus probes do.
            scene.forceActiveFocus(Qt.OtherFocusReason);
            tryCompare(scene, "activeFocus", true);
            for (let i = 0; i < 24 && !second.activeFocus; ++i)
                verify(TestProbe.keyClick(scene, Qt.Key_Tab));
            tryCompare(second, "activeFocus", true);
            tryCompare(second, "visualFocus", true);
            compare((findChild(second.background, "iaSegmentFrame") as Rectangle).border.width, IA.Appearance.metrics.focusWidth);
            const originalTrack = String(track.color);
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast,
                tokens: {colors: {canvas: originalTrack}}};
            verify(TestProbe.render(bar));
            compare(bar.background, track);
            compare(track.color, bar.backgroundColor);
            verify(String(track.color) !== originalTrack);
            compare(bar.currentIndex, 1);
            compare(second.activeFocus, true);
        }

        function test_menuSlots_data(): var {
            const rows = [];
            for (const rtl of [false, true]) {
                for (const touch of [false, true])
                    rows.push({tag: rtl + "-" + touch, rtl: rtl, touch: touch});
            }
            return rows;
        }

        function test_menuSlots(data: var): void {
            IA.Appearance.input = {density: data.touch ? "touch" : "compact", fontScale: data.touch ? 1.5 : 1};
            const menu = createTemporaryObject(slotMenuComponent, scene, {parent: scene}) as IAMenu;
            verify(menu !== null);
            menu.open();
            tryCompare(menu, "opened", true);
            for (let i = 0; i < menu.count; ++i)
                menu.itemAt(i).LayoutMirroring.enabled = data.rtl;
            verify(TestProbe.render(menu.contentItem));
            let labelEdge = null;
            let iconEdge = null;
            let markEdge = null;
            for (let i = 0; i < menu.count; ++i) {
                const row = menu.itemAt(i) as IAMenuItem;
                const label = findChild(row, "iaLabel") as Text;
                verify(label !== null);
                const edge = label.mapToItem(menu.contentItem, data.rtl ? label.width : 0, 0).x;
                if (labelEdge === null)
                    labelEdge = edge;
                fuzzyCompare(edge, labelEdge, 0.5, "Native menu text padding keeps every label in the same lane");
                if (row.showsIcon) {
                    const icon = findChild(row, "iaMenuIcon") as IA.Icon;
                    verify(icon !== null);
                    const position = icon.mapToItem(menu.contentItem, 0, 0).x;
                    if (iconEdge === null)
                        iconEdge = position;
                    fuzzyCompare(position, iconEdge, 0.5, "Action icons retain their slot with or without a check mark");
                }
                if (row.checkable) {
                    const position = row.indicator.mapToItem(menu.contentItem, 0, 0).x;
                    if (markEdge === null)
                        markEdge = position;
                    fuzzyCompare(position, markEdge, 0.5, "Check marks retain their slot with or without an action icon");
                }
                if (row.subMenu !== null) {
                    const arrow = row.arrow;
                    const slot = findChild(row, "iaArrowSlot") as Item;
                    verify(slot !== null);
                    compare(slot.width, row.height);
                    compare(slot.height, row.height);
                    const box = arrow.mapToItem(row, 0, 0);
                    const top = box.y, bottom = row.height - box.y - arrow.height;
                    const end = data.rtl ? box.x : row.width - box.x - arrow.width;
                    fuzzyCompare(top, bottom, 1);
                    fuzzyCompare(end, top, 1, "Arrow has equal clearance to all facing row edges");
                }
            }
            // A menu with no accessories collapses the shared leading lane.
            for (let i = 0; i < menu.count; ++i) {
                const row = menu.itemAt(i) as IAMenuItem;
                row.checkable = false;
                row.icon.source = "";
            }
            verify(TestProbe.render(menu.contentItem));
            compare((menu.itemAt(0) as IAMenuItem).leadingPadding, 0);
            menu.close();
        }

        function test_menuDangerAndSeparators_data(): var { return test_choiceLocalOffTracks_data(); }

        function test_menuDangerAndSeparators(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            const item = createTemporaryObject(menuItemComponent, scene, {
                text: "Remove collection", width: 240, tone: IA.Presentation.Destructive, hoverEnabled: false
            }) as IAMenuItem;
            verify(item !== null);
            const surface = item.background as IA.ControlBackground;
            const label = findChild(item, "iaLabel") as Text;
            verify(TestProbe.render(item));
            compare(surface.color.a, 0);
            compare(label.color, item.foregroundColor);
            // P2 can finalize a generated danger label for hover headroom.
            // An explicit semantic text role proves the role-to-pixel mapping.
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast,
                tokens: {colors: {errorText: "#b03050", destructiveText: "#00ff00"}}};
            compare(label.color, "#b03050");
            compare(surface.color.a, 0);
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            item.highlighted = true;
            compare(surface.color, item.selectedBackgroundColor);
            compare(surface.color, IA.Appearance.colors.accent);
            compare(label.color, IA.Appearance.colors.onAccent);
            verify(menuPairContrast(label.color, surface.color) >= 7);
            for (const component of [menuSeparatorComponent, toolSeparatorComponent]) {
                const separator = createTemporaryObject(component, scene);
                verify(separator !== null);
                const line = findChild(separator, "iaSeparator") as Rectangle;
                verify(line !== null);
                compare(line.color, IA.Appearance.colors.separator);
                separator.foregroundColor = "magenta";
                verify(TestProbe.render(separator));
                compare(line.color, "#ff00ff");
                IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast,
                    tokens: {colors: {separator: "green", text: "red"}}};
                compare(line.color, "#ff00ff", "Explicit line overrides stay owned");
                separator.foregroundColor = undefined;
                compare(line.color, "#008000", "RESET resumes the live separator role, not text");
                separator.foregroundColor = "blue";
                separator.resetAppearance();
                compare(line.color, "#008000");
                IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
                compare(line.color, IA.Appearance.colors.separator);
            }
        }

        function test_choiceCenters(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            const control = createTemporaryObject(data.component, scene, {
                x: 20, y: 20, width: 240, text: "One shared choice row", hoverEnabled: false
            }) as T.AbstractButton;
            verify(control !== null);
            control.LayoutMirroring.enabled = data.rtl;
            const row = control.contentItem as IA.ChoiceRow;
            verify(row !== null, "Every Qt choice template uses Foundation.ChoiceRow");
            compare(control.indicator, row.indicator, "Native interaction retains the real indicator");
            verify(TestProbe.render(control));
            verify(TestProbe.activate(control));
            const indicator = control.indicator as IA.ChoiceIndicator;
            const center = indicator.mark as Rectangle;
            verify(center !== null);
            const thumb = findChild(indicator, "iaThumb") as Rectangle;
            const glyph = center.children.length ? center.children[0] as IA.Icon : null;
            const reference = control instanceof IARadioButton
                ? createTemporaryObject(radioReferenceComponent, scene, {
                    x: 320, y: indicator.mapToItem(scene, 0, 0).y,
                    referenceBorderColor: indicator.colors.borderColor,
                    referenceCenterColor: indicator.colors.backgroundColor
                }) : null;
            if (control instanceof IARadioButton)
                verify(reference !== null);
            compare(indicator.color.a, 0, "The outline-to-center lane is not painted over");
            compare(indicator.border.color, control.borderColor);
            const labelCenter = row.mapToItem(control, 0, row.height / 2).y;
            fuzzyCompare(indicator.mapToItem(control, 0, indicator.height / 2).y, labelCenter, 0.001);
            compare(row.mirrored, data.rtl);
            compare(indicator.x, data.rtl ? row.width - indicator.width : 0);
            compare(data.rtl ? row.rightPadding : row.leftPadding, indicator.width + control.spacing);
            compare(data.rtl ? row.leftPadding : row.rightPadding, 0);

            for (const on of [false, true]) {
                control.checked = on;
                compare(indicator.selected, on);
                compare(center.color, on ? indicator.colors.choiceCenterColor
                    : thumb ? indicator.colors.choiceTrackColor : indicator.colors.backgroundColor);
                if (on) {
                    compare(center.color, control.selectedBackgroundColor);
                    compare(indicator.markColor, "#ffffff", "Default on pair has a light mark");
                    verify(menuPairContrast(indicator.markColor, center.color) >= 7);
                }
                if (thumb) {
                    compare(thumb.color, indicator.markColor);
                    const gap = (center.height - thumb.height) / 2;
                    verify(gap >= IA.Appearance.metrics.choiceGap);
                    fuzzyCompare(thumb.y, gap, 0.001);
                    const edge = indicator.visualPosition < 0.5 ? thumb.x
                        : center.width - thumb.x - thumb.width;
                    fuzzyCompare(edge, gap, 0.001, "Thumb has equal facing-edge clearance");
                } else if (glyph) {
                    compare(center.radius, IA.Appearance.metrics.choiceCenterRadius,
                        "A thicker outline must not consume the checkbox center's rounded corners");
                    compare(glyph.visible, on);
                    compare(glyph.color, indicator.markColor);
                }
                for (const focused of [false, true]) {
                    indicator.focused = focused;
                    if (reference) {
                        reference.focused = focused;
                        reference.referenceBorderColor = focused ? indicator.colors.focusedBorderColor : indicator.colors.borderColor;
                        reference.referenceCenterColor = on ? indicator.colors.choiceCenterColor : indicator.colors.backgroundColor;
                    }
                    verify(TestProbe.render(control));
                    verify(center.width > 0 && center.height > 0);
                    // The resting lane is fixed; focus consumes part of it,
                    // rather than moving the center to preserve its full gap.
                    fuzzyCompare(center.x - IA.Appearance.metrics.borderWidth, IA.Appearance.metrics.choiceGap, 0.001);
                    fuzzyCompare(center.y - IA.Appearance.metrics.borderWidth, IA.Appearance.metrics.choiceGap, 0.001);
                    fuzzyCompare(indicator.width - center.x - center.width - IA.Appearance.metrics.borderWidth,
                        IA.Appearance.metrics.choiceGap, 0.001);
                    verify(center.y > indicator.border.width, "Default focus leaves an unpainted lane");
                    const image = grabImage(scene);
                    // A straight-edge sample measures an actual unpainted lane,
                    // not merely geometry. Avoid the rounded corner and tick.
                    const lane = indicator.mapToItem(scene, indicator.width / 2,
                        (indicator.border.width + center.y) / 2);
                    const outside = indicator.mapToItem(scene, -2, indicator.height / 2);
                    if (reference && focused) {
                        compare(samplePixel(image, indicator, indicator.width / 2, (indicator.border.width + center.y) / 2),
                            samplePixel(image, reference, reference.width / 2, (indicator.border.width + center.y) / 2),
                            "The original focused radio pixel matches independent native outline/center AA");
                    } else {
                        compare(image.pixel(Math.floor(lane.x * image.width / scene.width),
                            Math.floor(lane.y * image.height / scene.height)),
                            image.pixel(Math.floor(outside.x * image.width / scene.width),
                                Math.floor(outside.y * image.height / scene.height)),
                            "The gap has exactly the host backdrop, not the outline or center fill");
                    }
                }
                indicator.focused = false;
                indicator.hovered = true;
                compare(center.color, on ? indicator.colors.choiceCenterColor
                    : thumb ? indicator.colors.choiceTrackColor : indicator.colors.backgroundColor);
                const hover = center.color.toString();
                indicator.hovered = false;
                verify(center.color.toString() !== hover, "Both off and on have visible hover feedback");
                indicator.down = true;
                compare(center.color, on ? indicator.colors.choiceCenterColor
                    : thumb ? indicator.colors.choiceTrackColor : indicator.colors.backgroundColor);
                indicator.down = false;
            }
            if (control instanceof IACheckBox) {
                control.checkState = Qt.PartiallyChecked;
                compare(indicator.selected, true);
                verify(glyph.visible);
                compare(glyph.source, IA.Appearance.icons.minus);
                compare(center.color, indicator.colors.choiceCenterColor);
            }
            control.checked = true;
            control.enabled = false;
            compare(center.color, indicator.colors.choiceCenterColor, "Disabled-on retains its center");
            verify(center.color.a > 0);
            compare(indicator.markColor, indicator.colors.choiceMarkColor);
            if (thumb)
                compare(thumb.color, indicator.markColor);
            else if (glyph)
                compare(glyph.color, indicator.markColor);
            compare(indicator.border.color, control.disabledBorderColor);
        }

        function samplePixel(image: var, item: Item, x: real, y: real): color {
            const point = item.mapToItem(scene, x, y);
            return image.pixel(Math.floor(point.x * image.width / scene.width),
                Math.floor(point.y * image.height / scene.height));
        }

        function test_radioLaneComposition_data(): var {
            const rows = [];
            for (const mode of test_choiceLocalOffTracks_data()) {
                for (const scale of [1, 2]) {
                    for (const rtl of [false, true])
                        rows.push({tag: mode.tag + "-scale" + scale + (rtl ? "-rtl" : "-ltr"),
                            scheme: mode.scheme, contrast: mode.contrast, scale: scale, rtl: rtl});
                }
            }
            return rows;
        }

        function test_radioLaneComposition(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            const previousWidth = scene.width;
            scene.width = 800;
            try {
                const stage = createTemporaryObject(radioRasterComponent, scene,
                    {scale: data.scale, transformOrigin: Item.TopLeft});
                verify(stage !== null);
                const control = stage.radio as IARadioButton;
                control.LayoutMirroring.enabled = data.rtl;
                verify(TestProbe.render(stage));
                verify(TestProbe.activate(stage));
                const indicator = control.indicator as IA.ChoiceIndicator;
                const center = indicator.mark as Rectangle;
                const references = [];
                for (let index = 0; index < 3; ++index) {
                    const reference = createTemporaryObject(radioReferenceComponent, stage, {
                        x: 180 + 40 * index, y: indicator.mapToItem(stage, 0, 0).y,
                        referenceBorderColor: indicator.colors.borderColor,
                        referenceCenterColor: indicator.colors.backgroundColor,
                        paintsOutline: index !== 1, paintsCenter: index !== 0
                    });
                    verify(reference !== null);
                    references.push(reference);
                }
                for (const on of [false, true]) {
                    control.checked = on;
                    for (const focused of [false, true]) {
                        indicator.focused = focused;
                        for (const reference of references) {
                            reference.focused = focused;
                            reference.referenceBorderColor = focused ? indicator.colors.focusedBorderColor : indicator.colors.borderColor;
                            reference.referenceCenterColor = on ? indicator.colors.choiceCenterColor : indicator.colors.backgroundColor;
                        }
                        const laneY = (indicator.border.width + center.y) / 2;
                        const actualPixels = [];
                        const referencePixels = [];
                        for (const backdrop of ["#ffffff", "#000000", "#35794b"]) {
                            stage.color = backdrop;
                            verify(TestProbe.render(stage));
                            const image = grabImage(scene);
                            verify(image.width > 0 && image.height > 0);
                            const left = indicator.mapToItem(scene, 0, 0);
                            const right = indicator.mapToItem(scene, indicator.width, 0);
                            fuzzyCompare((right.x - left.x) / indicator.width, data.scale, 0.001,
                                "The native and 2x transforms are actually rendered");
                            const actual = samplePixel(image, indicator, indicator.width / 2, laneY);
                            const expected = samplePixel(image, references[2], references[2].width / 2, laneY);
                            actualPixels.push(actual);
                            referencePixels.push(expected);
                            compare(actual, expected, "Exact independent composition at the original physical sample");
                            const outside = samplePixel(image, indicator, -2, indicator.height / 2);
                            if (!focused)
                                compare(actual, outside, "Resting radio gap is exactly the current backdrop");
                            compare(samplePixel(image, references[1], references[1].width / 2, laneY), outside,
                                "The isolated center does not paint the original lane sample");
                            compare(expected, samplePixel(image, references[0], references[0].width / 2, laneY),
                                "Native outline AA independently accounts for the lane contribution");
                            // Preserve the measured physical neighborhood, not a
                            // replacement point chosen because it happens to pass.
                            const physicalScale = data.scale * image.width / scene.width;
                            for (let dy = -2; dy <= 2; ++dy) {
                                for (let dx = -2; dx <= 2; ++dx) {
                                    compare(samplePixel(image, indicator, indicator.width / 2 + dx / physicalScale, laneY + dy / physicalScale),
                                        samplePixel(image, references[2], references[2].width / 2 + dx / physicalScale, laneY + dy / physicalScale),
                                        "Exact native composition across the original pixel neighborhood");
                                }
                            }
                        }
                        verify(referencePixels[0].r > referencePixels[1].r
                            && referencePixels[0].g > referencePixels[1].g
                            && referencePixels[0].b > referencePixels[1].b,
                            "The independent native lane transmits the backdrop");
                        verify(actualPixels[0].r > actualPixels[1].r
                            && actualPixels[0].g > actualPixels[1].g
                            && actualPixels[0].b > actualPixels[1].b,
                            "An opaque neutral gap cannot pass the consumer transmission check");
                    }
                }
            } finally {
                scene.width = previousWidth;
            }
        }

        function test_menuHighlightPair_data(): var {
            const rows = [];
            for (const scheme of ["light", "dark"]) {
                for (const contrast of ["normal", "high"]) {
                    for (const accent of ["", "#F5D90A", scheme === "light" ? "#F2F3F5" : "#303237", "#7A1F1F"]) {
                        for (const rtl of [false, true])
                            rows.push({tag: scheme + "-" + contrast + "-" + accent + "-" + rtl,
                                scheme: scheme, contrast: contrast, accent: accent, rtl: rtl});
                    }
                }
            }
            return rows;
        }

        function test_choiceLocalOffTracks_data(): var {
            const rows = [];
            for (const scheme of ["light", "dark"]) {
                for (const contrast of ["normal", "high"])
                    rows.push({tag: scheme + "-" + contrast, scheme: scheme, contrast: contrast});
            }
            return rows;
        }

        function test_choiceLocalOffTracks(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            const control = createTemporaryObject(switchComponent, scene, {
                text: "Local off pair", hoverEnabled: false,
                backgroundColor: "#c0d0e0", foregroundColor: "#111111",
                hoverBackgroundColor: "#b0c0d0", pressedBackgroundColor: "#a0b0c0",
                disabledBackgroundColor: "#909090"
            }) as IASwitch;
            verify(control !== null);
            const indicator = control.indicator as IA.ChoiceIndicator;
            const center = indicator.mark as Rectangle;
            compare(center.color, "#c0d0e0", "An explicit off background owns the inner track");
            indicator.hovered = true;
            compare(center.color, "#b0c0d0");
            indicator.down = true;
            compare(center.color, "#a0b0c0");
            indicator.down = false;
            indicator.hovered = false;
            control.enabled = false;
            compare(center.color, "#909090");
        }

        function menuPairContrast(text: color, fill: color): real {
            const luminances = [];
            for (const value of [text, fill]) {
                compare(value.a, 1);
                const channels = [value.r, value.g, value.b];
                for (let index = 0; index < channels.length; ++index) {
                    const channel = channels[index];
                    channels[index] = channel <= 0.04045 ? channel / 12.92
                        : Math.pow((channel + 0.055) / 1.055, 2.4);
                }
                luminances.push(0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2]);
            }
            return (Math.max(luminances[0], luminances[1]) + 0.05)
                / (Math.min(luminances[0], luminances[1]) + 0.05);
        }

        function test_menuHighlightPair(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast,
                tokens: {colors: data.accent.length > 0 ? {accent: data.accent} : {}}};
            const item = createTemporaryObject(menuItemComponent, scene, {
                x: 20, y: 20, width: 240, text: "Command", checkable: true, checked: true
            }) as IAMenuItem;
            verify(item !== null);
            item.LayoutMirroring.enabled = data.rtl;
            const background = item.background as IA.ControlBackground;
            const mark = item.indicator as IA.Icon;
            verify(background !== null && mark !== null);
            verify(TestProbe.render(item));
            const content = item.contentItem;
            const geometry = [item.x, item.y, item.width, item.height, item.implicitWidth, item.implicitHeight,
                content.x, content.y, content.width, content.height];
            compare(background.color.a, 0, "Checking alone does not add a band");
            verify(mark.visible);
            item.highlighted = true;
            compare(background.color, item.selectedBackgroundColor);
            compare(background.color, IA.Appearance.colors.accent);
            compare(background.foregroundColor, IA.Appearance.colors.onAccent);
            const primary = createTemporaryObject(buttonComponent, scene, {tone: IA.Presentation.Primary, visible: false}) as IAButton;
            verify(primary !== null);
            compare(background.border.color, (primary.background as IA.ControlBackground)
                .resolvedAppearance.stateTable.colors(IA.Presentation.Rest).borderColor);
            const outlined = menuPairContrast(background.color, IA.Appearance.colors.popup) < (data.contrast === "high" ? 7 : 5);
            compare(background.border.width, outlined ? IA.Appearance.metrics.borderWidth : 0);
            verify(TestProbe.render(item));
            const pixels = grabImage(scene);
            fuzzyCompare(samplePixel(pixels, background, background.width / 2, 0.5),
                outlined ? background.border.color : background.color, 2, "Native inward border pixel");
            fuzzyCompare(samplePixel(pixels, background, background.width / 2, background.height - 5),
                background.color, 2, "Exact accent fill pixel");
            compare([item.x, item.y, item.width, item.height, item.implicitWidth, item.implicitHeight,
                content.x, content.y, content.width, content.height], geometry, "No row or content layout shift");
            compare(mark.color, background.foregroundColor);
            const label = findChild(item, "iaLabel") as Text;
            verify(label !== null);
            compare(label.color, background.foregroundColor);
            verify(menuPairContrast(label.color, background.color) >= 7,
                "Actual menu label reaches the 7:1 target on the exact accent");
            background.active = false;
            compare(background.color, item.selectedBackgroundColor, "Menu highlight is not inactive selection");
            compare(label.color, IA.Appearance.colors.onAccent);
            verify(menuPairContrast(label.color, background.color) >= 7);
            item.enabled = false;
            compare(background.color.a, 0, "Disabled wins even over a highlighted checked row");
            compare(background.foregroundColor, item.disabledForegroundColor);
            verify(mark.visible, "Disabling does not erase the checked mark");
            item.inactiveBackgroundColor = "#123456";
            item.inactiveForegroundColor = "#ffffff";
            item.enabled = true;
            compare(background.color, "#123456", "Explicit inactive colors still own that pair");
            compare(background.foregroundColor, "#ffffff");
            item.destroy();
        }

        function visualItemCount(item: Item): int {
            let count = 0;
            for (const child of item.children)
                count += 1 + visualItemCount(child);
            return count;
        }

        function test_pressedInset_data(): var {
            const rows = [];
            const controls = [
                {name: "button", component: buttonComponent, inset: true},
                {name: "tool", component: toolComponent, inset: true},
                {name: "tab", component: tabComponent, inset: false},
                {name: "delegate", component: delegateComponent, inset: false}
            ];
            for (const scheme of ["light", "dark"]) {
                for (const contrast of ["normal", "high"]) {
                    for (const customized of [false, true]) {
                        for (const control of controls)
                            rows.push({tag: scheme + "-" + contrast + "-" + control.name + "-" + customized,
                                scheme: scheme, contrast: contrast, component: control.component,
                                inset: control.inset, customized: customized});
                    }
                }
            }
            return rows;
        }

        function test_pressedInset(data: var): void {
            IA.Appearance.input = data.customized ? {scheme: data.scheme, contrast: data.contrast, reducedMotion: true,
                tokens: {metrics: {pressInsetDepth: 12, pressContentShift: 2},
                         motion: {pressInsetOpacity: 0.4}}} : {scheme: data.scheme, contrast: data.contrast};
            const control = createTemporaryObject(data.component, scene, {
                x: 40, y: 40, width: 200, height: 50, text: "Press"
            }) as T.AbstractButton;
            verify(control !== null);
            verify(TestProbe.render(control));
            verify(TestProbe.activate(control));
            verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
            tryCompare(control, "hovered", false);
            const background = (control instanceof IATabButton
                ? findChild(control.background, "iaSegmentFrame") : control.background) as IA.ControlBackground;
            const content = control.contentItem as IA.IconLabel;
            verify(background !== null && content !== null);
            const inset = findChild(background, "iaPressInset") as Rectangle;
            verify(inset !== null);
            compare(background.color, background.resolvedAppearance.backgroundColor);
            compare(inset.visible, false);
            compare(content.mapToItem(control, 0, 0).y - content.y, 0);
            const geometry = [control.width, control.height, control.implicitWidth, control.implicitHeight,
                content.x, content.y, content.implicitWidth, content.implicitHeight, content.baselineOffset];
            verify(TestProbe.move(control, control.width / 2, control.height / 2));
            tryCompare(control, "hovered", true);
            const hoverFill = background.color;
            compare(hoverFill, background.resolvedAppearance.hoverBackgroundColor);
            verify(TestProbe.press(control, control.width / 2, control.height / 2));
            try {
                tryCompare(control, "down", true);
                verify(TestProbe.render(control));
                compare(background.color, data.inset ? hoverFill : background.resolvedAppearance.pressedBackgroundColor);
                compare(inset.visible, data.inset);
                const shift = data.inset ? IA.Appearance.metrics.pressContentShift : 0;
                compare(content.contentShift, shift);
                fuzzyCompare(content.mapToItem(control, 0, 0).y - content.y, shift, 0.001);
                compare([control.width, control.height, control.implicitWidth, control.implicitHeight,
                    content.x, content.y, content.implicitWidth, content.implicitHeight, content.baselineOffset], geometry);
                if (data.inset) {
                    verify(background.color !== background.resolvedAppearance.pressedBackgroundColor);
                    // Decorative shading is clipped inside the painted border;
                    // unlike real indicator contents, it follows focus width.
                    compare(inset.x, background.border.width);
                    compare(inset.y, background.border.width);
                    compare(inset.radius, Math.max(0, background.radius - background.border.width));
                    const gradient = inset.gradient as Gradient;
                    verify(gradient !== null);
                    compare(gradient.stops.length, 2);
                    compare(gradient.stops[0].position, 0);
                    compare(gradient.stops[0].color, Qt.rgba(0, 0, 0, IA.Appearance.motion.pressInsetOpacity));
                    compare(gradient.stops[1].position,
                        Math.min(0.5, IA.Appearance.metrics.pressInsetDepth / Math.max(1, inset.height)));
                    compare(gradient.stops[1].color, IA.Appearance.colors.transparent);
                    // A held inset button follows its actual hover state, never the pressed fill.
                    compare(background.color, control.hovered
                        ? background.resolvedAppearance.hoverBackgroundColor
                        : background.resolvedAppearance.backgroundColor);
                    if (data.customized) {
                        control.enabled = false;
                        verify(TestProbe.render(control));
                        compare(inset.visible, false);
                        compare(content.contentShift, 0);
                        compare(background.color, background.resolvedAppearance.disabledBackgroundColor);
                    }
                }
            } finally {
                TestProbe.move(scene, scene.width - 2, scene.height - 2);
                TestProbe.release(scene, scene.width - 2, scene.height - 2);
            }
            tryCompare(control, "down", false);
            verify(TestProbe.render(control));
            compare(inset.visible, false);
            compare(content.contentShift, 0);
            fuzzyCompare(content.mapToItem(control, 0, 0).y - content.y, 0, 0.001);
        }

        function test_focusedKeyboardPressInset_data(): var {
            const rows = [];
            for (const mode of test_choiceLocalOffTracks_data()) {
                for (const component of [buttonComponent, toolComponent])
                    rows.push({tag: mode.tag + (component === buttonComponent ? "-button" : "-tool"),
                        scheme: mode.scheme, contrast: mode.contrast, component: component});
            }
            return rows;
        }

        function test_focusedKeyboardPressInset(data: var): void {
            IA.Appearance.input = {scheme: data.scheme, contrast: data.contrast};
            const control = createTemporaryObject(data.component, scene, {
                x: 40, y: 40, width: 240, height: 60, text: "Keyboard press", hoverEnabled: false
            }) as T.AbstractButton;
            verify(control !== null);
            verify(TestProbe.render(control));
            verify(TestProbe.activate(control));
            tryCompare(TestProbe.hostWindow(control), "active", true);
            verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
            control.forceActiveFocus(Qt.TabFocusReason);
            tryCompare(control, "visualFocus", true);
            const background = control.background as IA.ControlBackground;
            const inset = findChild(background, "iaPressInset") as Rectangle;
            verify(background !== null && inset !== null);
            verify(TestProbe.render(control));
            const restingWidth = background.outlined ? IA.Appearance.metrics.borderWidth : 0;
            const geometry = [inset.x, inset.y, inset.width, inset.height, inset.radius];
            compare(background.border.width, IA.Appearance.metrics.focusWidth);
            compare(geometry, [background.border.width, background.border.width,
                background.width - 2 * background.border.width, background.height - 2 * background.border.width,
                Math.max(0, background.radius - background.border.width)],
                "Focused decorative shading is clipped inside the painted border");
            verify(background.border.width > restingWidth);
            // Keep the original inward-border sample that caught overpainting
            // when decorative shading incorrectly used a resting-only inset.
            const borderPoint = background.mapToItem(scene, background.width / 2,
                (restingWidth + background.border.width) / 2);
            const gradientPoint = background.mapToItem(scene, background.width / 4,
                background.border.width + 1.5);
            const before = grabImage(scene);
            const scaleX = before.width / scene.width;
            const scaleY = before.height / scene.height;
            verify(before.width > 0 && before.height > 0);
            fuzzyCompare(before.pixel(Math.floor(borderPoint.x * scaleX), Math.floor(borderPoint.y * scaleY)),
                background.border.color, 0.03, "Focused border renders before the held key press");
            const unpressed = before.pixel(Math.floor(gradientPoint.x * scaleX), Math.floor(gradientPoint.y * scaleY));
            keyPress(Qt.Key_Space);
            try {
                tryCompare(control, "down", true);
                tryCompare(control, "visualFocus", true);
                verify(TestProbe.render(control));
                compare(inset.visible, true);
                compare([inset.x, inset.y, inset.width, inset.height, inset.radius], geometry,
                    "A held keyboard press retains border-clipped paint geometry");
                const gradient = inset.gradient as Gradient;
                verify(gradient !== null);
                compare(gradient.stops.length, 2);
                compare(gradient.stops[0].color, Qt.rgba(0, 0, 0, IA.Appearance.motion.pressInsetOpacity));
                compare(gradient.stops[1].position,
                    Math.min(0.5, IA.Appearance.metrics.pressInsetDepth / Math.max(1, inset.height)));
                compare(gradient.stops[1].color, IA.Appearance.colors.transparent);
                const held = grabImage(scene);
                fuzzyCompare(held.pixel(Math.floor(borderPoint.x * scaleX), Math.floor(borderPoint.y * scaleY)),
                    background.border.color, 0.03, "The held press gradient must not tint the inward focus border");
                compare(held.pixel(Math.floor(borderPoint.x * scaleX), Math.floor(borderPoint.y * scaleY)),
                    before.pixel(Math.floor(borderPoint.x * scaleX), Math.floor(borderPoint.y * scaleY)),
                    "The focused border pixel is exactly unchanged during the held key");
                const pressed = held.pixel(Math.floor(gradientPoint.x * scaleX), Math.floor(gradientPoint.y * scaleY));
                compare(pressed.a, unpressed.a);
                verify(pressed.r <= unpressed.r && pressed.g <= unpressed.g && pressed.b <= unpressed.b);
                verify(pressed.r < unpressed.r - 0.01 || pressed.g < unpressed.g - 0.01
                    || pressed.b < unpressed.b - 0.01, "The press gradient actually shades the inner surface");
            } finally {
                keyRelease(Qt.Key_Space);
            }
            tryCompare(control, "down", false);
            verify(TestProbe.render(control));
            compare(inset.visible, false);
            compare([inset.x, inset.y, inset.width, inset.height, inset.radius], geometry);
            const after = grabImage(scene);
            fuzzyCompare(after.pixel(Math.floor(borderPoint.x * scaleX), Math.floor(borderPoint.y * scaleY)),
                background.border.color, 0.03, "Release retains the focused border");
            compare(after.pixel(Math.floor(borderPoint.x * scaleX), Math.floor(borderPoint.y * scaleY)),
                before.pixel(Math.floor(borderPoint.x * scaleX), Math.floor(borderPoint.y * scaleY)),
                "Release preserves the exact focused border pixel");
            fuzzyCompare(after.pixel(Math.floor(gradientPoint.x * scaleX), Math.floor(gradientPoint.y * scaleY)),
                unpressed, 0.03, "Release removes the press gradient");
        }

        function verifySelectedForeground(control: var, foreground: color): void {
            compare(foreground, TestProbe.hostWindow(control).active
                    ? control.selectedForegroundColor : control.inactiveForegroundColor,
                    "Actual selected mark/text uses the resolved foreground for the window state");
            compare(foreground.a, 1);
            compare(foreground.r, foreground.g, "A black seed retains a neutral foreground");
            compare(foreground.g, foreground.b, "A black seed retains a neutral foreground");
            // The asserted surface is opaque black and all foreground channels are equal.
            for (const channel of [foreground.r, Math.round(foreground.r * 255) / 255]) {
                const luminance = channel <= 0.04045 ? channel / 12.92
                    : Math.pow((channel + 0.055) / 1.055, 2.4);
                const ratio = (luminance + 0.05) / 0.05;
                verify(ratio >= 7, "Rendered selected foreground/black meets the label target (full/RGB8): " + ratio);
            }
        }

        function test_pressedCheckedPair_data(): var {
            return [
                {tag: "button", component: buttonComponent, choice: false, inset: true},
                {tag: "tool", component: toolComponent, choice: false, inset: true},
                {tag: "tab", component: tabComponent, choice: false},
                {tag: "delegate", component: delegateComponent, choice: false},
                {tag: "check", component: checkComponent, choice: true},
                {tag: "partialCheck", component: checkComponent, choice: true, partial: true},
                {tag: "radio", component: radioComponent, choice: true},
                {tag: "switch", component: switchComponent, choice: true}
            ];
        }

        function test_pressedCheckedPair(data: var): void {
            const properties = {
                x: 40, y: 40, width: 200, height: 50, text: "Selected",
                checkable: true, checked: true,
                backgroundColor: "#eeeeee", foregroundColor: "#111111",
                selectedBackgroundColor: "#000000",
                inactiveBackgroundColor: "#000000"
            };
            if (!data.inset)
                properties.pressedBackgroundColor = "#dddddd";
            const control = createTemporaryObject(data.component, scene, properties) as T.AbstractButton;
            verify(control !== null);
            if (data.partial)
                (control as IACheckBox).checkState = Qt.PartiallyChecked;
            verify(TestProbe.render(control));
            verify(TestProbe.activate(control));
            const background = (data.choice ? control.indicator : control instanceof IATabButton
                ? findChild(control.background, "iaSegmentFrame") : control.background) as Rectangle;
            verify(background !== null);
            let foreground = null;
            if (!data.choice) {
                foreground = findChild(control, "iaLabel") as Text;
            } else {
                const indicator = control.indicator as IA.ChoiceIndicator;
                foreground = control instanceof IASwitch ? findChild(indicator, "iaThumb")
                    : control instanceof IACheckBox ? indicator.mark.children[0] : null;
            }
            verify(foreground !== null || control instanceof IARadioButton);
            const center = data.choice ? (control.indicator as IA.ChoiceIndicator).mark as Rectangle : null;
            if (data.choice) {
                compare(background.color.a, 0);
                compare(center.color, "#000000");
                verifySelectedForeground(control, (control.indicator as IA.ChoiceIndicator).markColor);
            } else {
                compare(background.color, "#000000");
                verifySelectedForeground(control, foreground.color);
            }
            verify(TestProbe.press(control, control.width / 2, control.height / 2));
            try {
                tryCompare(control, "down", true);
                verify(TestProbe.render(control));
                if (data.inset) {
                    const surface = background as IA.ControlBackground;
                    const state = TestProbe.hostWindow(control).active ? IA.Presentation.SelectedHover : IA.Presentation.InactiveHover;
                    compare(background.color, surface.resolvedAppearance.stateTable.colors(state).backgroundColor);
                    verify(background.color !== "#000000", "Checked hover composes with the inset press");
                } else if (data.choice) {
                    const indicator = control.indicator as IA.ChoiceIndicator;
                    compare(center.color, indicator.colors.choiceCenterColor);
                    verify(center.color.toString() !== "#dddddd", "An on choice retains its on pair when pressed");
                    verifySelectedForeground(control, indicator.markColor);
                    if (foreground !== null)
                        compare(foreground.color, indicator.markColor, "The actual glyph/thumb consumes the pressed on pair");
                } else {
                    compare(background.color, "#dddddd");
                }
                const inset = findChild(control.background, "iaPressInset") as Rectangle;
                verify(inset !== null);
                compare(inset.visible, Boolean(data.inset));
                if (data.inset)
                    verifySelectedForeground(control, foreground.color);
                else if (!data.choice)
                    compare(foreground.color, "#111111", "Pressed fill retains its paired foreground");
            } finally {
                TestProbe.move(scene, scene.width - 2, scene.height - 2);
                TestProbe.release(scene, scene.width - 2, scene.height - 2);
            }
            tryCompare(control, "down", false);
            verify(TestProbe.render(control));
            // Native Switch keeps its press outside the bounds and toggles on release.
            if (control instanceof IASwitch) {
                compare(control.checked, false);
                compare(background.color.a, 0);
                compare(center.color, "#eeeeee", "The explicit off background still reaches the inner track");
                compare(foreground.color, "#111111");
            } else if (data.choice) {
                compare(center.color, "#000000");
                verifySelectedForeground(control, (control.indicator as IA.ChoiceIndicator).markColor);
            } else {
                compare(background.color, "#000000");
                verifySelectedForeground(control, foreground.color);
            }
        }
    }
}
