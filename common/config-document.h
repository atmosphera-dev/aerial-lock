#pragma once

#include <QByteArray>
#include <QJsonObject>
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
    bool present = false, userValid = true, bundleValid = false, migrated = false;
    bool acquisitionAllowed() const { return bundleValid && userValid && sourcesValid; }
    bool sourcesValid = true;
};

ConfigDocument readConfig(const ConfigPaths &paths);
QJsonObject mergeObjects(QJsonObject base, const QJsonObject &overrides);
QJsonObject mergePatch(QJsonObject base, const QJsonObject &patch);
// Validate only requested leaves. Existing unknown/invalid fields are preserved.
QStringList validatePatch(const QJsonObject &patch, const QJsonObject &schema);
QStringList changedPaths(const QJsonObject &patch, const QString &prefix = {});
QString sourceIdentity(const QJsonObject &source);

}
