// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.MenuSeparator {
    id: control
    property alias foregroundColor: appearance.foregroundColor
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    IA.SeparatorAppearance { id: appearance; source: IA.Appearance }
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    horizontalPadding: IA.Appearance.metrics.horizontalPadding
    verticalPadding: IA.Appearance.metrics.verticalPadding
    contentItem: Item {
        implicitHeight: line.thickness
        IA.Separator {
            id: line
            objectName: "iaSeparator"
            color: control.foregroundColor
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.right: parent.right
        }
    }
}
