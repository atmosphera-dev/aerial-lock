#pragma once

#include <QObject>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <array>
#include <functional>

// One lock episode's noninteractive power-action owner. No authentication,
// compositor or theme dependencies; a successful request never releases a lock.
class PowerActions : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool secure READ secure WRITE setSecure NOTIFY changed)
    Q_PROPERTY(bool canSuspend READ canSuspend NOTIFY changed)
    Q_PROPERTY(bool canHibernate READ canHibernate NOTIFY changed)
    Q_PROPERTY(bool canReboot READ canReboot NOTIFY changed)
    Q_PROPERTY(bool canPowerOff READ canPowerOff NOTIFY changed)
    Q_PROPERTY(QString suspendReason READ suspendReason NOTIFY changed)
    Q_PROPERTY(QString hibernateReason READ hibernateReason NOTIFY changed)
    Q_PROPERTY(QString rebootReason READ rebootReason NOTIFY changed)
    Q_PROPERTY(QString powerOffReason READ powerOffReason NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString pendingAction READ pendingAction NOTIFY changed)
    Q_PROPERTY(QString activeAction READ activeAction NOTIFY changed)
    Q_PROPERTY(QString blockerWho READ blockerWho NOTIFY changed)
    Q_PROPERTY(QString blockerWhy READ blockerWhy NOTIFY changed)
    Q_PROPERTY(bool confirmationRequired READ confirmationRequired NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool interactionActive READ interactionActive NOTIFY changed)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY changed)
public:
    explicit PowerActions(QObject *parent = nullptr);
    // Native-only test seam. No QML/environment/config transport selector.
    PowerActions(const QDBusConnection &connection, QObject *parent);
    bool enabled() const { return allowed; }
    bool secure() const { return covered; }
    void setEnabled(bool value);
    void setSecure(bool value);
    bool canSuspend() const { return capable(0); }
    bool canHibernate() const { return capable(1); }
    bool canReboot() const { return capable(2); }
    bool canPowerOff() const { return capable(3); }
    QString suspendReason() const { return reason(0); }
    QString hibernateReason() const { return reason(1); }
    QString rebootReason() const { return reason(2); }
    QString powerOffReason() const { return reason(3); }
    QString state() const { return phase; }
    QString pendingAction() const;
    QString activeAction() const { return pending >= 0 ? pendingAction() : failure.isEmpty() ? QString{} : lastAction; }
    QString blockerWho() const { return who; }
    QString blockerWhy() const { return why; }
    bool confirmationRequired() const { return phase == "confirming"; }
    bool busy() const { return phase == "checking" || phase == "executing" || phase == "waiting"; }
    bool interactionActive() const { return confirmationRequired() || busy(); }
    QString errorCode() const { return failure; }
    Q_INVOKABLE void request(const QString &action);
    Q_INVOKABLE void confirm();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void refresh();
signals:
    void changed();
private slots:
    void ownerChanged(const QString &, const QString &, const QString &current);
    void prepareSleep(bool preparing);
    void prepareShutdown(bool preparing);
    void sessionsChanged(const QString &, const QDBusObjectPath &);
    void usersChanged(uint, const QDBusObjectPath &);
    void propertiesChanged(const QString &, const QVariantMap &, const QStringList &);
private:
    bool active() const { return allowed && covered; }
    bool capable(int index) const;
    QString reason(int index) const;
    void activationChanged();
    void adoptOwner(const QString &current);
    void resolveOwner();
    void subscribe(const QString &name, bool connect);
    void snapshotPreparing();
    void check();
    void dispatch(quint64 transaction, const QString &service);
    void inhibited(quint64 transaction, const QString &service, int action);
    void finish(const QString &error = {});
    void prepared(bool preparing, bool sleep);
    void retire();
    bool readBlocker(const QDBusMessage &message, int action);
    void call(const QString &service, const QString &interface, const QString &method,
              const QList<QVariant> &arguments, std::function<void(const QDBusMessage &)> complete);
    static QString capabilityReason(const QString &value);
    static QString dbusReason(const QDBusMessage &message);
    QDBusConnection bus;
    QDBusServiceWatcher watcher;
    QTimer poll;
    bool allowed = false, covered = false, refreshing = false, dirty = false, resolving = false;
    bool sent = false, preparationSeen = false;
    int pending = -1, remaining = 0;
    quint64 ownerEpoch = 0, snapshotEpoch = 0, transactionEpoch = 0;
    QString owner, phase = QStringLiteral("idle"), failure, lastAction, who, why;
    std::array<QString, 4> capabilities{{"?pending", "?pending", "?pending", "?pending"}};
};
