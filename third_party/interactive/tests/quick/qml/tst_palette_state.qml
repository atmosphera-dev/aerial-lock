// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: scene
    width: 640
    height: 500
    Component { id: fixtureComponent; PaletteStateFixture {} }
    TestCase {
        name: "PaletteStateConsumers"
        when: windowShown
        property PaletteStateFixture fixture
        function init(): void {
            IA.Appearance.input = {};
            fixture = createTemporaryObject(fixtureComponent, scene) as PaletteStateFixture;
            verify(fixture !== null);
            verify(TestProbe.render(fixture));
            fixture.probe.resetCounts();
        }
        function test_accentAvoidsUnrelatedBindings_data(): list<var> {
            return [{tag: "one", controls: 1}, {tag: "hundred", controls: 100}, {tag: "thousand", controls: 1000}];
        }
        function test_accentAvoidsUnrelatedBindings(data: var): void {
            fixture.controls = data.controls;
            verify(TestProbe.render(fixture));
            fixture.probe.resetCounts();
            const originalFont = fixture.consumer.font;
            const originalHeight = fixture.button.minimumHeight;
            for (const accent of ["#8657b8", "#20845f", "#FFDF00"]) {
                IA.Appearance.input = {tokens: {colors: {accent: accent}}};
                verify(Qt.colorEqual(fixture.button.backgroundColor, accent));
                compare(fixture.consumer.color, IA.Appearance.colors.primaryText);
            }
            verify(TestProbe.render(fixture));
            compare(fixture.probe.countFor("font"), 0);
            compare(fixture.probe.countFor("height"), 0);
            compare(fixture.probe.countFor("aliasSize"), 0);
            compare(fixture.probe.countFor("aliasHeight"), 0);
            compare(fixture.probe.countFor("globalNeutral"), 0);
            compare(fixture.probe.countFor("globalScale"), 0);
            compare(fixture.probe.countFor("globalAccent"), 3);
            compare(fixture.probe.countFor("surface"), 3);
            compare(fixture.consumer.font, originalFont);
            compare(fixture.button.minimumHeight, originalHeight);
        }
        function test_nestedUpdateReachesFontBindings(): void {
            fixture.nestedEnabled = true;
            IA.Appearance.input = {fontScale: 2};
            compare(fixture.consumer.font.pixelSize, 26);
            compare(fixture.observedAliasSize, 26);
            compare(fixture.observedScale, 2);
            compare(fixture.observedAccent, "#8657b8");
            compare(fixture.probe.countFor("font"), 1);
            compare(fixture.probe.countFor("aliasSize"), 1);
            verify(fixture.probe.countFor("height") > 0);
            verify(TestProbe.render(fixture));
        }
        function test_localRootAndLiveReset(): void {
            fixture.facade.backgroundColor = "#8657b8";
            const text = fixture.consumer.color;
            const hover = fixture.facade.hoverBackgroundColor;
            fixture.probe.resetCounts();
            IA.Appearance.input = {tokens: {colors: {accent: "#20845f"}}};
            compare(fixture.consumer.color, text);
            compare(fixture.facade.hoverBackgroundColor, hover);
            compare(fixture.probe.countFor("surface"), 0);
            compare(fixture.probe.countFor("text"), 0);
            fixture.facade.backgroundColor = undefined;
            compare(fixture.consumer.color, IA.Appearance.colors.primaryText);
            verify(TestProbe.render(fixture));
        }
        function test_sourceReplacementAndDestruction(): void {
            const first = fixture.probe.createAppearance();
            const second = fixture.probe.createAppearance();
            verify(first !== null && second !== null);
            fixture.facade.source = first;
            fixture.probe.resetCounts();
            fixture.facade.source = second;
            compare(fixture.probe.countFor("font"), 0);
            compare(fixture.probe.countFor("height"), 0);
            compare(fixture.probe.countFor("surface"), 0);
            first.input = {fontScale: 2};
            compare(fixture.consumer.font.pixelSize, 13);
            second.input = {fontScale: 2};
            compare(fixture.consumer.font.pixelSize, 26);
            fixture.facade.backgroundColor = "#8657b8";
            verify(fixture.probe.destroyAppearance(second));
            compare(fixture.facade.source, null);
            compare(fixture.consumer.font.pixelSize, 13);
            compare(fixture.facade.backgroundColor, "#8657b8");
            verify(fixture.probe.countFor("font") >= 2);
            verify(TestProbe.render(fixture));
            verify(fixture.probe.destroyAppearance(first));
        }
    }
}
