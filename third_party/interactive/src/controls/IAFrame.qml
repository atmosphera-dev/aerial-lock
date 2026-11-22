// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.Frame {
    id: control
    property alias backgroundColor: appearance.backgroundColor
    property alias foregroundColor: appearance.foregroundColor
    property alias borderColor: appearance.borderColor
    property alias disabledBorderColor: appearance.disabledBorderColor
    property alias controlRadius: appearance.controlRadius
    property alias fontFamily: appearance.fontFamily
    property alias fontPixelSize: appearance.fontPixelSize
    property alias fontWeight: appearance.fontWeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    readonly property IA.ControlAppearance _appearance: IA.ControlAppearance { id: appearance; source: IA.Appearance; family: "surface" }
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    horizontalPadding: appearance.horizontalPadding
    verticalPadding: appearance.verticalPadding
    font: appearance.resolvedFont
    contentItem: Item { }
    background: IA.ControlBackground { resolvedAppearance: appearance }
}
