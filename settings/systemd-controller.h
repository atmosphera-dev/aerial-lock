#pragma once
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QJsonObject>
#include <QObject>
#include <QVariantMap>
#include <functional>

namespace Aerial::Settings {
// Session user manager only. Unit and operation strings are fixed allowlists.
class SystemdController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QVariantMap units READ units NOTIFY changed)
    Q_PROPERTY(QVariantMap idle READ idle NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit SystemdController(QDBusConnection bus, QObject *parent = nullptr, int operationTimeoutMs = 30000);
    bool available() const { return connected; }
    QVariantMap units() const { return states; }
    QVariantMap idle() const;
    QString error() const { return problem; }
    void setIdleWritesPending(bool pending);
    Q_INVOKABLE void operate(const QString &unit, const QString &operation);
    Q_INVOKABLE void refresh();
signals:
    void changed();
    void operationFinished(const QString &unit, const QString &operation, bool success);
private slots:
    void ownerChanged(const QString &, const QString &, const QString &);
    void propertiesChanged(const QString &, const QVariantMap &, const QStringList &, const QDBusMessage &);
    void filesChanged(const QDBusMessage &);
    void unitChanged(const QString &, const QDBusObjectPath &, const QDBusMessage &);
    void jobRemoved(uint, const QDBusObjectPath &, const QString &, const QString &, const QDBusMessage &);
private:
    struct Operation {
        QString kind, job, beforeInvocation;
        bool jobDone = false;
        QMap<QString, QString> earlyJobs;
        quint64 serial = 0;
    };
    void discover();
    void adopt(const QString &owner);
    void subscribe();
    void sample(const QString &unit);
    void retrySample(const QString &unit, quint64 serial);
    void call(const QString &method, const QVariantList &arguments, std::function<void(const QDBusMessage &)> complete);
    void callOperation(const QString &unit, const QString &method, const QVariantList &arguments, std::function<void(const QDBusMessage &)> complete);
    void startJob(const QString &unit);
    void finish(const QString &unit, bool success, const QString &error = {});
    void checkOperation(const QString &unit);
    QDBusConnection bus;
    QString owner, problem;
    QVariantMap states;
    QMap<QString, Operation> operations;
    QMap<QString, quint64> samples;
    quint64 generation = 0;
    quint64 operationSerial = 0;
    bool connected = false;
    bool idleWritesPending = false;
    const int operationTimeoutMs;
};
}
