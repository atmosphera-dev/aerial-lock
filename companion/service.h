#pragma once
#include "config-repository.h"
#include "../common/dbus-types.h"
#include <QDBusConnection>
#include <QDBusContext>

namespace Aerial {
class CacheStore;
class PlannerState;
class TransferQueue;
class PolicyMonitor;
class JobObject;
// The generated adaptor owns wire dispatch. Business rules remain in the
// repository/managers, shared by automatic actions and API requests.
class CompanionService : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_PROPERTY(uint ApiVersion READ apiVersion CONSTANT)
    Q_PROPERTY(QString Version READ version CONSTANT)
    Q_PROPERTY(QString Profile READ profile CONSTANT)
    Q_PROPERTY(QString ConfigRevision READ revision)
    Q_PROPERTY(QString State READ status)
    Q_PROPERTY(QString DeferReason READ deferReason)
    Q_PROPERTY(qulonglong ManagedBytes READ managedBytes)
    Q_PROPERTY(bool UsageKnown READ usageKnown)
    Q_PROPERTY(qulonglong PartialBytes READ partialBytes)
    Q_PROPERTY(qulonglong ReservedBytes READ reservedBytes)
    Q_PROPERTY(qulonglong MaxBytes READ maxBytes)
    Q_PROPERTY(uint ReadyClips READ readyClips)
    Q_PROPERTY(qulonglong NextCheck READ nextCheck)
    Q_PROPERTY(QVariantMap Paths READ paths)
    Q_PROPERTY(QVariantMap Observations READ observations)
public:
    CompanionService(ConfigRepository &config, QDBusConnection bus, QObject *parent = nullptr);
    bool start();
    uint apiVersion() const { return 1; }
    QString version() const;
    QString profile() const { return config.document().profile; }
    QString revision() const { return config.document().revision; }
    void bind(PlannerState *state, CacheStore *cache, TransferQueue *queue, PolicyMonitor *policy);
    QString status() const;
    QString deferReason() const;
    qulonglong managedBytes() const;
    bool usageKnown() const;
    qulonglong partialBytes() const;
    qulonglong reservedBytes() const;
    qulonglong maxBytes() const;
    uint readyClips() const;
    qulonglong nextCheck() const;
    QVariantMap paths() const;
    QVariantMap observations() const;
public slots:
    QString GetSettings(QString &effectiveJson, QString &revision);
    QString UpdateSettings(const QString &expectedRevision, const QString &mergePatchJson);
    void SetPaused(bool paused);
    void EnsureContent(const QVariantMap &hints);
    QDBusObjectPath Refresh(const QStringList &sourceIds);
    QDBusObjectPath Acquire(const QStringList &sourceIds, const QVariantMap &options);
    SourceList ListSources();
    QList<QDBusObjectPath> ListJobs();
    void Cancel(const QDBusObjectPath &job);
    QDBusObjectPath Prune(bool dryRun);
    void ReportPlayback(const QString &profile, const QString &assetKey);
signals:
    void SettingsChanged(const QString &revision, const QStringList &paths);
    void ContentChanged(qulonglong generation);
    void JobAdded(const QDBusObjectPath &job);
private:
    void error(const ConfigTransaction &result);
    void notify();
    void syncJobs();
    QDBusObjectPath jobPath(const QString &id);
    QDBusObjectPath acceptedJob(const QString &id);
    bool available();
    QJsonArray configuredEntries() const;
    ConfigRepository &config;
    QDBusConnection bus;
    PlannerState *state = nullptr;
    CacheStore *cache = nullptr;
    TransferQueue *queue = nullptr;
    PolicyMonitor *policy = nullptr;
    QMap<QString, JobObject *> objects;
    QVariantMap lastProperties;
    qulonglong generation = 0;
};
}
