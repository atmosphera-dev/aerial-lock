#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <memory>
#include <optional>

namespace Aerial {
class OwnedDirectory;
bool contentKey(const QString &key);
QString contentDigest(const QByteArray &bytes);

// An episode owns this shared flock FD, independent of the companion lifetime.
// Failure freezes an empty snapshot; reads never wait on maintenance or D-Bus.
class ContentSnapshot {
public:
    // An explicitly empty generation means the episode captured no content.
    // It must not fall through to a newer current.json on a client respawn.
    explicit ContentSnapshot(const QString &cacheRoot, std::optional<QString> generation = std::nullopt);
    ~ContentSnapshot();
    ContentSnapshot(const ContentSnapshot &) = delete;
    ContentSnapshot &operator=(const ContentSnapshot &) = delete;
    QJsonArray entries() const { return inventory; }
    QString generation() const { return revision; }
    bool valid() const { return ready; }
    QJsonArray forSources(const QJsonArray &sources) const;
    bool available(const QJsonObject &entry) const;
    static QJsonObject lastPlayback(const QString &stateRoot);
    static bool reportPlayback(const QString &stateRoot, const QString &key, qint64 timestamp);
private:
    int lock = -1;
    bool ready = false;
    QString revision;
    QString root;
    QJsonArray inventory;
};
}
