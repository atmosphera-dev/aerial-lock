#pragma once
#include "common/config-document.h"
#include "common/dbus-types.h"
#include <QDBusMessage>

namespace Aerial::Client {
inline constexpr auto Service = "org.aeriallock.Aerial1";
inline constexpr auto Path = "/org/aeriallock/Aerial1";
inline constexpr auto JobInterface = "org.aeriallock.Aerial1.Job1";
inline constexpr auto Properties = "org.freedesktop.DBus.Properties";
QVariant unpack(const QVariant &value);
QJsonObject settingPatch(const QString &key, QJsonValue value, bool *valid = nullptr);
bool mayActivate(const ConfigPaths &paths, const QString &installedBundle);
QDBusMessage propertyCall(const QString &owner, const QString &path, const QString &interface);
}
