// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import InterActive
import InterActive.Foundation as IA

IADialog {
    id: control
    property color colorValue: "white"
    readonly property color currentColor: editor.currentColor
    // QtQuick's Window QML type excludes QQuickView. Accept any QQuickWindow.
    property var parentWindow: null
    signal colorEdited(color value)
    parent: parentWindow ? parentWindow.contentItem : null
    popupType: T.Popup.Window
    modal: false
    closePolicy: T.Popup.CloseOnEscape
    anchors.centerIn: parent
    readonly property real _wideFooterWidth: buttons.implicitContentWidth + leftPadding + rightPadding
        + buttons.captionWidth + buttons.spacing
    implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding,
        implicitHeaderWidth, _wideFooterWidth, IA.Appearance.metrics.dialogMinWidth)
    width: Math.min(implicitWidth, Math.max(0, host.availableGeometry.width - 2 * margins))
    horizontalPadding: IA.Appearance.metrics.spacingL
    verticalPadding: IA.Appearance.metrics.spacingS
    spacing: 0
    readonly property IA.ScreenGeometry _sourceGeometry: IA.ScreenGeometry { id: host; window: control.parentWindow }
    IAColorEditor {
        id: editor
        objectName: "iaColorEditor"
        width: parent ? parent.width : implicitWidth
        colorValue: control.colorValue
        onColorEdited: value => control.colorEdited(value)
    }
    footer: IADialogButtonBox {
        id: buttons
        objectName: "iaDialogButtons"
        visible: count > 0
        standardButtons: T.DialogButtonBox.Close
        readonly property bool stacked: control.width < control._wideFooterWidth
        readonly property real captionWidth: caption.implicitWidth
        readonly property real captionHeight: caption.implicitHeight
        leftPadding: control.leftPadding + (!stacked && !control.mirrored ? captionWidth + spacing : 0)
        rightPadding: control.rightPadding + (!stacked && control.mirrored ? captionWidth + spacing : 0)
        topPadding: control.topPadding + (stacked ? captionHeight + spacing : 0)
        bottomPadding: control.bottomPadding
        fontFamily: control.fontFamily
        fontPixelSize: control.fontPixelSize
        fontWeight: control.fontWeight
        background: Item {
            IA.Separator { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top }
            Item {
                x: control.mirrored && !buttons.stacked ? parent.width - width - control.rightPadding : control.leftPadding
                y: control.topPadding
                width: buttons.stacked ? Math.max(0, parent.width - control.leftPadding - control.rightPadding) : buttons.captionWidth
                height: buttons.stacked ? buttons.captionHeight : Math.max(0, parent.height - control.topPadding - control.bottomPadding)
                IALabel {
                    id: caption
                    objectName: "iaColorLiveCaption"
                    anchors.fill: parent
                    text: "Changes apply live; closing keeps edits."
                    fontPixelSize: IA.Appearance.typography.supportingSize
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
    }
}
