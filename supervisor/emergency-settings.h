#pragma once

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <cmath>
#include <limits>

// Read-only projection of ConfigStore's authentication leaves. A broken
// theme/media field is deliberately not an authentication dependency here.
struct EmergencySettings {
    QString service;
    int watchdog = 60000;
    bool ignoreLeadingSpaces = false;
    QStringList warnings;
};

inline EmergencySettings emergencySettings(const QString &bundle, const QString &userPath,
                                             const QString &serviceOverride)
{
    EmergencySettings result;
    auto read = [&result](const QString &path, const QString &reason) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            result.warnings.append(reason);
            return QJsonObject();
        }
        QJsonParseError error;
        auto json = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !json.isObject()) {
            result.warnings.append(reason);
            return QJsonObject();
        }
        return json.object();
    };
    auto defaults = read(bundle + QStringLiteral("/Config/defaults.json"), QStringLiteral("bundle-auth-settings-unavailable"));
    auto user = read(userPath, QStringLiteral("user-auth-settings-unavailable"));
    auto field = [&defaults, &user](const QString &key, bool string) {
        auto valid = [string](const QJsonValue &v) { return string ? v.isString() : v.isDouble(); };
        return valid(user.value(key)) ? user.value(key) : defaults.value(key);
    };
    const auto service = field(QStringLiteral("pamService"), true);
    if (!serviceOverride.isEmpty()) result.service = serviceOverride;
    else if (service.isString()) result.service = service.toString().isEmpty() ? QStringLiteral("aerial-lock") : service.toString();
    if (result.service.contains(QChar(0))) {
        result.service.clear();
        result.warnings.append(QStringLiteral("auth-service-invalid"));
    }
    if (result.service.isEmpty()) result.warnings.append(QStringLiteral("auth-service-unavailable"));
    const auto watchdog = field(QStringLiteral("pamWatchdogTimeoutMs"), false);
    const double value = watchdog.toDouble(-1);
    if (std::isfinite(value) && value >= 0 && value <= double(std::numeric_limits<int>::max()))
        result.watchdog = int(value); // QML int conversion: conformance-covered.
    else result.warnings.append(QStringLiteral("auth-watchdog-invalid"));
    const auto ignoreLeadingSpaces = user.value(QStringLiteral("ignoreLeadingSpaces"));
    result.ignoreLeadingSpaces = ignoreLeadingSpaces.isBool() ? ignoreLeadingSpaces.toBool()
        : defaults.value(QStringLiteral("ignoreLeadingSpaces")).toBool();
    return result;
}

inline EmergencySettings emergencySettings(const QString &bundle)
{
    QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (config.isEmpty()) config = qEnvironmentVariable("HOME") + QStringLiteral("/.config");
    return emergencySettings(bundle, config + QStringLiteral("/aerial-lock/config.json"),
                             qEnvironmentVariable("AERIAL_LOCK_PAM_SERVICE"));
}
