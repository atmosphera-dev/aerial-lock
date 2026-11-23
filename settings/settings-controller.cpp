#include "settings-controller.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QColor>
#include <QFileInfo>
#include <QQmlEngine>

namespace Aerial::Settings {
namespace {
QJsonValue leaf(QJsonObject object, const QString &key)
{
    const auto parts = key.split('.');
    QJsonValue value(object);
    for (const auto &part : parts) value = value.toObject().value(part);
    return value;
}
QString encoded(const QVariant &value)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}
QVariantMap choice(const QString &label, const QVariant &value) { return {{"label", label}, {"value", value}, {"token", encoded(value)}}; }
QString shortError(const QString &error)
{
    if (error.endsWith(".AcceptedResultUnavailable")) return "The previous service accepted this change, but its updated state is unavailable. Check the service state before trying again.";
    if (error.endsWith(".Conflict")) return "Changed elsewhere. The service value has been restored. Try your change again.";
    if (error.endsWith(".InvalidSetting")) return "The service rejected this value. Its current value has been restored.";
    return "Couldn't save: " + error.section('.', -1) + ".";
}
}
SettingsController::SettingsController(Client::ServiceConnection &service, QObject *parent)
    : QObject(parent), service(service), portalClient(QDBusConnection::sessionBus()), sourceController(service, portalClient),
      cacheController(service), themeCatalog(service), systemdController(QDBusConnection::sessionBus()),
      noticesModel(service, sourceController, systemdController, *cacheController.statusModel())
{
    connect(this, &SettingsController::changed, this, [this] {
        const auto automatic = controls().value("automatic").toMap();
        statusModel()->setAutomaticIntent(automatic.value("on").toBool(), automatic.value("pending").toBool());
    });
    connect(&service, &Client::ServiceConnection::changed, this, &SettingsController::sampled);
    connect(&service, &Client::ServiceConnection::mutationFinished, this, [this](quint64 id, const QVariant &result, const QString &error) {
        auto it = intents.find(id);
        if (it == intents.end()) return;
        const auto key = it->key;
        if (latest.value(key) != id) { intents.erase(it); return; }
        if (!error.isEmpty()) {
            errors[key] = shortError(error); latest.remove(key); intents.erase(it);
            this->service.refresh();
        } else {
            it->finished = true; it->revision = result.toString();
            // The client delivers completion after its post-reply read. Use
            // that coherent snapshot directly, or retain the intent for recovery.
            if (this->service.ready()) { sampled(); return; }
            this->service.refresh();
        }
        observeIdleWrites();
        emit changed();
    });
    sampled();
}
void SettingsController::sampled()
{
    if (service.ready()) {
        for (auto it = intents.begin(); it != intents.end();) {
            if (it->finished) {
                if (latest.value(it->key) == it.key()) latest.remove(it->key);
                it = intents.erase(it);
            } else ++it;
        }
    }
    if (!editOwner.isEmpty() && (editOwner != service.uniqueOwner() || service.errorName().endsWith("ProfileMismatch"))) {
        // Temporary read failure is not an owner change. Only this owner may
        // receive unsent edits after its coherent snapshot becomes available.
        for (const auto &key : editOrder) errors[key] = shortError(service.errorName());
        queued.clear(); editOrder.clear();
        editOwner.clear();
    }
    dispatchEdit();
    observeIdleWrites();
    emit changed();
}
QVariantMap SettingsController::controls() const
{
    const auto &snapshot = service.snapshot();
    QVariantMap result;
    const QStringList keys{"automatic", "acquisition.checkIntervalSeconds", "acquisition.automatic.allowedResolutions",
        "acquisition.automatic.allowMetered", "acquisition.automatic.connectionTypes.ethernet", "acquisition.automatic.connectionTypes.wifi",
        "acquisition.automatic.connectionTypes.cellular", "acquisition.automatic.connectionTypes.bluetooth", "acquisition.automatic.connectionTypes.vpn",
        "acquisition.automatic.connectionTypes.other", "acquisition.automatic.connectionTypes.unknown", "acquisition.automatic.power.ac",
        "acquisition.automatic.power.battery", "acquisition.automatic.power.unknown", "cache.maxBytes", "cache.rotationDays",
        "theme", "backgroundColor", "powerActions.enabled", "idle.timeoutSeconds", "idle.respectInhibitors", "idle.lockOnSuspend", "idle.logindLockUnlock"};
    for (const auto &key : keys) {
        const auto settingPath = key == "automatic" ? QString("acquisition.automatic.enabled") : key;
        QVariant value = leaf(snapshot.effective, settingPath).toVariant();
        const bool inherited = key != "automatic" && (leaf(snapshot.user, key).isUndefined() || leaf(snapshot.user, key) != leaf(snapshot.effective, key));
        if (latest.contains(key)) value = intents.value(latest.value(key)).value;
        if (queued.contains(key)) value = queued.value(key);
        QVariantList choices;
        if (key == "acquisition.checkIntervalSeconds") choices = {choice("Every hour", 3600), choice("Every 6 hours", 21600), choice("Every day", 86400)};
        if (key == "acquisition.automatic.allowedResolutions") choices = {choice("Up to 1080p", QVariantList{1080}), choice("Allow 4K", QVariantList{1080, 2160})};
        if (key == "cache.maxBytes") for (qint64 gb : {2, 5, 10, 20, 50}) choices << choice(QString::number(gb) + " GB", gb * 1000000000LL);
        if (key == "cache.rotationDays") choices = {choice("Every day", 1), choice("Every week", 7), choice("Every month", 30), choice("Never", 0)};
        if (key == "idle.timeoutSeconds") {
            choices << choice("Never", 0);
            for (int minute = 1; minute <= 60; ++minute) choices << choice(QString::number(minute) + (minute == 1 ? " minute" : " minutes"), minute * 60);
        }
        int selected = -1;
        for (int i = 0; i < choices.size(); ++i) if (QJsonValue::fromVariant(choices[i].toMap().value("value")) == QJsonValue::fromVariant(value)) selected = i;
        const bool custom = !choices.isEmpty() && selected < 0;
        if (custom) { choices.prepend(choice("Custom: " + encoded(value), value)); selected = 0; }
        const bool on = key == "acquisition.automatic.allowedResolutions" ? value.toList().contains(2160) : value.toBool();
        QString problem = errors.value(key);
        for (const auto &entry : snapshot.properties.value("SettingsDiagnostics").toList()) {
            const auto diagnostic = entry.toMap();
            const auto path = diagnostic.value("settingPath").toString();
            if (problem.isEmpty() && !path.isEmpty() && (settingPath == path || settingPath.startsWith(path + '.')))
                problem = "Using the service default: invalid setting at " + path + ".";
        }
        QVariantMap descriptor{{"value", value}, {"on", on}, {"inherited", inherited}, {"pending", latest.contains(key) || queued.contains(key)},
            {"error", problem}, {"choices", choices}, {"selected", selected}, {"custom", custom}};
        if (key == "backgroundColor") {
            const QColor color(value.toString());
            descriptor["color"] = color.isValid() ? color : QColor(Qt::black);
            if (service.ready() && !color.isValid()) descriptor["error"] = "The configured color is invalid. Choose a valid color; the file is left unchanged until you save.";
        }
        result[key] = descriptor;
    }
    return result;
}
QString SettingsController::panel() const
{
    if (!service.ready()) return "unavailable";
    for (const auto &entry : service.snapshot().properties.value("SettingsDiagnostics").toList())
        if (entry.toMap().value("severity").toString() == "error") return "diagnostic";
    return "settings";
}
QString SettingsController::diagnostic() const
{
    if (!service.ready()) return NoticesModel::connectionDiagnostic(service);
    QStringList lines;
    for (const auto &entry : service.snapshot().properties.value("SettingsDiagnostics").toList()) {
        const auto item = entry.toMap();
        QString text = item.value("file").toString() + ": " + item.value("code").toString();
        if (item.contains("line")) text += " at line " + item.value("line").toString() + ", column " + item.value("column").toString();
        if (item.contains("settingPath")) text += " (" + item.value("settingPath").toString() + ")";
        lines << text;
    }
    return lines.join('\n');
}
QString StatusFormatter::summary(const Client::ServiceConnection::Snapshot &snapshot)
{
    return StatusModel::project(snapshot).value("summary").toString();
}
QString StatusFormatter::ready(qulonglong count) { Client::ServiceConnection::Snapshot snapshot; snapshot.properties["ReadyClips"] = QVariant::fromValue(count); return StatusModel::project(snapshot).value("ready").toString(); }
QString SettingsController::status() const { return cacheController.facts().value("summary").toString(); }
QString SettingsController::connectionObservation() const
{
    const auto facts = service.snapshot().properties.value("Observations").toMap();
    const auto type = facts.value("connectionType", "unknown").toString();
    const QMap<QString, QString> names{{"wifi", "Wi-Fi"}, {"ethernet", "Ethernet"}, {"cellular", "Mobile broadband"}, {"vpn", "VPN"}};
    const auto metering = facts.value("metering", "unknown").toString();
    return "Now: " + names.value(type, type) + ", " + (metering == "metered" ? "metered." : metering == "unmetered" ? "not metered." : "metering unknown.");
}
QString SettingsController::powerObservation() const
{
    const auto power = service.snapshot().properties.value("Observations").toMap().value("power", "unknown").toString();
    return "Now: " + (power == "ac" ? QString("AC power.") : power == "battery" ? QString("Battery.") : QString("Unknown power source."));
}
void SettingsController::edit(const QString &key, const QVariant &value)
{
    if (!controls().contains(key) || panel() != "settings") return;
    const QVariant sent = key == "acquisition.automatic.allowedResolutions" && value.metaType().id() == QMetaType::Bool
        ? QVariant(value.toBool() ? QVariantList{1080, 2160} : QVariantList{1080}) : value;
    errors.remove(key);
    editOwner = service.uniqueOwner();
    if (!queued.contains(key)) editOrder.append(key);
    queued[key] = sent;
    dispatchEdit(); observeIdleWrites(); emit changed();
}
void SettingsController::observeIdleWrites()
{
    bool pending = false;
    for (auto it = queued.cbegin(); it != queued.cend(); ++it) pending |= it.key().startsWith("idle.");
    for (const auto &intent : intents) pending |= intent.key.startsWith("idle.");
    systemdController.setIdleWritesPending(pending);
}
void SettingsController::dispatchEdit()
{
    // Only unsent edits may be coalesced. Never replay a rejected mutation, and
    // wait for its post-reply snapshot before choosing the next revision.
    if (!intents.isEmpty() || editOrder.isEmpty() || panel() != "settings") return;
    const auto key = editOrder.takeFirst();
    const auto sent = queued.take(key);
    errors.remove(key);
    const auto id = key == "automatic" ? service.setPaused(!sent.toBool())
        : service.updateSettings(service.snapshot().revision, Client::settingPatch(key, QJsonValue::fromVariant(sent)));
    intents[id] = {key, sent, false, {}}; latest[key] = id;
}
void SettingsController::retry() { service.refresh(); }
void SettingsController::dispatchAction(const QString &id, const QString &sourceId)
{
    const auto facts = statusModel()->facts();
    if (!id.isEmpty() && id == facts.value("primaryActionId")) { cacheController.fetchOrStop(); return; }
    if (!id.isEmpty() && id == facts.value("secondaryActionId")) { edit("automatic", id == "resume-downloads"); return; }
    if (id == "open-sources" || id == "open-about") { emit pageRequested(id.mid(5)); return; }
    if (id == "retry-service") { retry(); return; }
    if (id == "start-service") { systemdController.operate("aerial.service", "start"); return; }
    if (id == "restart-service") { systemdController.operate("aerial.service", "restart"); return; }
    if (id == "open-settings-folder") { openSettingsFolder(); return; }
    if (id == "open-cache-folder") { openCacheFolder(); return; }
    if (id == "clean-up" && panel() == "settings") { cacheController.prepareCleanup(); return; }
    for (const auto &entry : noticesModel.entries()) {
        const auto notice = entry.toMap();
        if (id.isEmpty() || notice.value("actionId") != id || notice.value("sourceId").toString() != sourceId) continue;
        if (id == "turn-on-idle") emit idleConfirmationRequested();
        else if (id == "start-idle" || id == "restart-idle") systemdController.operate("aerial-idle.service", id == "start-idle" ? "start" : "restart");
        else if (id == "retry-source" && panel() == "settings") sourceController.retry(sourceId);
        return;
    }
}
void SettingsController::confirmIdleEnable()
{
    if (noticesModel.idle().value("actionId") == "turn-on-idle") systemdController.operate("aerial-idle.service", "enableStart");
}
QVariantMap SettingsController::about() const
{
    const auto p = service.snapshot().properties;
    const auto paths = p.value("Paths").toMap();
    auto state = p.value("State").toString(); if (!state.isEmpty()) state[0] = state[0].toUpper();
    return {{"version", "aerial " + p.value("Version").toString() + ", interface version " + p.value("ApiVersion").toString()},
        {"state", state}, {"config", paths.value("config")}, {"cache", paths.value("cache")}, {"paths", paths}};
}
void SettingsController::setColor(const QString &text)
{
    const QColor color(text);
    if (!color.isValid()) { errors["backgroundColor"] = "Enter a valid color, such as #000000."; emit changed(); return; }
    edit("backgroundColor", color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb));
}
QString SettingsController::canonicalColor(const QString &text) const
{
    const QColor color(text);
    return color.isValid() ? color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb) : QString();
}
void SettingsController::openSettingsFolder() { portalClient.openFolder(QFileInfo(service.snapshot().properties.value("Paths").toMap().value("config").toString()).absolutePath()); }
void SettingsController::openCacheFolder() { portalClient.openFolder(service.snapshot().properties.value("Paths").toMap().value("cache").toString()); }
void SettingsController::choose(const QString &key, const QString &token)
{
    QJsonParseError error;
    const auto array = QJsonDocument::fromJson(('[' + token + ']').toUtf8(), &error).array();
    if (error.error != QJsonParseError::NoError || array.size() != 1) return;
    edit(key, array.first().toVariant());
}
void registerTypes()
{
    qmlRegisterUncreatableType<SettingsController>("AerialLock.Settings", 1, 0, "SettingsController", "Owned by the application");
    qmlRegisterUncreatableType<SourcesController>("AerialLock.Settings", 1, 0, "SourcesController", "Owned by the application");
    qmlRegisterUncreatableType<CacheController>("AerialLock.Settings", 1, 0, "CacheController", "Owned by the application");
    qmlRegisterUncreatableType<StatusModel>("AerialLock.Settings", 1, 0, "StatusModel", "Owned by the application");
    qmlRegisterUncreatableType<NoticesModel>("AerialLock.Settings", 1, 0, "NoticesModel", "Owned by the application");
    qmlRegisterUncreatableType<ThemeCatalog>("AerialLock.Settings", 1, 0, "ThemeCatalog", "Owned by the application");
    qmlRegisterUncreatableType<PortalClient>("AerialLock.Settings", 1, 0, "PortalClient", "Owned by the application");
    qmlRegisterUncreatableType<SystemdController>("AerialLock.Settings", 1, 0, "SystemdController", "Owned by the application");
}
}
