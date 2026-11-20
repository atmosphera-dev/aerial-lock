#pragma once

#include "source-resolver.h"
#include "../common/owned-directory.h"
#include <QJsonArray>
#include <QSet>
#include <functional>
#include <optional>

namespace Aerial {
struct CacheUsage { quint64 bytes = 0, partial = 0, reserved = 0; bool safe = false; };

class CacheStore {
public:
    explicit CacheStore(const QString &root, std::optional<quint64> initialMaximum = std::nullopt,
                        std::function<void(const QString &)> transactionObserver = {});
    ~CacheStore();
    bool valid() const { return owned; }
    bool recover();
    QString path() const { return directory.path(); }
    void configure(quint64 maximum, quint64 reserve) { cap = maximum; freeReserve = reserve; }
    // Playback eligibility protects the last usable fallback. It does not
    // alter owned inventory or the all-configured reconciliation catalogue.
    void setPlaybackSources(const QSet<QString> &sources) { playbackSources = sources; }
    CacheUsage usage() const;
    bool reserve(quint64 remaining);
    void releaseReservation() { remaining = 0; }
    bool canWrite(quint64 bytes, bool useReservation = false) const;
    bool canAcquire(const Representation &representation, const QString &evict = {}) const;
    bool writeMetadata(const QString &relative, const QJsonObject &object);
    QJsonObject metadata(const QString &relative) const;
    bool contains(const QString &key) const;
    QJsonArray entries() const { return completed; }
    int openPartial(const QString &key, bool truncate = false) const;
    int openForProbe(const QString &key) const;
    bool append(int fd, const QByteArray &chunk);
    bool complete(const Representation &rep, const QJsonObject &probe, qint64 now);
    bool discardPartial(const QString &key);
    bool reconcile(const QSet<QString> &manifestPaths, const QSet<QString> &representationKeys);
    QJsonObject prune(bool dryRun, const QSet<QString> &protectedKeys = {}, quint64 needed = 0);
    bool evict(const QString &key);
    bool persistIndex(bool reclamation = false);
    const OwnedDirectory &files() const { return directory; }
private:
    bool canWrite(quint64 bytes, bool useReservation, const CacheUsage &usage, quint64 available) const;
    bool removeFile(const QString &relative);
    bool recoverTransaction();
    bool prepareTransaction(const QString &operation, const QJsonObject &entry, bool reclamation = false);
    void observe(const QString &step) const;
    OwnedDirectory directory;
    std::function<void(const QString &)> transactionObserver;
    int writer = -1;
    bool owned = false, writerOwned = false, indexCommitted = false;
    QJsonObject pending;
    quint64 cap = 0, freeReserve = 0, remaining = 0;
    QJsonArray completed;
    std::optional<QSet<QString>> playbackSources;
};
}
