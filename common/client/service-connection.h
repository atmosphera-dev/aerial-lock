#pragma once
#include "client-support.h"
#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QSet>
#include <functional>
#include <memory>

class CompanionClient;
namespace Aerial::Client {
// Process-owned transport. Controllers consume coherent snapshots and send
// intents, never generated property getters (which block in Qt).
class ServiceConnection : public QObject {
    Q_OBJECT
public:
    struct Snapshot {
        QJsonObject user, effective;
        QString revision;
        QVariantMap properties;
        QVariantList sources, jobs;
    };
    ServiceConnection(QDBusConnection bus, ConfigPaths paths, QString installedBundle, QObject *parent = nullptr);
    ~ServiceConnection() override;
    void start();
    void refresh();
    bool ready() const { return available; }
    QString errorName() const { return error; }
    QString uniqueOwner() const { return owner; }
    const Snapshot &snapshot() const { return published; }
    quint64 updateSettings(const QString &revision, const QJsonObject &patch);
    quint64 setPaused(bool paused);
    quint64 acquire(const QStringList &sources = {}, const QVariantMap &options = {});
    quint64 refreshSources(const QStringList &sources = {});
    quint64 prune(bool dryRun);
    quint64 cancel(const QDBusObjectPath &job);
signals:
    void changed();
    void mutationFinished(quint64 intent, const QVariant &result, const QString &errorName);
private slots:
    void ownerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void propertiesChanged(const QString &interface, const QVariantMap &, const QStringList &, const QDBusMessage &message);
    void settingsChanged(const QString &, const QStringList &, const QDBusMessage &message);
    void contentChanged(qulonglong, const QDBusMessage &message);
    void jobAdded(const QDBusObjectPath &, const QDBusMessage &message);
    void jobFinished(const QString &, const QString &, const QDBusMessage &message);
    void disconnected();
private:
    enum Section { Settings = 1, Facts = 2, Sources = 4, Jobs = 8, All = 15 };
    using Completion = std::function<void(const QDBusMessage &)>;
    void watch(const QDBusPendingCall &call, Completion complete);
    void discover(bool activate);
    void adopt(const QString &unique);
    void fail(const QString &name, int section = 0);
    void invalidate(int sections);
    void sample();
    void settled(int section);
    void sampleJobs();
    void publish();
    quint64 mutate(const QString &method, const QVariantList &arguments);
    void finish(quint64 id, const QVariant &result, const QString &error);
    bool fromOwner(const QDBusMessage &message) const;
    QDBusConnection bus;
    ConfigPaths paths;
    QString installedBundle, profile, owner, error;
    Snapshot staged, published;
    std::unique_ptr<CompanionClient> client;
    QMap<QString, QVariantMap> observedJobs;
    QMap<QString, QVariantMap> signaledJobs;
    QMap<QString, quint64> jobSignalSerials;
    quint64 generation = 0, nextIntent = 0;
    QSet<quint64> intents;
    struct Settled { quint64 id; QVariant result; QString error; };
    QList<Settled> settling;
    void flushSettling();
    int dirty = 0, pending = 0, failed = 0, remainingJobs = 0;
    bool started = false, verified = false, available = false, discoveryPending = false;
    int deferredProgress = 0;
    void progress(int sections);
};
}
