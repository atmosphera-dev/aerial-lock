pragma Singleton
import QtQuick
import InterActive.Foundation as IA

QtObject {
    readonly property real edge: IA.Appearance.metrics.spacingXL
    readonly property real controlLane: 180
}
