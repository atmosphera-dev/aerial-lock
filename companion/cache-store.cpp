#include "cache-store.h"
#include "../common/content-snapshot.h"

#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <fcntl.h>
#include <limits>
#include <sys/file.h>
#include <unistd.h>

namespace Aerial {
CacheStore::CacheStore(const QString &root, std::optional<quint64> initialMaximum) : directory(root, true)
{
    if (initialMaximum) cap = *initialMaximum;
    if (!directory.valid()) return;
    bool safe = false;
    const auto present = directory.files(&safe);
    if (!safe) return;
    const auto ownership = metadata("ownership.json");
    if (ownership.value("format").toString() != "aerial-lock-managed-v1") {
        // Do not silently adopt an existing directory containing user files.
        const QByteArray marker("{\"format\":\"aerial-lock-managed-v1\"}");
        if (!present.isEmpty() || (initialMaximum && quint64(marker.size()) > cap)
            || !directory.replace("ownership.json", marker)) return;
    }
    writer = directory.open("owner.lock", O_RDWR | O_CREAT);
    if (writer < 0 || flock(writer, LOCK_EX | LOCK_NB)) return;
    const int playback = directory.open("playback.lock", O_RDWR | O_CREAT);
    if (playback < 0) return;
    close(playback);
    const ContentSnapshot snapshot(root);
    if (snapshot.valid()) {
        for (const auto &value : snapshot.entries()) {
            auto item = value.toObject();
            item["path"] = "objects/" + item.value("key").toString() + ".media";
            completed.append(item);
        }
    } else {
        // A corrupt existing index is not an empty cache and must not be
        // overwritten or inferred from filenames. Missing index is first run.
        struct stat st{};
        if (directory.identity("current.json", st)) return;
    }
    owned = true;
}

CacheStore::~CacheStore() { if (writer >= 0) close(writer); }

CacheUsage CacheStore::usage() const
{
    CacheUsage result;
    result.reserved = remaining;
    const auto files = directory.files(&result.safe);
    if (!result.safe) return result;
    for (const auto &file : files) {
        struct stat st{};
        if (!directory.identity(file, st) || st.st_size < 0 || quint64(st.st_size) > std::numeric_limits<quint64>::max() - result.bytes) {
            result.safe = false; return result;
        }
        result.bytes += st.st_size;
        if (file.startsWith("partials/")) result.partial += st.st_size;
    }
    return result;
}

bool CacheStore::canWrite(quint64 bytes, bool useReservation) const
{
    if (!owned || !cap) return false;
    const auto used = usage();
    const auto promised = useReservation ? std::max(remaining, bytes) : remaining;
    if (!used.safe || used.bytes > cap || promised > cap - used.bytes) return false;
    if (!useReservation && bytes > cap - used.bytes - promised) return false;
    const auto available = directory.available();
    return available >= freeReserve && bytes <= available - freeReserve;
}

bool CacheStore::reserve(quint64 bytes)
{
    const auto previous = remaining;
    remaining = 0;
    if (!canWrite(bytes)) { remaining = previous; return false; }
    remaining = bytes;
    return true;
}

bool CacheStore::canAcquire(const Representation &rep) const
{
    if (rep.bytes < 0) return canWrite(1);
    struct stat partial{};
    const auto received = directory.identity("partials/" + rep.key + ".part", partial) ? quint64(partial.st_size) : 0;
    // Include the next immutable generation plus the bounded probe response,
    // pointer and both copies of atomic resume metadata. These are publication
    // headroom, not a second content quota. Received partial bytes count once.
    const auto index = QJsonDocument(QJsonObject{{"version", 1}, {"entries", completed}}).toJson(QJsonDocument::Compact);
    const auto metadata = QJsonDocument(rep.metadata).toJson(QJsonDocument::Compact);
    const quint64 overhead = index.size() + metadata.size() + 4096
        + 2 * (rep.url.toEncoded().size() + rep.sourceId.toUtf8().size() + rep.assetId.toUtf8().size() + 2048);
    const auto remainingBytes = quint64(rep.bytes) - std::min(quint64(rep.bytes), received);
    return remainingBytes <= std::numeric_limits<quint64>::max() - overhead && canWrite(remainingBytes + overhead);
}

bool CacheStore::writeMetadata(const QString &relative, const QJsonObject &object)
{
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    // Count the old and replacement bytes while atomic replacement is staged.
    return canWrite(bytes.size()) && directory.replace(relative, bytes);
}

QJsonObject CacheStore::metadata(const QString &relative) const
{
    return QJsonDocument::fromJson(directory.read(relative, SourceResolver::maximumManifest)).object();
}

bool CacheStore::contains(const QString &key) const
{
    for (const auto &value : completed) if (value.toObject().value("key") == key) return true;
    return false;
}

int CacheStore::openPartial(const QString &key, bool truncate) const
{
    if (!owned || !contentKey(key)) return -1;
    return directory.open("partials/" + key + ".part", O_RDWR | O_CREAT | (truncate ? O_TRUNC : O_APPEND), true);
}

int CacheStore::openForProbe(const QString &key) const
{
    return contentKey(key) ? directory.open("partials/" + key + ".part", O_RDONLY) : -1;
}

bool CacheStore::append(int fd, const QByteArray &chunk)
{
    if (!canWrite(chunk.size(), true)) return false;
    const auto count = ::write(fd, chunk.constData(), chunk.size());
    // Partial disk writes are real usage even when ENOSPC interrupts a chunk.
    if (count > 0) remaining -= std::min<quint64>(remaining, count);
    return count == chunk.size();
}

bool CacheStore::removeFile(const QString &relative)
{
    struct stat st{};
    return directory.identity(relative, st) && directory.remove(relative, st);
}

bool CacheStore::discardPartial(const QString &key)
{
    if (!contentKey(key)) return false;
    bool ok = true;
    for (const auto &suffix : {".part", ".json"}) {
        const auto relative = "partials/" + key + suffix;
        struct stat st{};
        if (directory.identity(relative, st) && !directory.remove(relative, st)) ok = false;
    }
    return ok;
}

bool CacheStore::complete(const Representation &rep, const QJsonObject &probe, qint64 now)
{
    if (!contentKey(rep.key) || !probe.value("initialFrameDecoded").toBool() || contains(rep.key)) return false;
    releaseReservation();
    const auto part = "partials/" + rep.key + ".part", object = "objects/" + rep.key + ".media";
    struct stat st{};
    if (!directory.identity(part, st) || st.st_size <= 0 || !directory.rename(part, object)) return false;
    QJsonObject identity{{"device", qint64(st.st_dev)}, {"inode", qint64(st.st_ino)},
        {"seconds", qint64(st.st_mtim.tv_sec)}, {"nanoseconds", qint64(st.st_mtim.tv_nsec)}};
    const QJsonObject entry{{"key", rep.key}, {"sourceId", rep.sourceId}, {"assetId", rep.assetId},
        {"path", object}, {"bytes", qint64(st.st_size)}, {"height", rep.height}, {"variant", rep.variant},
        {"metadata", rep.metadata}, {"probe", probe}, {"completedAt", now}, {"identity", identity}};
    completed.append(entry);
    if (!persistIndex()) {
        completed.removeLast();
        // No index entry exists. Keep the verified bytes as a resumable part.
        directory.rename(object, part);
        return false;
    }
    removeFile("partials/" + rep.key + ".json");
    return true;
}

bool CacheStore::persistIndex(bool reclamation)
{
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"entries", completed}}).toJson(QJsonDocument::Compact);
    const auto hash = contentDigest(bytes), relative = "generations/" + hash + ".json";
    struct stat st{};
    const bool existing = directory.identity(relative, st);
    // A lowered cap can already be below pinned usage. Reclamation still needs
    // an atomic, smaller index before unlinking data. Permit only that shrinking
    // bookkeeping transaction under the caller's exclusive playback lock; no
    // acquisition/growth path uses this allowance. Usage always reports all bytes.
    const auto current = metadata("current.json").value("generation").toString();
    const auto prior = contentKey(current) ? directory.read("generations/" + current + ".json", 8 * 1024 * 1024) : QByteArray{};
    const bool shrinking = reclamation && bytes.size() < prior.size() && usage().safe;
    const auto pointer = QJsonDocument(QJsonObject{{"version", 1}, {"generation", hash}}).toJson(QJsonDocument::Compact);
    if (!existing && (!(canWrite(bytes.size()) || (shrinking && directory.available() >= freeReserve + bytes.size() + pointer.size()))
        || !directory.replace(relative, bytes))) return false;
    if (existing && directory.read(relative, 8 * 1024 * 1024) != bytes) return false;
    if (!(shrinking ? directory.replace("current.json", pointer) : writeMetadata("current.json", {{"version", 1}, {"generation", hash}}))) {
        if (!existing) removeFile(relative);
        return false;
    }
    return true;
}

