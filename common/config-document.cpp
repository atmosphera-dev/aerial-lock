#include "config-document.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUrl>
#include <cmath>

namespace Aerial {
namespace {
QString digest(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString homePath(const char *variable, const QString &fallback)
{
    const auto value = qEnvironmentVariable(variable);
    return QDir::cleanPath(value.isEmpty() ? QDir::homePath() + fallback : value);
}

bool objectFile(const QString &path, QByteArray &bytes, QJsonObject &object, QStringList &errors)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errors.append(path + ": cannot read object");
        return false;
    }
    bytes = file.readAll();
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        errors.append(path + ": invalid JSON object");
        return false;
    }
    object = doc.object();
    return true;
}

bool matches(const QJsonValue &value, const QJsonObject &spec)
{
    const auto type = spec.value("type").toString();
    bool valid = type == "object" ? value.isObject()
        : type == "array" ? value.isArray()
        : type == "string" ? value.isString()
        : type == "boolean" ? value.isBool()
        : (type == "number" || type == "integer") && value.isDouble() && std::isfinite(value.toDouble());
    if (!valid) return false;
    if (type == "integer" && std::floor(value.toDouble()) != value.toDouble()) return false;
    if (spec.contains("minimum") && value.toDouble() < spec.value("minimum").toDouble()) return false;
    if (spec.contains("maximum") && value.toDouble() > spec.value("maximum").toDouble()) return false;
    if (spec.contains("enum") && !spec.value("enum").toArray().contains(value)) return false;
    if (value.isString() && value.toString().contains(QChar(0))) return false;
    if (value.isString() && spec.contains("minLength") && value.toString().size() < spec.value("minLength").toInt()) return false;
    if (type == "array" && spec.contains("items")) {
        QSet<QByteArray> seen;
        for (const auto &item : value.toArray()) {
            if (!matches(item, spec.value("items").toObject())) return false;
            const auto encoded = QJsonDocument(QJsonArray{item}).toJson(QJsonDocument::Compact);
            if (spec.value("uniqueItems").toBool() && seen.contains(encoded)) return false;
            seen.insert(encoded);
        }
    }
    return true;
}

// A bad object reports each affected leaf, preserving existing diagnostics.
void invalidLeaves(const QJsonObject &spec, const QString &path, QStringList &errors)
{
    const auto properties = spec.value("properties").toObject();
    if (properties.isEmpty()) errors.append(path + " invalid; using default");
    else for (auto it = properties.begin(); it != properties.end(); ++it)
        invalidLeaves(it.value().toObject(), path.isEmpty() ? it.key() : path + "." + it.key(), errors);
}

QJsonObject normalize(QJsonObject candidate, const QJsonObject &defaults, const QJsonObject &spec,
                      const QString &prefix, QStringList &errors)
{
    const auto properties = spec.value("properties").toObject();
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const auto key = it.key();
        const auto field = it.value().toObject();
        const auto path = prefix.isEmpty() ? key : prefix + "." + key;
        if (!candidate.contains(key) && field.value("optional").toBool()) continue;
        if (!matches(candidate.value(key), field)) {
            invalidLeaves(field, path, errors);
            if (defaults.contains(key)) candidate[key] = defaults.value(key);
            else candidate.remove(key);
        } else if (field.value("type") == "object") {
            candidate[key] = normalize(candidate.value(key).toObject(), defaults.value(key).toObject(),
                                       field, path, errors);
        }
    }
    return candidate;
}

bool completeValue(const QJsonValue &value, const QJsonObject &spec)
{
    if (!matches(value, spec)) return false;
    if (value.isObject()) {
        const auto properties = spec.value("properties").toObject();
        const auto object = value.toObject();
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            const auto field = it.value().toObject();
            if (!object.contains(it.key()) && field.value("optional").toBool()) continue;
            if (!completeValue(object.value(it.key()), field)) return false;
        }
    }
    return true;
}

