// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: scene
    width: 640
    height: 820
    IAButton { id: customized; x: 40; y: 40; text: "Publish"; backgroundColor: "purple" }
    IAButton { id: ordinary; x: 240; y: 40; text: "Default" }
    Component {
        id: mixedComponent
        MixedFixture {
            width: scene.Window.window ? scene.Window.window.width : scene.width
            height: scene.Window.window ? scene.Window.window.height : scene.height
        }
    }
    TestCase {
        name: "AppearanceContract"
        when: windowShown

        function init(): void {
            IA.Appearance.input = {};
            customized.resetAppearance();
            ordinary.resetAppearance();
            ordinary.enabled = true;
            ordinary.checkable = false;
            ordinary.checked = false;
            mouseMove(scene, scene.width - 1, scene.height - 1);
        }

        function test_renderedAppearanceAliasBlocks_data(): var {
            const type = ["fontFamily", "fontPixelSize", "fontWeight"];
            const surface = ["backgroundColor", "foregroundColor", "borderColor", "disabledBorderColor", "controlRadius"].concat(type);
            const interaction = ["backgroundColor", "foregroundColor", "hoverBackgroundColor", "pressedBackgroundColor",
                "disabledBackgroundColor", "disabledForegroundColor", "inactiveBackgroundColor", "inactiveForegroundColor",
                "selectedBackgroundColor", "selectedForegroundColor", "borderColor", "disabledBorderColor", "focusColor", "controlRadius"]
                .concat(type, ["minimumHeight"]);
            const rows = [];
            function add(names, properties) {
                for (const name of names)
                    rows.push({tag: name, control: name, properties: properties.concat(["appearanceDiagnostics"])});
            }
            add(["IAButton", "IAToolButton"], interaction.filter(function(name) { return name !== "pressedBackgroundColor"; }));
            add(["IAItemDelegate", "IAMenuItem", "IATabButton", "IACheckBox", "IARadioButton", "IASwitch"], interaction);
            add(["IAPane", "IAFrame", "IAMenu", "IAToolTip"], surface);
            add(["IAPopup", "IADialog"], surface.slice(0, 4).concat(["focusColor"], surface.slice(4)));
            add(["IATabBar"], surface.filter(function(name) { return name !== "foregroundColor"; }));
            add(["IAScrollView"], ["backgroundColor", "borderColor", "disabledBorderColor", "controlRadius"]);
            add(["IAToolBar"], ["backgroundColor", "foregroundColor", "borderColor"].concat(type));
            add(["IADialogButtonBox"], ["backgroundColor", "foregroundColor", "controlRadius"].concat(type));
            add(["IALabel"], ["backgroundColor", "foregroundColor", "disabledForegroundColor", "controlRadius"].concat(type));
            add(["IAToolSeparator", "IAMenuSeparator"], ["foregroundColor"]);
            add(["IAScrollBar"], ["backgroundColor", "foregroundColor", "hoverBackgroundColor", "pressedBackgroundColor",
                "disabledForegroundColor", "controlRadius"]);
            add(["IAProgressBar"], ["backgroundColor", "fillColor", "foregroundColor", "disabledBackgroundColor", "disabledForegroundColor", "controlRadius"]);
            add(["IASlider"], ["backgroundColor", "fillColor", "foregroundColor", "hoverBackgroundColor", "pressedBackgroundColor",
                "disabledBackgroundColor", "disabledForegroundColor", "borderColor", "disabledBorderColor", "focusColor", "controlRadius", "minimumHeight"]);
            add(["IATextField"], ["backgroundColor", "foregroundColor", "disabledBackgroundColor", "disabledForegroundColor",
                "borderColor", "disabledBorderColor", "focusColor", "controlRadius"].concat(type, ["minimumHeight"]));
            return rows;
        }

        function test_renderedAppearanceAliasBlocks(data: var): void {
            const component = Qt.createComponent("qrc:/qt/qml/InterActive/" + data.control + ".qml");
            tryCompare(component, "status", Component.Ready);
            const control = createTemporaryObject(component, scene);
            verify(control !== null);
            compare(TestProbe.appearanceProperties(control), data.properties, data.control + " rendered-property set and declaration order");
        }

        function test_overrideAndReset(): void {
            customized.backgroundColor = "purple";
            customized.fontPixelSize = 18;
            customized.controlRadius = 9;
            const light = String(ordinary.backgroundColor);
            IA.Appearance.input = {scheme: "dark"};
            compare(customized.backgroundColor, "#800080");
            compare(customized.fontPixelSize, 18);
            compare(customized.controlRadius, 9);
            verify(String(ordinary.backgroundColor) !== light);
            compare((customized.background as Rectangle).color, customized.backgroundColor);
            const label = findChild(customized, "iaLabel") as Text;
            verify(label !== null);
            compare(label.font.pixelSize, 18);
            compare((customized.background as Rectangle).radius, 9);
            customized.resetAppearance();
            compare(customized.backgroundColor, ordinary.backgroundColor);
            compare(customized.fontPixelSize, ordinary.fontPixelSize);
            IA.Appearance.input = {};
            compare(customized.backgroundColor, light);
        }

        function test_partialInputAndRenderedReset(): void {
            customized.backgroundColor = "purple";
            customized.fontPixelSize = 18;
            customized.controlRadius = 9;
            const label = findChild(customized, "iaLabel") as Text;
            verify(label !== null);
            IA.Appearance.input = {tokens: {colors: {control: "#ccddee"},
                                             typography: {controlSize: 21},
                                             metrics: {controlRadius: 7}}};
            compare((customized.background as Rectangle).color, "#800080");
            compare(label.font.pixelSize, 18);
            compare((customized.background as Rectangle).radius, 9);
            compare((ordinary.background as Rectangle).color, "#ccddee");
            const ordinaryLabel = findChild(ordinary, "iaLabel") as Text;
            verify(ordinaryLabel !== null);
            compare(ordinaryLabel.font.pixelSize, 21);
            compare((ordinary.background as Rectangle).radius, 7);
            customized.resetAppearance();
            compare(customized.backgroundColor, ordinary.backgroundColor);
            compare(customized.fontPixelSize, ordinary.fontPixelSize);
            compare(label.font.pixelSize, ordinaryLabel.font.pixelSize);
            compare((customized.background as Rectangle).radius, 7);
        }

        function test_equalDefaultIsStillOverride(): void {
            const initial = String(ordinary.backgroundColor);
            customized.backgroundColor = initial;
            IA.Appearance.input = {scheme: "dark"};
            compare(customized.backgroundColor, initial);
            verify(String(ordinary.backgroundColor) !== initial);
            customized.backgroundColor = undefined;
            compare(customized.backgroundColor, ordinary.backgroundColor);
        }

        function test_buttonInput(): void {
            const before = ordinary.checked;
            ordinary.checkable = true;
            mouseClick(ordinary);
            compare(ordinary.checked, !before);
            ordinary.forceActiveFocus(Qt.TabFocusReason);
            keyClick(Qt.Key_Space);
            compare(ordinary.checked, before);
            ordinary.enabled = false;
            mouseClick(ordinary);
            compare(ordinary.checked, before);
            compare((ordinary.background as Rectangle).color, ordinary.disabledBackgroundColor);
            ordinary.enabled = true;
            ordinary.checkable = false;
        }
    }

    TestCase {
        id: mixedTests
        name: "MixedControls"
        when: windowShown
        property MixedFixture fixture

        function init(): void {
            IA.Appearance.input = {};
            customized.visible = false;
            ordinary.visible = false;
            fixture = createTemporaryObject(mixedComponent, scene) as MixedFixture;
            verify(fixture !== null);
            verify(waitForRendering(fixture));
            verify(TestProbe.activate(fixture));
        }

        function cleanup(): void {
            IA.Appearance.input = {};
            customized.visible = true;
            ordinary.visible = true;
        }

        function click(item: Item): void {
            verify(TestProbe.click(item, item.width / 2, item.height / 2));
        }

        function replaceName(text: string): void {
            click(fixture.editor);
            tryCompare(fixture.editor, "activeFocus", true);
            verify(TestProbe.keyClick(fixture.editor, Qt.Key_A, Qt.ControlModifier));
            verify(TestProbe.typeAscii(fixture.editor, text));
            compare(fixture.editor.text, text);
        }

        function test_editCommitAndKeyboard(): void {
            replaceName("Avery Stone");
            compare(fixture.committedSummary.text, "Saved: Maya Chen");
            click(fixture.applyButton);
            compare(fixture.committedSummary.text, "Saved: Avery Stone");
            replaceName("Morgan Lee");
            fixture.applyButton.forceActiveFocus(Qt.TabFocusReason);
            tryCompare(fixture.applyButton, "activeFocus", true);
            verify(TestProbe.keyClick(fixture.applyButton, Qt.Key_Space));
            compare(fixture.committedSummary.text, "Saved: Morgan Lee");

            replaceName("Uncommitted draft");
            click(fixture.disabledButton);
            compare(fixture.committedSummary.text, "Saved: Morgan Lee");
            fixture.applyButton.forceActiveFocus(Qt.TabFocusReason);
            fixture.applyButton.enabled = false;
            verify(TestProbe.keyClick(fixture.applyButton, Qt.Key_Space));
            compare(fixture.committedSummary.text, "Saved: Morgan Lee");
            compare((fixture.applyButton.background as Rectangle).color,
                    fixture.applyButton.disabledBackgroundColor);
        }

        function test_cancelledPressDoesNotCommit(): void {
            replaceName("Uncommitted draft");
            const button = fixture.applyButton;
            verify(TestProbe.press(button, button.width / 2, button.height / 2));
            verify(TestProbe.move(fixture, fixture.width - 2, fixture.height - 2));
            verify(TestProbe.release(fixture, fixture.width - 2, fixture.height - 2));
            compare(fixture.committedSummary.text, "Saved: Maya Chen");
        }

        function test_selectReplaceUndo(): void {
            replaceName("Replacement");
            verify(TestProbe.keyClick(fixture.editor, Qt.Key_Z, Qt.ControlModifier));
            compare(fixture.editor.text, "Maya Chen");
            compare(fixture.committedSummary.text, "Saved: Maya Chen");
            replaceName("Final draft");
            click(fixture.applyButton);
            compare(fixture.committedSummary.text, "Saved: Final draft");
        }

        function test_choicesAndRadioExclusivity(): void {
            compare(fixture.choiceSummary.text, "Alerts disabled");
            click(fixture.check);
            compare(fixture.choiceSummary.text, "Alerts enabled");
            fixture.check.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(fixture.check, Qt.Key_Space));
            compare(fixture.choiceSummary.text, "Alerts disabled");

            click(fixture.radioTwo);
            verify(fixture.radioTwo.checked);
            verify(!fixture.radioOne.checked);
            compare(fixture.radioSummary.text, "All updates");
            fixture.radioOne.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(fixture.radioOne, Qt.Key_Space));
            verify(fixture.radioOne.checked);
            verify(!fixture.radioTwo.checked);
            compare(fixture.radioSummary.text, "Quiet updates");

            click(fixture.toggle);
            compare(fixture.switchSummary.text, "Sync enabled");
            fixture.toggle.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(fixture.toggle, Qt.Key_Space));
            compare(fixture.switchSummary.text, "Sync paused");
            fixture.toggle.enabled = false;
            click(fixture.toggle);
            verify(TestProbe.keyClick(fixture.toggle, Qt.Key_Space));
            compare(fixture.switchSummary.text, "Sync paused");
        }

        function test_tabsAndDelegateOutcome(): void {
            click(fixture.secondTab);
            compare(fixture.tabs.currentIndex, 1);
            compare(fixture.tabSummary.text, "Details preview");
            fixture.secondTab.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(fixture.secondTab, Qt.Key_Left));
            compare(fixture.tabs.currentIndex, 0);
            compare(fixture.tabSummary.text, "Overview preview");
            click(fixture.delegate);
            compare(fixture.recordSummary.text, "Blue category selected");
        }

        function test_sliderDrivesReadoutAndProgress(): void {
            const fill = findChild(fixture.progress, "iaFill") as Rectangle;
            verify(fill !== null);
            compare(fixture.valueSummary.text, "Intensity: 37%");
            compare(fixture.progress.position, 0.37);
            fuzzyCompare(fill.width, fixture.progress.contentItem.width * 0.37, 0.5);
            compare(fill.color, fixture.progress.fillColor);
            fixture.slider.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(fixture.slider, Qt.Key_Right));
            compare(fixture.valueSummary.text, "Intensity: 38%");
            compare(fixture.progress.value, 38);
            verify(TestProbe.click(fixture.slider, fixture.slider.width * 0.75, fixture.slider.height / 2));
            verify(fixture.slider.value > 50);
            compare(fixture.valueSummary.text, "Intensity: " + Math.round(fixture.slider.value) + "%");
            compare(fixture.progress.value, fixture.slider.value);
            verify(TestProbe.click(fixture.slider, fixture.slider.width - 1, fixture.slider.height / 2));
            compare(fixture.valueSummary.text, "Intensity: 100%");
            compare(fixture.progress.position, 1);
            fuzzyCompare(fill.width, fixture.progress.contentItem.width, 0.5);
            verify(TestProbe.click(fixture.slider, 1, fixture.slider.height / 2));
            compare(fixture.valueSummary.text, "Intensity: 0%");
            compare(fixture.progress.position, 0);
            compare(fill.width, 0);
        }

        function test_liveAppearancePreservesDraftSelectionFocus(): void {
            replaceName("Retained draft");
            verify(TestProbe.keyClick(fixture.editor, Qt.Key_A, Qt.ControlModifier));
            const selected = fixture.editor.selectedText;
            const light = String((fixture.editor.background as Rectangle).color);
            fixture.applyButton.backgroundColor = "purple";
            fixture.applyButton.fontPixelSize = 18;
            fixture.applyButton.controlRadius = 9;
            IA.Appearance.input = {scheme: "dark", fontScale: 1.5, tokens: {metrics: {controlRadius: 7}}};
            verify(waitForRendering(fixture));
            compare(fixture.editor.text, "Retained draft");
            compare(fixture.editor.selectedText, selected);
            verify(fixture.editor.activeFocus);
            compare(fixture.committedSummary.text, "Saved: Maya Chen");
            verify(String((fixture.editor.background as Rectangle).color) !== light);
            compare((fixture.editor.background as Rectangle).radius, 7);
            compare(fixture.editor.font.pixelSize, Math.round(IA.Appearance.typography.controlSize));
            compare((fixture.applyButton.background as Rectangle).color, "#800080");
            compare((fixture.applyButton.background as Rectangle).radius, 9);
            const label = findChild(fixture.applyButton, "iaLabel") as Text;
            verify(label !== null);
            compare(label.font.pixelSize, 18);
            fixture.applyButton.resetAppearance();
            compare(label.font.pixelSize, fixture.editor.font.pixelSize);
            compare((fixture.applyButton.background as Rectangle).radius, 7);
            click(fixture.applyButton);
            compare(fixture.committedSummary.text, "Saved: Retained draft");
        }
    }
}
