#include "service-connection.h"
#include "companion_client.h"
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QJsonDocument>
#include <QTimer>
#include <utility>

namespace Aerial::Client {
namespace {
QString localError(const char *code) { return QStringLiteral("org.aeriallock.Aerial1.Error.") + code; }
}
ServiceConnection::ServiceConnection(QDBusConnection bus, ConfigPaths paths, QString installedBundle, QObject *parent)
    : QObject(parent), bus(std::move(bus)), paths(std::move(paths)), installedBundle(std::move(installedBundle))
{
    registerBusTypes();
    profile = readConfig(this->paths).profile;
}
ServiceConnection::~ServiceConnection() = default;

void ServiceConnection::watch(const QDBusPendingCall &call, Completion complete)
{
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    const auto handled = std::make_shared<bool>(false);
    const auto done = [watcher, handled, complete = std::move(complete)] {
        if (*handled) return;
        *handled = true;
        const auto reply = watcher->reply(); watcher->deleteLater(); complete(reply);
    };
    connect(watcher, &QDBusPendingCallWatcher::finished, this, done);
    if (watcher->isFinished()) QTimer::singleShot(0, this, done);
}

void ServiceConnection::start()
{
    if (started) return;
    started = true;
    // Empty senders avoid Qt's synchronous well-known-name resolution. Every
    // application signal is authenticated against our verified unique owner.
    const bool subscribed = bus.connect("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged",
        this, SLOT(ownerChanged(QString,QString,QString)))
        && bus.connect({}, {}, Properties, "PropertiesChanged", this, SLOT(propertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)))
        && bus.connect({}, Path, Service, "SettingsChanged", this, SLOT(settingsChanged(QString,QStringList,QDBusMessage)))
        && bus.connect({}, Path, Service, "ContentChanged", this, SLOT(contentChanged(qulonglong,QDBusMessage)))
        && bus.connect({}, Path, Service, "JobAdded", this, SLOT(jobAdded(QDBusObjectPath,QDBusMessage)))
        && bus.connect({}, {}, JobInterface, "Finished", this, SLOT(jobFinished(QString,QString,QDBusMessage)));
    bus.connect({}, "/org/freedesktop/DBus/Local", "org.freedesktop.DBus.Local", "Disconnected", this, SLOT(disconnected()));
    if (!bus.isConnected() || !subscribed) { disconnected(); return; }
    discover(mayActivate(paths, installedBundle));
}

void ServiceConnection::refresh()
{
    if (!started) { start(); return; }
    if (!verified) discover(mayActivate(paths, installedBundle));
    else invalidate(All);
}

void ServiceConnection::discover(bool activate)
{
    if (discoveryPending) return;
    discoveryPending = true;
    const auto epoch = generation;
    auto call = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner");
    call << QString(Service);
    watch(bus.asyncCall(call, 5000), [this, epoch, activate](const QDBusMessage &message) {
        if (epoch != generation) return;
        discoveryPending = false;
        const QDBusPendingReply<QString> reply(message);
        if (!reply.isError()) { adopt(reply.value()); return; }
        if (!activate || reply.error().name() != "org.freedesktop.DBus.Error.NameHasNoOwner") { fail(reply.error().name()); return; }
        // Only installed ordinary profiles reach this activation request. It
        // imports no caller environment; all subsequent calls use a unique name.
        const auto epoch = generation;
        auto call = QDBusMessage::createMethodCall(Service, Path, Service, "GetSettings");
        watch(bus.asyncCall(call, 5000), [this, epoch](const QDBusMessage &message) {
            if (epoch != generation) return;
            if (message.type() == QDBusMessage::ErrorMessage) fail(message.errorName());
            else discover(false);
        });
    });
}

