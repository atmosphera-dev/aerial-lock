// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "PaletteState_p.h"
#include <functional>

namespace InterActive {

class LocalColorOverrides
{
public:
    std::shared_ptr<const StateTableData> install(const PaletteState &palette, const ControlColorMapping &mapping,
        const std::shared_ptr<const StateTableData> &shared, const ControlColorValues &overrides,
        bool coupleBorder, const void *owner, quint64 revision);
    static std::shared_ptr<const StateTableData> buildStateTable(const ControlColorValues &values,
        std::shared_ptr<const ControlFindings> findings, const StateTableSharedColors &sharedColors,
        const QColor &backdrop, bool invalidFocus, const StateTableRecipe &recipe);
private:
    struct BranchInputs {
        std::array<QColor, 4> colors{};
        qreal target = 0;
        qreal blend = 0;
        quint8 ownership = 0;
        bool operator==(const BranchInputs &other) const
        {
            return colors == other.colors && target == other.target
                && blend == other.blend && ownership == other.ownership;
        }
    };
    struct Branch {
        BranchInputs inputs;
        QColor result;
        bool valid = false;
        QColor evaluate(const BranchInputs &next, const std::function<QColor()> &calculate,
                        const char *operation, const void *owner, quint64 revision);
    };
    struct PairInputs {
        QColor text, surface, backdrop;
        qreal target = 0;
        bool localText = false;
        bool localSurface = false;
        bool operator==(const PairInputs &other) const
        {
            return text == other.text && surface == other.surface && backdrop == other.backdrop
                && target == other.target && localText == other.localText && localSurface == other.localSurface;
        }
    };
    struct Pair {
        PairInputs inputs;
        QVariantList findings;
        bool valid = false;
    };
    Branch m_resting, m_hover, m_pressed, m_foreground, m_selected, m_inactive;
    std::array<Pair, 6> m_pairs;
    std::shared_ptr<const StateTableData> m_table;
};

} // namespace InterActive
