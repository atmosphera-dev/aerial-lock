import QtQuick
import QtQuick.Controls as C
import AerialLock.Settings

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
    width: 920
    height: 760
    minimumWidth: 720
    minimumHeight: 540
    title: "Aerial"

    function select(key: string): void {
        const destination = destinations.find(entry => entry.key === key);
        if (destination) page = destination.index;
    }

    Shell {
        anchors.fill: parent
        pages: root.connected ? root.destinations : root.destinations.filter(entry => entry.key === "about")
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
            title: root.controller.panel === "diagnostic" ? "Settings need attention" : "The aerial service isn't running"
            description: root.controller.diagnostic
            actionText: "Try again"
            onActionRequested: root.controller.retry()
        }
    }
    AddSourceDialog {
        parent: C.Overlay.overlay
        visible: root.controller.sources.sheetOpen
        draft: root.controller.sources.draft
        appleAvailable: root.controller.sources.appleAvailable
        onFieldEdited: (key, value) => root.controller.sources.field(key, value)
        onChoosePathRequested: root.controller.sources.choosePath()
        onSaveRequested: root.controller.sources.save()
        onDismissed: root.controller.sources.dismiss()
    }
    Connections {
        target: root.controller
        function onPageRequested(key: string): void { root.select(key); }
    }
}
