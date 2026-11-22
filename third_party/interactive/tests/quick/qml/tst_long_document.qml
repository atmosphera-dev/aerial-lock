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
    Component { id: fixtureComponent; LongDocumentFixture {} }
    TestCase {
        name: "LongDocumentConsumers"
        when: windowShown
        property LongDocumentFixture fixture
        function init(): void {
            IA.Appearance.input = {};
            fixture = createTemporaryObject(fixtureComponent, scene) as LongDocumentFixture;
            verify(fixture !== null);
            verify(TestProbe.activate(fixture));
            fixture.prepare();
            verify(TestProbe.render(fixture));
            wait(100);
            verify(TestProbe.render(fixture));
            fixture.probe.resetCounts();
        }
        function test_accentPreservesDocumentAndEditing(): void {
            const before = fixture.probe.snapshot();
            const editing = fixture.editingState();
            verify(before.characters >= 1000000);
            verify(before.paragraphs >= 10000);
            verify(before.undo);
            verify(editing.focus);
            verify(editing.scroll > 0);
            for (let i = 0; i < 30; ++i)
                IA.Appearance.input = {tokens: {colors: {accent: Qt.hsva(i / 30, 0.65, 0.85, 1)}}};
            verify(TestProbe.render(fixture));
            compare(fixture.probe.snapshot(), before);
            compare(fixture.editingState(), editing);
            compare(fixture.probe.countFor("documentFont"), 0);
            compare(fixture.probe.countFor("contentsChange"), 0);
            compare(fixture.probe.countFor("documentSizeChanged"), 0);
            fixture.editor.undo();
            verify(fixture.probe.snapshot().textSha256 !== before.textSha256);
            verify(fixture.probe.snapshot().redo);
            fixture.editor.redo();
            compare(fixture.probe.snapshot().textSha256, before.textSha256);
        }
        function test_foregroundAndFontPositiveControls(): void {
            const before = fixture.probe.snapshot();
            IA.Appearance.input = {tokens: {colors: {text: "#102030"}}};
            compare(fixture.editor.color, "#102030");
            verify(TestProbe.render(fixture));
            compare(fixture.probe.countFor("documentFont"), 0);
            compare(fixture.probe.snapshot().textSha256, before.textSha256);
            compare(fixture.probe.snapshot().identity, before.identity);
            fixture.probe.resetCounts();
            IA.Appearance.input = {fontScale: 1.5, tokens: {colors: {text: "#102030"}}};
            compare(fixture.editor.font.pixelSize, 20);
            verify(TestProbe.render(fixture));
            compare(fixture.probe.countFor("documentFont"), 1);
            verify(fixture.probe.countFor("documentSizeChanged") > 0);
            verify(fixture.probe.snapshot().height > before.height);
            compare(fixture.probe.snapshot().textSha256, before.textSha256);
            compare(fixture.probe.snapshot().identity, before.identity);
        }
    }
}
