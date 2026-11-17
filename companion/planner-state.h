#pragma once

#include "../common/owned-directory.h"
#include <QJsonObject>
#include <QSet>
#include <functional>

namespace Aerial {
// Durable scheduler facts, never preferences. The caller owns the cache and
// this state root for its lifetime. Opaque representation keys bind retries
// and retirement to the exact source/asset/revision/URL.
class PlannerState {
public:
    PlannerState(const QString &root, std::function<qint64()> clock,
                 std::function<quint32()> random);
    ~PlannerState();
    bool valid() const { return ready; }
    bool eligible(const QString &key) const;
    qint64 nextAttempt(const QString &key) const;
    bool failed(const QString &key, const QString &error, int httpStatus = 0,
                const QByteArray &retryAfter = {});
    bool clearFailure(const QString &key);
    bool initializeRotation();
    qint64 lastRotation() const;
    bool rotated(const QString &removedKey);
    bool retired(const QString &key) const;
    qint64 retiredAt(const QString &key) const;
    bool recordPlayback(const QString &key, qint64 timestamp);
    qint64 playedAt(const QString &key) const;
    bool reconcile(const QSet<QString> &catalogue, const QSet<QString> &complete);
    QJsonObject jobs() const { return document.value("jobs").toObject(); }
    bool saveJob(const QString &key, const QJsonObject &job);
    QJsonObject rotation() const { return document.value("rotation").toObject(); }
    bool beginRotation(const QString &victim, const QString &replacement);
    bool finishRotation();
    bool clearRotation();
    bool reintroduced(const QString &key);
private:
    bool save(const QJsonObject &next);
    OwnedDirectory directory;
    std::function<qint64()> clock;
    std::function<quint32()> random;
    QJsonObject document;
    int owner = -1;
    bool ready = false;
};
}
