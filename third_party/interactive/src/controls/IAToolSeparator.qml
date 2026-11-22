// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.ToolSeparator {
    id: control
    property alias foregroundColor: appearance.foregroundColor
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    IA.SeparatorAppearance { id: appearance; source: IA.Appearance }
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    horizontalPadding: IA.Appearance.metrics.verticalPadding
    verticalPadding: IA.Appearance.metrics.verticalPadding
    contentItem: Item {
        implicitWidth: control.vertical ? line.thickness : IA.Appearance.metrics.toolHeight
        implicitHeight: control.vertical ? IA.Appearance.metrics.toolHeight : line.thickness
        IA.Separator {
            id: line
            objectName: "iaSeparator"
            color: control.foregroundColor
            anchors.centerIn: parent
            width: control.vertical ? thickness : parent.width
            height: control.vertical ? parent.height : thickness
            orientation: control.orientation
        }
    }
}
