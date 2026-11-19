#include "service.h"
#include "job-object.h"
#include "cache-store.h"
#include "planner-state.h"
#include "transfer-queue.h"
#include "policy-monitor.h"
#include "../common/content-snapshot.h"
#include "companion_adaptor.h"
#include "service.moc"
#include <QDBusMessage>
#include <QJsonDocument>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

namespace Aerial {
CompanionService::CompanionService(ConfigRepository &repository, QDBusConnection connection, QObject *parent)
    : QObject(parent), config(repository), bus(std::move(connection))
{
    registerBusTypes();
    new Aerial1Adaptor(this);
    connect(&config, &ConfigRepository::changed, this, [this](const QString &revision, const QStringList &paths) {
        emit SettingsChanged(revision, paths);
        notify();
    });
}

bool CompanionService::start()
{
    // Methods are available before the non-replacing bus name is claimed.
    if (!bus.registerObject("/org/aeriallock/Aerial1", this, QDBusConnection::ExportAdaptors)) return false;
    if (!bus.registerService("org.aeriallock.Aerial1")) {
        bus.unregisterObject("/org/aeriallock/Aerial1");
        return false;
    }
    return true;
}

QString CompanionService::version() const
{
    QFile file(config.document().paths.bundle + "/VERSION");
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.read(256)).trimmed() : QStringLiteral("development");
}

void CompanionService::bind(PlannerState *planner, CacheStore *store, TransferQueue *transfers, PolicyMonitor *monitor)
{
    if (queue != transfers && transfers) {
        connect(transfers, &TransferQueue::changed, this, [this] { notify(); });
        connect(transfers, &TransferQueue::jobAdded, this, [this](const QString &id) {
            syncJobs(); emit JobAdded(jobPath(id));
        });
        connect(transfers, &TransferQueue::jobChanged, this, [this] { syncJobs(); notify(); });
        connect(transfers, &TransferQueue::contentChanged, this, [this] {
            generation = std::max(generation + 1, qulonglong(QDateTime::currentMSecsSinceEpoch()));
            emit ContentChanged(generation); notify();
        });
    }
    if (policy != monitor && monitor) connect(monitor, &PolicyMonitor::changed, this, [this] { notify(); });
    state = planner; cache = store; queue = transfers; policy = monitor;
    syncJobs(); notify();
}

QString CompanionService::status() const
{
    if (queue) return queue->status();
    return deferReason() == "StorageError" ? "error" : "deferred";
}
QString CompanionService::deferReason() const
{
    if (queue) return queue->deferReason();
    if ((state && !state->valid()) || (cache && !cache->valid())) return "StorageError";
    if (!maxBytes()) return "CacheFull";
    return "Initializing";
}
bool CompanionService::usageKnown() const
{
    return cache ? cache->usage().safe && cache->metadata("ownership.json").value("format") == "aerial-lock-managed-v1"
                 : !QFileInfo::exists(config.document().paths.cache);
}
qulonglong CompanionService::managedBytes() const { return cache && usageKnown() ? cache->usage().bytes : 0; }
qulonglong CompanionService::partialBytes() const { return cache && usageKnown() ? cache->usage().partial : 0; }
qulonglong CompanionService::reservedBytes() const { return cache && usageKnown() ? cache->usage().reserved : 0; }
qulonglong CompanionService::maxBytes() const { return config.document().effective.value("cache").toObject().value("maxBytes").toInteger(); }
qulonglong CompanionService::nextCheck() const { return queue ? std::max<qint64>(0, queue->nextCheck()) : 0; }
QJsonArray CompanionService::configuredEntries() const
{
    QSet<QString> configured;
    for (const auto &source : config.document().effective.value("sources").toArray()) configured.insert(source.toObject().value("id").toString());
    QJsonArray result;
    if (cache) {
        const auto visible = cache->valid() ? cache->entries() : ContentSnapshot(cache->path()).entries();
        for (const auto &entry : visible)
            if (configured.contains(entry.toObject().value("sourceId").toString())) result.append(entry);
    }
    return result;
}
uint CompanionService::readyClips() const { return uint(configuredEntries().size()); }
QVariantMap CompanionService::paths() const
{
    const auto &paths = config.document().paths;
    return {{"bundle", paths.bundle}, {"config", paths.config}, {"cache", paths.cache}, {"state", paths.state}, {"runtime", paths.runtime}};
}
QVariantMap CompanionService::observations() const { return policy ? policy->observations().toVariantMap() : QVariantMap{}; }

