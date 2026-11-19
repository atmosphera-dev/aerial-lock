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
namespace {
bool matches(const struct stat &st, const QJsonObject &entry)
{
    const auto identity = entry.value("identity").toObject();
    return st.st_size == entry.value("bytes").toInteger(-1)
        && qint64(st.st_ino) == identity.value("inode").toInteger(-1)
        && st.st_mtim.tv_sec == identity.value("seconds").toInteger(-1)
        && st.st_mtim.tv_nsec == identity.value("nanoseconds").toInteger(-1);
}
QJsonObject mediaEntry(const Representation &rep, const QJsonObject &probe, qint64 bytes,
                      const QJsonObject &identity, qint64 now)
{
    return {{"key", rep.key}, {"sourceId", rep.sourceId}, {"assetId", rep.assetId},
        {"path", "objects/" + rep.key + ".media"}, {"bytes", bytes}, {"height", rep.height}, {"variant", rep.variant},
        {"metadata", rep.metadata}, {"probe", probe}, {"completedAt", now}, {"identity", identity}};
}
QByteArray encoded(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
}

CacheStore::CacheStore(const QString &root, std::optional<quint64> initialMaximum,
                      std::function<void(const QString &)> observer)
    : directory(root, true), transactionObserver(std::move(observer))
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
    writerOwned = true;
    const int playback = directory.open("playback.lock", O_RDWR | O_CREAT);
    if (playback < 0) return;
    close(playback);
    recover();
}

bool CacheStore::recover()
{
    if (!writerOwned || !directory.valid()) return false;
    owned = false;
    completed = {};
    // The durable commit pointer precedes the public pointer. It is the recovery
    // authority, never a directory scan or a filename claiming to be complete.
    struct stat commitStat{};
    const bool hasCommit = directory.identity("committed.json", commitStat);
    const auto commit = metadata("committed.json");
    if (hasCommit && (commit.value("version").toInt() != 1 || !contentKey(commit.value("generation").toString()))) return false;
    if (hasCommit) {
        const auto index = commit.value("index").toObject();
        const auto bytes = QJsonDocument(index).toJson(QJsonDocument::Compact);
        if (index.value("version").toInt() != 1 || !index.value("entries").isArray()
            || contentDigest(bytes) != commit.value("generation")) return false;
        const auto relative = "generations/" + commit.value("generation").toString() + ".json";
        if (directory.read(relative, 8 * 1024 * 1024) != bytes && !directory.replace(relative, bytes)) return false;
    }
    {
        const ContentSnapshot snapshot(directory.path(), hasCommit ? std::optional<QString>(commit.value("generation").toString()) : std::nullopt);
        if (snapshot.valid()) {
            for (const auto &value : snapshot.entries()) {
                auto item = value.toObject();
                item["path"] = "objects/" + item.value("key").toString() + ".media";
                completed.append(item);
            }
        } else {
            // Without a validated receipt an existing corrupt index is not an
            // empty cache and must never be inferred from media filenames.
            struct stat st{};
            if (hasCommit || directory.identity("current.json", st)) return false;
        }
    }
    owned = true;
    pending = metadata("pending.json");
    struct stat pendingStat{};
    if ((pending.isEmpty() && directory.identity("pending.json", pendingStat)) || !recoverTransaction()) { owned = false; return false; }
    const QJsonObject pointer{{"version", 1}, {"generation", commit.value("generation")}};
    if (hasCommit && metadata("current.json") != pointer) {
        // Existing verified data may be above a newly lowered cap. Repair only
        // its small pointer; real usage remains visible and acquisition stops.
        if (!directory.replace("current.json", QJsonDocument(pointer).toJson(QJsonDocument::Compact))) owned = false;
    }
    return owned;
}

CacheStore::~CacheStore() { if (writer >= 0) close(writer); }

void CacheStore::observe(const QString &step) const
{
    if (transactionObserver) transactionObserver(step);
}

