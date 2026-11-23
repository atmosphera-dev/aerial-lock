#include "status-model.h"
#include "cache-controller.h"
#include <QDateTime>

namespace Aerial::Settings {
namespace {
QString localTime(const QVariant &seconds)
{
    return seconds.toLongLong() > 0 ? QDateTime::fromSecsSinceEpoch(seconds.toLongLong()).toLocalTime().toString("ddd h:mm AP") : "Not yet";
}
}
StatusModel::StatusModel(Client::ServiceConnection &service, QObject *parent) : QObject(parent), service(service)
{
    connect(&service, &Client::ServiceConnection::changed, this, &StatusModel::sample);
    sample();
}
void StatusModel::sample()
{
    auto snapshot = service.snapshot();
    if (automaticIntent) {
        auto acquisition = snapshot.effective.value("acquisition").toObject();
        auto automatic = acquisition.value("automatic").toObject(); automatic["enabled"] = *automaticIntent;
        acquisition["automatic"] = automatic; snapshot.effective["acquisition"] = acquisition;
    }
    const auto next = project(snapshot, service.ready());
    if (next != current) { current = next; emit changed(); }
}
void StatusModel::setAutomaticIntent(bool on, bool pending)
{
    const auto next = pending ? std::optional<bool>(on) : std::nullopt;
    if (next == automaticIntent) return;
    automaticIntent = next; sample();
}
QVariantMap StatusModel::transfer(const Client::ServiceConnection::Snapshot &snapshot)
{
    for (const auto &entry : snapshot.jobs) {
        const auto job = entry.toMap();
        if (job.value("Cancellable").toBool() && job.value("State") == "downloading") return job;
    }
    return {};
}
bool StatusModel::needsAttention(const QVariantMap &p)
{
    if (p.value("DeferReason") == "StorageError" || p.value("DeferReason") == "InvalidConfig") return true;
    for (const auto &entry : p.value("SettingsDiagnostics").toList()) if (entry.toMap().value("severity") == "error") return true;
    return false;
}
QString StatusModel::badge(const QVariantMap &p, bool downloading, bool paused)
{
    if (needsAttention(p) || p.value("State") == "error") return "problem";
    if (downloading) return "downloading";
    if (paused || (p.value("DeferReason") != "None" && !p.value("DeferReason").toString().isEmpty())) return "paused";
    return "ready";
}
QVariantMap StatusModel::project(const Client::ServiceConnection::Snapshot &snapshot, bool available)
{
    const auto &p = snapshot.properties;
    const auto job = transfer(snapshot), details = job.value("Details").toMap();
    const bool downloading = !job.isEmpty();
    const bool paused = !snapshot.effective.value("acquisition").toObject().value("automatic").toObject().value("enabled").toBool();
    const auto total = job.value("TotalBytes").toULongLong(), received = job.value("ReceivedBytes").toULongLong();
    const bool known = downloading && job.value("TotalKnown").toBool() && total > 0;
    const double progress = known ? double(qMin(1.L, static_cast<long double>(received) / total)) : 0.;
    const auto count = p.value("ReadyClips").toULongLong();
    const auto ready = QString::number(count) + (count == 1 ? " video ready" : " videos ready");
    const auto reason = p.value("DeferReason").toString();
    const QMap<QString, QString> reasons{{"Metered", "metered connection"}, {"ConnectionTypeDisallowed", "connection not allowed"},
        {"PowerDisallowed", "power source not allowed"}, {"Offline", "offline"}, {"Initializing", "checking connections"},
        {"NoAllowedResolution", "no quality allowed"}, {"NoRemoteSources", "no online sources"}, {"CacheFull", "cache full"}, {"PlaybackProtected", "videos in use"}};
    QString headline = !available ? "Service not running" : reason == "StorageError" ? "Cache needs attention"
        : reason == "InvalidConfig" ? "Settings need attention" : downloading ? "Downloading " + details.value("assetTitle", "video").toString()
        : paused ? "Paused" : !reason.isEmpty() && reason != "None" ? "Waiting: " + reasons.value(reason, "checking download conditions") : "Up to date, " + ready;
    auto summary = headline;
    if (available && downloading && known && !needsAttention(p)) summary += ", " + QString::number(int(qMin(100.L, 100.L * received / total))) + "%";
    const auto receivedText = CacheController::bytes(received), totalText = known ? CacheController::bytes(total) : QString("unknown size");
    bool editable = available;
    for (const auto &entry : p.value("SettingsDiagnostics").toList()) if (entry.toMap().value("severity") == "error") editable = false;
    QVariantMap result{{"headline", headline}, {"title", headline}, {"summary", summary},
        {"detail", available && downloading ? receivedText + " of " + totalText : QString()},
        {"downloading", available && downloading}, {"source", details.value("sourceName").toString()},
        {"progressKnown", available && known}, {"progress", available ? progress : 0.}, {"received", receivedText}, {"total", totalText},
        {"readyCount", QVariant::fromValue(count)}, {"ready", ready}, {"readyCountText", QString::number(count) + (count == 1 ? " video" : " videos")},
        {"nextCheck", "Next check: " + localTime(p.value("NextCheck"))},
        {"nextCheckText", localTime(p.value("NextCheck"))}, {"lastCheck", "Last check: " + localTime(p.value("LastCheck"))}, {"reason", reason},
        {"attention", needsAttention(p)}, {"badge", available ? badge(p, downloading, paused) : "problem"},
        {"primaryActionId", editable ? (downloading ? "stop-download" : "fetch-videos") : ""},
        {"primaryActionLabel", editable ? (downloading ? "Stop download" : "Fetch videos now") : ""},
        {"secondaryActionId", editable ? (paused ? "resume-downloads" : "pause-downloads") : ""},
        {"secondaryActionLabel", editable ? (paused ? "Resume automatic downloads" : "Pause automatic downloads") : ""}};
    const auto storage = CacheController::storageFacts(p);
    for (auto it = storage.cbegin(); it != storage.cend(); ++it) result.insert(it.key(), it.value());
    return result;
}
}
