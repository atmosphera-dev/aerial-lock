// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import InterActive
import InterActive.Foundation as IA

Rectangle {
    id: scene
    width: 1200
    height: 820
    color: IA.Appearance.colors.canvas

    // Four button tones plus six other kinds, thirty of each: exactly 300 controls.
    readonly property list<Component> specimens: [neutral, primary, caution, danger,
                                                 tool, field, check, toggle, delegate, slider]
    Component { id: neutral; IAButton { objectName: "perfControl"; text: "Neutral"; tone: IA.Presentation.Neutral } }
    Component { id: primary; IAButton { objectName: "perfControl"; text: "Primary"; tone: IA.Presentation.Primary } }
    Component { id: caution; IAButton { objectName: "perfControl"; text: "Caution"; tone: IA.Presentation.Caution } }
    Component { id: danger; IAButton { objectName: "perfControl"; text: "Danger"; tone: IA.Presentation.Destructive } }
    Component { id: tool; IAToolButton { objectName: "perfControl"; text: "Tool" } }
    Component { id: field; IATextField { objectName: "perfControl"; text: "Field" } }
    Component { id: check; IACheckBox { objectName: "perfControl"; text: "Check" } }
    Component { id: toggle; IASwitch { objectName: "perfControl"; text: "Switch" } }
    Component { id: delegate; IAItemDelegate { objectName: "perfControl"; text: "Delegate" } }
    Component { id: slider; IASlider { objectName: "perfControl"; value: 0.5 } }

    Grid {
        id: grid
        anchors.fill: parent
        anchors.margins: 16
        anchors.bottomMargin: 64 // Empty lane for dispatch-only mouse moves.
        columns: 15
        spacing: 4
        Repeater {
            model: 300
            Loader {
                required property int index
                width: (grid.width - (grid.columns - 1) * grid.spacing) / grid.columns
                height: (grid.height - 19 * grid.spacing) / 20
                sourceComponent: scene.specimens[index % scene.specimens.length]
            }
        }
    }
}
