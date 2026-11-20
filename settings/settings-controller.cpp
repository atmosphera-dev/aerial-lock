#include "settings-controller.h"
#include <QJsonArray>
#include <QJsonDocument>
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
SettingsController::SettingsController(Client::ServiceConnection &service, QObject *parent) : QObject(parent), service(service)
{
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
        "acquisition.automatic.power.battery", "acquisition.automatic.power.unknown"};
    for (const auto &key : keys) {
        const auto settingPath = key == "automatic" ? QString("acquisition.automatic.enabled") : key;
        QVariant value = leaf(snapshot.effective, settingPath).toVariant();
        const bool inherited = key != "automatic" && (leaf(snapshot.user, key).isUndefined() || leaf(snapshot.user, key) != leaf(snapshot.effective, key));
        if (latest.contains(key)) value = intents.value(latest.value(key)).value;
        if (queued.contains(key)) value = queued.value(key);
        QVariantList choices;
        if (key == "acquisition.checkIntervalSeconds") choices = {choice("Every hour", 3600), choice("Every 6 hours", 21600), choice("Every day", 86400)};
        if (key == "acquisition.automatic.allowedResolutions") choices = {choice("Up to 1080p", QVariantList{1080}), choice("Allow 4K", QVariantList{1080, 2160})};
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
        result[key] = QVariantMap{{"value", value}, {"on", on}, {"inherited", inherited}, {"pending", latest.contains(key) || queued.contains(key)},
            {"error", problem}, {"choices", choices}, {"selected", selected}, {"custom", custom}};
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
    if (!service.ready()) return service.errorName().isEmpty() ? "Connecting to aerial..." : "Couldn't connect: " + service.errorName().section('.', -1);
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
    const auto reason = snapshot.properties.value("DeferReason").toString();
    if (reason == "StorageError") return "Cache needs attention";
    if (reason == "InvalidConfig") return "Settings need attention";
    if (!leaf(snapshot.effective, "acquisition.automatic.enabled").toBool()) return "Paused";
    for (const auto &entry : snapshot.jobs) {
        const auto job = entry.toMap();
        if (!job.value("Cancellable").toBool() || job.value("State").toString() != "downloading") continue;
        const auto details = job.value("Details").toMap();
        QString text = "Downloading " + details.value("assetTitle", "video").toString();
        const auto total = job.value("TotalBytes").toULongLong();
        if (job.value("TotalKnown").toBool() && total) text += ", " + QString::number(int(qMin(100.L, 100.L * job.value("ReceivedBytes").toULongLong() / total))) + "%";
        return text;
    }
    const QMap<QString, QString> reasons{{"Metered", "metered connection"}, {"ConnectionTypeDisallowed", "connection not allowed"},
        {"PowerDisallowed", "power source not allowed"}, {"Offline", "offline"}, {"Initializing", "checking connections"},
        {"NoAllowedResolution", "no quality allowed"}, {"NoRemoteSources", "no online sources"}, {"CacheFull", "cache full"}, {"PlaybackProtected", "videos in use"}};
    if (!reason.isEmpty() && reason != "None") return "Waiting: " + reasons.value(reason, reason);
    return QString::number(snapshot.properties.value("ReadyClips").toUInt()) + " videos ready";
}
QString SettingsController::status() const { return service.ready() ? StatusFormatter::summary(service.snapshot()) : "Service not running"; }
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
    dispatchEdit(); emit changed();
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
}
}