void ServiceConnection::adopt(const QString &unique)
{
    if (unique == owner && verified) { invalidate(All); return; }
    ++generation; discoveryPending = false;
    // Clear ownership/readiness before delivering terminal intent callbacks.
    verified = available = false;
    const auto active = intents;
    const auto replied = std::exchange(settling, {});
    for (const auto &completion : replied)
        finish(completion.id, completion.result, completion.error.isEmpty() ? localError("AcceptedResultUnavailable") : completion.error);
    for (auto id : active) finish(id, {}, localError("OwnerChanged"));
    client.reset(); observedJobs.clear(); signaledJobs.clear(); jobSignalSerials.clear(); owner = unique;
    dirty = pending = failed = remainingJobs = 0;
    deferredProgress = 0;
    staged = {}; published = {}; error.clear(); emit changed();
    if (owner.isEmpty()) { fail("org.freedesktop.DBus.Error.NameHasNoOwner"); return; }
    const auto epoch = generation;
    watch(bus.asyncCall(propertyCall(owner, Path, Service), 5000), [this, epoch](const QDBusMessage &message) {
        if (epoch != generation) return;
        const QDBusPendingReply<QVariantMap> reply(message);
        if (reply.isError()) { fail(reply.error().name()); return; }
        if (unpack(reply.value()).toMap().value("Profile").toString() != profile) { fail(localError("ProfileMismatch")); return; }
        // Generated constructors with unique names do not perform GetNameOwner.
        client = std::make_unique<CompanionClient>(owner, Path, bus);
        client->setTimeout(5000); verified = true; invalidate(All);
    });
}

void ServiceConnection::ownerChanged(const QString &name, const QString &, const QString &newOwner)
{
    if (name == Service) adopt(newOwner);
}
void ServiceConnection::fail(const QString &name, int section)
{
    failed |= section; pending &= ~section;
    available = false; error = name; emit changed();
    flushSettling();
    // A signal received during the failed read still owns a follow-up read.
    // Consume it now rather than waiting for another section to finish. Never
    // retry a mutation, or a read for which no invalidation was received.
    dirty |= std::exchange(deferredProgress, 0);
    if (verified && dirty) sample();
}
void ServiceConnection::disconnected()
{
    adopt({}); fail("org.freedesktop.DBus.Error.Disconnected");
}
bool ServiceConnection::fromOwner(const QDBusMessage &message) const { return !owner.isEmpty() && message.service() == owner; }
void ServiceConnection::propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated, const QDBusMessage &message)
{
    if (!fromOwner(message)) return;
    if (interface == Service && message.path() == Path) {
        bool byteProgress = !changed.isEmpty() && invalidated.isEmpty();
        for (auto it = changed.cbegin(); it != changed.cend(); ++it)
            byteProgress &= it.key() == "ManagedBytes" || it.key() == "PartialBytes" || it.key() == "ReservedBytes";
        if (byteProgress) progress(Facts);
        else {
            const bool storageFailure = unpack(changed.value("State")).toString() == "error"
                || unpack(changed.value("DeferReason")).toString() == "StorageError"
                || invalidated.contains("State") || invalidated.contains("DeferReason");
            invalidate(storageFailure ? Facts | Jobs | Sources : Facts);
        }
    }
    else if (interface == JobInterface) {
        int sections = Jobs;
        const auto previous = signaledJobs.value(message.path(), observedJobs.value(message.path()));
        if (previous.isEmpty()) sections |= Sources;
        for (const auto &key : {"State", "SourceId"})
            if (invalidated.contains(key) || (changed.contains(key) && unpack(changed.value(key)) != previous.value(key))) sections |= Sources;
        if (invalidated.contains("Details") || (changed.contains("Details")
            && unpack(changed.value("Details")).toMap().value("sourceResults") != previous.value("Details").toMap().value("sourceResults"))) sections |= Sources;
        auto signaled = previous;
        for (auto it = changed.cbegin(); it != changed.cend(); ++it) signaled[it.key()] = unpack(it.value());
        for (const auto &key : invalidated) signaled.remove(key);
        signaledJobs[message.path()] = signaled; ++jobSignalSerials[message.path()];
        // Progress received during a Jobs read belongs to the next publication
        // round. Do not hold the current coherent snapshot/mutation barrier open
        // until a continuous stream becomes quiet. Substantive changes still
        // invalidate Sources and retain the existing consistency barrier.
        if (sections == Jobs) progress(Jobs);
        else invalidate(sections);
    }
}
void ServiceConnection::settingsChanged(const QString &, const QStringList &, const QDBusMessage &message)
{ if (fromOwner(message)) invalidate(All); }
void ServiceConnection::contentChanged(qulonglong, const QDBusMessage &message)
{ if (fromOwner(message)) invalidate(Facts | Sources | Jobs); }
void ServiceConnection::jobAdded(const QDBusObjectPath &, const QDBusMessage &message)
{ if (fromOwner(message)) invalidate(Jobs | Sources | Facts); }
void ServiceConnection::jobFinished(const QString &, const QString &, const QDBusMessage &message)
{ if (fromOwner(message)) invalidate(Jobs | Sources | Facts); }

