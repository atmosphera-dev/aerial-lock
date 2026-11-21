#include "blocking-client.h"
#include <QDBusConnectionInterface>
#include <QDBusReply>

namespace Aerial::Client {
QVariantMap properties(const QDBusConnection &bus, const QString &object, const QString &interface,
                       QDBusError &error, const QString &destination)
{
    const QDBusReply<QVariantMap> reply = bus.call(propertyCall(destination, object, interface), QDBus::Block, 5000);
    if (!reply.isValid()) { error = reply.error(); return {}; }
    return unpack(reply.value()).toMap();
}

Observer::Observer(QDBusConnection bus) : bus(std::move(bus)), watcher(Service, this->bus, QDBusServiceWatcher::WatchForOwnerChange)
{
    this->bus.connect(Service, {}, Properties, "PropertiesChanged", this, SLOT(changed(QString,QVariantMap,QStringList)));
    connect(&watcher, &QDBusServiceWatcher::serviceOwnerChanged, &loop, &QEventLoop::quit);
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
}
void Observer::wait() { timer.start(250); loop.exec(); }
void Observer::changed(const QString &, const QVariantMap &, const QStringList &) { loop.quit(); }

QVariantList jobs(CompanionClient &client, QDBusError &error)
{
    auto reply = client.ListJobs(); reply.waitForFinished();
    if (reply.isError()) { error = reply.error(); return {}; }
    QVariantList result;
    for (const auto &job : reply.value()) {
        auto row = properties(client.connection(), job.path(), JobInterface, error, client.service());
        if (error.isValid()) return {};
        row["path"] = job.path(); result.append(row);
    }
    return result;
}

std::unique_ptr<CompanionClient> currentOwner(const QDBusConnection &bus, const QString &profile, QDBusError &error)
{
    const auto owner = bus.interface()->serviceOwner(Service);
    if (!owner.isValid()) { error = owner.error(); return {}; }
    auto client = std::make_unique<CompanionClient>(owner.value(), Path, bus);
    client->setTimeout(5000);
    auto settings = client->GetSettings(); settings.waitForFinished();
    if (settings.isError()) { error = settings.error(); return {}; }
    if (client->profile() != profile) {
        error = QDBusError(QDBusMessage::createError("org.aeriallock.Aerial1.Error.ProfileMismatch", "profile differs")); return {};
    }
    return client;
}
}
