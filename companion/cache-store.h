#pragma once

#include "source-resolver.h"
#include "../common/owned-directory.h"
#include <QJsonArray>
#include <QSet>
#include <functional>

namespace Aerial {
struct CacheUsage { quint64 bytes = 0, partial = 0, reserved = 0; bool safe = false; };

class CacheStore {
public:
    explicit CacheStore(const QString &root);
    ~CacheStore();
    bool valid() const { return owned; }
    QString path() const { return directory.path(); }
    void configure(quint64 maximum, quint64 reserve) { cap = maximum; freeReserve = reserve; }
    CacheUsage usage() const;
    bool reserve(quint64 remaining);
    void releaseReservation() { remaining = 0; }
    bool canWrite(quint64 bytes, bool useReservation = false) const;
    bool writeMetadata(const QString &relative, const QJsonObject &object);
    QJsonObject metadata(const QString &relative) const;
    bool contains(const QString &key) const;
    QJsonArray entries() const { return completed; }
    int openPartial(const QString &key, bool truncate = false) const;
    int openForProbe(const QString &key) const;
    bool append(int fd, const QByteArray &chunk);
    bool complete(const Representation &rep, const QJsonObject &probe, qint64 now);
    bool discardPartial(const QString &key);
    QJsonObject prune(bool dryRun, const QSet<QString> &protectedKeys = {}, quint64 needed = 0);
    bool persistIndex();
    const OwnedDirectory &files() const { return directory; }
private:
    bool removeFile(const QString &relative);
    OwnedDirectory directory;
    int writer = -1;
    bool owned = false;
    quint64 cap = 0, freeReserve = 0, remaining = 0;
    QJsonArray completed;
};
}
