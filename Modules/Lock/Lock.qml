import QtQuick
import Quickshell
import AerialLock.Native
import Quickshell.Wayland
import "../../Services"

Scope {
    id: root

    required property ConfigStore config
    required property Logger logger

    signal unlocked()
    signal unlockRequested()

    property bool locked: true
    property bool unlockHandled: false
    // The compositor's ext-session-lock `locked` confirmation for this lock.
    readonly property bool secure: root.lockEndpoint.secure
    // Construct-time native boundary seam; normal shells use WlSessionLock.
    property var sessionLockOverride: null
    readonly property var lockEndpoint: root.sessionLockOverride || sessionLock

    Binding {
        target: root.lockEndpoint
        property: "locked"
        value: root.locked
        when: root.sessionLockOverride !== null
    }

    // d43: the response accepted from the shared draft, queued for PAM's
    // first password prompt (first prompt with leading-space filtering off).
    // null = no queued response. Empty initial submissions
    // don't start PAM; empty answers to active prompts go straight to PAM. Cleared
    // before pam.respond, and on failure, abort and teardown.
    property var acceptedResponse: null
    readonly property bool ignoreLeadingSpaces: root.config.data.ignoreLeadingSpaces === true
    // True exactly while a PAM call is in flight (start/respond until its
    // next prompt, completion or error). The PAM watchdog follows it: it never
    // runs while PAM waits for the user.
    property bool unlockInProgress: false
    onUnlockInProgressChanged: {
        if (unlockInProgress)
            watchdog.restart()
        else
            watchdog.stop()
    }
    property bool attemptCancelled: false
    property string authStatusMessage: ""
    property bool responseLimitHint: false
    readonly property string statusMessage: responseLimitHint
            ? (i18n.passwordTooLong || "Password too long (maximum %1 characters)")
                .replace("%1", String(lockContext.auth.maxResponseSize - 1))
            : authStatusMessage
    property int authState: authIdle

    readonly property int authIdle: 0
    readonly property int authPrompting: 1
    readonly property int authAuthenticating: 2
    readonly property int authFailed: 3
    readonly property int authMaxTries: 4
    readonly property int authError: 5
    readonly property int authSuccess: 6
    readonly property bool authIsError: authState === authFailed
            || authState === authMaxTries
            || authState === authError

    function authStateName(state) {
        switch (state) {
        case root.authIdle: return "idle"
        case root.authPrompting: return "prompting"
        case root.authAuthenticating: return "authenticating"
        case root.authFailed: return "failed"
        case root.authMaxTries: return "max-tries"
        case root.authError: return "error"
        case root.authSuccess: return "success"
        }
        return "unknown"
    }

    // The auth owner emits once, before a synchronous PAM callback can move
    // it again. Reasons are fixed labels, never prompts, responses or input.
    function setAuthState(state, reason) {
        if (root.authState === state)
            return
        logger.i("lock-state", "auth " + root.authStateName(root.authState)
                 + " -> " + root.authStateName(state) + " reason=" + reason)
        root.authState = state
    }

    function requestUnlock(reason) {
        if (!root.locked)
            return
        root.powerEndpoint.cancel()
        logger.i("lock-state", "lock " + (root.secure ? "engaged" : "requested")
                 + " -> unlock-requested reason=" + reason)
        root.unlockRequested()
        root.locked = false
        // Read the native getters after the setter returns, even if it omitted
        // its notify signal (Quickshell 0.3.2). Also escape a setter that is
        // still realizing its target; notifications can arrive reentrantly.
        Qt.callLater(root.observeRelease)
        root.observeRelease()
    }

    function observeRelease() {
        if (root.unlockHandled || root.lockEndpoint.locked || root.lockEndpoint.secure)
            return
        root.unlockHandled = true
        // Native surfaces can retire before the shell's deferred teardown.
        // Confirmed release is terminal: do not migrate the panel between
        // disappearing surfaces while the release stack is still unwinding.
        root.tearingDown = true
        logger.i("lock-state", "lock " + (root.locked ? "requested-or-engaged" : "unlock-requested")
                 + " -> released reason=" + (root.locked ? "compositor-invalidated" : "compositor-unlocked"))
        if (root.locked)
            logger.w("aerial-lock", "lock invalidated externally by compositor")
        root.unlocked()
    }

    readonly property var i18n: config.i18n

    // Construct-time test seam (Tests/lock-lifecycle-shell.qml); the
    // production shell never overrides the real PAM context.
    property var pamEndpoint: pam
    // Construct-time platform boundary, following pamEndpoint. Production
    // always uses the native owner; the real-session test supplies its peer.
    property var powerEndpoint: powerActions
    readonly property bool powerEnabled: root.config.data.powerActions
            ? root.config.data.powerActions.enabled : false
    readonly property LockContext context: lockContext

    property string panelScreenName: ""
    property bool themeFallbackPinned: false
    property bool themeTopicsSet: false

    // Output failover: every lock surface registers {surface, screen,
    // attachment (its content item), background}. The panel output is
    // selected among *usable* registrations — a registered surface whose
    // screen is still in Quickshell.screens — and stays pinned while it
    // exists. A nonempty screen list alone does not prove a lock surface.
    property var surfaceRecords: []
    property bool tearingDown: false

    // Video background resolution (d36/d40): SourceCache publishes ONE
    // frozen playlist per lock after its nonblocking inventory resolves
    // (theme topics applied, 2000 ms deadline). Solid background while it
    // resolves; authentication never waits on it. LockBackground.qml is
    // the only MpvQml importer and only activates with a nonempty list —
    // libmpv never starts for nothing. qt6-mpvqml itself is a hard
    // dependency (README).
    readonly property var bgItems: sourceCache.playlist
    readonly property bool hasBackground: sourceCache.resolved && bgItems.length > 0
    readonly property var selectedSurface: {
        var usable = root.usableSurfaces()
        for (var i = 0; i < usable.length; i++) {
            if (usable[i].screen.name === root.panelScreenName)
                return usable[i]
        }
        return null
    }
    // The one producer of the shared video/accent presentation.
    readonly property LockBackground selectedBackground: selectedSurface ? selectedSurface.background : null
    // Private per-locker runtime directory created by the launch wrapper
    // (Scripts/aerial-lock-run). Frame grabs never fall back to /tmp:
    // without it, accent sensing is simply off.
    readonly property string frameDirectory: Quickshell.env("AERIAL_LOCK_RUNTIME_DIR") || ""

    SourceCache {
        id: sourceCache
        config: root.config
    }

    CompanionClient { id: companion }

    // d38: the one context object the panel (and future themes) consume.
    // Lock.qml owns every binding into it; LockContext itself knows no
    // PAM, no MpvQml, no pixels.
    //
    // Grouped-property syntax on alias groups (auth.status: …) compiles
    // under qmllint but the runtime rejects it ("Cannot assign to
    // non-existent property") — aliases carry no static group type. Bind
    // via Binding elements instead.
    LockContext {
        id: lockContext

        Component.onCompleted: {
            auth.input.normalizeDraft = draft => root.normalizePasswordDraft(draft)
            submitRequested.connect(root.tryUnlock)
            dismissRequested.connect(root.dismissRequested)
            powerRequested.connect(root.requestPower)
            powerConfirmRequested.connect(function () { root.powerEndpoint.confirm() })
            powerCancelRequested.connect(function () { root.powerEndpoint.cancel() })
            powerErrorDismissRequested.connect(function () { root.powerEndpoint.clearError() })
        }
    }

    Binding { target: lockContext.auth; property: "status"; value: root.statusMessage }
    Binding { target: lockContext.auth; property: "inProgress"; value: root.unlockInProgress }
    Binding { target: lockContext.auth; property: "isError"; value: root.authIsError }
    // PAM retains the previous echo mode after completion/abort. It is
    // meaningful only for a response requested by the active conversation.
    Binding { target: lockContext.auth; property: "responseVisible"; value: root.pamEndpoint.active && root.pamEndpoint.responseRequired && root.pamEndpoint.responseVisible }
    Binding { target: lockContext.auth; property: "maxResponseSize"; value: config.pamLimits.maxResponseSize }
    Binding { target: lockContext.auth; property: "dismissAvailable"; value: config.buildFlags.debugAllowDismiss }

    PowerActions { id: powerActions }
    Binding { target: root.powerEndpoint; property: "enabled"; value: root.powerEnabled }
    Binding { target: root.powerEndpoint; property: "secure"; value: root.locked && root.secure && !root.tearingDown }
    Binding { target: lockContext.power; property: "enabled"; value: root.powerEnabled }
    Binding { target: lockContext.power; property: "canSuspend"; value: root.powerEndpoint.canSuspend }
    Binding { target: lockContext.power; property: "canHibernate"; value: root.powerEndpoint.canHibernate }
    Binding { target: lockContext.power; property: "canReboot"; value: root.powerEndpoint.canReboot }
    Binding { target: lockContext.power; property: "canPowerOff"; value: root.powerEndpoint.canPowerOff }
    Binding { target: lockContext.power; property: "suspendReason"; value: root.powerReason(root.powerEndpoint.suspendReason) }
    Binding { target: lockContext.power; property: "hibernateReason"; value: root.powerReason(root.powerEndpoint.hibernateReason) }
    Binding { target: lockContext.power; property: "rebootReason"; value: root.powerReason(root.powerEndpoint.rebootReason) }
    Binding { target: lockContext.power; property: "powerOffReason"; value: root.powerReason(root.powerEndpoint.powerOffReason) }
    Binding { target: lockContext.power; property: "state"; value: root.powerEndpoint.state }
    Binding { target: lockContext.power; property: "pendingAction"; value: root.powerEndpoint.pendingAction }
    Binding { target: lockContext.power; property: "activeAction"; value: root.powerEndpoint.activeAction }
    Binding { target: lockContext.power; property: "blockerWho"; value: root.powerEndpoint.blockerWho }
    Binding { target: lockContext.power; property: "blockerWhy"; value: root.powerEndpoint.blockerWhy }
    Binding { target: lockContext.power; property: "confirmationRequired"; value: root.powerEndpoint.confirmationRequired }
    Binding { target: lockContext.power; property: "busy"; value: root.powerEndpoint.busy }
    Binding { target: lockContext.power; property: "error"; value: {
            if (root.powerEndpoint.errorCode === "inhibited") {
                var who = root.powerEndpoint.blockerWho
                var why = root.powerEndpoint.blockerWhy
                var pattern = !who ? lockContext.labels.powerBlockedByApplication
                        : why ? lockContext.labels.powerBlockedBy : lockContext.labels.powerBlockedByName
                var values = [root.powerActionLabel(root.powerEndpoint.activeAction), who, why]
                return pattern.replace(/%([123])/g, function(match, number) { return values[Number(number) - 1] })
            }
            if (root.powerEndpoint.errorCode === "failed") {
                switch (root.powerEndpoint.activeAction) {
                case "reboot": return lockContext.labels.powerRestartFailed
                case "powerOff": return lockContext.labels.powerOffFailed
                case "suspend": return lockContext.labels.powerSuspendFailed
                case "hibernate": return lockContext.labels.powerHibernateFailed
                }
            }
            return root.powerReason(root.powerEndpoint.errorCode)
        } }
    Binding { target: lockContext.power; property: "status"; value: {
            if (root.powerEndpoint.state === "checking") return lockContext.labels.powerChecking
            if (root.powerEndpoint.state === "executing" || root.powerEndpoint.state === "waiting") {
                switch (root.powerEndpoint.pendingAction) {
                case "reboot": return lockContext.labels.powerRestarting
                case "powerOff": return lockContext.labels.powerShuttingDown
                case "suspend": return lockContext.labels.powerSuspending
                case "hibernate": return lockContext.labels.powerHibernating
                }
            }
            return ""
        } }

    function powerActionLabel(action) {
        switch (action) {
        case "suspend": return lockContext.labels.powerSuspend
        case "hibernate": return lockContext.labels.powerHibernate
        case "reboot": return lockContext.labels.powerReboot
        case "powerOff": return lockContext.labels.powerOff
        }
        return ""
    }

    function powerReason(reason) {
        switch (reason) {
        case "": return ""
        case "unknown": return lockContext.labels.powerChecking
        case "unsupported": return lockContext.labels.powerNotSupported
        case "authorization": return lockContext.labels.powerAuthorizationRequired
        case "denied": return lockContext.labels.powerDenied
        case "inhibited": return lockContext.labels.powerInhibited
        case "service": return lockContext.labels.powerServiceUnavailable
        case "uncertain": return lockContext.labels.powerUncertain
        case "cancelled": return lockContext.labels.powerCancelled
        case "in-progress": return lockContext.labels.powerAlreadyInProgress
        case "failed": return lockContext.labels.powerFailed
        }
        return lockContext.labels.powerUnavailable
    }

    function requestPower(action) {
        if (!root.powerEnabled || !root.locked || !root.secure || root.tearingDown
                || lockContext.power.interactionActive)
            return
        var available = action === "suspend" ? root.powerEndpoint.canSuspend
                : action === "hibernate" ? root.powerEndpoint.canHibernate
                : action === "reboot" ? root.powerEndpoint.canReboot
                : action === "powerOff" ? root.powerEndpoint.canPowerOff : false
        if (available) {
            root.attemptCancelled = true
            root.acceptedResponse = null
            root.pamEndpoint.abort()
            root.enterPrompting("", "power-interaction")
        }
        root.powerEndpoint.request(action)
    }

    Connections {
        target: lockContext.power
        function onInteractionActiveChanged() {
            if (!lockContext.power.interactionActive && root.locked)
                Qt.callLater(themeHost.focusInput)
        }
    }

    // A hint overlays the current auth status only until the next content
    // change. Every PAM status update replaces it, even an identical prompt.
    function setAuthStatus(message) {
        root.responseLimitHint = false
        root.authStatusMessage = message
    }

    Connections {
        target: lockContext.auth.input
        function onResponseLimitRejected() {
            if (lockContext.auth.maxResponseSize > 0 && !root.unlockInProgress)
                root.responseLimitHint = true
        }
        function onTextChanged() { root.responseLimitHint = false }
    }

    Connections {
        target: lockContext.auth
        function onMaxResponseSizeChanged() { root.responseLimitHint = false }
    }

    Binding { target: lockContext.session; property: "outputName"; value: root.panelScreenName }
    Binding { target: lockContext.session; property: "loginName"; value: root.pamEndpoint.user }
    Binding { target: lockContext.session; property: "avatarPath"; value: {
            var home = Quickshell.env("HOME")
            return home && home.length > 0 ? home + "/.face" : ""
        } }

    // d40 presentation projections: the panel and themes read metrics,
    // colors and labels from the context alone — neither ConfigStore nor
    // Logger crosses into theme code. Labels carry the shipped English
    // fallback so a theme never renders an empty prompt.
    Binding { target: lockContext.panel; property: "widthMax"; value: config.data.panel.widthMax }
    Binding { target: lockContext.panel; property: "fieldHeight"; value: config.data.panel.fieldHeight }
    Binding { target: lockContext.panel; property: "radius"; value: config.data.panel.radius }
    Binding { target: lockContext.panel; property: "fieldRadius"; value: config.data.panel.fieldRadius }
    Binding { target: lockContext.panel; property: "outerMargin"; value: config.data.panel.outerMargin }
    Binding { target: lockContext.panel; property: "spacing"; value: config.data.panel.spacing }
    Binding { target: lockContext.panel; property: "fontSize"; value: config.data.panel.fontSize }

    Binding { target: lockContext.colors; property: "text"; value: config.data.colors.text }
    Binding { target: lockContext.colors; property: "textError"; value: config.data.colors.textError }
    Binding { target: lockContext.colors; property: "input"; value: config.data.colors.input }
    Binding { target: lockContext.colors; property: "panelFill"; value: config.data.colors.panelFill }
    Binding { target: lockContext.colors; property: "panelBorder"; value: config.data.colors.panelBorder }
    Binding { target: lockContext.colors; property: "fieldFill"; value: config.data.colors.fieldFill }
    Binding { target: lockContext.colors; property: "fieldBorderFocused"; value: config.data.colors.fieldBorderFocused }
    Binding { target: lockContext.colors; property: "fieldBorder"; value: config.data.colors.fieldBorder }

    Binding { target: lockContext.labels; property: "prompt"; value: i18n.prompt || "Enter password" }
    Binding { target: lockContext.labels; property: "authenticating"; value: i18n.authenticating || "Authenticating..." }
    Binding { target: lockContext.labels; property: "authFailed"; value: i18n.authFailed || "Authentication failed" }
    Binding { target: lockContext.labels; property: "maxTries"; value: i18n.maxTries || "Too many attempts" }
    Binding { target: lockContext.labels; property: "authError"; value: i18n.authError || "Authentication error" }
    Binding { target: lockContext.labels; property: "timedOut"; value: i18n.timedOut || "Authentication timed out — try again" }
    Binding { target: lockContext.labels; property: "dismissDebug"; value: i18n.dismissDebug || "Dismiss (debug)" }
    Binding { target: lockContext.labels; property: "unlocked"; value: i18n.unlocked || "Unlocked" }
    Binding { target: lockContext.labels; property: "powerSuspend"; value: i18n.powerSuspend || "Suspend" }
    Binding { target: lockContext.labels; property: "powerHibernate"; value: i18n.powerHibernate || "Hibernate" }
    Binding { target: lockContext.labels; property: "powerReboot"; value: i18n.powerReboot || "Restart" }
    Binding { target: lockContext.labels; property: "powerOff"; value: i18n.powerOff || "Shut down" }
    Binding { target: lockContext.labels; property: "powerTitlePower"; value: i18n.powerTitlePower || "Power" }
    Binding { target: lockContext.labels; property: "powerTitleReboot"; value: i18n.powerTitleReboot || "Reboot" }
    Binding { target: lockContext.labels; property: "powerTitleSleep"; value: i18n.powerTitleSleep || "Sleep" }
    Binding { target: lockContext.labels; property: "powerTitleShutDown"; value: i18n.powerTitleShutDown || "Shut Down" }
    Binding { target: lockContext.labels; property: "powerTitleShutdown"; value: i18n.powerTitleShutdown || "Shutdown" }
    Binding { target: lockContext.labels; property: "powerConfirmQuestion"; value: i18n.powerConfirmQuestion || "Are you sure?" }
    Binding { target: lockContext.labels; property: "powerCancel"; value: i18n.powerCancel || "Cancel" }
    Binding { target: lockContext.labels; property: "powerDismiss"; value: i18n.powerDismiss || "Dismiss" }
    Binding { target: lockContext.labels; property: "powerChecking"; value: i18n.powerChecking || "Checking availability..." }
    Binding { target: lockContext.labels; property: "powerRequesting"; value: i18n.powerRequesting || "Requesting %1..." }
    Binding { target: lockContext.labels; property: "powerRestarting"; value: i18n.powerRestarting || "Restarting..." }
    Binding { target: lockContext.labels; property: "powerShuttingDown"; value: i18n.powerShuttingDown || "Shutting down..." }
    Binding { target: lockContext.labels; property: "powerSuspending"; value: i18n.powerSuspending || "Suspending..." }
    Binding { target: lockContext.labels; property: "powerHibernating"; value: i18n.powerHibernating || "Hibernating..." }
    Binding { target: lockContext.labels; property: "powerRestartFailed"; value: i18n.powerRestartFailed || "The system could not restart." }
    Binding { target: lockContext.labels; property: "powerOffFailed"; value: i18n.powerOffFailed || "The system could not shut down." }
    Binding { target: lockContext.labels; property: "powerSuspendFailed"; value: i18n.powerSuspendFailed || "The system could not suspend." }
    Binding { target: lockContext.labels; property: "powerHibernateFailed"; value: i18n.powerHibernateFailed || "The system could not hibernate." }
    Binding { target: lockContext.labels; property: "powerBlockedBy"; value: i18n.powerBlockedBy || "%1 is blocked by %2: %3" }
    Binding { target: lockContext.labels; property: "powerBlockedByName"; value: i18n.powerBlockedByName || "%1 is blocked by %2." }
    Binding { target: lockContext.labels; property: "powerBlockedByApplication"; value: i18n.powerBlockedByApplication || "%1 is blocked by an application." }
    Binding { target: lockContext.labels; property: "powerUnavailable"; value: i18n.powerUnavailable || "This action is not available." }
    Binding { target: lockContext.labels; property: "powerNotSupported"; value: i18n.powerNotSupported || "This system does not support this action." }
    Binding { target: lockContext.labels; property: "powerAuthorizationRequired"; value: i18n.powerAuthorizationRequired || "This action needs additional authorization and is unavailable while locked." }
    Binding { target: lockContext.labels; property: "powerDenied"; value: i18n.powerDenied || "The system did not allow this action." }
    Binding { target: lockContext.labels; property: "powerInhibited"; value: i18n.powerInhibited || "An application is preventing this action. Try again after it finishes." }
    Binding { target: lockContext.labels; property: "powerServiceUnavailable"; value: i18n.powerServiceUnavailable || "System power controls are unavailable. Try again later." }
    Binding { target: lockContext.labels; property: "powerFailed"; value: i18n.powerFailed || "The system could not complete this action." }
    Binding { target: lockContext.labels; property: "powerUncertain"; value: i18n.powerUncertain || "The system did not confirm the request. It may still happen." }
    Binding { target: lockContext.labels; property: "powerCancelled"; value: i18n.powerCancelled || "The system cancelled this action." }
    Binding { target: lockContext.labels; property: "powerAlreadyInProgress"; value: i18n.powerAlreadyInProgress || "A system action is already in progress." }

    // Video playback export for LockContext — exactly one producer, the
    // selected surface's background (null when there is none).
    Binding { target: lockContext.video; property: "position"; value: root.selectedBackground ? root.selectedBackground.position : 0 }
    Binding { target: lockContext.video; property: "duration"; value: root.selectedBackground ? root.selectedBackground.duration : 0 }
    Binding { target: lockContext.video; property: "hasVideo"; value: root.selectedBackground ? root.selectedBackground.hasVideo : false }
    Binding { target: lockContext.video; property: "path"; value: root.selectedBackground ? root.selectedBackground.currentFilename : "" }
    Binding { target: lockContext.video; property: "accent"; value: root.selectedBackground ? root.selectedBackground.accentColor : "transparent" }

    Connections {
        target: lockContext.video
        function onPathChanged() { root.updateVideoInfo(lockContext.video.path) }
    }

    Connections {
        target: lockContext.video
        function onPositionChanged() { root.updatePoi(lockContext.video.position) }
    }

    function dismissRequested() {
        logger.d("Lock", "dismissRequested received")
        if (root.config.buildFlags.debugAllowDismiss) {
            root.attemptCancelled = true
            root.pamEndpoint.abort()
            logger.d("Lock", "dismiss: setting root.locked = false")
            root.requestUnlock("dev-dismiss")
        }
    }

    // Explicit external release uses the same WlSessionLock/teardown path as
    // PAM success. Cancel first: abort may synchronously complete the attempt.
    function release() {
        if (!root.locked)
            return
        root.attemptCancelled = true
        root.acceptedResponse = null
        root.unlockInProgress = false
        root.pamEndpoint.abort()
        root.requestUnlock("external-release")
    }

    function updateVideoInfo(path) {
        var info = sourceCache.lookup(path)
        lockContext.video.id = (info && info.id) ? info.id : ""
        lockContext.video.name = (info && info.name) ? info.name : ""
        lockContext.video.secondaryName = (info && info.secondaryName) ? info.secondaryName : ""
        lockContext.video.scene = (info && info.scene) ? info.scene : ""
        lockContext.video.timeOfDay = (info && info.timeOfDay) ? info.timeOfDay : ""
        lockContext.video.sourceName = (info && info.sourceName) ? info.sourceName : ""
        lockContext.poi.map = (info && info.poi) ? info.poi : ({})
        logger.d("LockContext", "video: id=" + lockContext.video.id
                 + " name=" + lockContext.video.name
                 + " source=" + lockContext.video.sourceName)
        updatePoi(lockContext.video.position)
    }

    function updatePoi(position) {
        var map = lockContext.poi.map
        var text = ""
        var at = -1
        var nums = []
        for (var k in map)
            nums.push(Number(k))
        nums.sort(function (a, b) { return a - b })
        for (var j = 0; j < nums.length; j++) {
            if (position >= nums[j])
                at = nums[j]
            else
                break
        }
        if (at >= 0)
            text = map[String(at)] || ""
        if (lockContext.poi.text !== text || lockContext.poi.at !== at) {
            lockContext.poi.text = text
            lockContext.poi.at = at
            logger.d("LockContext", "poi: text=\"" + text + "\" at=" + at
                     + " position=" + position.toFixed(1))
        }
    }

    // d40: the configured whole-surface theme (or the built-in panel as
    // ThemeHost's fallback). Untrusted QML is never the surface component:
    // the host is owned by this Scope (QObject parent) and only its visual
    // parent moves, through attachTo(), into the selected trusted surface.
    // One theme/editor instance for the whole lock, so moving outputs keeps
    // the draft, selection, composition and PAM conversation.
    ThemeHost {
        id: themeHost
        parent: null
        anchors.fill: parent
        visible: parent !== null
        enabled: visible
        focus: visible && themeHost.editorActive
        context: lockContext
        config: root.config
        logger: root.logger
        allowLoading: sessionLock.secure && root.locked
        fallbackPinned: root.themeFallbackPinned
        onFallbackRequested: root.themeFallbackPinned = true
        onReady: root.publishThemeTopics()
    }

    onSelectedSurfaceChanged: attachPanel()
    onSelectedBackgroundChanged: attachPanel()

    // Scenery-only backdrop for themes that declare a `backdrop` property:
    // the selected surface's background, null when the video background is
    // inactive — themes must tolerate that.
    function attachPanel() {
        if (root.tearingDown)
            return
        var selected = root.selectedSurface
        themeHost.attachTo(selected ? selected.attachment : null, root.selectedBackground)
        root.publishThemeTopics()
    }

    // d40: the theme's topic preference is known once its content is loaded
    // (fallback panel: none). Start the inventory then — its snapshot is
    // frozen per lock and gates the background.
    function publishThemeTopics() {
        if (!sessionLock.secure || !themeHost.settled || root.themeTopicsSet)
            return
        root.themeTopicsSet = true
        sourceCache.topics = themeHost.preferredTopics
        sourceCache.begin()
        // The supervisor's episode snapshot and the local reader pin precede
        // this optional hint. It never waits for inventory or acquisition.
        var height = 0
        var screens = Quickshell.screens || []
        for (var i = 0; i < screens.length; i++)
            height = Math.max(height, screens[i].height)
        companion.ensureContent(Quickshell.shellDir, root.config.profile, height, sourceCache.topics)
    }

    Compositor {
        id: compositor
    }

    function usableSurfaces() {
        var result = []
        var screens = Quickshell.screens || []
        for (var i = 0; i < root.surfaceRecords.length; i++) {
            var record = root.surfaceRecords[i]
            if (!record.screen || !record.attachment)
                continue
            for (var j = 0; j < screens.length; j++) {
                if (screens[j] === record.screen) {
                    result.push(record)
                    break
                }
            }
        }
        return result
    }

    function registerSurface(surface, screen, attachment, background) {
        if (root.tearingDown)
            return
        var records = root.surfaceRecords.filter(function (record) { return record.surface !== surface })
        records.push({ surface: surface, screen: screen, attachment: attachment, background: background })
        root.surfaceRecords = records
        root.tryResolvePanel()
    }

    function unregisterSurface(surface) {
        if (root.tearingDown)
            return
        root.surfaceRecords = root.surfaceRecords.filter(function (record) { return record.surface !== surface })
        root.reconcilePanel(true)
    }

    // The compositor's focused output when it is a usable surface, else the
    // first usable surface — never a screen without a lock surface. "" parks
    // the editor until a surface registers.
    function resolvePanelScreen() {
        var records = root.usableSurfaces()
        var focused = compositor.focusedOutputName
        for (var i = 0; i < records.length; i++) {
            if (records[i].screen.name === focused)
                return focused
        }
        if (focused && focused.length > 0 && records.length > 0)
            logger.w("aerial-lock", "focused output \"" + focused
                     + "\" has no usable lock surface; using " + records[0].screen.name)
        return records.length ? records[0].screen.name : ""
    }

    function reconcilePanel(immediate) {
        if (root.tearingDown)
            return
        var records = root.usableSurfaces()
        for (var i = 0; i < records.length; i++) {
            if (records[i].screen.name === root.panelScreenName)
                return // keep the selected usable surface pinned
        }
        var hadSelection = root.panelScreenName !== ""
        if (hadSelection || immediate || compositor.focusedOutputName !== "" || compositor.backendName === "none") {
            resolveGrace.stop()
            root.panelScreenName = root.resolvePanelScreen()
        } else if (compositor.backendReady) {
            // Give the compositor backend a moment to report focus.
            resolveGrace.restart()
        }
        root.attachPanel()
    }

    function tryResolvePanel() { root.reconcilePanel(false) }

    Connections {
        target: Quickshell
        function onScreensChanged() { root.reconcilePanel(true) }
    }

    Timer {
        id: resolveGrace
        interval: 50
        repeat: false
        onTriggered: {
            root.panelScreenName = root.resolvePanelScreen()
            root.attachPanel()
        }
    }

    Component.onCompleted: {
        logger.d("Lock", "Component.onCompleted")
        if (root.locked)
            logger.i("lock-state", "lock idle -> requested reason=startup")
        root.tryResolvePanel()
        root.enterPrompting("", "initial-prompt")
    }

    Connections {
        target: compositor
        function onBackendReadyChanged() {
            root.tryResolvePanel()
        }
        function onFocusedOutputNameChanged() {
            root.tryResolvePanel()
        }
    }

    WlSessionLock {
        id: sessionLock
        locked: root.sessionLockOverride === null && root.locked

        WlSessionLockSurface {
            id: lockSurface
            color: config.data.backgroundColor

            property bool complete: false
            function register() {
                if (complete)
                    root.registerSurface(lockSurface, lockSurface.screen, lockSurface.contentItem, bgLoader.background)
            }
            Component.onCompleted: { complete = true; register() }
            onScreenChanged: register()
            Component.onDestruction: root.unregisterSurface(lockSurface)

            // Only these native windows are eligible for pre-focus key
            // retention. Settings, themes' auxiliary windows and popups are not.
            readonly property var inputWindow: surfaceInput.surfaceWindow
            Binding {
                target: lockSurface.inputWindow
                property: "objectName"
                value: "aerial-lock-surface"
                when: lockSurface.inputWindow !== null
            }

            // A window can receive its first key before Qt's activation-time
            // focus resolution. Construct its editor with the surface, directly
            // in the window's focus scope, rather than behind a Loader scope.
            // The panel surface leaves input exclusively to its theme's field.
            AuthInputSink {
                id: surfaceInput
                panelAttached: root.selectedSurface !== null && root.selectedSurface.surface === lockSurface
                input: lockContext.auth.input
                inputBusy: lockContext.auth.inProgress || lockContext.power.interactionActive
                responseVisible: lockContext.auth.responseVisible
                maxResponseSize: lockContext.auth.maxResponseSize
                onSubmitRequested: lockContext.auth.submit()
            }

            // Video background (d32) — behind the theme (first child
            // paints last-on-bottom). Never eager: active stays false when
            // no source is configured, so libmpv is not started for
            // nothing. This is resource isolation, not plugin-optionality:
            // qt6-mpvqml is required.
            Loader {
                id: bgLoader
                anchors.fill: parent
                active: root.hasBackground
                source: "LockBackground.qml"
                readonly property LockBackground background: item as LockBackground

                onLoaded: {
                    // LockBackground.applySource() runs on the items change;
                    // the list is final by the time the Loader activates.
                    background.items = Qt.binding(function () { return root.bgItems })
                    background.playbackFailed.connect(function (path) {
                        sourceCache.playbackFailed(path)
                    })
                    background.loadFailed.connect(function (message) {
                        root.logger.w("aerial-lock", "video background failed: " + message)
                    })
                    background.frameObserved.connect(function (path) {
                        sourceCache.reportPlayback(path)
                        var info = sourceCache.lookup(path)
                        if (info && info.assetKey)
                            companion.reportPlayback(root.config.profile, info.assetKey)
                    })
                    // Only the consumed (selected) background samples accents.
                    background.frameDirectory = root.frameDirectory
                    background.samplingActive = Qt.binding(function () {
                        return bgLoader.background !== null && root.selectedBackground === bgLoader.background
                    })
                    lockSurface.register()
                }
                onStatusChanged: {
                    if (status === Loader.Error)
                        root.logger.w("aerial-lock", "video background unavailable — solid color")
                    lockSurface.register()
                }
            }

            // Stop playback before shell.qml tears the lock tree down on
            // unlock (mpv render context out of the destruction path).
            Connections {
                target: root
                function onLockedChanged() {
                    if (!root.locked && bgLoader.background)
                        bgLoader.background.shutdown()
                }
            }
        }
    }

    // Every auth state is entered through these two, so a state set before
    // a (possibly synchronous) PAM call can never overwrite the state that
    // call's own message/completion produced.
    function enterPrompting(message, reason) {
        root.unlockInProgress = false       // waiting on USER, not PAM
        root.setAuthState(root.authPrompting, reason || "pam-prompt")
        root.setAuthStatus(message && message.length > 0 ? message : (i18n.prompt || "Enter password"))
    }

    function enterAuthenticating() {
        root.unlockInProgress = true
        root.setAuthState(root.authAuthenticating, "submit")
        root.setAuthStatus(i18n.authenticating || "Authenticating...")
        watchdog.restart() // a new PAM call, even if one was already in flight
    }

    AuthContext {
        id: pam
        config: root.config.pamService
        configDirectory: "/etc/pam.d"
        user: Quickshell.env("USER") || Quickshell.env("LOGNAME") || ""
    }

    Connections {
        target: root.pamEndpoint

        // Conversation is keyed on pamMessage, emitted per message; the
        // change-gated responseRequiredChanged fires only once per
        // conversation on quickshell 0.3.1, so it never sees prompt 2+.
        function onPamMessage() {
            var endpoint = root.pamEndpoint
            logger.d("Lock", "PAM message: " + endpoint.message + " (error=" + endpoint.messageIsError + ")")
            if (endpoint.responseRequired) {
                // PAM is asking for input.
                if (root.acceptedResponse !== null && !(root.ignoreLeadingSpaces && endpoint.responseVisible)) {
                    // Phase 1: the just-submitted response answers this
                    // prompt. Clear before respond so the property holds a
                    // value only across the async gap; the state is set
                    // before respond, which may answer synchronously.
                    var response = root.acceptedResponse.text
                    root.acceptedResponse = null
                    root.enterAuthenticating()
                    endpoint.respond(response)
                } else {
                    // Let the user answer explicitly. If an echo-on prompt
                    // preceded the password prompt, keep the password queued.
                    root.enterPrompting(endpoint.message)
                }
            } else if (endpoint.message && endpoint.message.length > 0) {
                // Informational (fingerprint "touch the sensor", etc.) —
                // PAM is still working; field stays disabled. Traffic during
                // an in-flight call restarts the inactivity watchdog.
                root.setAuthStatus(endpoint.message)
                if (root.unlockInProgress)
                    watchdog.restart()
            }
        }

        function onCompleted(result) {
            watchdog.stop()
            if (root.attemptCancelled) {
                logger.d("Lock", "AuthContext.onCompleted ignored: attempt cancelled")
                return
            }
            logger.d("Lock", "AuthContext.onCompleted: result=" + result +
                " (Success=" + AuthContext.Success + ")")
            root.unlockInProgress = false
            root.acceptedResponse = null
            if (result === AuthContext.Success) {
                root.setAuthState(root.authSuccess, "pam-success")
                root.setAuthStatus(i18n.unlocked || "Unlocked")
                logger.d("Lock", "PAM Success: setting root.locked = false")
                root.requestUnlock("pam-success")
            } else if (result === AuthContext.MaxTries) {
                root.setAuthState(root.authMaxTries, "pam-max-tries")
                root.setAuthStatus(i18n.maxTries || "Too many attempts")
            } else {
                root.setAuthState(root.authFailed, "pam-failed")
                root.setAuthStatus(i18n.authFailed || "Authentication failed")
            }
        }

        function onError(error) {
            watchdog.stop()
            if (root.attemptCancelled) {
                logger.d("Lock", "AuthContext.onError ignored: attempt cancelled")
                return
            }
            root.unlockInProgress = false
            root.acceptedResponse = null
            root.setAuthState(root.authError, "pam-error")
            root.setAuthStatus(i18n.authError || "Authentication error")
        }
    }

    // Watchdog: a PAM transaction can hang without ever asking anything
    // (slow module, hardware-key touch nobody makes, offline pam_sss). This
    // is an inactivity timer, not a wall-clock deadline — any PAM traffic
    // restarts it, so chatty stacks run as long as they need. It runs only
    // while a PAM call is in flight (unlockInProgress); a prompt waiting for
    // the user is never timed out. On fire the abort returns to a fresh
    // prompt; it never unlocks.
    Timer {
        id: watchdog
        interval: config.data.pamWatchdogTimeoutMs
        repeat: false
        running: false
        onTriggered: {
            logger.d("Lock", "PAM watchdog: no PAM traffic for " + interval + "ms — aborting stuck transaction")
            root.attemptCancelled = true
            root.pamEndpoint.abort()
            root.unlockInProgress = false
            root.acceptedResponse = null
            root.setAuthState(root.authError, "timed-out")
            root.setAuthStatus(i18n.timedOut || "Authentication timed out — try again")
        }
    }

    Component.onDestruction: {
        // Surfaces unregister while the tree is torn down; never migrate then.
        root.tearingDown = true
        root.attemptCancelled = true
        root.pamEndpoint.abort()
    }

    Connections {
        target: root.lockEndpoint
        function onSecureStateChanged() {
            logger.d("Lock", "sessionLock.secure changed: secure=" + root.lockEndpoint.secure)
            // Coverage confirmation starts optional theme work; window
            // activation remains the separate keyboard-focus backstop.
            if (root.lockEndpoint.secure) {
                logger.i("lock-state", "lock requested -> engaged reason=compositor-confirmed")
                root.publishThemeTopics()
                Qt.callLater(themeHost.focusInput)
            }
            Qt.callLater(root.observeRelease)
            root.observeRelease()
        }
        function onLockStateChanged() {
            logger.d("Lock", "sessionLock.locked changed: locked=" + root.lockEndpoint.locked +
                " secure=" + root.lockEndpoint.secure +
                " ourIntent=" + root.locked +
                " unlockHandled=" + root.unlockHandled)

            Qt.callLater(root.observeRelease)
            root.observeRelease()
        }
    }

    function normalizePasswordDraft(draft: string): string {
        // Only the initial password draft is filtered, not answers to PAM's
        // interactive prompts. Match literal Space, not other whitespace.
        return root.ignoreLeadingSpaces && !root.pamEndpoint.active ? draft.replace(/^ +/, "") : draft
    }

    function tryUnlock(writer, generation) {
        if (lockContext.power.interactionActive)
            return
        logger.d("Lock", "tryUnlock: unlockInProgress=" + unlockInProgress +
            " pam.responseRequired=" + root.pamEndpoint.responseRequired)
        if (unlockInProgress) {
            return
        }

        // Check after the editor's authority-checked flush, before consumption
        // changes response-scoped input state or status.
        const answeringPrompt = root.pamEndpoint.responseRequired
        if (!answeringPrompt && root.normalizePasswordDraft(lockContext.auth.input.text).length === 0)
            return

        // Snapshot and consume the shared draft exactly once; the consume
        // invalidates stale view publications (all mirrors clear).
        var submission = lockContext.auth.input.consumeSubmission(writer, generation)
        if (submission === null)
            return
        var draft = answeringPrompt ? submission.text : root.normalizePasswordDraft(submission.text)

        if (answeringPrompt) {
            logger.d("Lock", "tryUnlock: calling pam.respond")
            root.enterAuthenticating()
            root.pamEndpoint.respond(draft) // an empty answer to an active prompt is valid
        } else {
            logger.d("Lock", "tryUnlock: calling pam.start")
            root.attemptCancelled = false
            // Reserve the response before the (possibly synchronous) first
            // password prompt so it never becomes feedback on this attempt.
            root.acceptedResponse = ({ "text": draft })
            root.enterAuthenticating()
            if (!root.pamEndpoint.start()) {
                watchdog.stop()
                root.acceptedResponse = null
                root.unlockInProgress = false
                root.setAuthState(root.authError, "pam-start-failed")
                root.setAuthStatus(i18n.authError || "Authentication error")
                logger.w("aerial-lock", "pam.start() failed for service " + root.pamEndpoint.config)
            }
        }
    }
}
