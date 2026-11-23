import QtQuick
import QtQuick.Templates as T
import InterActive

SettingsDialog {
    signal confirmed()
    title: "Turn on automatic locking"
    standardButtons: T.Dialog.Apply | T.Dialog.Cancel
    onOpened: {
        const button = standardButton(T.Dialog.Apply);
        button.text = "Turn on";
        button.objectName = "idle-turn-on";
    }
    onApplied: { confirmed(); close(); }
    IALabel {
        width: parent.width
        wrapMode: Text.Wrap
        text: "Use one idle owner. If swayidle, hypridle or another daemon already handles locking, turn that setup off yourself before enabling aerial-idle. Aerial will not change other daemons or desktop configuration."
    }
}
