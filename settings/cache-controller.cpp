#include "cache-controller.h"
#include "settings-controller.h"
#include <QDateTime>
#include <algorithm>

namespace Aerial::Settings {
namespace {
QString localTime(const QVariant &seconds)
{
    return seconds.toLongLong() > 0 ? QDateTime::fromSecsSinceEpoch(seconds.toLongLong()).toLocalTime().toString("ddd h:mm AP") : "Not yet";
}
QString outcome(const QVariantMap &result, bool preview)
{
    return (preview ? "Can remove " : "Removed ") + CacheController::bytes(result.value("removedBytes").toULongLong())
        + " in " + result.value("removedFiles").toString() + " files."
        + (result.value("protected").toBool() ? " Videos in use are protected." : " Videos in use stay.");
}
}
CacheController::CacheController(Client::ServiceConnection &service, QObject *parent) : QObject(parent), service(service)
{
    connect(&service, &Client::ServiceConnection::changed, this, &CacheController::sampled);
    connect(&service, &Client::ServiceConnection::mutationFinished, this, [this](quint64 id, const QVariant &result, const QString &error) {
        if (id == action) { action = 0; problem = error.isEmpty() ? QString() : error.endsWith(".AcceptedResultUnavailable")
            ? "The previous service accepted the download change. Check Recent activity before trying again." : "Couldn't change download: " + error.section('.', -1); emit changed(); }
        if (id != pending) return;
        pending = 0;
        if (!error.isEmpty()) { cleanupState = "error"; problem = error.endsWith(".AcceptedResultUnavailable")
            ? "The previous service accepted cleanup, but its result is unavailable. Check Recent activity before trying again."
            : "Couldn't clean up: " + error.section('.', -1); }
        else jobPath = qvariant_cast<QDBusObjectPath>(result).path();
        sampled();
    });
}
QString CacheController::bytes(qulonglong value)
{
    if (value >= 1000000000ULL) return QString::number(value / 1000000000.0, 'f', 1) + " GB";
    if (value >= 1000000ULL) return QString::number(value / 1000000.0, 'f', 1) + " MB";
    if (value >= 1000ULL) return QString::number(value / 1000.0, 'f', 1) + " kB";
    return QString::number(value) + " bytes";
}
QVariantMap CacheController::transfer() const
{
    for (const auto &entry : service.snapshot().jobs) {
        const auto job = entry.toMap();
        if (job.value("Cancellable").toBool() && job.value("State").toString() == "downloading") return job;
    }
    return {};
}
QVariantMap CacheController::facts() const
{
    const auto p = service.snapshot().properties;
    const auto current = transfer(); const auto details = current.value("Details").toMap();
    const auto total = current.value("TotalBytes").toULongLong(), received = current.value("ReceivedBytes").toULongLong();
    const auto managed = p.value("ManagedBytes").toULongLong(), partial = p.value("PartialBytes").toULongLong();
    const auto reserved = p.value("ReservedBytes").toULongLong(), limit = p.value("MaxBytes").toULongLong();
    const long double used = static_cast<long double>(managed) + partial + reserved;
    const auto free = used >= limit ? 0ULL : limit - managed - partial - reserved;
    const bool known = p.value("UsageKnown").toBool();
    const long double denominator = qMax(static_cast<long double>(limit), used);
    QVariantList segments, legend;
    for (const auto &entry : {qMakePair(QString("videos"), managed), qMakePair(QString("partial"), partial), qMakePair(QString("reserved"), reserved)}) {
        segments << QVariantMap{{"kind", entry.first}, {"fraction", denominator > 0 ? double(entry.second / denominator) : 0.0}};
        const QString label = entry.first == "videos" ? "Videos " : entry.first == "partial" ? "Partial download " : "Reserved ";
        legend << QVariantMap{{"kind", entry.first}, {"label", label + bytes(entry.second)}};
    }
    legend << QVariantMap{{"kind", "free"}, {"label", "Free " + bytes(free)}};
    return {{"downloading", !current.isEmpty()}, {"title", !current.isEmpty() ? "Downloading " + details.value("assetTitle", "video").toString() : StatusFormatter::summary(service.snapshot())},
        {"source", details.value("sourceName").toString()}, {"progressKnown", current.value("TotalKnown").toBool() && total > 0},
        {"progress", total ? double(qMin(1.L, static_cast<long double>(received) / total)) : 0.0},
        {"received", bytes(received)}, {"total", current.value("TotalKnown").toBool() ? bytes(total) : "unknown size"},
        {"ready", StatusFormatter::ready(p.value("ReadyClips", 0).toULongLong())}, {"nextCheck", "Next check: " + localTime(p.value("NextCheck"))}, {"lastCheck", "Last check: " + localTime(p.value("LastCheck"))},
        {"usage", known ? bytes(managed + partial) + " of " + bytes(limit) + " used" : "Cache usage unavailable"},
        {"free", known ? (used > limit ? bytes(qulonglong(used - limit)) + " over limit" : bytes(free) + " free") : ""},
        {"segments", known ? segments : QVariantList{}}, {"legend", known ? legend : QVariantList{}},
        {"reason", p.value("DeferReason").toString()}};
}
QVariantList CacheController::activity() const
{
    auto jobs = service.snapshot().jobs;
    std::stable_sort(jobs.begin(), jobs.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("Details").toMap().value("createdAt").toLongLong() > b.toMap().value("Details").toMap().value("createdAt").toLongLong();
    });
    QVariantList result;
    for (const auto &entry : jobs) {
        const auto job = entry.toMap(), details = job.value("Details").toMap();
        const auto state = job.value("State").toString(), error = job.value("ErrorCode").toString();
        const auto title = details.value("assetTitle").toString(), source = details.value("sourceName").toString();
        QString text = job.value("Kind").toString() + ": " + state;
        if (!title.isEmpty()) text = title + (source.isEmpty() ? "" : " from " + source) + ": " + state;
        if (!error.isEmpty()) text += " (" + error + ")";
        result << QVariantMap{{"text", text}, {"error", !error.isEmpty()}, {"time", job.value("Cancellable").toBool() ? "Now" : localTime(details.value("finishedAt"))}, {"glyph", !error.isEmpty() ? "alert" : job.value("Cancellable").toBool() ? "download" : "checkcircle"}};
    }
    return result;
}
QVariantMap CacheController::cleanup() const
{
    return {{"state", cleanupState}, {"visible", cleanupVisible}, {"message", pruneResult.isEmpty() ? QString() : outcome(pruneResult, preview)}, {"error", problem}};
}
void CacheController::fetchOrStop()
{
    if (!service.ready() || action) return;
    const auto current = transfer(); problem.clear();
    action = current.isEmpty() ? service.acquire() : service.cancel(QDBusObjectPath(current.value("path").toString())); emit changed();
}
void CacheController::prune(bool dryRun)
{
    if (!service.ready() || pending || !jobPath.isEmpty()) return;
    preview = dryRun; jobOwner = service.uniqueOwner(); pruneResult.clear(); problem.clear(); cleanupState = "pending"; cleanupVisible = true;
    pending = service.prune(dryRun); emit changed();
}
void CacheController::prepareCleanup() { if (pending || !jobPath.isEmpty()) { cleanupVisible = true; emit changed(); } else prune(true); }
void CacheController::confirmCleanup() { if (cleanupVisible && cleanupState == "confirm") prune(false); }
void CacheController::dismissCleanup() { if (cleanupVisible) { cleanupVisible = false; emit changed(); } }
void CacheController::sampled()
{
    if (!jobPath.isEmpty()) {
        if (service.uniqueOwner() != jobOwner) {
            jobPath.clear(); cleanupState = "error"; problem = "The service changed while cleaning up. Check Recent activity before trying again.";
        } else if (!service.ready()) {
            cleanupState = "error";
            problem = "Cleanup was accepted, but its result could not be read. Retry the service connection to check its result; do not repeat cleanup.";
        } else for (const auto &entry : service.snapshot().jobs) {
            const auto job = entry.toMap();
            if (job.value("path").toString() != jobPath || job.value("Cancellable").toBool()) continue;
            jobPath.clear(); pruneResult = job.value("Details").toMap().value("prune").toMap();
            if (!job.value("ErrorCode").toString().isEmpty() || pruneResult.isEmpty()) {
                cleanupState = "error"; problem = "Clean up failed: " + job.value("ErrorCode").toString();
            } else { cleanupState = preview ? "confirm" : "result"; problem.clear(); }
            break;
        }
    }
    emit changed();
}
}
