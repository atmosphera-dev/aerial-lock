// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick

Rectangle {
    property int orientation: Qt.Horizontal
    property real thickness: Appearance.metrics.hairlineWidth / Screen.devicePixelRatio
    color: Appearance.colors.separator
    implicitWidth: orientation === Qt.Vertical ? thickness : 0
    implicitHeight: orientation === Qt.Horizontal ? thickness : 0
}