void ServiceConnection::invalidate(int sections)
{
    dirty |= sections;
    if (verified) sample();
}
void ServiceConnection::progress(int sections)
{
    if (pending) deferredProgress |= sections;
    else invalidate(sections);
}
void ServiceConnection::sample()
{
    const auto epoch = generation;
    const auto needed = dirty & ~pending;
    dirty &= ~needed; pending |= needed; failed &= ~needed;
    if (needed & Settings) watch(client->GetSettings(), [this, epoch](const QDBusMessage &message) {
        if (epoch != generation) return;
        const QDBusPendingReply<QString, QString, QString> reply(message);
        if (reply.isError()) { fail(reply.error().name(), Settings); return; }
        QJsonParseError userError, effectiveError;
        const auto user = QJsonDocument::fromJson(reply.argumentAt<0>().toUtf8(), &userError);
        const auto effective = QJsonDocument::fromJson(reply.argumentAt<1>().toUtf8(), &effectiveError);
        if (userError.error || effectiveError.error || !user.isObject() || !effective.isObject()) { fail(localError("InvalidReply"), Settings); return; }
        staged.user = user.object(); staged.effective = effective.object(); staged.revision = reply.argumentAt<2>(); settled(Settings);
    });
    if (needed & Facts) watch(bus.asyncCall(propertyCall(owner, Path, Service), 5000), [this, epoch](const QDBusMessage &message) {
        if (epoch != generation) return;
        const QDBusPendingReply<QVariantMap> reply(message);
        if (reply.isError()) { fail(reply.error().name(), Facts); return; }
        staged.properties = unpack(reply.value()).toMap();
        if (staged.properties.value("Profile").toString() != profile) { verified = false; fail(localError("ProfileMismatch"), Facts); return; }
        settled(Facts);
    });
    if (needed & Sources) watch(client->ListSources(), [this, epoch](const QDBusMessage &message) {
        if (epoch != generation) return;
        const QDBusPendingReply<SourceList> reply(message);
        if (reply.isError()) { fail(reply.error().name(), Sources); return; }
        staged.sources = unpack(QVariant::fromValue(reply.value())).toList(); settled(Sources);
    });
    if (needed & Jobs) sampleJobs();
}
void ServiceConnection::sampleJobs()
{
    const auto epoch = generation;
    watch(client->ListJobs(), [this, epoch](const QDBusMessage &message) {
        if (epoch != generation) return;
        const QDBusPendingReply<QList<QDBusObjectPath>> reply(message);
        if (reply.isError()) { fail(reply.error().name(), Jobs); return; }
        staged.jobs.clear(); remainingJobs = reply.value().size();
        QSet<QString> listed;
        for (const auto &job : reply.value()) listed.insert(job.path());
        for (auto it = observedJobs.begin(); it != observedJobs.end();) {
            if (!listed.contains(it.key())) it = observedJobs.erase(it); else ++it;
        }
        for (auto it = jobSignalSerials.begin(); it != jobSignalSerials.end();) {
            if (!listed.contains(it.key())) { signaledJobs.remove(it.key()); it = jobSignalSerials.erase(it); } else ++it;
        }
        if (!remainingJobs) { settled(Jobs); return; }
        for (const auto &job : reply.value()) {
            const auto object = job.path();
            const auto signalSerial = jobSignalSerials.value(object);
            watch(bus.asyncCall(propertyCall(owner, object, JobInterface), 5000), [this, epoch, object, signalSerial](const QDBusMessage &message) {
                if (epoch != generation) return;
                const QDBusPendingReply<QVariantMap> reply(message);
                if (!reply.isError()) {
                    auto row = unpack(reply.value()).toMap(); row["path"] = object; observedJobs[object] = row; staged.jobs.append(row);
                    // A reply must not rewind the transition baseline past a
                    // signal received while that reply was in flight.
                    if (jobSignalSerials.value(object) == signalSerial) signaledJobs.remove(object);
                } else if (reply.error().type() == QDBusError::UnknownObject || reply.error().type() == QDBusError::UnknownInterface) {
                    // Retention may remove a listed job between ListJobs/GetAll.
                    dirty |= Jobs;
                } else { failed |= Jobs; available = false; error = reply.error().name(); emit changed(); }
                if (--remainingJobs == 0) {
                    if (failed & Jobs) fail(error, Jobs);
                    else settled(Jobs);
                }
            });
        }
    });
}
void ServiceConnection::settled(int section)
{
    pending &= ~section;
    if (dirty) sample();
    publish();
}
void ServiceConnection::publish()
{
    if (pending || dirty || failed || !verified) return;
    // Cross-section revision agreement is the final bootstrap barrier.
    if (staged.properties.value("ConfigRevision").toString() != staged.revision) { invalidate(Settings | Facts); return; }
    published = staged; available = true; error.clear(); emit changed();
    flushSettling();
    const auto next = std::exchange(deferredProgress, 0);
    if (next) invalidate(next);
}
void ServiceConnection::flushSettling()
{
    const auto done = std::exchange(settling, {});
    for (const auto &completion : done) finish(completion.id, completion.result, completion.error);
}

