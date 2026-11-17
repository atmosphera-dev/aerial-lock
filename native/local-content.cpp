#include "local-content.h"
#include "local-content.moc"
#include <QDateTime>

void LocalContent::freeze(const QString &cacheRoot, const QString &stateRoot, const QVariantList &sources)
{
    if (frozen) return;
    frozen = true;
    this->stateRoot = stateRoot;
    if (qEnvironmentVariableIsSet("AERIAL_LOCK_CONTENT_GENERATION")) {
        const auto root = qEnvironmentVariable("AERIAL_LOCK_CONTENT_ROOT");
        const auto generation = root == cacheRoot ? qEnvironmentVariable("AERIAL_LOCK_CONTENT_GENERATION") : QString();
        snapshot = std::make_unique<Aerial::ContentSnapshot>(cacheRoot, generation);
    } else snapshot = std::make_unique<Aerial::ContentSnapshot>(cacheRoot);
    entries = snapshot->forSources(QJsonArray::fromVariantList(sources));
    emit changed();
}

QVariantList LocalContent::forSource(const QString &sourceId) const
{
    QVariantList result;
    for (const auto &value : entries) {
        const auto entry = value.toObject();
        if (entry.value("sourceId") != sourceId) continue;
        const auto metadata = entry.value("metadata").toObject();
        result.append(QVariantMap{{"path", entry.value("path").toString()},
            {"matchText", metadata.value("accessibilityLabel").toString() + ' ' + metadata.value("title").toString() + ' ' + metadata.value("scene").toString()},
            {"assetId", entry.value("assetId").toString()}});
    }
    return result;
}

QVariantMap LocalContent::lookup(const QString &path) const
{
    for (const auto &value : entries) {
        const auto entry = value.toObject();
        if (entry.value("path") != path) continue;
        const auto metadata = entry.value("metadata").toObject();
        QVariantMap result{{"id", entry.value("assetId").toString()}, {"assetKey", entry.value("key").toString()},
            {"sourceName", entry.value("sourceName").toString()}, {"name", metadata.value("accessibilityLabel").toString()},
            {"secondaryName", metadata.value("title").toString()}, {"scene", metadata.value("scene").toString()},
            {"timeOfDay", metadata.value("timeOfDay").toString()}};
        result["poi"] = metadata.value("pointsOfInterest").toObject().toVariantMap();
        return result;
    }
    return {};
}

void LocalContent::reportPlayback(const QString &path)
{
    const auto key = lookup(path).value("assetKey").toString();
    if (!key.isEmpty()) Aerial::ContentSnapshot::reportPlayback(stateRoot, key, QDateTime::currentSecsSinceEpoch());
}
