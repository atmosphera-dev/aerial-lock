import QtQuick
import InterActive

// The projection owns uint64 accounting, unknown and over-cap normalization.
IAProgressBar {
    required property var facts
    implicitWidth: Insets.controlLane
    visible: facts.usageKnown
    value: facts.storageFraction
    Accessible.name: facts.usage + ", " + facts.free
}