void CompanionService::notify()
{
    const QVariantMap properties{{"ConfigRevision", revision()}, {"State", status()}, {"DeferReason", deferReason()},
        {"ManagedBytes", QVariant::fromValue(managedBytes())}, {"UsageKnown", usageKnown()}, {"PartialBytes", QVariant::fromValue(partialBytes())},
        {"ReservedBytes", QVariant::fromValue(reservedBytes())}, {"MaxBytes", QVariant::fromValue(maxBytes())},
        {"ReadyClips", readyClips()}, {"NextCheck", QVariant::fromValue(nextCheck())}, {"Observations", observations()}};
    QVariantMap changed;
    for (auto it = properties.begin(); it != properties.end(); ++it)
        if (lastProperties.value(it.key()) != it.value()) changed.insert(it.key(), it.value());
    lastProperties = properties;
    if (changed.isEmpty()) return;
    auto signal = QDBusMessage::createSignal("/org/aeriallock/Aerial1", "org.freedesktop.DBus.Properties", "PropertiesChanged");
    signal << QString("org.aeriallock.Aerial1") << changed << QStringList{};
    bus.send(signal);
}

QDBusObjectPath CompanionService::jobPath(const QString &id)
{
    return QDBusObjectPath(id.isEmpty() ? "/" : "/org/aeriallock/Aerial1/jobs/j" + id);
}
QDBusObjectPath CompanionService::acceptedJob(const QString &id)
{
    if (id.isEmpty()) error({{}, "StorageError", {"job could not be persisted"}});
    return jobPath(id);
}
void CompanionService::syncJobs()
{
    const auto jobs = queue ? queue->jobs() : state ? state->jobs() : QJsonObject{};
    for (auto it = objects.begin(); it != objects.end();) {
        if (jobs.contains(it.key())) { ++it; continue; }
        bus.unregisterObject(jobPath(it.key()).path()); delete it.value(); it = objects.erase(it);
    }
    for (auto it = jobs.begin(); it != jobs.end(); ++it) {
        if (!contentKey(it.key())) continue;
        if (!objects.contains(it.key())) {
            auto object = new JobObject(jobPath(it.key()).path(), bus, this);
            object->update(it.value().toObject());
            if (!bus.registerObject(jobPath(it.key()).path(), object, QDBusConnection::ExportAdaptors)) { delete object; continue; }
            objects.insert(it.key(), object);
        } else objects.value(it.key())->update(it.value().toObject());
    }
}

bool CompanionService::available()
{
    if (queue) return true;
    error({{}, deferReason(), {"content queue is not available"}}); return false;
}

void CompanionService::EnsureContent(const QVariantMap &hints)
{
    // A different bundle/XDG profile must never trigger production acquisition.
    if (hints.value("profile").toString() != profile()) { error({{}, "ProfileMismatch", {"profile differs"}}); return; }
    for (auto it = hints.begin(); it != hints.end(); ++it)
        if (it.key() != "profile" && it.key() != "height" && it.key() != "topics") { error({{}, "InvalidSetting", {"unknown hint"}}); return; }
    if (hints.contains("topics") && hints.value("topics").metaType().id() != QMetaType::QStringList) { error({{}, "InvalidSetting", {"topics must be strings"}}); return; }
    if (hints.contains("height")) {
        const auto height = hints.value("height");
        if (height.metaType().id() != QMetaType::Int || height.toInt() <= 0) { error({{}, "InvalidSetting", {"height must be a positive integer"}}); return; }
        const auto result = config.observeHeight(height.toInt());
        if (!result.ok()) { error(result); return; }
    }
    if (!available()) return;
    queue->ensure(hints.value("topics").toStringList());
    if (!queue->automaticDeferReason().isEmpty()) error({{}, "PolicyDeferred", {queue->automaticDeferReason()}});
}