QJsonObject CacheStore::prune(bool dryRun, const QSet<QString> &protectedKeys, quint64 needed)
{
    QJsonObject result{{"removedBytes", qint64(0)}, {"protected", false}, {"dryRun", dryRun}};
    const int lock = directory.open("playback.lock", O_RDWR);
    if (!owned || lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) {
        if (lock >= 0) close(lock);
        result["protected"] = true; return result;
    }
    quint64 removed = 0;
    auto candidates = completed;
    // Last verified fallback survives even if the caller has no recency hint.
    while (candidates.size() > 1) {
        qsizetype selected = -1;
        qint64 oldest = std::numeric_limits<qint64>::max();
        for (qsizetype i = 0; i < candidates.size(); ++i) {
            const auto entry = candidates.at(i).toObject();
            if (protectedKeys.contains(entry.value("key").toString())) continue;
            const auto timestamp = entry.value("playedAt").toInteger(entry.value("completedAt").toInteger());
            if (timestamp < oldest) { oldest = timestamp; selected = i; }
        }
        if (selected < 0) break;
        const auto entry = candidates.takeAt(selected).toObject();
        struct stat st{};
        const auto relative = entry.value("path").toString();
        const auto identity = entry.value("identity").toObject();
        if (!directory.identity(relative, st) || qint64(st.st_dev) != identity.value("device").toInteger()
            || qint64(st.st_ino) != identity.value("inode").toInteger()
            || st.st_size != entry.value("bytes").toInteger()
            || st.st_mtim.tv_sec != identity.value("seconds").toInteger()
            || st.st_mtim.tv_nsec != identity.value("nanoseconds").toInteger()) break;
        if (!dryRun) {
            const auto old = completed;
            completed = candidates;
            if (!persistIndex(true)) { completed = old; break; }
            if (!directory.remove(relative, st)) break;
        }
        removed += st.st_size;
        if (needed && removed >= needed) break;
    }
    // Old generations can be removed only under the same reader lock. Their
    // filenames are hashes and identities are rechecked at deletion time.
    if (!dryRun) {
        const auto current = metadata("current.json").value("generation").toString();
        for (const auto &relative : directory.files()) {
            if (relative.startsWith("generations/") && relative != "generations/" + current + ".json"
                && contentKey(relative.mid(12, 64)) && relative == "generations/" + relative.mid(12, 64) + ".json") removeFile(relative);
        }
    }
    close(lock);
    result["removedBytes"] = qint64(removed);
    return result;
}

bool CacheStore::evict(const QString &key)
{
    if (!contains(key)) return true;
    QSet<QString> protectedKeys;
    for (const auto &value : completed) {
        const auto candidate = value.toObject().value("key").toString();
        if (candidate != key) protectedKeys.insert(candidate);
    }
    const auto result = prune(false, protectedKeys, 1);
    return !result.value("protected").toBool() && result.value("removedBytes").toInteger() > 0 && !contains(key);
}
}
