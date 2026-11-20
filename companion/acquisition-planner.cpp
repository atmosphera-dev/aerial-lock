#include "acquisition-planner.h"
#include "../common/config-document.h"

#include <QSet>
#include <algorithm>
#include <limits>

namespace Aerial {
AcquisitionPlan AcquisitionPlanner::evaluate(const QJsonObject &settings, const QList<Representation> &catalogue,
    const QJsonArray &completed, const PlannerState &state, qint64 now, bool manual,
    const QStringList &sourceIds, const QStringList &topics)
{
    AcquisitionPlan plan;
    if (!state.valid()) { plan.deferReason = "StorageError"; return plan; }
    const auto automatic = settings.value("acquisition").toObject().value("automatic").toObject();
    if (!manual && !automatic.value("enabled").toBool()) { plan.deferReason = "Paused"; return plan; }
    QList<int> allowed;
    for (const auto &value : automatic.value("allowedResolutions").toArray()) allowed.append(value.toInt());
    if (!manual && allowed.isEmpty()) { plan.deferReason = "NoAllowedResolution"; return plan; }
    QSet<QString> configured, covered, keys;
    QJsonArray active;
    for (const auto &value : settings.value("sources").toArray()) {
        const auto source = value.toObject();
        if (!sourceEnabled(source)) continue;
        if (source.value("type") != "apple" && source.value("type") != "community") continue;
        const auto id = source.value("id").toString(); configured.insert(id);
        if (!sourceIds.isEmpty() && !sourceIds.contains(id)) continue;
        plan.candidates.append(SourceResolver::select(catalogue, source,
            settings.value("preferredResolution").toInt(), allowed, manual));
    }
    if (configured.isEmpty()) { plan.deferReason = "NoRemoteSources"; return plan; }
    for (const auto &value : completed) {
        const auto entry = value.toObject();
        keys.insert(entry.value("key").toString());
        if (!configured.contains(entry.value("sourceId").toString())) continue;
        covered.insert(entry.value("sourceId").toString()); active.append(entry);
    }
    const auto days = settings.value("cache").toObject().value("rotationDays").toInteger();
    const bool rotationDue = !manual && days > 0 && now >= state.lastRotation()
        && days <= (now - state.lastRotation()) / 86400 && !active.isEmpty();
    QList<Representation> retired;
    qint64 earliestRetry = std::numeric_limits<qint64>::max();
    QList<Representation> eligible;
    for (const auto &rep : plan.candidates) {
        if (keys.contains(rep.key)) continue;
        if (!state.eligible(rep.key)) { earliestRetry = std::min(earliestRetry, state.nextAttempt(rep.key)); continue; }
        if (!manual && state.retired(rep.key)) retired.append(rep);
        else eligible.append(rep);
    }
    plan.candidates = eligible;
    const bool revisitRetired = plan.candidates.isEmpty() && rotationDue && !retired.isEmpty();
    if (revisitRetired) {
        std::sort(retired.begin(), retired.end(), [&state](const Representation &a, const Representation &b) {
            if (state.retiredAt(a.key) != state.retiredAt(b.key)) return state.retiredAt(a.key) < state.retiredAt(b.key);
            return a.key < b.key;
        });
        // Revisit retired content only at the next cadence, never hourly.
        plan.candidates = retired;
    }
    if (!revisitRetired) SourceResolver::smallestFirst(plan.candidates);
    if (manual) plan.priority = "manual";
    else if (active.isEmpty()) plan.priority = "critical";
    else {
        // Critical starts with the globally smallest eligible item. Once a
        // fallback exists, cover configured sources before soft topic variety.
        auto rank = [&covered, &topics](const Representation &rep) {
            if (!covered.contains(rep.sourceId)) return 0;
            const auto text = rep.metadata.value("scene").toString() + ' ' + rep.metadata.value("title").toString()
                + ' ' + rep.metadata.value("accessibilityLabel").toString();
            for (const auto &topic : topics) if (!topic.isEmpty() && text.contains(topic, Qt::CaseInsensitive)) return 1;
            return 2;
        };
        if (!revisitRetired)
            std::stable_sort(plan.candidates.begin(), plan.candidates.end(), [&](const Representation &a, const Representation &b) {
                return rank(a) < rank(b);
            });
        plan.priority = !plan.candidates.isEmpty() && !covered.contains(plan.candidates.first().sourceId) ? "coverage" : "variety";
    }
    if (rotationDue && !plan.candidates.isEmpty()) {
        // Select a victim, but do not delete it here. The queue must first
        // validate the replacement inside the cap and obtain exclusive playback
        // protection. A lone fallback can be replaced only after that succeeds.
        qint64 oldest = std::numeric_limits<qint64>::max();
        for (const auto &value : active) {
            const auto entry = value.toObject(); const auto key = entry.value("key").toString();
            const auto timestamp = std::max(state.playedAt(key), entry.value("completedAt").toInteger());
            if (timestamp < oldest || (timestamp == oldest && key < plan.rotationVictim)) {
                oldest = timestamp; plan.rotationVictim = key;
            }
        }
    }
    if (plan.candidates.isEmpty()) plan.deferReason = earliestRetry != std::numeric_limits<qint64>::max() ? "Backoff" : "NoEligibleAssets";
    return plan;
}
}
