// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: scene
    width: 520
    height: 220

    Item { id: focusEntry }

    Row {
        id: buttons
        x: 32
        y: 32
        spacing: 24
        IAButton { id: neutral; text: "Action" }
        IAButton { id: solid; text: "Action"; checkable: true }
        IAButton { id: customized; text: "Action" }
    }
    IALabel {
        id: semanticLabel
        anchors.left: buttons.left
        anchors.top: buttons.bottom
        anchors.topMargin: 24
        text: "Semantic label"
    }
    IASlider { id: slider; x: 32; y: 120; width: 240; value: 0.37 }
    IAProgressBar { id: progress; x: 32; y: 170; width: 240; value: 0.37 }

    TestCase {
        name: "SolidButtons"
        when: windowShown

        function init(): void {
            IA.Appearance.input = {};
            solid.resetAppearance();
            solid.enabled = true;
            solid.checked = false;
            neutral.enabled = true;
            customized.resetAppearance();
            customized.backgroundColor = "purple";
            customized.foregroundColor = "white";
            customized.borderColor = "purple";
            semanticLabel.enabled = true;
            slider.resetAppearance();
            progress.resetAppearance();
            verify(TestProbe.render(scene));
            verify(TestProbe.activate(scene));
            tryCompare(TestProbe.hostWindow(scene), "active", true);
            focusEntry.forceActiveFocus(Qt.OtherFocusReason);
            verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
        }

        function cleanup(): void {
            TestProbe.move(scene, scene.width - 2, scene.height - 2);
            TestProbe.release(scene, scene.width - 2, scene.height - 2);
            solid.enabled = true;
            neutral.enabled = true;
            IA.Appearance.input = {};
        }

        function geometry(button: IAButton): var {
            const background = button.background as Rectangle;
            const label = findChild(button, "iaLabel") as Text;
            verify(background !== null && label !== null);
            const origin = label.mapToItem(button, 0, 0);
            return [button.width, button.height, button.implicitWidth, button.implicitHeight,
                    button.leftPadding, button.rightPadding, button.topPadding, button.bottomPadding,
                    background.x, background.y, background.width, background.height,
                    background.radius, background.border.width, origin.x, origin.y,
                    label.width, label.height, label.baselineOffset,
                    label.font.pixelSize, label.font.weight];
        }

        function contrastRatio(foreground: color, background: color): real {
            // Independent WCAG sRGB calculation; these default-profile pairs are opaque.
            const luminances = [];
            for (const value of [foreground, background]) {
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

        function verifyGlobalContrast(): void {
            const colors = IA.Appearance.colors;
            const textPairs = [
                ["tooltipText", "tooltipBackground"],
                ["inverseText", "inverseSurface"], ["inverseSecondary", "inverseSurface"]
            ];
            for (const surface of ["canvas", "content", "popup", "control", "controlHover",
                                   "controlPressed", "selection", "inactiveSelection"])
                textPairs.push(["text", surface]);
            for (const text of ["secondaryText", "placeholderText", "link", "accentText",
                                "errorText", "warningText", "successText"])
                textPairs.push([text, "content"]);
            for (const role of ["error", "warning", "success"])
                textPairs.push([role + "Text", role + "Surface"]);
            for (const role of ["accent", "error", "warning"]) {
                const text = "on" + role.charAt(0).toUpperCase() + role.slice(1);
                for (const state of ["", "Hover", "Pressed"])
                    textPairs.push([text, role + state]);
            }

            const expected = {};
            for (const role of ["primary", "caution", "destructive"]) {
                compare(colors[role + "Outline"], colors[role + "Text"]);
                for (const state of ["Surface", "Hover", "Pressed"])
                    textPairs.push([role + "Text", role + state]);
                for (const background of [role + "Surface"]) {
                    const ratio = contrastRatio(colors[role + "Outline"], colors[background]);
                    // Diagnostic tolerance is 0.001; no text-pair tolerance is applied below.
                    if (ratio + 0.001 < 3)
                        expected["tokens.colors." + role + "Outline/" + background] = ratio;
                }
            }
            for (const pair of textPairs) {
                const fg = colors[pair[0]], bg = colors[pair[1]];
                const ratio = contrastRatio(fg, bg);
                verify(ratio >= 5, pair.join("/") + " contrast: " + ratio);
                const roundedRatio = contrastRatio(
                    Qt.rgba(Math.round(fg.r * 255) / 255, Math.round(fg.g * 255) / 255, Math.round(fg.b * 255) / 255, 1),
                    Qt.rgba(Math.round(bg.r * 255) / 255, Math.round(bg.g * 255) / 255, Math.round(bg.b * 255) / 255, 1));
                verify(roundedRatio >= 5, pair.join("/") + " 8-bit contrast: " + roundedRatio);
            }
            for (const role of ["outline", "focus"])
                verify(contrastRatio(colors[role], colors.content) >= 3, role + "/content");

            const diagnostics = IA.Appearance.diagnostics;
            compare(diagnostics.length, Object.keys(expected).length,
                    "Expected outline diagnostics: " + Object.keys(expected).join(", "));
            for (const diagnostic of diagnostics) {
                verify(expected[diagnostic.path] !== undefined, "Unexpected or duplicate diagnostic: " + diagnostic.path);
                compare(diagnostic.code, "low-contrast");
                compare(diagnostic.severity, "warning");
                compare(diagnostic.target, 3);
                fuzzyCompare(diagnostic.ratio, expected[diagnostic.path], 0.001, diagnostic.path);
                delete expected[diagnostic.path];
            }
            compare(Object.keys(expected).length, 0, "No measured outline failure is missing its diagnostic");
        }

        function verifyVisual(button: IAButton, backgroundColor: color, foregroundColor: color,
                              expectedGeometry: var): void {
            verify(TestProbe.render(button));
            const background = button.background as Rectangle;
            const label = findChild(button, "iaLabel") as Text;
            verify(background !== null && label !== null);
            verify(background.visible && label.visible);
            compare(background.color, backgroundColor, "Actual background Rectangle");
            compare(label.color, foregroundColor, "Actual IconLabel Text");
            if (button.enabled) {
                verify(contrastRatio(label.color, background.color) >= 5, "Rendered normal text meets the 5:1 target");
                const fg = label.color, bg = background.color;
                const roundedRatio = contrastRatio(
                    Qt.rgba(Math.round(fg.r * 255) / 255, Math.round(fg.g * 255) / 255, Math.round(fg.b * 255) / 255, 1),
                    Qt.rgba(Math.round(bg.r * 255) / 255, Math.round(bg.g * 255) / 255, Math.round(bg.b * 255) / 255, 1));
                verify(roundedRatio >= 5, "Rendered 8-bit normal text contrast: " + roundedRatio);
            }
            const focused = button.visualFocus;
            const focusBorder = contrastRatio(button.focusColor, background.color) >= 3
                ? button.focusColor : label.color;
            compare(background.border.color, focused ? focusBorder
                : button.enabled ? button.borderColor : IA.Appearance.colors.outline);
            if (button === solid && button.enabled && !focused)
                compare(background.border.color, label.color, "Semantic border matches rendered text");
            const pressed = button.enabled && button.down;
            const inset = findChild(background, "iaPressInset") as Rectangle;
            verify(inset !== null);
            compare(inset.visible, pressed);
            const translatedGeometry = expectedGeometry.slice();
            translatedGeometry[13] = focused ? IA.Appearance.metrics.focusWidth : IA.Appearance.metrics.borderWidth;
            translatedGeometry[15] += pressed ? IA.Appearance.metrics.pressContentShift : 0;
            compare(geometry(button), translatedGeometry, "Only the rendered content shifts during press; layout stays fixed");
        }

        function test_renderedStates_data(): var {
            return [
                {tag: "light-primary", scheme: "light", tone: IA.Presentation.Primary,
                    role: "primary", labelRole: "accentText"},
                {tag: "dark-primary", scheme: "dark", tone: IA.Presentation.Primary,
                    role: "primary", labelRole: "accentText"},
                {tag: "light-caution", scheme: "light", tone: IA.Presentation.Caution,
                    role: "caution", labelRole: "warningText"},
                {tag: "dark-caution", scheme: "dark", tone: IA.Presentation.Caution,
                    role: "caution", labelRole: "warningText"},
                {tag: "light-destructive", scheme: "light", tone: IA.Presentation.Destructive,
                    role: "destructive", labelRole: "errorText"},
                {tag: "dark-destructive", scheme: "dark", tone: IA.Presentation.Destructive,
                    role: "destructive", labelRole: "errorText"}
            ];
        }

        function test_valueFills_data(): var {
            return [{tag: "light", scheme: "light"}, {tag: "dark", scheme: "dark"}];
        }

        function test_valueFills(data: var): void {
            IA.Appearance.input = {scheme: data.scheme};
            slider.resetAppearance();
            progress.resetAppearance();
            verify(TestProbe.render(scene));
            const sliderFill = findChild(slider, "iaFill") as Rectangle;
            const progressFill = findChild(progress, "iaFill") as Rectangle;
            verify(sliderFill !== null && progressFill !== null);
            compare(sliderFill.color, IA.Appearance.colors.accent);
            compare(progressFill.color, IA.Appearance.colors.accent);
            compare(sliderFill.color, "#5c9cf5");
            const widths = [sliderFill.width, progressFill.width];
            // Value fills follow accent, not a separately overridden button surface.
            IA.Appearance.input = {scheme: data.scheme, tokens: {colors: {
                accent: "#70c8aa", primarySurface: "#b48ae0"
            }}};
            verify(TestProbe.render(scene));
            verify(findChild(slider, "iaFill") === sliderFill);
            verify(findChild(progress, "iaFill") === progressFill);
            compare(sliderFill.color, "#70c8aa");
            compare(progressFill.color, "#70c8aa");
            verify(sliderFill.color !== IA.Appearance.colors.primarySurface);
            compare([sliderFill.width, progressFill.width], widths);
            compare(slider.value, 0.37);
            compare(progress.value, 0.37);
            slider.fillColor = "purple";
            progress.fillColor = "purple";
            IA.Appearance.input = {scheme: data.scheme === "light" ? "dark" : "light",
                tokens: {colors: {accent: "#d8a5ed"}}};
            verify(TestProbe.render(scene));
            compare(sliderFill.color, "#800080");
            compare(progressFill.color, "#800080");
            compare([sliderFill.width, progressFill.width], widths);
            slider.resetAppearance();
            progress.resetAppearance();
            verify(TestProbe.render(scene));
            compare(sliderFill.color, IA.Appearance.colors.accent);
            compare(progressFill.color, IA.Appearance.colors.accent);
            compare(sliderFill.color, "#d8a5ed");
            compare([sliderFill.width, progressFill.width], widths);
        }

        function test_renderedStates(data: var): void {
            solid.tone = data.tone;
            customized.tone = data.tone;
            semanticLabel.tone = data.tone;
            IA.Appearance.input = {scheme: data.scheme};
            verify(TestProbe.render(scene));
            const originalGeometry = geometry(solid);
            compare(geometry(neutral), originalGeometry, "Tone does not change font, padding or size");
            compare(geometry(customized), originalGeometry);
            const labelSize = Qt.size(semanticLabel.width, semanticLabel.height);

            for (const scheme of [data.scheme, data.scheme === "light" ? "dark" : "light"]) {
                IA.Appearance.input = {scheme: scheme};
                const colors = IA.Appearance.colors;
                const surface = colors[data.role + "Surface"];
                const hover = solid.hoverBackgroundColor;
                const selected = solid.selectedBackgroundColor;
                const text = colors[data.role + "Text"];
                const outline = colors[data.role + "Outline"];
                verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
                tryCompare(solid, "hovered", false);
                compare(solid.checked, false);
                compare(solid.borderColor, outline);
                compare(neutral.borderColor, colors.outline);
                verifyVisual(solid, surface, text, originalGeometry);
                if (data.role === "primary")
                    compare((solid.background as Rectangle).color, "#5c9cf5", "Exact default primary in both schemes");
                else
                    compare((solid.background as Rectangle).color,
                        data.role === "caution" ? colors.warning : colors.error, "Final generated semantic fill reaches the control");
                verifyVisual(neutral, colors.control, colors.text, originalGeometry);
                verifyVisual(customized, "purple", "white", originalGeometry);
                compare(customized.borderColor, "#800080");
                compare(semanticLabel.color, colors[data.labelRole]);
                compare((semanticLabel.background as Rectangle).color, colors.transparent);
                compare(Qt.size(semanticLabel.width, semanticLabel.height), labelSize);
                compare(solid.appearanceDiagnostics.length, 0);
                verifyGlobalContrast();

                verify(TestProbe.move(solid, solid.width / 2, solid.height / 2));
                tryCompare(solid, "hovered", true);
                verifyVisual(solid, hover, text, originalGeometry);
                verify(TestProbe.press(solid, solid.width / 2, solid.height / 2));
                try {
                    tryCompare(solid, "down", true);
                    verifyVisual(solid, hover, text, originalGeometry);
                } finally {
                    verify(TestProbe.release(solid, solid.width / 2, solid.height / 2));
                }
                tryCompare(solid, "down", false);
                tryCompare(solid, "checked", true);
                verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
                verifyVisual(solid, selected, text, originalGeometry);

                // Enter with a real Tab so the single keyboard focus border is exercised.
                verify(TestProbe.move(neutral, neutral.width / 2, neutral.height / 2));
                tryCompare(neutral, "hovered", true);
                verifyVisual(neutral, neutral.hoverBackgroundColor, colors.text, originalGeometry);
                verify(TestProbe.press(neutral, neutral.width / 2, neutral.height / 2));
                try {
                    tryCompare(neutral, "down", true);
                    verifyVisual(neutral, neutral.hoverBackgroundColor, colors.text, originalGeometry);
                } finally {
                    verify(TestProbe.release(neutral, neutral.width / 2, neutral.height / 2));
                }
                tryCompare(neutral, "down", false);
                tryCompare(neutral, "activeFocus", true);
                verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
                verify(TestProbe.keyClick(neutral, Qt.Key_Tab));
                tryCompare(solid, "activeFocus", true);
                tryCompare(solid, "visualFocus", true);
                verifyVisual(solid, selected, text, originalGeometry);
                const background = solid.background as Rectangle;
                for (const child of background.children) {
                    const rectangle = child as Rectangle;
                    if (rectangle !== null)
                        compare(rectangle.objectName, "iaPressInset", "Only the press inset, no additional focus perimeter");
                }
                compare(background.border.color, contrastRatio(colors.focus, selected) >= 3 ? colors.focus : text);
                compare(background.border.width, IA.Appearance.metrics.focusWidth);
                verify(TestProbe.keyClick(solid, Qt.Key_Space));
                tryCompare(solid, "checked", false);
                verifyVisual(solid, surface, text, originalGeometry);

                solid.enabled = false;
                neutral.enabled = false;
                semanticLabel.enabled = false;
                verify(TestProbe.click(solid, solid.width / 2, solid.height / 2));
                compare(solid.checked, false);
                verifyVisual(solid, colors.disabledSurface, colors.disabledText, originalGeometry);
                compare(semanticLabel.color, colors.disabledText);
                verifyVisual(neutral, colors.disabledSurface, colors.disabledText, originalGeometry);
                solid.enabled = true;
                neutral.enabled = true;
                semanticLabel.enabled = true;
                verify(TestProbe.move(scene, scene.width - 2, scene.height - 2));
                tryCompare(solid, "hovered", false);
                verifyVisual(solid, surface, text, originalGeometry);
            }

            customized.resetAppearance();
            verifyVisual(customized, solid.backgroundColor, solid.foregroundColor, originalGeometry);
            compare(customized.borderColor, solid.borderColor);
        }
    }
}
