#pragma once

#include "planner-state.h"
#include "source-resolver.h"
#include <QJsonArray>

namespace Aerial {
struct AcquisitionPlan {
    QList<Representation> candidates;
    QString priority, deferReason;
    QString rotationVictim;
};

// Pure selection over normalized settings, discovered lengths and durable
// scheduler facts. Queue/transport/cache mutations stay in their owners.
class AcquisitionPlanner {
public:
    static AcquisitionPlan evaluate(const QJsonObject &settings, const QList<Representation> &catalogue,
        const QJsonArray &completed, const PlannerState &state, qint64 now, bool manual = false,
        const QStringList &sourceIds = {}, const QStringList &topics = {});
};
}
