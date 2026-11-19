#include "content-snapshot.h"
#include "owned-directory.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSet>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace Aerial {
bool contentKey(const QString &key)
{
    if (key.size() != 64) return false;
    for (const auto c : key) if (!((c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'))) return false;
    return true;
}

QString contentDigest(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

ContentSnapshot::ContentSnapshot(const QString &cacheRoot, std::optional<QString> generation) : root(cacheRoot)
{
    if (generation && generation->isEmpty()) return;
    OwnedDirectory cache(cacheRoot);
    lock = cache.open("playback.lock", O_RDONLY);
    if (lock < 0 || flock(lock, LOCK_SH | LOCK_NB)) return;
    bool ok = false;
    QString key;
    if (generation) key = *generation;
    else {
        const auto current = QJsonDocument::fromJson(cache.read("current.json", 4096, &ok)).object();
        if (!ok || current.value("version").toInt() != 1) return;
        key = current.value("generation").toString();
    }
    if (!contentKey(key)) return;
    const auto raw = cache.read("generations/" + key + ".json", 8 * 1024 * 1024, &ok);
    const auto index = QJsonDocument::fromJson(raw).object();
    if (!ok || contentDigest(raw) != key || index.value("version").toInt() != 1 || !index.value("entries").isArray()) return;
    QJsonArray validated;
    QSet<QString> keys;
    for (const auto &value : index.value("entries").toArray()) {
        auto entry = value.toObject();
        const auto assetKey = entry.value("key").toString();
        const auto relative = "objects/" + assetKey + ".media";
        if (!contentKey(assetKey) || keys.contains(assetKey) || entry.value("sourceId").toString().isEmpty()
            || entry.value("assetId").toString().isEmpty() || entry.value("path").toString() != relative
            || !available(entry)) continue;
        keys.insert(assetKey);
        entry["path"] = cache.path() + '/' + relative;
        validated.append(entry);
    }
    inventory = validated;
    revision = key;
    ready = true;
}

ContentSnapshot::~ContentSnapshot() { if (lock >= 0) close(lock); }

bool ContentSnapshot::available(const QJsonObject &entry) const
{
    const auto key = entry.value("key").toString();
    if (!contentKey(key)) return false;
    OwnedDirectory cache(root);
    struct stat st{};
    const auto identity = entry.value("identity").toObject();
    return cache.identity("objects/" + key + ".media", st) && st.st_size > 0
        && entry.value("bytes").toInteger() == st.st_size
        // The descriptor-rooted path supplies the filesystem context. st_dev is
        // a mount-local identifier, not persistent identity (legacy entries may
        // still contain it). Inode, size and nanosecond mtime survive remounts.
        && qint64(st.st_ino) == identity.value("inode").toInteger()
        && st.st_mtim.tv_sec == identity.value("seconds").toInteger()
        && st.st_mtim.tv_nsec == identity.value("nanoseconds").toInteger();
}

QJsonArray ContentSnapshot::forSources(const QJsonArray &sources) const
{
    QJsonArray result;
    for (const auto &value : sources) {
        const auto source = value.toObject();
        if (source.value("type") != "apple" && source.value("type") != "community") continue;
        for (const auto &item : inventory) {
            auto entry = item.toObject();
            if (entry.value("sourceId") != source.value("id")) continue;
            entry["sourceName"] = source.value("name");
            result.append(entry);
        }
    }
    return result;
}

QJsonObject ContentSnapshot::lastPlayback(const QString &stateRoot)
{
    OwnedDirectory state(stateRoot);
    const auto value = QJsonDocument::fromJson(state.read("last-playback.json", 4096)).object();
    return contentKey(value.value("key").toString()) ? value : QJsonObject{};
}

bool ContentSnapshot::reportPlayback(const QString &stateRoot, const QString &key, qint64 timestamp)
{
    if (!contentKey(key) || timestamp < 0) return false;
    OwnedDirectory state(stateRoot, true);
    const int lock = state.open("playback-state.lock", O_RDWR | O_CREAT);
    if (lock < 0) return false;
    const bool ok = flock(lock, LOCK_EX | LOCK_NB) == 0 && state.replace("last-playback.json",
        QJsonDocument(QJsonObject{{"version", 1}, {"key", key}, {"playedAt", timestamp}}).toJson(QJsonDocument::Compact));
    close(lock);
    return ok;
}
}
