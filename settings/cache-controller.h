#pragma once
#include "common/client/service-connection.h"

namespace Aerial::Settings {
class CacheController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap facts READ facts NOTIFY changed)
    Q_PROPERTY(QVariantList activity READ activity NOTIFY changed)
    Q_PROPERTY(QVariantMap cleanup READ cleanup NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit CacheController(Client::ServiceConnection &service, QObject *parent = nullptr);
    QVariantMap facts() const;
    QVariantList activity() const;
    QVariantMap cleanup() const;
    QString error() const { return problem; }
    Q_INVOKABLE void fetchOrStop();
    Q_INVOKABLE void prepareCleanup();
    Q_INVOKABLE void confirmCleanup();
    Q_INVOKABLE void dismissCleanup();
    static QString bytes(qulonglong value);
signals:
    void changed();
private:
    void sampled();
    void prune(bool dryRun);
    QVariantMap transfer() const;
    Client::ServiceConnection &service;
    quint64 pending = 0, action = 0;
    QString jobPath, jobOwner, problem, cleanupState;
    QVariantMap pruneResult;
    bool preview = true;
    bool cleanupVisible = false;
};
}
