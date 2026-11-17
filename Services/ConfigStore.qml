import QtQuick
import Quickshell
import Quickshell.Io
import AerialLock.Native 1.0

Scope {
    id: root

    property bool ready: false
    property bool failed: false
    property string errorMessage: ""
    property var data: ({})
    property var i18n: ({})

    readonly property alias pamLimits: pamLimitsObj
    readonly property alias buildFlags: buildFlagsObj
    readonly property string profile: local.profile
    readonly property string configDir: {
        var xdg = Quickshell.env("XDG_CONFIG_HOME")
        if (xdg && xdg.length > 0) return xdg + "/aerial-lock"
        var home = Quickshell.env("HOME")
        return home ? home + "/.config/aerial-lock" : ""
    }
    readonly property string configPath: configDir + "/config.json"
    readonly property string i18nDir: Quickshell.shellDir + "/Config/i18n"
    readonly property string pamService: {
        var override = Quickshell.env("AERIAL_LOCK_PAM_SERVICE")
        if (override && override.length > 0) return override
        return (data && data.pamService) ? data.pamService : "aerial-lock"
    }
    readonly property string pamConfigDirectory: "/etc/pam.d"

    LocalConfig { id: local }
    PamLimits { id: pamLimitsObj }
    BuildFlags { id: buildFlagsObj }
    FileView {
        id: i18nFile
        path: ""
        blockLoading: true
        printErrors: false
    }

    Component.onCompleted: {
        buildFlagsObj.load()
        initialize()
    }

    function initialize() {
        var loaded = local.load(Quickshell.shellDir, configPath)
        for (var i = 0; i < local.diagnostics.length; i++)
            Logger.w("ConfigStore", local.diagnostics[i])
        if (!loaded) {
            fail("Bundle configuration failed validation")
            return
        }
        root.data = local.data
        loadI18n(root.data.language || "en")
        root.ready = true
    }

    function loadI18n(lang) {
        var strings = readI18n(i18nDir + "/en.json")
        if (lang !== "en")
            strings = mergeLabels(strings, readI18n(i18nDir + "/" + lang + ".json"))
        root.i18n = mergeLabels(strings, readI18n(configDir + "/i18n/" + lang + ".json"))
    }

    // Presentation labels have their own per-key precedence, not settings
    // normalization. The native shared reader is the only settings reader.
    function mergeLabels(base, override) {
        var result = {}
        for (var key in base) result[key] = base[key]
        for (var okey in override) result[okey] = override[okey]
        return result
    }

    function readI18n(path) {
        i18nFile.path = path
        i18nFile.waitForJob()
        var strings = ({})
        try {
            var raw = i18nFile.text()
            if (!raw || raw.length === 0)
                throw new Error("missing, unreadable or empty file")
            var parsed = JSON.parse(raw)
            if (!isPlainObject(parsed))
                throw new Error("expected an object of nonempty strings")
            var invalid = false
            for (var key in parsed) {
                if (typeof parsed[key] === "string" && parsed[key].trim().length > 0)
                    strings[key] = parsed[key]
                else
                    invalid = true
            }
            if (invalid)
                Logger.w("ConfigStore", "i18n " + path + ": invalid or blank values; using fallback for those keys")
        } catch (e) {
            Logger.w("ConfigStore", "i18n " + path + " (" + e + "); using fallback")
        }
        return strings
    }

    function isPlainObject(v) {
        return v !== null && typeof v === "object" && !Array.isArray(v)
    }

    function fail(msg) {
        Logger.w("ConfigStore", "FAILED: " + msg)
        errorMessage = msg
        failed = true
    }
}
