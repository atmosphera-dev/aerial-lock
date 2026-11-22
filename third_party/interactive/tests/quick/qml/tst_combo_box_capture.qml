// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Rectangle {
    id: scene
    anchors.fill: parent
    implicitWidth: 640; implicitHeight: 480
    color: IA.Appearance.colors.content
    property bool rtl: false
    Binding {
        target: scene.Window.window ? scene.Window.window.LayoutMirroring : null
        property: "enabled"
        value: scene.rtl
        restoreMode: Binding.RestoreBindingOrValue
    }
    Binding {
        target: scene.Window.window ? scene.Window.window.LayoutMirroring : null
        property: "childrenInherit"
        value: true
        restoreMode: Binding.RestoreBindingOrValue
    }
    Column {
        x: 40; y: 32; width: 320
        spacing: IA.Appearance.metrics.spacingL
        IALabel { text: "Combo box"; fontPixelSize: IA.Appearance.typography.headingSize }
        IAComboBox { id: ordinary; width: 240; model: ["Daily", "Weekly", "Custom: 137 minutes"] }
        IAComboBox { width: 240; model: ["Same as Downloads"]; inherited: true }
        IAComboBox { width: 240; model: ["Midnight"]; leadingColor: "#172438" }
        IAComboBox { width: 240; model: ["Unavailable"]; enabled: false }
        IAComboBox { width: 240; model: ["A very long custom value that must elide"]; leadingColor: "#234567" }
    }
    TestCase {
        name: "ComboBoxCapture"
        when: windowShown
        function test_frames_data(): var {
            const rows = [];
            for (const scheme of ["light", "dark"])
                for (const density of ["compact", "touch"])
                    for (const rtl of [false, true])
                        rows.push({tag:scheme+"-"+density+"-"+rtl,scheme:scheme,density:density,rtl:rtl});
            return rows;
        }
        function test_frames(data): void {
            IA.Appearance.input = {scheme:data.scheme,density:data.density,reducedMotion:true};
            scene.rtl = data.rtl;
            verify(TestProbe.activateForInput(ordinary));
            ordinary.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.render(scene));
            const prefix = "combo-" + data.tag;
            verify(TestProbe.captureWindow(scene, prefix + "-closed.png"));
            if (TestProbe.isWayland()) {
                verify(TestProbe.nativeKeyHostReady(ordinary));
                verify(TestProbe.nativeKeyClick(ordinary, Qt.Key_Space));
            } else verify(TestProbe.keyClick(ordinary, Qt.Key_Space));
            tryCompare(ordinary.popup, "opened", true);
            verify(TestProbe.render(ordinary.popup.contentItem));
            verify(TestProbe.captureWindow(scene, prefix + "-open.png"));
            ordinary.popup.close();
            tryCompare(ordinary.popup, "visible", false);
        }
    }
}
