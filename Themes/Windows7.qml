import QtQuick
import Qt5Compat.GraphicalEffects
import "../Services"
import "../Modules/Lock" as LockModule

// Windows7 (d44) — faithful adaptation of the qylock windows_7 reference
// (Darkkal44/qylock @ f6561e2, GPLv3; see Themes/ATTRIBUTION.md) over
// Aerial's video: centered login column at the reference −10s offset with
// the rounded-square glass avatar frame, name and Locked label, the compact
// field with its attached glossy arrow, and the footer with time/date center
// and working power controls right. Unbacked selectors are not drawn.
//
// Intentional deviations (d44 plan): native AuthField masking instead of
// the reference's transparent editor + separate dot row; the session's own
// avatar via the shared Avatar (never the reference's bundled picture); the
// power controls use context.power; no entrance fade (input is never
// delayed); status renders genuine PAM prompts/errors and grows when
// wrapping. Narrow/portrait (d44 §9.1, compact stacking): the column
// already stacks vertically; the field and footer clamp to the surface.
FocusScope {
    id: root

    required property LockContext context
    readonly property int authWriterToken: field.writerToken
    property Item backdrop: null

    focus: true

    readonly property real s: Math.max(1, height) / 768
    readonly property string uiFont: "Segoe UI, Ubuntu, sans-serif"

    function focusInput() {
        if (context.power.confirmationRequired) powerRow.focusFirst()
        else field.focusField()
    }

    // Reference's boot vignette over the (possibly video) background.
    RadialGradient {
        anchors.fill: parent
        opacity: 0.35
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: "#1a0d1a30" }
        }
    }

    // ---- Centered login column ----
    Column {
        id: loginPanel
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -10 * root.s
        spacing: 0
        transform: Translate { id: shakeShift }

        // Rounded-square glass avatar frame (reference: 132s, radius 18s).
        Item {
            id: pfpFrame
            objectName: "pfpFrame"
            width: 132 * root.s
            height: 132 * root.s
            anchors.horizontalCenter: parent.horizontalCenter

            Rectangle {
                anchors.fill: parent
                radius: 18 * root.s
                color: "transparent"
                border.color: "#99d0e8f8"
                border.width: 2 * root.s
                layer.enabled: true
                layer.effect: DropShadow {
                    transparentBorder: true
                    horizontalOffset: 0
                    verticalOffset: 2
                    radius: 16 * root.s
                    samples: 24
                    color: "#70000000"
                }
            }
            Rectangle {
                id: glassFrame
                anchors.fill: parent
                anchors.margins: 2 * root.s
                radius: 16 * root.s
                gradient: Gradient {
                    GradientStop { position: 0.00; color: "#e0f8ffff" }
                    GradientStop { position: 0.20; color: "#99b8ddf0" }
                    GradientStop { position: 0.50; color: "#446090b8" }
                    GradientStop { position: 0.85; color: "#993a7aaa" }
                    GradientStop { position: 1.00; color: "#e05090c0" }
                }
                // The session's own avatar, square-cropped per the
                // reference's 8s-radius image crop at 5s margins.
                LockModule.Avatar {
                    anchors.fill: parent
                    anchors.margins: 5 * root.s
                    diameter: width
                    cornerRadius: 8 * root.s
                    source: root.context.session.avatarPath
                    loginName: root.context.session.loginName
                    fallbackFill: "#40608a88"
                    foreground: "#f0f8ff"
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 5 * root.s
                    radius: 12 * root.s
                    color: "transparent"
                    border.color: "#40000000"
                    border.width: 1 * root.s
                    z: 10
                }
                Rectangle {
                    anchors.top: parent.top
                    anchors.topMargin: 2 * root.s
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 12 * root.s
                    height: parent.height * 0.45
                    radius: 12 * root.s
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#70ffffff" }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                    z: 15
                }
            }
        }

        Item { width: 1; height: 18 * root.s }

        Text {
            id: userNameText
            objectName: "userName"
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.context.session.loginName
            textFormat: Text.PlainText
            font.family: root.uiFont
            font.pixelSize: 26 * root.s
            color: "white"
            style: Text.Raised
            styleColor: "#40000000"
        }

        Item { width: 1; height: 4 * root.s }

        Text {
            id: lockedLabel
            objectName: "lockedLabel"
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Locked"
            font.family: root.uiFont
            font.pixelSize: 14 * root.s
            color: "#c8dce8"
            style: Text.Raised
            styleColor: "#40000000"
        }

        Item { width: 1; height: 14 * root.s }

        // The FIELD is centered; the arrow attaches 8s beyond its right
        // edge (the combined group is NOT centered).
        Item {
            id: passwordRow
            anchors.horizontalCenter: parent.horizontalCenter
            width: fieldFrame.width
            height: 28 * root.s

            Rectangle {
                id: fieldFrame
                objectName: "fieldFrame"
                anchors.centerIn: parent
                width: Math.min(240 * root.s, root.width - 60 * root.s)
                height: 28 * root.s
                radius: 2 * root.s
                color: "#f8fdff"
                border.color: field.hasActiveFocus ? "#3c7fb1" : "#40708898"
                border.width: 1 * root.s

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1 * root.s
                    radius: 1 * root.s
                    color: "transparent"
                    border.color: "#15000000"
                    border.width: 1 * root.s
                }

                // Placeholder lives outside the core, driven by the shared
                // draft's emptiness; it never hides a real PAM status.
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 6 * root.s
                    text: "Password"
                    font.family: root.uiFont
                    font.pixelSize: 13 * root.s
                    color: "#80404050"
                    opacity: root.context.auth.input.text.length === 0 ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 400; easing.type: Easing.InOutSine } }
                }

                LockModule.AuthField {
                    id: field
                    objectName: "authField"
                    anchors { fill: parent; leftMargin: 6 * root.s; rightMargin: 6 * root.s }
                    context: root.context
                    textColor: "#101820"
                    fontFamily: root.uiFont
                    fontPixelSize: 13 * root.s
                }
            }

            // Glossy submit arrow, attached to the field's right edge.
            Item {
                id: arrowBtn
                objectName: "submitArrow"
                anchors.left: fieldFrame.right
                anchors.leftMargin: 8 * root.s
                anchors.verticalCenter: fieldFrame.verticalCenter
                width: 28 * root.s
                height: 28 * root.s
                enabled: !root.context.auth.inProgress
                opacity: enabled ? 1.0 : 0.5
                scale: arrowMouse.pressed ? 0.92 : (arrowMouse.containsMouse && enabled ? 1.08 : 1.0)
                Behavior on scale { NumberAnimation { duration: 100 } }

                Rectangle {
                    anchors.fill: parent
                    radius: 14 * root.s
                    color: "transparent"
                    border.color: arrowMouse.containsMouse ? "#80b0ccee" : "#40708898"
                    border.width: 1 * root.s
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1 * root.s
                    radius: 13 * root.s
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: arrowMouse.containsMouse ? "#88c8e0ff" : "#60a0c4e8" }
                        GradientStop { position: 0.5; color: arrowMouse.containsMouse ? "#666090b4" : "#404870a0" }
                        GradientStop { position: 1.0; color: arrowMouse.containsMouse ? "#886090b8" : "#505888b0" }
                    }
                }
                Rectangle {
                    anchors.top: parent.top
                    anchors.topMargin: 2 * root.s
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 8 * root.s
                    height: 11 * root.s
                    radius: 7 * root.s
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#45ffffff" }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                    z: 2
                }
                Canvas {
                    anchors.fill: parent
                    anchors.margins: 6 * root.s
                    z: 5
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        ctx.strokeStyle = "white"
                        ctx.lineWidth = 2.2 * root.s
                        ctx.lineCap = "round"
                        ctx.lineJoin = "round"
                        ctx.beginPath()
                        ctx.moveTo(2, height / 2)
                        ctx.lineTo(width - 2, height / 2)
                        ctx.moveTo(width - 6, height / 2 - 4)
                        ctx.lineTo(width - 2, height / 2)
                        ctx.lineTo(width - 6, height / 2 + 4)
                        ctx.stroke()
                    }
                }
                // MouseAreas never take keyboard focus: a pointer press
                // cannot steal the editor's focus mid-composition.
                MouseArea {
                    id: arrowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: field.submit()
                }
                Accessible.role: Accessible.Button
                Accessible.name: "Submit password"
            }
        }

        Item { width: 1; height: 8 * root.s }

        // Reserved status region: genuine PAM prompts/errors, wrapping and
        // growing instead of clipping instructions.
        Text {
            id: statusText
            objectName: "statusText"
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(318 * root.s, root.width - 60 * root.s)
            height: Math.max(implicitHeight, 15 * root.s)
            verticalAlignment: Text.AlignTop
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: text.length > 0
            text: root.context.auth.status
            textFormat: Text.PlainText
            font.family: root.uiFont
            font.pixelSize: 12 * root.s
            color: root.context.auth.isError ? "#ffddaa" : "#dce8f0"
            style: Text.Raised
            styleColor: "#60000000"
        }

    }

    // ---- Footer bar ----
    Item {
        id: bottomBar
        objectName: "footerBar"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        // Compare the free half beside the centered clock, not a removed
        // Session selector. A long message stacks above its controls.
        readonly property bool narrow: powerRow.visible && root.width / 2 - clockRow.implicitWidth / 2
                < powerRow.normalWidth + 40 * root.s
        height: (narrow ? 30 * root.s : 0) + Math.max(52 * root.s, powerRow.height)

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#28000010" }
                GradientStop { position: 1.0; color: "#60001030" }
            }
        }
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1 * root.s
            color: "#30a0c8e0"
        }

        // Center: time / separator / date, from the shared clock.
        Row {
            id: clockRow
            objectName: "clockRow"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12 * root.s

            Text {
                text: Qt.formatTime(root.context.clock.now, "h:mm AP")
                textFormat: Text.PlainText
                font.family: root.uiFont
                font.pixelSize: 18 * root.s
                font.weight: Font.DemiBold
                color: "white"
                style: Text.Raised
                styleColor: "#80000000"
                anchors.verticalCenter: parent.verticalCenter
            }
            Item {
                width: 2 * root.s
                height: 18 * root.s
                anchors.verticalCenter: parent.verticalCenter
                Rectangle { anchors.left: parent.left; width: 1 * root.s; height: parent.height; color: "#40000000" }
                Rectangle { anchors.right: parent.right; width: 1 * root.s; height: parent.height; color: "#40ffffff" }
            }
            Text {
                text: Qt.formatDate(root.context.clock.now, "dddd, MMMM d")
                textFormat: Text.PlainText
                font.family: root.uiFont
                font.pixelSize: 14 * root.s
                color: "#e0f0ff"
                style: Text.Raised
                styleColor: "#80000000"
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        LockModule.PowerActionControls {
            id: powerRow
            objectName: "powerRow"
            context: root.context
            anchors.right: parent.right
            anchors.rightMargin: 20 * root.s
            anchors.top: parent.top
            actions: [
                {action: "reboot", label: root.context.labels.powerReboot, name: "restartAction", available: root.context.power.canReboot},
                {action: "powerOff", label: root.context.labels.powerOff, name: "shutdownAction", available: root.context.power.canPowerOff}
            ]
            visible: active || controls.length > 0
            // Measure the renderer's unwrapped natural width, including its
            // shaping/spacing, rather than a second font-metrics layout.
            readonly property real normalWidth: naturalButtons.implicitWidth
            readonly property real messageWidth: Math.ceil(messageText.implicitWidth)
            readonly property real freeWidth: bottomBar.narrow ? root.width - 40 * root.s
                    : Math.max(0, root.width / 2 - clockRow.implicitWidth / 2 - 40 * root.s)
            readonly property bool stacked: message.length > 0
                    && messageWidth + buttons.implicitWidth + 12 * root.s > freeWidth
            width: stacked ? root.width - 40 * root.s
                    : buttons.implicitWidth + (message.length ? messageWidth + 12 * root.s : 0)
                        + (waiting ? 24 * root.s : 0)
            height: 52 * root.s + (stacked ? messageText.implicitHeight + 12 * root.s : 0)
            focusControl: function(index) {
                var button = buttonRepeater.itemAt(index) as LockModule.PowerActionButton
                if (button) button.focusControl()
            }
            Row {
                id: naturalButtons
                visible: false
                spacing: 8 * root.s
                Repeater {
                    model: powerRow.availableActions
                    Win7GlassButton { required property var modelData; label: modelData.label }
                }
            }
            Text {
                id: messageText
                objectName: "powerMessage"
                text: powerRow.message
                textFormat: Text.PlainText
                font.family: root.uiFont
                font.pixelSize: 12 * root.s
                color: powerRow.failed ? "#ffddaa" : "#c0d8e8"
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.Wrap
                anchors.right: powerRow.stacked ? parent.right : buttons.left
                anchors.rightMargin: powerRow.stacked ? 0 : 12 * root.s
                anchors.top: powerRow.stacked ? parent.top : undefined
                anchors.verticalCenter: powerRow.stacked ? undefined : buttons.verticalCenter
                width: powerRow.stacked ? parent.width : powerRow.messageWidth
                visible: text.length > 0
            }
            Image {
                objectName: "powerProgress"
                anchors.right: messageText.left
                anchors.rightMargin: 8 * root.s
                anchors.verticalCenter: messageText.verticalCenter
                width: 16 * root.s; height: width
                visible: powerRow.waiting
                sourceSize.width: width * 2; sourceSize.height: height * 2
                source: "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><circle cx='8' cy='8' r='6' fill='none' stroke='%2396c3e6' stroke-opacity='.35' stroke-width='2.6'/><path d='M8 2a6 6 0 0 1 6 6' fill='none' stroke='%23e6f6ff' stroke-width='2.6' stroke-linecap='round'/></svg>"
                RotationAnimator on rotation { running: powerRow.visible && powerRow.waiting; from: 0; to: 360; duration: 1200; loops: Animation.Infinite }
            }
            Row {
                id: buttons
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 52 * root.s
                spacing: 8 * root.s
                Repeater {
                    id: buttonRepeater
                    model: powerRow.controls
                    Win7GlassButton {
                        required property var modelData
                        objectName: modelData.name
                        label: modelData.label
                        anchors.verticalCenter: parent.verticalCenter
                        trapNavigation: powerRow.confirming
                        onActivated: powerRow.activate(modelData.action)
                        onNavigate: function(direction) { powerRow.navigate(direction) }
                    }
                }
            }
        }

        states: State {
            name: "narrow"
            when: bottomBar.narrow
            AnchorChanges {
                target: clockRow
                anchors.verticalCenter: undefined
                anchors.bottom: bottomBar.bottom
            }
        }
    }

    // Reference vertical shake, independent of layout anchors; the column
    // keeps its −10s resting position throughout.
    Connections {
        target: root.context.auth
        function onIsErrorChanged() {
            if (root.context.auth.isError)
                shakeAnim.start()
        }
    }
    SequentialAnimation {
        id: shakeAnim
        NumberAnimation { target: shakeShift; property: "y"; from: 0; to: -10 * root.s; duration: 50 }
        NumberAnimation { target: shakeShift; property: "y"; to: 10 * root.s; duration: 50 }
        NumberAnimation { target: shakeShift; property: "y"; to: -5 * root.s; duration: 50 }
        NumberAnimation { target: shakeShift; property: "y"; to: 5 * root.s; duration: 50 }
        NumberAnimation { target: shakeShift; property: "y"; to: 0; duration: 50 }
    }

    // Reference glass styling; intents and focus live in the shared control.
    component Win7GlassButton: LockModule.PowerActionButton {
        id: glassButton
        width: pwText.implicitWidth + 24
        height: 26 * root.s
        layer.enabled: hovered || activeFocus
        layer.effect: DropShadow {
            color: "#598cc8ff"
            radius: 6 * root.s
            samples: 13
            horizontalOffset: 0
            verticalOffset: 0
        }
        Rectangle {
            anchors.fill: parent
            radius: 3 * root.s
            color: "transparent"
            border.color: glassButton.hovered || glassButton.activeFocus ? "#80b0ccee" : "#40708898"
            border.width: 1 * root.s
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1 * root.s
            radius: 2 * root.s
            gradient: Gradient {
                GradientStop { position: 0.0; color: glassButton.hovered || glassButton.activeFocus ? "#88c8e0ff" : "#60a0c4e8" }
                GradientStop { position: 0.5; color: glassButton.hovered || glassButton.activeFocus ? "#666090b4" : "#404870a0" }
                GradientStop { position: 1.0; color: glassButton.hovered || glassButton.activeFocus ? "#886090b8" : "#505888b0" }
            }
        }
        Rectangle {
            anchors.top: parent.top
            anchors.topMargin: 1 * root.s
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 2 * root.s
            height: 9 * root.s
            radius: 3 * root.s
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#40ffffff" }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
        Text {
            id: pwText
            anchors.centerIn: parent
            text: parent.label
            textFormat: Text.PlainText
            font.family: root.uiFont
            font.pixelSize: 13 * root.s
            color: "white"
        }
        Canvas {
            anchors.fill: parent
            anchors.margins: 3 * root.s
            visible: glassButton.activeFocus
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = "#bfffffff"
                ctx.lineWidth = 1
                ctx.setLineDash([root.s, root.s])
                ctx.strokeRect(0.5, 0.5, width - 1, height - 1)
            }
        }
    }
}
