#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>

namespace Aerial {

// Local, read-only configuration. No D-Bus, media or network dependencies.
struct ConfigPaths {
    QString bundle, config, cache, state, runtime;
    static ConfigPaths environment(const QString &bundle);
};

struct ConfigDocument {
    ConfigPaths paths;
    QByteArray raw;
    QJsonObject user, projectedUser, effective, schema;
    QString profile, revision;
    QStringList diagnostics;
    QJsonArray settingsDiagnostics, defaultSources;
    bool present = false, userValid = true, bundleValid = false, migrated = false;
    bool idleBundleValid = false;
    bool acquisitionAllowed() const { return bundleValid && userValid && sourcesValid; }
    bool sourcesValid = true;
};

ConfigDocument readConfig(const ConfigPaths &paths);
// A missing bus owner may be activated only from the ordinary installed profile.
// Custom profiles must address an existing owner and compare its profile first.
bool isDefaultProfile(const ConfigPaths &paths, const QString &installedBundle, const QString &home, const QString &runtime);
QJsonObject mergeObjects(QJsonObject base, const QJsonObject &overrides);
QJsonObject mergePatch(QJsonObject base, const QJsonObject &patch);
// Validate only requested leaves. Existing unknown/invalid fields are preserved.
QStringList validatePatch(const QJsonObject &patch, const QJsonObject &schema);
QStringList changedPaths(const QJsonObject &patch, const QString &prefix = {});
QString sourceIdentity(const QJsonObject &source);
// Selection only. Disabled sources remain configured owners of cached data.
inline bool sourceEnabled(const QJsonObject &source) { return source.value("enabled").toBool(true); }

}