QDBusObjectPath CompanionService::Refresh(const QStringList &ids) { return available() ? acceptedJob(queue->refresh(ids)) : jobPath({}); }
QDBusObjectPath CompanionService::Acquire(const QStringList &ids, const QVariantMap &options)
{
    for (auto it = options.begin(); it != options.end(); ++it)
        if (it.key() != "limit") { error({{}, "InvalidSetting", {"unknown acquire option"}}); return jobPath({}); }
    if (options.contains("limit") && (options.value("limit").metaType().id() != QMetaType::Int || options.value("limit").toInt() < 0)) {
        error({{}, "InvalidSetting", {"limit must be a nonnegative integer"}}); return jobPath({});
    }
    return available() ? acceptedJob(queue->acquire(ids, options.value("limit").toInt())) : jobPath({});
}
QDBusObjectPath CompanionService::Prune(bool dryRun) { return available() ? acceptedJob(queue->prune(dryRun)) : jobPath({}); }
SourceList CompanionService::ListSources()
{
    SourceList result;
    for (const auto &value : config.document().effective.value("sources").toArray()) {
        const auto source = value.toObject(); uint count = 0;
        for (const auto &entry : configuredEntries()) if (entry.toObject().value("sourceId") == source.value("id")) ++count;
        QString diagnostic; qint64 latest = -1;
        const auto records = queue ? queue->jobs() : state ? state->jobs() : QJsonObject{};
        for (const auto &value : records) {
            const auto record = value.toObject();
            const auto created = record.value("createdAt").toInteger();
            if (created >= latest && record.value("errors").toObject().contains(source.value("id").toString())) {
                latest = created; diagnostic = record.value("errors").toObject().value(source.value("id").toString()).toString();
            }
        }
        const bool remote = source.value("type") == "apple" || source.value("type") == "community";
        // URL/path disclosure is restricted to explicit GetSettings inspection.
        result.append({{"id", source.value("id").toString()}, {"name", source.value("name").toString()},
            {"type", source.value("type").toString()}, {"readyClips", count}, {"errorCode", diagnostic},
            {"state", !remote ? QString("unmanaged") : count ? QString("ready") : diagnostic.isEmpty() ? status() : QString("error")}});
    }
    return result;
}
QList<QDBusObjectPath> CompanionService::ListJobs()
{
    syncJobs(); QList<QDBusObjectPath> result;
    for (auto it = objects.begin(); it != objects.end(); ++it) result.append(jobPath(it.key()));
    return result;
}
void CompanionService::Cancel(const QDBusObjectPath &path)
{
    for (auto it = objects.begin(); it != objects.end(); ++it)
        if (jobPath(it.key()) == path && queue && queue->cancel(it.key())) return;
    error({{}, "UnknownJob", {"job is unknown or already terminal"}});
}
void CompanionService::ReportPlayback(const QString &requestedProfile, const QString &key)
{
    if (requestedProfile != profile()) { error({{}, "ProfileMismatch", {"profile differs"}}); return; }
    for (const auto &entry : configuredEntries()) if (entry.toObject().value("key").toString() == key) {
        if (state) state->recordPlayback(key, QDateTime::currentSecsSinceEpoch());
        return;
    }
}

QString CompanionService::GetSettings(QString &effective, QString &revision)
{
    config.reload();
    const auto &doc = config.document();
    effective = QString::fromUtf8(QJsonDocument(doc.effective).toJson(QJsonDocument::Compact));
    revision = doc.revision;
    return QString::fromUtf8(QJsonDocument(doc.user).toJson(QJsonDocument::Compact));
}

void CompanionService::error(const ConfigTransaction &result)
{
    if (!result.ok() && calledFromDBus())
        sendErrorReply("org.aeriallock.Aerial1.Error." + result.error, result.diagnostics.join("; "));
}

QString CompanionService::UpdateSettings(const QString &expected, const QString &patch)
{
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(patch.toUtf8(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        error({{}, "InvalidSetting", {"merge patch must be a JSON object"}});
        return {};
    }
    const auto result = config.update(expected, document.object());
    error(result);
    return result.revision;
}

void CompanionService::SetPaused(bool paused)
{
    config.reload();
    error(config.update(config.document().revision, {{"acquisition", QJsonObject{{"automatic", QJsonObject{{"enabled", !paused}}}}}}));
}
}
