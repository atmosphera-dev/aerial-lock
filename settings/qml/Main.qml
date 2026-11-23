import QtQuick
import QtQuick.Controls as C
import AerialLock.Settings
import InterActive
import InterActive.Foundation as IA

C.ApplicationWindow {
    id: root
    required property SettingsController controller
    property int page: SettingsController.OverviewPage
    readonly property string intervalText: controller.controls["acquisition.checkIntervalSeconds"].choices[
        controller.controls["acquisition.checkIntervalSeconds"].selected]?.label ?? ""
    readonly property var destinations: [
        { index: SettingsController.OverviewPage, key: "overview", title: "Overview", icon: Qt.resolvedUrl("icons/sky.svg") },
        { index: SettingsController.SourcesPage, key: "sources", title: "Sources", icon: Qt.resolvedUrl("icons/stack.svg") },
        { index: SettingsController.DownloadsPage, key: "downloads", title: "Downloads", icon: Qt.resolvedUrl("icons/download.svg") },
        { index: SettingsController.LockPage, key: "lock", title: "Lock screen", icon: Qt.resolvedUrl("icons/lock.svg") },
        { index: SettingsController.AboutPage, key: "about", title: "About", icon: Qt.resolvedUrl("icons/info.svg") }
    ]
    readonly property bool connected: controller.panel === "settings"
    readonly property bool configurationBlocked: controller.panel === "diagnostic"
    width: 920
    height: 760
    minimumWidth: 640
    minimumHeight: 480
    title: "Aerial"

    function select(key: string): void {
        const destination = destinations.find(entry => entry.key === key);
        if (destination) page = destination.index;
    }

    Shell {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: portalFeedback.visible ? portalFeedback.top : parent.bottom
        pages: root.connected ? root.destinations : root.destinations.filter(entry => entry.key === "overview" || entry.key === "about")
        currentPage: root.destinations.find(entry => entry.index === root.page)?.key ?? "overview"
        onSelected: key => root.select(key)
        Overview {
            objectName: "overviewPage"
            anchors.fill: parent
            visible: root.connected && root.page === SettingsController.OverviewPage
            facts: root.controller.statusModel.facts
            notices: root.controller.notices.entries
            activity: root.controller.cache.activity
            onActionRequested: (actionId, sourceId) => root.controller.dispatchAction(actionId, sourceId)
        }
        SourcesPage {
            objectName: "sourcesPage"
            anchors.fill: parent
            visible: root.connected && root.page === SettingsController.SourcesPage
            sourceModel: root.controller.sources.rowModel
            busy: root.controller.sources.busy
            error: root.controller.sources.error
            onAddRequested: root.controller.sources.begin("")
            onToggled: (id, on) => root.controller.sources.toggle(id, on)
            onEditRequested: id => root.controller.sources.begin(id)
            onRemoveRequested: id => root.controller.sources.remove(id)
            onCheckRequested: id => root.controller.sources.retry(id)
            onOrderStarted: id => root.controller.sources.beginReorder(id)
            onMoveRequested: (id, delta) => root.controller.sources.move(id, delta)
            onDroppedBefore: id => root.controller.sources.dropBefore(id)
        }
        DownloadsPage {
            objectName: "downloadsPage"
            anchors.fill: parent
            visible: root.connected && root.page === SettingsController.DownloadsPage
            controller: root.controller
        }
        LockScreenPage {
            objectName: "lockPage"
            anchors.fill: parent
            visible: root.connected && root.page === SettingsController.LockPage
            controller: root.controller
        }
        AboutPage {
            objectName: "aboutPage"
            anchors.fill: parent
            visible: root.page === SettingsController.AboutPage
            controller: root.controller
        }
        EmptyPage {
            objectName: "servicePanel"
            anchors.fill: parent
            visible: !root.connected && root.page !== SettingsController.AboutPage
            title: root.configurationBlocked ? "Settings need attention" : "The aerial service isn't available"
            description: root.configurationBlocked
                ? "Fix the settings file, then try again. The file is left unchanged.\n\n" + root.controller.diagnostic
                : root.controller.diagnostic
            actionText: "Try again"
            secondaryText: root.configurationBlocked ? "Open folder"
                : root.controller.systemd.available ? "Start service" : ""
            error: root.controller.systemd.error
            onActionRequested: root.controller.dispatchAction("retry-service")
            onSecondaryRequested: root.controller.dispatchAction(root.configurationBlocked ? "open-settings-folder" : "start-service")
        }
    }
    IALabel {
        id: portalFeedback
        objectName: "portalError"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Insets.edge
        visible: text.length > 0
        text: root.controller.portal.error
        foregroundColor: IA.Appearance.colors.secondaryText
        wrapMode: Text.Wrap
    }
    AddSourceDialog {
        objectName: "sourceSheet"
        parent: C.Overlay.overlay
        visible: root.controller.sources.sheetOpen
        draft: root.controller.sources.draft
        appleAvailable: root.controller.sources.appleAvailable
        onFieldEdited: (key, value) => root.controller.sources.field(key, value)
        onChoosePathRequested: root.controller.sources.choosePath()
        onSaveRequested: root.controller.sources.save()
        onDismissed: root.controller.sources.dismiss()
    }
    IdleDialog {
        id: idleDialog
        objectName: "idleDialog"
        onConfirmed: root.controller.confirmIdleEnable()
    }
    Connections {
        target: root.controller
        function onPageRequested(key: string): void { root.select(key); }
        function onIdleConfirmationRequested(): void { idleDialog.open(); }
    }
}
