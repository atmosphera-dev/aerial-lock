#pragma once
#include "common/client/service-connection.h"
#include <QObject>
#include <QVariantMap>

namespace Aerial::Settings {
class SettingsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap controls READ controls NOTIFY changed)
    Q_PROPERTY(QString panel READ panel NOTIFY changed)
    Q_PROPERTY(QString diagnostic READ diagnostic NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString connectionObservation READ connectionObservation NOTIFY changed)
    Q_PROPERTY(QString powerObservation READ powerObservation NOTIFY changed)
public:
    explicit SettingsController(Client::ServiceConnection &service, QObject *parent = nullptr);
    QVariantMap controls() const;
    QString panel() const;
    QString diagnostic() const;
    QString status() const;
    QString connectionObservation() const;
    QString powerObservation() const;
    Q_INVOKABLE void edit(const QString &key, const QVariant &value);
    Q_INVOKABLE void choose(const QString &key, const QString &token);
    Q_INVOKABLE void retry();
signals:
    void changed();
private:
    struct Intent { QString key; QVariant value; bool finished = false; QString revision; };
    void sampled();
    void dispatchEdit();
    Client::ServiceConnection &service;
    QMap<quint64, Intent> intents;
    QMap<QString, quint64> latest;
    QMap<QString, QString> errors;
    QMap<QString, QVariant> queued;
    QStringList editOrder;
    QString editOwner;
};
class StatusFormatter {
public:
    static QString summary(const Client::ServiceConnection::Snapshot &snapshot);
};
void registerTypes();
}