QJsonArray normalizeSources(const QJsonArray &list, const QJsonObject &spec, QStringList &errors)
{
    QJsonArray result;
    QSet<QString> names, identities;
    for (qsizetype i = 0; i < list.size(); ++i) {
        auto source = list.at(i).toObject();
        const auto path = QStringLiteral("sources[%1]").arg(i);
        const auto type = source.value("type").toString();
        const auto name = source.value("name").toString();
        const auto url = source.value("source").toString();
        QString reason;
        if (!completeValue(list.at(i), spec)) reason = " has invalid type/name/source/id/enabled/resolution fields";
        if (source.contains("minResolution") && source.contains("maxResolution")
            && source.value("minResolution").toDouble() > source.value("maxResolution").toDouble())
            reason = " has an inverted resolution band";
        if (type == "apple" || type == "community") {
            const QUrl remote(url);
            if (!remote.isValid() || (remote.scheme() != "https" && remote.scheme() != "http")
                || remote.host().isEmpty()) reason = ".source must be an HTTP(S) URL";
        }
        auto safe = name;
        safe.replace(' ', '-');
        QString filtered;
        for (const auto c : safe) {
            const auto n = c.unicode();
            if ((n >= 'a' && n <= 'z') || (n >= 'A' && n <= 'Z') || (n >= '0' && n <= '9')
                || c == '.' || c == '_' || c == '-') filtered.append(c);
        }
        if (filtered.isEmpty() || filtered == "." || filtered == "..") reason = ".name has no safe cache name";
        if (names.contains(filtered)) reason = " duplicates legacy cache name";
        const auto id = sourceIdentity(source);
        if (identities.contains(id)) reason = " duplicates source identity";
        if (!reason.isEmpty()) {
            errors.append(path + reason + "; entry dropped");
            continue;
        }
        source["safeName"] = filtered;
        source["id"] = id;
        source["enabled"] = sourceEnabled(source);
        names.insert(filtered);
        identities.insert(id);
        result.append(source);
    }
    return result;
}

void patchErrors(const QJsonObject &patch, const QJsonObject &spec, const QString &prefix, QStringList &errors)
{
    const auto properties = spec.value("properties").toObject();
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const auto path = prefix.isEmpty() ? it.key() : prefix + "." + it.key();
        if (!properties.contains(it.key())) { errors.append(path + ": unknown setting"); continue; }
        if (it.value().isNull()) continue;
        const auto field = properties.value(it.key()).toObject();
        if (!matches(it.value(), field)) { errors.append(path + ": invalid value"); continue; }
        if (field.value("type") == "object") patchErrors(it.value().toObject(), field, path, errors);
        if (path == "sources") {
            QStringList sourceErrors;
            normalizeSources(it.value().toArray(), field.value("sourceItem").toObject(), sourceErrors);
            errors.append(sourceErrors);
        }
    }
}
}

ConfigPaths ConfigPaths::environment(const QString &bundle)
{
    return {QDir::cleanPath(QFileInfo(bundle).absoluteFilePath()),
            homePath("XDG_CONFIG_HOME", "/.config") + "/aerial-lock/config.json",
            homePath("XDG_CACHE_HOME", "/.cache") + "/aerial-lock/managed-v1",
            homePath("XDG_STATE_HOME", "/.local/state") + "/aerial-lock",
            homePath("XDG_RUNTIME_DIR", "/.local/run") + "/aerial-lock"};
}

bool isDefaultProfile(const ConfigPaths &paths, const QString &installedBundle, const QString &home, const QString &runtime)
{
    return paths.bundle == QDir::cleanPath(installedBundle)
        && paths.config == home + "/.config/aerial-lock/config.json"
        && paths.cache == home + "/.cache/aerial-lock/managed-v1"
        && paths.state == home + "/.local/state/aerial-lock"
        && paths.runtime == runtime + "/aerial-lock";
}

QJsonObject mergeObjects(QJsonObject base, const QJsonObject &overrides)
{
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        base[it.key()] = base.value(it.key()).isObject() && it.value().isObject()
            ? QJsonValue(mergeObjects(base.value(it.key()).toObject(), it.value().toObject())) : it.value();
    return base;
}

QJsonObject mergePatch(QJsonObject base, const QJsonObject &patch)
{
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it.value().isNull()) base.remove(it.key());
        else if (it.value().isObject()) base[it.key()] = mergePatch(base.value(it.key()).toObject(), it.value().toObject());
        else base[it.key()] = it.value();
    }
    return base;
}