bool CacheStore::prepareTransaction(const QString &operation, const QJsonObject &entry, bool reclamation)
{
    if (!pending.isEmpty()) return false;
    const QJsonObject receipt{{"version", 1}, {"operation", operation}, {"entry", entry}};
    const auto bytes = QJsonDocument(receipt).toJson(QJsonDocument::Compact);
    if (!(canWrite(bytes.size()) || (reclamation && usage().safe && directory.available() >= freeReserve
        && quint64(bytes.size()) <= directory.available() - freeReserve))) return false;
    if (!directory.replace("pending.json", bytes)) {
        if (directory.lastMutation() == OwnedDirectory::Mutation::Unsynchronized) owned = false;
        return false;
    }
    observe("prepared");
    return true;
}

bool CacheStore::recoverTransaction()
{
    if (pending.isEmpty()) return true;
    const auto entry = pending.value("entry").toObject();
    const auto key = entry.value("key").toString(), operation = pending.value("operation").toString();
    const auto object = "objects/" + key + ".media", partial = "partials/" + key + ".part";
    if (pending.value("version").toInt() != 1 || !contentKey(key) || entry.value("path") != object
        || !entry.value("probe").toObject().value("initialFrameDecoded").toBool()
        || (operation != "publish" && operation != "remove")) return false;
    struct stat st{};
    if (operation == "publish" && !contains(key) && directory.identity(object, st)) {
        // The rename happened but the commit did not. Retain verified bytes as
        // a resumable partial instead of promoting an uncommitted filename.
        if (!matches(st, entry) || !directory.rename(object, partial)) return false;
    }
    if (operation == "publish" && contains(key)) removeFile("partials/" + key + ".json");
    if (operation == "remove" && !contains(key) && directory.identity(object, st)) {
        if (!matches(st, entry)) return false;
        const int lock = directory.open("playback.lock", O_RDWR);
        if (lock < 0) return false;
        const bool exclusive = flock(lock, LOCK_EX | LOCK_NB) == 0;
        const bool removed = exclusive && directory.remove(object, st);
        close(lock);
        // A surviving reader may still reopen this old generation. Leave its
        // receipt/bytes counted and inhibit growth until maintenance can retry.
        if (!exclusive) return true;
        if (!removed) return false;
    }
    // A previous unlink may have happened without its directory sync. Before
    // retiring that receipt, make even an already-absent name durable.
    if (operation == "remove" && !contains(key) && !directory.synchronizeParent(object)) return false;
    if (!removeFile("pending.json")) return false;
    pending = {};
    return true;
}

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
    return canWrite(bytes, useReservation, usage(), directory.available());
}

