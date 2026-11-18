#pragma once

#include "acquisition-planner.h"
#include "config-repository.h"
#include "manifest-store.h"
#include "transfer.h"
#include <QTimer>
#include <memory>

namespace Aerial {
// One application owner for automatic and explicit work. Neither D-Bus nor the
// CLI implements scheduling. Transport, clock and policy observations are seams.
class TransferQueue : public QObject {
    Q_OBJECT
public:
    TransferQueue(ConfigRepository &config, CacheStore &cache, PlannerState &state,
        QNetworkAccessManager &transport, QString probe, QByteArray appleRoot,
        std::function<qint64()> clock, QObject *parent = nullptr);
    ~TransferQueue();
    void start();
    QString acquire(const QStringList &sources = {}, int perSourceLimit = 0);
    QString refresh(const QStringList &sources = {});
    QString prune(bool dryRun);
    void ensure(const QStringList &topics = {});
    bool cancel(const QString &id);
    void setPolicyReason(const QString &reason);
    void evaluate(); // Also the injected-clock test seam, same timer path.
    QJsonObject job(const QString &id) const;
    QJsonObject jobs() const { auto result = state.jobs(); if (!active.isEmpty()) result[active] = job(active); return result; }
    QString status() const { return phase; }
    QString deferReason() const { return reason; }
    QString automaticDeferReason() const { return automaticReason(); }
    qint64 nextCheck() const { return next; }
signals:
    void changed();
    void jobAdded(const QString &id);
    void jobChanged(const QString &id);
    void contentChanged();
private:
    QString submit(const QString &kind, const QStringList &sources, int limit = 0, bool dryRun = false);
    QString automaticReason() const;
    void schedule();
    void armCheck();
    void pump();
    void nextManifest();
    bool reconcile();
    void sizeCandidates();
    void nextTransfer();
    void startTransfer(const Representation &rep);
    void plan();
    void finish(const QString &error = {});
    void save(std::function<void()> continuation);
    void retryPersistence();
    void invalidate();
    void publishState(const QString &phase, const QString &reason = {});
    QList<Representation> selected() const;
    int readyCount() const;
    bool finalizeRotation();
    ConfigRepository &config;
    CacheStore &cache;
    PlannerState &state;
    QNetworkAccessManager &transport;
    QString probe;
    QByteArray appleRoot;
    std::function<qint64()> clock;
    QTimer timer, persistenceTimer;
    std::function<void()> afterPersistence;
    QPointer<ManifestStore> manifest;
    QPointer<RepresentationSizer> sizer;
    QPointer<Transfer> transfer;
    QStringList pending, topics;
    QString active, phase = "idle", reason, policyReason;
    QJsonObject current;
    QList<QJsonObject> sources;
    QList<Representation> catalogue;
    QSet<QString> attempted;
    QMap<QString, int> available;
    qsizetype sourceIndex = 0;
    qint64 next = 0;
    quint64 decision = 0;
    bool running = false, scheduled = false, invalidated = false;
};
}
