#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QVariantMap>

namespace Aerial {
// Observations are independent of settings. The only remote operation is
// Properties.GetAll; tests inject a private bus, never a product env switch.
class PolicyMonitor : public QObject {
    Q_OBJECT
public:
    explicit PolicyMonitor(QDBusConnection bus, QObject *parent = nullptr);
    void start();
    bool ready() const;
    QJsonObject observations() const;
    QString deferReason(const QJsonObject &settings) const;
signals:
    void changed();
private slots:
    void propertiesChanged(const QString &interface, const QVariantMap &values,
        const QStringList &invalidated, const QDBusMessage &message);
private:
    struct Object {
        QString service, path, interface;
        QVariantMap values;
        QSet<QString> invalidated;
        quint64 serial = 0;
        bool pending = false, dirty = false, sampled = false;
    };
    QString watch(const QString &service, const QString &path, const QString &interface);
    void sample(const QString &key);
    void topology();
    void ownerChanged(const QString &service);
    QVariantMap values(const QString &service, const QString &path, const QString &interface) const;
    QDBusConnection bus;
    QMap<QString, Object> objects;
    quint64 serial = 0;
    bool started = false;
};
}