bool CacheStore::canWrite(quint64 bytes, bool useReservation, const CacheUsage &used, quint64 available) const
{
    if (!owned || !cap || !pending.isEmpty()) return false;
    const auto promised = useReservation ? std::max(remaining, bytes) : remaining;
    if (!used.safe || used.bytes > cap || promised > cap - used.bytes) return false;
    if (!useReservation && bytes > cap - used.bytes - promised) return false;
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

bool CacheStore::canAcquire(const Representation &rep, const QString &evict) const
{
    // Unknown-length downloads can use ordinary streamed enforcement, but are
    // not evidence that deleting a verified clip will make a replacement fit.
    if (rep.bytes < 0) return evict.isEmpty() && canWrite(1);
    auto used = usage();
    auto available = directory.available();
    auto inventory = completed;
    if (!evict.isEmpty()) {
        if (!owned || !used.safe || !pending.isEmpty() || rep.key == evict) return false;
        qsizetype victim = -1;
        for (qsizetype i = 0; i < inventory.size(); ++i)
            if (inventory[i].toObject().value("key") == evict) victim = i;
        if (victim < 0) return false;
        const auto entry = inventory[victim].toObject();
        const auto usable = [this](const QJsonValue &value) {
            return !configuredSources || configuredSources->contains(value.toObject().value("sourceId").toString());
        };
        if (usable(entry) && std::count_if(inventory.begin(), inventory.end(), usable) <= 1) return false;
        struct stat file{};
        if (!directory.identity(entry.value("path").toString(), file) || !matches(file, entry)) return false;
        quint64 removed = file.st_size;
        inventory.removeAt(victim);
        // prune replaces the commit/pointer, creates one generation and removes
        // all old generations under its exclusive playback lock. Project those
        // exact bytes, retaining every partial/manifest/unknown managed file.
        for (const auto &path : directory.files()) {
            const bool generation = path.startsWith("generations/") && contentKey(path.mid(12, 64))
                && path == "generations/" + path.mid(12, 64) + ".json";
            if (!generation && path != "committed.json" && path != "current.json") continue;
            if (!directory.identity(path, file) || file.st_size < 0 || quint64(file.st_size) > used.bytes - std::min(used.bytes, removed)) return false;
            removed += file.st_size;
        }
        const QJsonObject index{{"version", 1}, {"entries", inventory}};
        const auto bytes = encoded(index);
        const auto hash = contentDigest(bytes);
        const quint64 added = bytes.size() + encoded({{"version", 1}, {"generation", hash}, {"index", index}}).size()
            + encoded({{"version", 1}, {"generation", hash}}).size();
        if (removed > used.bytes || added > std::numeric_limits<quint64>::max() - (used.bytes - removed)) return false;
        used.bytes = used.bytes - removed + added;
        // Disk-free projection never wraps, even on a saturated filesystem API.
        available = available > std::numeric_limits<quint64>::max() - removed ? std::numeric_limits<quint64>::max() : available + removed;
        if (available < added) return false;
        available -= added;
    }
    struct stat partial{};
    const auto saved = metadata("partials/" + rep.key + ".json");
    quint64 received = 0;
    const auto checkpoint = saved.value("received").toInteger(-1);
    if (directory.identity("partials/" + rep.key + ".part", partial) && checkpoint >= 0 && checkpoint <= partial.st_size
        && saved.value("key") == rep.key && saved.value("url") == rep.url.toString(QUrl::FullyEncoded)
        && saved.value("total").toInteger(-1) == rep.bytes && !saved.value("validator").toString().isEmpty()
        && SourceResolver::remoteUrl(QUrl(saved.value("effectiveUrl").toString()))) received = quint64(checkpoint);
    // Use the same entry encoding as publication, with bounded probe/identity
    // headroom. Metadata exists in the transaction, generation and new commit,
    // not just once. Existing resume files remain counted in actual usage.
    const auto maximum = std::numeric_limits<qint64>::max();
    const auto entry = mediaEntry(rep, {{"initialFrameDecoded", true}, {"budget", QString(4096, 'x')}}, rep.bytes,
        {{"inode", maximum}, {"seconds", maximum}, {"nanoseconds", maximum}}, maximum);
    inventory.append(entry);
    const QJsonObject index{{"version", 1}, {"entries", inventory}};
    const auto bytes = encoded(index);
    if (bytes.size() > 8 * 1024 * 1024) return false;
    const auto hash = contentDigest(bytes);
    const quint64 overhead = encoded({{"version", 1}, {"operation", "publish"}, {"entry", entry}}).size()
        + bytes.size() + encoded({{"version", 1}, {"generation", hash}, {"index", index}}).size()
        + encoded({{"version", 1}, {"generation", hash}}).size()
        + 2 * (2 * rep.url.toEncoded().size() + 2048);
    const auto remainingBytes = quint64(rep.bytes) - std::min(quint64(rep.bytes), received);
    return remainingBytes <= std::numeric_limits<quint64>::max() - overhead
        && canWrite(remainingBytes + overhead, false, used, available);
}

bool CacheStore::writeMetadata(const QString &relative, const QJsonObject &object)
{
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    // Count the old and replacement bytes while atomic replacement is staged.
    if (bytes.size() > SourceResolver::maximumManifest || !canWrite(bytes.size())) return false;
    const bool saved = directory.replace(relative, bytes);
    if (!saved && directory.lastMutation() == OwnedDirectory::Mutation::Unsynchronized) owned = false;
    return saved;
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
    if (!owned || !pending.isEmpty() || !contentKey(key)) return -1;
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
    if (!directory.identity(relative, st)) return false;
    const bool removed = directory.remove(relative, st);
    if (!removed && directory.lastMutation() == OwnedDirectory::Mutation::Unsynchronized) owned = false;
    return removed;
}

bool CacheStore::discardPartial(const QString &key)
{
    if (!contentKey(key)) return false;
    bool ok = true;
    for (const auto &suffix : {".part", ".json"}) {
        const auto relative = "partials/" + key + suffix;
        struct stat st{};
        if (directory.identity(relative, st) && !removeFile(relative)) { ok = false; if (!owned) break; }
    }
    return ok;
}

bool CacheStore::reconcile(const QSet<QString> &manifestPaths, const QSet<QString> &representationKeys)
{
    if (!owned || !recoverTransaction()) return false;
    if (!pending.isEmpty()) return true;
    bool safe = false;
    const auto files = directory.files(&safe);
    if (!safe) return false;
    const int lock = directory.open("playback.lock", O_RDWR);
    const bool exclusive = lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0;
    const auto current = metadata("current.json").value("generation").toString();
    bool ok = true;
    for (const auto &relative : files) {
        const auto leaf = relative.mid(relative.lastIndexOf('/') + 1);
        bool temporary = leaf.startsWith(".write-") && leaf.size() == 39;
        for (const auto c : leaf.mid(7)) if (!((c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'))) temporary = false;
        const auto manifest = relative.startsWith("manifests/") && contentKey(relative.mid(10, 64))
            && relative == "manifests/" + relative.mid(10, 64) + ".json";
        const auto key = relative.mid(9, 64);
        const auto partial = relative.startsWith("partials/") && contentKey(key)
            && (relative == "partials/" + key + ".part" || relative == "partials/" + key + ".json");
        const auto generation = relative.startsWith("generations/") && contentKey(relative.mid(12, 64))
            && relative == "generations/" + relative.mid(12, 64) + ".json";
        if (temporary || (manifest && !manifestPaths.contains(relative)) || (partial && !representationKeys.contains(key))
            || (generation && exclusive && relative != "generations/" + current + ".json")) {
            // A managed namespace is not permission to follow a replaced link.
            // removeFile reopens/rechecks ownership, single-link identity and
            // containment immediately before unlink; unknown files survive.
            if (!removeFile(relative)) { ok = false; break; }
        }
    }
    if (lock >= 0) close(lock);
    return ok;
}

bool CacheStore::complete(const Representation &rep, const QJsonObject &probe, qint64 now)
{
    if (!contentKey(rep.key) || !probe.value("initialFrameDecoded").toBool() || contains(rep.key)) return false;
    releaseReservation();
    const auto part = "partials/" + rep.key + ".part", object = "objects/" + rep.key + ".media";
    struct stat st{};
    if (!directory.identity(part, st) || st.st_size <= 0) return false;
    QJsonObject identity{{"inode", qint64(st.st_ino)},
        {"seconds", qint64(st.st_mtim.tv_sec)}, {"nanoseconds", qint64(st.st_mtim.tv_nsec)}};
    const auto entry = mediaEntry(rep, probe, st.st_size, identity, now);
    if (!prepareTransaction("publish", entry)) return false;
    if (!directory.rename(part, object)) {
        if (directory.lastMutation() == OwnedDirectory::Mutation::Unsynchronized) owned = false;
        else removeFile("pending.json");
        return false;
    }
    observe("renamed");
    completed.append(entry);
    if (!persistIndex()) {
        // Once committed, never roll back bytes that a public snapshot might
        // already contain. Startup will finish the pointer transaction.
        if (indexCommitted || !owned) { owned = false; return false; }
        completed.removeLast();
        // No index entry exists. Keep the verified bytes as a resumable part.
        if (directory.rename(object, part)) removeFile("pending.json");
        else owned = false;
        return false;
    }
    removeFile("partials/" + rep.key + ".json");
    if (!removeFile("pending.json")) owned = false;
    return true;
}

bool CacheStore::persistIndex(bool reclamation)
{
    indexCommitted = false;
    const QJsonObject index{{"version", 1}, {"entries", completed}};
    const auto bytes = QJsonDocument(index).toJson(QJsonDocument::Compact);
    if (bytes.size() > 8 * 1024 * 1024) return false;
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
    const QJsonObject commit{{"version", 1}, {"generation", hash}, {"index", index}};
    const auto commitBytes = QJsonDocument(commit).toJson(QJsonDocument::Compact);
    if (!existing) {
        if (!(canWrite(bytes.size()) || (shrinking && directory.available() >= freeReserve + bytes.size() + pointer.size()))) return false;
        if (!directory.replace(relative, bytes)) {
            if (directory.lastMutation() == OwnedDirectory::Mutation::Unsynchronized) owned = false;
            return false;
        }
    }
    if (existing && directory.read(relative, 8 * 1024 * 1024) != bytes) return false;
    observe("generation");
    if (!(shrinking ? directory.replace("committed.json", commitBytes) : writeMetadata("committed.json", commit))) {
        if (directory.lastMutation() == OwnedDirectory::Mutation::Unsynchronized) owned = false;
        if (owned && !existing) removeFile(relative);
        return false;
    }
    indexCommitted = true;
    observe("committed");
    if (!(shrinking ? directory.replace("current.json", pointer) : writeMetadata("current.json", {{"version", 1}, {"generation", hash}}))) return false;
    observe("published");
    return true;
}

QJsonObject CacheStore::prune(bool dryRun, const QSet<QString> &protectedKeys, quint64 needed)
{
    QJsonObject result{{"removedBytes", qint64(0)}, {"protected", false}, {"dryRun", dryRun}};
    if (!pending.isEmpty()) {
        if (!dryRun && !recoverTransaction()) { owned = false; result["error"] = "StorageError"; return result; }
        if (!pending.isEmpty()) { result["protected"] = true; return result; }
    }
    const int lock = directory.open("playback.lock", O_RDWR);
    if (!owned || lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) {
        if (lock >= 0) close(lock);
        result["protected"] = true; return result;
    }
    quint64 removed = 0;
    auto candidates = completed;
    // Protect the last fallback the consumer can actually use, not a clip from
    // a removed source. The playback lock still protects every old generation.
    const auto usable = [this](const QJsonObject &entry) {
        return !configuredSources || configuredSources->contains(entry.value("sourceId").toString());
    };
    while (!candidates.isEmpty()) {
        const auto usableCount = std::count_if(candidates.begin(), candidates.end(), [&](const QJsonValue &entry) { return usable(entry.toObject()); });
        qsizetype selected = -1;
        qint64 oldest = std::numeric_limits<qint64>::max();
        for (qsizetype i = 0; i < candidates.size(); ++i) {
            const auto entry = candidates.at(i).toObject();
            if (protectedKeys.contains(entry.value("key").toString())) continue;
            if (usable(entry) && usableCount <= 1) continue;
            const auto timestamp = entry.value("playedAt").toInteger(entry.value("completedAt").toInteger());
            if (timestamp < oldest) { oldest = timestamp; selected = i; }
        }
        if (selected < 0) break;
        const auto entry = candidates.takeAt(selected).toObject();
        struct stat st{};
        const auto relative = entry.value("path").toString();
        if (!directory.identity(relative, st) || !matches(st, entry)) break;
        if (!dryRun) {
            if (!prepareTransaction("remove", entry, true)) break;
            const auto old = completed;
            completed = candidates;
            if (!persistIndex(true)) {
                if (!indexCommitted && owned) { completed = old; removeFile("pending.json"); }
                else owned = false;
                break;
            }
            if (!directory.remove(relative, st)) {
                if (directory.lastMutation() == OwnedDirectory::Mutation::Unsynchronized) owned = false;
                pending = metadata("pending.json"); break;
            }
            observe("removed");
            if (!removeFile("pending.json")) { owned = false; break; }
        }
        removed += st.st_size;
        if (needed && removed >= needed) break;
    }
    // Old generations can be removed only under the same reader lock. Their
    // filenames are hashes and identities are rechecked at deletion time.
    if (!dryRun && owned && pending.isEmpty()) {
        const auto current = metadata("current.json").value("generation").toString();
        for (const auto &relative : directory.files()) {
            if (relative.startsWith("generations/") && relative != "generations/" + current + ".json"
                && contentKey(relative.mid(12, 64)) && relative == "generations/" + relative.mid(12, 64) + ".json") removeFile(relative);
        }
    }
    close(lock);
    if (!owned) result["error"] = "StorageError";
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
