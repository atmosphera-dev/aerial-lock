// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.TabBar {
    id: control
    property alias backgroundColor: appearance.backgroundColor
    property alias borderColor: appearance.borderColor
    property alias disabledBorderColor: appearance.disabledBorderColor
    property alias controlRadius: appearance.controlRadius
    property alias fontFamily: appearance.fontFamily
    property alias fontPixelSize: appearance.fontPixelSize
    property alias fontWeight: appearance.fontWeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    readonly property IA.ControlAppearance _appearance: IA.ControlAppearance { id: appearance; source: IA.Appearance; family: "tabBar" }
    implicitWidth: contentWidth + leftPadding + rightPadding
    implicitHeight: contentHeight + topPadding + bottomPadding
    horizontalPadding: appearance.horizontalPadding
    verticalPadding: appearance.verticalPadding
    spacing: IA.Appearance.metrics.spacingXS
    font: appearance.resolvedFont
    contentItem: IA.SegmentGroup {
        id: segments
        resolvedAppearance: appearance
        mirrored: control.mirrored
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: control.spacing
    }
    background: segments.createBackground(control)
}
