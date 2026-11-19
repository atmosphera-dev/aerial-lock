#include "companion-client.h"
#include "companion-client.moc"
#include <QDBusMessage>
#include <QDir>
#include <unistd.h>

#ifndef AERIAL_COMPANION_DATADIR
#define AERIAL_COMPANION_DATADIR "/usr/share/aerial-lock"
#endif

void CompanionContentClient::ensureContent(const QString &bundle, const QString &profile, int height, const QStringList &topics)
{
    if (ensured || profile.isEmpty()) return;
    ensured = true;
    auto message = QDBusMessage::createMethodCall("org.aeriallock.Aerial1", "/org/aeriallock/Aerial1",
                                                  "org.aeriallock.Aerial1", "EnsureContent");
    const auto paths = Aerial::ConfigPaths::environment(bundle);
    // Activation inherits the manager's environment, never this caller's.
    // A custom/private profile may only notify an already running owner;
    // the service checks the opaque profile before doing anything.
    message.setAutoStartService(Aerial::isDefaultProfile(paths, QStringLiteral(AERIAL_COMPANION_DATADIR), QDir::homePath(),
                                             "/run/user/" + QString::number(getuid())));
    QVariantMap hints{{"profile", profile}, {"topics", topics}};
    if (height > 0) hints.insert("height", height);
    message << hints;
    (bus ? *bus : QDBusConnection::sessionBus()).send(message);
}

void CompanionContentClient::reportPlayback(const QString &profile, const QString &assetKey)
{
    if (profile.isEmpty() || assetKey.isEmpty()) return;
    auto message = QDBusMessage::createMethodCall("org.aeriallock.Aerial1", "/org/aeriallock/Aerial1",
                                                  "org.aeriallock.Aerial1", "ReportPlayback");
    message.setAutoStartService(false); // recency never starts acquisition
    message << profile << assetKey;
    (bus ? *bus : QDBusConnection::sessionBus()).send(message);
}
