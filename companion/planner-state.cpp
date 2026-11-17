#include "planner-state.h"
#include "../common/content-snapshot.h"

#include <QDateTime>
#include <QJsonDocument>
#include <algorithm>
#include <fcntl.h>
#include <limits>
#include <sys/file.h>
#include <unistd.h>

namespace Aerial {
PlannerState::PlannerState(const QString &root, std::function<qint64()> clock,
                           std::function<quint32()> random)
    : directory(root, true), clock(std::move(clock)), random(std::move(random))
{
    owner = directory.open("planner.lock", O_RDWR | O_CREAT);
    if (owner < 0 || flock(owner, LOCK_EX | LOCK_NB)) return;
    bool ok = false;
    const auto raw = directory.read("planner-v1.json", 8 * 1024 * 1024, &ok);
    struct stat identity{};
    if (!ok && directory.identity("planner-v1.json", identity)) return;
    if (ok) {
        const auto parsed = QJsonDocument::fromJson(raw);
        if (!parsed.isObject() || parsed.object().value("version").toInt() != 1) return;
        document = parsed.object();
        for (const auto &name : {"retry", "retired", "played"}) {
            if (!document.value(name).isObject()) return;
            const auto map = document.value(name).toObject();
            for (auto it = map.begin(); it != map.end(); ++it) if (!contentKey(it.key())) return;
        }
    } else document = {{"version", 1}, {"retry", QJsonObject{}}, {"retired", QJsonObject{}}, {"played", QJsonObject{}}};
    ready = true;
}

PlannerState::~PlannerState() { if (owner >= 0) close(owner); }

bool PlannerState::save(const QJsonObject &next)
{
    if (!ready || !directory.replace("planner-v1.json", QJsonDocument(next).toJson(QJsonDocument::Compact))) return false;
    document = next;
    return true;
}

qint64 PlannerState::nextAttempt(const QString &key) const
{
    const auto entry = document.value("retry").toObject().value(key).toObject();
    return entry.value("permanent").toBool() ? std::numeric_limits<qint64>::max() : entry.value("next").toInteger();
}

bool PlannerState::eligible(const QString &key) const { return ready && contentKey(key) && nextAttempt(key) <= clock(); }

bool PlannerState::failed(const QString &key, const QString &error, int httpStatus, const QByteArray &retryAfter)
{
    if (!contentKey(key)) return false;
    auto retry = document.value("retry").toObject();
    const auto attempts = std::min(31, retry.value(key).toObject().value("attempts").toInt() + 1);
    const bool transient = error == "TransportError" || error == "Timeout" || error == "TransferFailed"
        || (error == "HttpError" && (httpStatus == 408 || httpStatus == 429 || httpStatus >= 500));
    // Retry policy bounds scheduler churn, not content volume or eligibility.
    const qint64 base = std::min<qint64>(86400, qint64(30) << std::min(12, attempts - 1));
    qint64 delay = base + (random() % quint32(base / 4 + 1));
    bool number = false;
    const auto seconds = retryAfter.toLongLong(&number);
    if (number && seconds >= 0) delay = std::max(delay, seconds);
    else {
        const auto date = QDateTime::fromString(QString::fromLatin1(retryAfter), Qt::RFC2822Date);
        if (date.isValid()) delay = std::max(delay, date.toSecsSinceEpoch() - clock());
    }
    const auto now = std::max<qint64>(0, clock());
    const auto nextTime = now + std::min(delay, std::numeric_limits<qint64>::max() - now);
    retry[key] = QJsonObject{{"attempts", attempts}, {"next", nextTime}, {"permanent", !transient}};
    auto next = document; next["retry"] = retry;
    return save(next);
}

bool PlannerState::clearFailure(const QString &key)
{
    auto retry = document.value("retry").toObject();
    if (!retry.contains(key)) return ready;
    retry.remove(key); auto next = document; next["retry"] = retry;
    return save(next);
}

bool PlannerState::initializeRotation()
{
    if (document.contains("lastRotation")) return ready;
    auto next = document; next["lastRotation"] = clock();
    return save(next);
}
qint64 PlannerState::lastRotation() const { return document.value("lastRotation").toInteger(); }
bool PlannerState::retired(const QString &key) const { return document.value("retired").toObject().contains(key); }
qint64 PlannerState::retiredAt(const QString &key) const { return document.value("retired").toObject().value(key).toInteger(); }

bool PlannerState::rotated(const QString &removedKey)
{
    if (!contentKey(removedKey)) return false;
    auto retired = document.value("retired").toObject(); retired[removedKey] = clock();
    auto next = document; next["retired"] = retired; next["lastRotation"] = clock();
    return save(next);
}

bool PlannerState::recordPlayback(const QString &key, qint64 timestamp)
{
    if (!contentKey(key) || timestamp < 0 || timestamp > clock()) return false;
    if (timestamp <= playedAt(key)) return ready;
    auto played = document.value("played").toObject(); played[key] = timestamp;
    auto next = document; next["played"] = played;
    return save(next);
}
qint64 PlannerState::playedAt(const QString &key) const { return document.value("played").toObject().value(key).toInteger(); }

bool PlannerState::reconcile(const QSet<QString> &catalogue, const QSet<QString> &complete)
{
    // Bound bookkeeping by the current catalogue/completed inventory, without
    // expiring live retries just because EnsureContent was called again.
    auto next = document;
    for (const auto &name : {"retry", "retired", "played"}) {
        auto map = next.value(name).toObject();
        for (auto it = map.begin(); it != map.end();) {
            if (!catalogue.contains(it.key()) && !complete.contains(it.key())) it = map.erase(it);
            else ++it;
        }
        next[name] = map;
    }
    return next == document ? ready : save(next);
}
}
