// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.DialogButtonBox {
    id: control
    // -1 selects automatically; NoButton explicitly removes primary emphasis.
    // This is visual prominence only, not Qt's default/keyboard action.
    property int principalAction: -1
    // Semantic ownership stays here. Reading the model and every attached role
    // makes insertion/removal and custom role changes live binding dependencies.
    readonly property bool hasRejectAction: {
        for (let index = 0; index < contentChildren.length; ++index) {
            const button = contentChildren[index] as T.AbstractButton;
            if (button !== null && button.T.DialogButtonBox.buttonRole === T.DialogButtonBox.RejectRole)
                return true;
        }
        return false;
    }
    readonly property int _principalAction: {
        if (principalAction !== -1)
            return principalAction;
        // Stable priority independent of the platform's visual button ordering.
        const completions = [T.DialogButtonBox.Ok, T.DialogButtonBox.Open,
            T.DialogButtonBox.Save, T.DialogButtonBox.SaveAll, T.DialogButtonBox.Retry,
            T.DialogButtonBox.Ignore, T.DialogButtonBox.Yes, T.DialogButtonBox.YesToAll,
            T.DialogButtonBox.Apply];
        for (const action of completions) {
            if (standardButtons & action)
                return action;
        }
        return T.DialogButtonBox.NoButton;
    }
    property alias backgroundColor: appearance.backgroundColor
    property alias foregroundColor: appearance.foregroundColor
    property alias controlRadius: appearance.controlRadius
    property alias fontFamily: appearance.fontFamily
    property alias fontPixelSize: appearance.fontPixelSize
    property alias fontWeight: appearance.fontWeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    readonly property IA.ControlAppearance _appearance: IA.ControlAppearance { id: appearance; source: IA.Appearance; family: "surface" }
    readonly property real _buttonExtent: {
        const buttons = contentChildren;
        let total = Math.max(0, buttons.length - 1) * spacing;
        for (const button of buttons)
            total += button.width;
        return total;
    }
    readonly property DialogButtonBoxLayout _layout: DialogButtonBoxLayout {
        parent: control
        actualExtent: control._buttonExtent
        nativeWidth: control.implicitContentWidth
        availableWidth: control.availableWidth
        alignment: control.alignment
        boxMirrored: control.mirrored
        rowDirection: buttonRow.effectiveLayoutDirection
        padLeft: control.leftPadding
        padRight: control.rightPadding
    }
    implicitWidth: _layout.naturalWidth
    implicitHeight: contentHeight + topPadding + bottomPadding
    // Allocated horizontal lane includes spacers; Qt owns vertical measurement.
    contentWidth: _layout.contentWidth
    horizontalPadding: appearance.horizontalPadding
    verticalPadding: appearance.verticalPadding
    spacing: appearance.spacing
    font: appearance.resolvedFont
    alignment: Qt.AlignRight
    delegate: IAButton {
        id: button
        fontFamily: control.fontFamily
        fontPixelSize: control.fontPixelSize
        fontWeight: control.fontWeight
        tone: {
            const role = T.DialogButtonBox.buttonRole;
            if (role === T.DialogButtonBox.DestructiveRole)
                return IA.Presentation.Destructive;
            const completion = role === T.DialogButtonBox.AcceptRole
                || role === T.DialogButtonBox.YesRole || role === T.DialogButtonBox.ApplyRole;
            // Qt publishes standardButtons after constructing all delegates.
            // Observe it even for an explicit (unchanged) principal choice.
            return completion && (control.standardButtons & control._principalAction) !== 0
                && control.count > 0
                && control.standardButton(control._principalAction) === button
                ? IA.Presentation.Primary : IA.Presentation.Neutral;
        }
    }
    contentItem: ListView {
        id: buttonRow
        // Natural extent changes preserve Qt's native resize dependency.
        implicitWidth: control._layout.committedExtent
        model: control.contentModel
        spacing: control.spacing
        orientation: ListView.Horizontal
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        headerPositioning: ListView.InlineHeader
        footerPositioning: ListView.InlineFooter
        header: Item {
            width: control._layout.headerWidth
            height: 0
        }
        footer: Item {
            width: control._layout.footerWidth
            height: 0
        }
    }
    background: IA.ControlBackground {
        resolvedAppearance: appearance
        outlined: false
    }
}