ConfigDocument readConfig(const ConfigPaths &paths)
{
    ConfigDocument result;
    result.paths = paths;
    QByteArray defaultsBytes, schemaBytes;
    QJsonObject defaults;
    const bool readDefaults = objectFile(paths.bundle + "/Config/defaults.json", defaultsBytes, defaults, result.diagnostics);
    const bool readSchema = objectFile(paths.bundle + "/Config/settings-schema.json", schemaBytes, result.schema, result.diagnostics);
    const auto idleSpec = result.schema.value("properties").toObject().value("idle").toObject();
    const auto idleFields = idleSpec.value("properties").toObject();
    const QJsonObject idleTypes{{"timeoutSeconds", "integer"}, {"respectInhibitors", "boolean"},
        {"lockOnSuspend", "boolean"}, {"logindLockUnlock", "boolean"}};
    result.idleBundleValid = readDefaults && readSchema && idleSpec.value("type") == "object"
        && completeValue(defaults.value("idle"), idleSpec);
    // The consumer requires this complete typed policy even if a damaged schema
    // omits its fields. Values and their constraints still come from the bundle.
    for (auto it = idleTypes.begin(); it != idleTypes.end(); ++it)
        if (!defaults.value("idle").toObject().contains(it.key())
            || idleFields.value(it.key()).toObject().value("type") != it.value()) result.idleBundleValid = false;
    if (!result.idleBundleValid) result.diagnostics.append("idle: incomplete or invalid bundled policy/schema");
    if (readDefaults && readSchema) {
        const auto before = result.diagnostics.size();
        normalize(defaults, {}, result.schema, {}, result.diagnostics);
        normalizeSources(defaults.value("sources").toArray(), result.schema.value("properties").toObject().value("sources").toObject().value("sourceItem").toObject(), result.diagnostics);
        result.bundleValid = result.idleBundleValid && result.diagnostics.size() == before;
    }
    const auto info = QFileInfo(paths.config);
    result.present = info.exists() || info.isSymLink();
    if (result.present) result.userValid = objectFile(paths.config, result.raw, result.user, result.diagnostics);
    result.projectedUser = result.user;
    if (!result.user.contains("sources") && (result.user.contains("video") || result.user.contains("videoDir") || result.user.contains("apple"))) {
        QJsonArray sources;
        for (const auto &key : {"video", "videoDir"}) {
            const auto value = result.user.value(key).toString();
            if (!value.isEmpty()) sources.append(QJsonObject{{"type", QString(key) == "video" ? "file" : "folder"}, {"name", key}, {"source", value}});
            result.projectedUser.remove(key);
        }
        if (result.user.value("apple").toObject().value("enabled").toBool())
            sources.append(QJsonObject{{"type", "apple"}, {"name", "Apple TV"}, {"source", "https://sylvan.apple.com/Aerials/resources-16.tar"}});
        result.projectedUser.remove("apple");
        result.projectedUser["sources"] = sources;
        result.migrated = true;
    }
    result.sourcesValid = !result.user.contains("sources") || result.user.value("sources").isArray();
    if (result.bundleValid) {
        result.effective = normalize(mergeObjects(defaults, result.projectedUser), defaults, result.schema, {}, result.diagnostics);
        if (!result.userValid || !result.sourcesValid) result.effective["sources"] = QJsonArray{};
        result.effective["sources"] = normalizeSources(result.effective.value("sources").toArray(), result.schema.value("properties").toObject().value("sources").toObject().value("sourceItem").toObject(), result.diagnostics);
    } else if (result.idleBundleValid) {
        // Content validation cannot disable the independent idle/suspend daemon.
        const QJsonObject idleDefaults{{"idle", defaults.value("idle")}};
        QJsonObject overrides;
        if (result.projectedUser.contains("idle")) overrides["idle"] = result.projectedUser.value("idle");
        result.effective = normalize(mergeObjects(idleDefaults, overrides), idleDefaults,
            {{"type", "object"}, {"properties", QJsonObject{{"idle", idleSpec}}}}, {}, result.diagnostics);
    }
    const auto identity = QJsonDocument(QJsonArray{paths.bundle, paths.config, paths.cache, paths.state, paths.runtime,
                                                  digest(defaultsBytes), digest(schemaBytes)}).toJson(QJsonDocument::Compact);
    result.profile = digest(identity);
    result.revision = digest(identity + (result.present ? "\nPRESENT\n" : "\nABSENT\n") + result.raw
                             + (result.userValid ? "\nVALID" : "\nINVALID"));
    return result;
}

QStringList validatePatch(const QJsonObject &patch, const QJsonObject &schema)
{
    QStringList errors;
    patchErrors(patch, schema, {}, errors);
    return errors;
}

QStringList changedPaths(const QJsonObject &patch, const QString &prefix)
{
    QStringList paths;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const auto path = prefix.isEmpty() ? it.key() : prefix + "." + it.key();
        if (it.value().isObject()) paths.append(changedPaths(it.value().toObject(), path));
        else paths.append(path);
    }
    return paths;
}

QString sourceIdentity(const QJsonObject &source)
{
    if (source.contains("id")) return source.value("id").toString();
    return digest(QJsonDocument(QJsonArray{source.value("type"), source.value("source")}).toJson(QJsonDocument::Compact));
}
}
