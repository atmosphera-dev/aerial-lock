// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import InterActive.Foundation as IA

T.Dialog {
    id: control
    property bool _popupDirectionReady: false
    Component.onCompleted: control._popupDirectionReady = true
    readonly property IA.PopupDirection _popupDirection: IA.PopupDirection {
        source: control.T.Overlay.overlay
        contentItem: control._popupDirectionReady ? control.contentItem : null
    }
    // Keep Qt's popup lifecycle while ordinary children belong to the natural body.
    default property alias bodyData: body.contentData
    property alias backgroundColor: appearance.backgroundColor
    property alias foregroundColor: appearance.foregroundColor
    property alias borderColor: appearance.borderColor
    property alias disabledBorderColor: appearance.disabledBorderColor
    property alias focusColor: appearance.focusColor
    property alias controlRadius: appearance.controlRadius
    property alias fontFamily: appearance.fontFamily
    property alias fontPixelSize: appearance.fontPixelSize
    property alias fontWeight: appearance.fontWeight
    readonly property alias appearanceDiagnostics: appearance.diagnostics
    function resetAppearance(): void { appearance.resetOverrides(); }
    readonly property IA.ControlAppearance _appearance: IA.ControlAppearance { id: appearance; source: IA.Appearance; family: "popup" }
    readonly property IA.ScreenGeometry _hostGeometry: IA.ScreenGeometry {
        window: control.parent ? control.parent.Window.window : null
    }
    readonly property real _heightBudget: {
        const host = _hostGeometry.window;
        const extent = popupType === T.Popup.Window ? _hostGeometry.availableGeometry.height
            : (host ? host.height : 0);
        return extent > 0 ? Math.max(0, extent - Math.max(0, topMargin) - Math.max(0, bottomMargin))
            : implicitHeight;
    }
    readonly property bool _hasRejectAction: (footer as IADialogButtonBox)?.hasRejectAction ?? false
    // All dividers belong to the frame's interior, never over its side outline.
    readonly property real _separatorInset: (background as Rectangle)?.border.width ?? 0
    implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding,
                            implicitHeaderWidth, implicitFooterWidth, IA.Appearance.metrics.dialogMinWidth)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
                    + implicitHeaderHeight + implicitFooterHeight
                    + (implicitHeaderHeight > 0 ? spacing : 0) + (implicitFooterHeight > 0 ? spacing : 0)
    height: Math.min(implicitHeight, _heightBudget)
    horizontalPadding: appearance.horizontalPadding
    verticalPadding: appearance.verticalPadding
    spacing: appearance.spacing
    margins: IA.Appearance.metrics.spacingS
    font: appearance.resolvedFont
    popupType: T.Popup.Item
    header: Item {
        id: heading
        readonly property real actionExtent: Math.max(Math.ceil(headingMetrics.height) + 2 * control.topPadding,
            IA.Appearance.metrics.toolHeight, IA.Appearance.metrics.targetSize)
        readonly property real titleGap: control.title.length > 0 && control._hasRejectAction ? IA.Appearance.metrics.spacingS : 0
        implicitWidth: titleLabel.implicitWidth + (control._hasRejectAction
            ? (control.mirrored ? control.rightPadding : control.leftPadding) + actionExtent + titleGap
            : control.leftPadding + control.rightPadding)
        implicitHeight: visible ? actionExtent : 0
        visible: control.title.length > 0 || control._hasRejectAction
        // The row consumes the resolved direction once. The header owns
        // both the title lane and the square top-trailing action, independently
        // of body spacing and the tool button's hidden text metrics.
        LayoutMirroring.enabled: false
        LayoutMirroring.childrenInherit: true
        FontMetrics { id: headingMetrics; font: titleLabel.font }
        RowLayout {
            anchors.fill: parent
            LayoutMirroring.enabled: false
            layoutDirection: control.mirrored ? Qt.RightToLeft : Qt.LeftToRight
            spacing: heading.titleGap
            IALabel {
                id: titleLabel
                objectName: "iaTitle"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: !control.mirrored || !closeButton.visible ? control.leftPadding : 0
                Layout.rightMargin: control.mirrored || !closeButton.visible ? control.rightPadding : 0
                text: control.title
                fontFamily: control.fontFamily
                fontPixelSize: appearance.headingPixelSize
                fontWeight: appearance.headingWeight
                foregroundColor: control.foregroundColor
                horizontalAlignment: control.mirrored ? Text.AlignRight : Text.AlignLeft
                elide: Text.ElideRight
            }
            IAToolButton {
                id: closeButton
                objectName: "iaDialogClose"
                visible: control._hasRejectAction
                Layout.preferredWidth: heading.actionExtent
                Layout.preferredHeight: heading.actionExtent
                Layout.minimumWidth: heading.actionExtent
                Layout.minimumHeight: heading.actionExtent
                LayoutMirroring.enabled: control.mirrored
                // The native hover/focus surface shares the popup's resolved
                // corner, so its full target stays inside the visible outline.
                controlRadius: control.controlRadius
                icon.source: "qrc:/qt/qml/InterActive/Foundation/icons/close.svg"
                display: T.AbstractButton.IconOnly
                Accessible.name: "Close"
                fontFamily: control.fontFamily
                fontPixelSize: control.fontPixelSize
                fontWeight: control.fontWeight
                foregroundColor: control.foregroundColor
                onClicked: control.reject()
            }
        }
        IA.Separator {
            objectName: "iaDialogTitleSeparator"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: control._separatorInset
            anchors.rightMargin: control._separatorInset
            anchors.bottom: parent.bottom
            height: 1
        }
    }
    contentItem: Item {
        implicitWidth: body.implicitWidth + scroll._lane
        implicitHeight: body.implicitHeight + 2 * (1 + IA.Appearance.metrics.spacingS)
        IAScrollView {
            id: scroll
            objectName: "iaDialogScroll"
            readonly property real _lane: T.ScrollBar.vertical.width + IA.Appearance.metrics.spacingXS
            anchors.fill: parent
            anchors.topMargin: 1 + IA.Appearance.metrics.spacingS
            anchors.bottomMargin: 1 + IA.Appearance.metrics.spacingS
            padding: 0
            leftPadding: control.mirrored ? _lane : 0
            rightPadding: control.mirrored ? 0 : _lane
            contentWidth: availableWidth
            contentHeight: body.implicitHeight
            background: null
            T.ScrollBar.horizontal.policy: T.ScrollBar.AlwaysOff
            contentItem: Flickable {
                id: viewport
                objectName: "iaDialogViewport"
                clip: true
                contentWidth: width
                contentHeight: body.implicitHeight
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                T.Pane {
                    id: body
                    objectName: "iaDialogBody"
                    width: viewport.width
                    height: implicitHeight
                    implicitWidth: implicitContentWidth
                    implicitHeight: implicitContentHeight
                    padding: 0
                }
            }
        }
        // These overlays share the viewport's visual owner. No detached object
        // property can outlive its explicit visual parent during popup teardown.
        Item {
            // Padding is already resolved to physical edges by the scroll owner.
            LayoutMirroring.enabled: false
            clip: true
            anchors.fill: scroll
            anchors.leftMargin: scroll.leftPadding
            anchors.rightMargin: scroll.rightPadding
            anchors.topMargin: scroll.topPadding
            anchors.bottomMargin: scroll.bottomPadding
            Rectangle {
                objectName: "iaDialogTopFade"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 16
                visible: viewport.contentY > viewport.originY && !IA.Appearance.reducedMotion
                gradient: Gradient {
                    GradientStop { position: 0; color: control.backgroundColor }
                    GradientStop { position: 1; color: "transparent" }
                }
            }
            Rectangle {
                objectName: "iaDialogBottomFade"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 16
                visible: bottomEdge.visible && !IA.Appearance.reducedMotion
                gradient: Gradient {
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 1; color: control.backgroundColor }
                }
            }
        }
        IA.Separator {
            id: topEdge
            objectName: "iaDialogTopEdge"
            x: -control.leftPadding + control._separatorInset
            width: control.width - 2 * control._separatorInset
            height: 1
            // The default heading already owns the permanent boundary. Custom
            // or absent headings keep the existing hidden-content fold line.
            visible: viewport.contentY > viewport.originY
                && (control.header !== heading || !heading.visible)
        }
        IA.Separator {
            id: bottomEdge
            objectName: "iaDialogBottomEdge"
            x: -control.leftPadding + control._separatorInset
            y: parent.height - height
            width: control.width - 2 * control._separatorInset
            height: 1
            visible: viewport.contentY + viewport.height < viewport.originY + viewport.contentHeight
        }
    }
    // T.Dialog connects this template footer and forwards standardButtons itself.
    footer: IADialogButtonBox {
        objectName: "iaDialogButtons"
        visible: count > 0
        // The dialog owns the complete surface and outline, including beneath the footer.
        background: null
        leftPadding: control.leftPadding
        rightPadding: control.rightPadding
        topPadding: control.topPadding
        bottomPadding: control.bottomPadding
        fontFamily: control.fontFamily
        fontPixelSize: control.fontPixelSize
        fontWeight: control.fontWeight
    }
    background: IA.ControlBackground { resolvedAppearance: appearance }
    T.Overlay.modal: Rectangle { color: IA.Appearance.colors.scrim }
    T.Overlay.modeless: Rectangle { color: IA.Appearance.colors.scrim }
}
