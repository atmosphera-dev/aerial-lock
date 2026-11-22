// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: root
    width: 640
    height: 500
    property alias probe: counter
    property alias facade: appearance
    property alias consumer: textConsumer
    property alias button: aliasConsumer
    property int controls: 1
    property bool nestedEnabled: false
    property bool nestedInstalled: false
    readonly property color observedAccent: counter.countValue(IA.Appearance.colors.accent, "globalAccent")
    readonly property color observedNeutral: counter.countValue(IA.Appearance.colors.content, "globalNeutral")
    readonly property real observedScale: counter.countValue(IA.Appearance.fontScale, "globalScale")
    readonly property real observedAliasSize: counter.countValue(aliasConsumer.fontPixelSize, "aliasSize")
    readonly property real observedAliasHeight: counter.countValue(aliasConsumer.minimumHeight, "aliasHeight")

    DocumentProbe { id: counter }
    // Created before the late facade connection to exercise A -> B -> C dispatch.
    Connections {
        target: IA.Appearance
        function onResolvedChanged(): void {
            if (!root.nestedEnabled || root.nestedInstalled)
                return;
            root.nestedInstalled = true;
            IA.Appearance.input = {fontScale: IA.Appearance.fontScale, tokens: {colors: {accent: "#8657b8"}}};
        }
    }
    IA.ControlAppearance { id: appearance; source: IA.Appearance; tone: IA.Presentation.Primary }
    Column {
        spacing: 12
        Rectangle {
            width: 300
            height: counter.countValue(appearance.minimumHeight, "height")
            color: counter.countValue(appearance.backgroundColor, "surface")
            Text {
                id: textConsumer
                objectName: "paletteTextConsumer"
                anchors.centerIn: parent
                text: "Finished shared palette"
                font: counter.countValue(appearance.resolvedFont, "font")
                color: counter.countValue(appearance.foregroundColor, "text")
            }
        }
        IAButton { id: aliasConsumer; text: "Real alias consumer"; tone: IA.Presentation.Primary }
    }
    Repeater {
        model: root.controls - 1
        delegate: Item {
            IA.ControlAppearance { source: IA.Appearance; tone: IA.Presentation.Primary }
        }
    }
}