quint64 ServiceConnection::mutate(const QString &method, const QVariantList &arguments)
{
    const auto id = ++nextIntent; intents.insert(id);
    const auto epoch = generation;
    if (!available || !verified) {
        QTimer::singleShot(0, this, [this, id] { finish(id, {}, localError("Unavailable")); }); return id;
    }
    auto call = QDBusMessage::createMethodCall(owner, Path, Properties, "Get");
    call.setAutoStartService(false); call << QString(Service) << QString("Profile");
    watch(bus.asyncCall(call, 5000), [this, id, epoch, method, arguments](const QDBusMessage &message) {
        if (epoch != generation || !intents.contains(id)) return;
        const QDBusPendingReply<QDBusVariant> reply(message);
        if (reply.isError()) { finish(id, {}, reply.error().name()); return; }
        if (unpack(QVariant::fromValue(reply.value())).toString() != profile) { finish(id, {}, localError("ProfileMismatch")); return; }
        // asyncCall on a unique generated proxy: no lookup, retry or new owner.
        watch(client->asyncCallWithArgumentList(method, arguments), [this, id, epoch, method](const QDBusMessage &message) {
            if (epoch != generation || !intents.contains(id)) return;
            QDBusError error;
            QVariant result;
            if (method == "UpdateSettings") {
                const QDBusPendingReply<QString> reply(message); error = reply.error();
                if (!reply.isError()) result = reply.value();
            } else if (method == "Acquire" || method == "Refresh" || method == "Prune") {
                const QDBusPendingReply<QDBusObjectPath> reply(message); error = reply.error();
                if (!reply.isError()) result = QVariant::fromValue(reply.value());
            } else { const QDBusPendingReply<> reply(message); error = reply.error(); }
            // Report the reply only with a snapshot sampled after it, so a
            // caller's next intent never carries the pre-mutation revision.
            settling.append({id, result, error.name()});
            invalidate(All);
        });
    });
    return id;
}
void ServiceConnection::finish(quint64 id, const QVariant &result, const QString &error)
{
    if (intents.remove(id)) emit mutationFinished(id, result, error);
}
quint64 ServiceConnection::updateSettings(const QString &revision, const QJsonObject &patch)
{ return mutate("UpdateSettings", {revision, QString::fromUtf8(QJsonDocument(patch).toJson(QJsonDocument::Compact))}); }
quint64 ServiceConnection::setPaused(bool paused) { return mutate("SetPaused", {paused}); }
quint64 ServiceConnection::acquire(const QStringList &sources, const QVariantMap &options) { return mutate("Acquire", {sources, options}); }
quint64 ServiceConnection::refreshSources(const QStringList &sources) { return mutate("Refresh", {sources}); }
quint64 ServiceConnection::prune(bool dryRun) { return mutate("Prune", {dryRun}); }
quint64 ServiceConnection::cancel(const QDBusObjectPath &job) { return mutate("Cancel", {QVariant::fromValue(job)}); }
}
