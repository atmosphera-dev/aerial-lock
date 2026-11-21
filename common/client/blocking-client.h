#pragma once
#include "client-support.h"
#include "companion_client.h"
#include <QEventLoop>
#include <QTimer>
#include <QDBusServiceWatcher>
#include <memory>

namespace Aerial::Client {
// Only command-line boundaries may use these blocking helpers.
QVariantMap properties(const QDBusConnection &bus, const QString &object, const QString &interface,
                       QDBusError &error, const QString &destination);
QVariantList jobs(CompanionClient &client, QDBusError &error);
std::unique_ptr<CompanionClient> currentOwner(const QDBusConnection &bus, const QString &profile, QDBusError &error);
class Observer : public QObject {
    Q_OBJECT
public:
    explicit Observer(QDBusConnection bus);
    void wait();
private slots:
    void changed(const QString &, const QVariantMap &, const QStringList &);
private:
    QDBusConnection bus;
    QDBusServiceWatcher watcher;
    QTimer timer;
    QEventLoop loop;
};
}
