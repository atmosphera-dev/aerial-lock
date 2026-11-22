// SPDX-License-Identifier: LGPL-3.0-or-later
#include "StateTable.h"
#include "PaletteState_p.h"

namespace InterActive {

StateColors StateTable::colors(Presentation::State state) const
{
    if (!m_data || state < Presentation::Rest || state > Presentation::Disabled)
        return {};
    auto colors = m_data->states[size_t(state)];
    colors.m_focusedBorderWidth = m_focusWidth;
    colors.m_borderWidth = colors.m_conditionalBorder ? m_borderWidth : 0;
    return colors;
}

QColor StateTable::selectionColor() const
{
    return m_data ? m_data->sharedColors[size_t(StateTableColor::Selection)] : QColor();
}

QColor StateTable::selectedTextColor() const
{
    return m_data ? m_data->sharedColors[size_t(StateTableColor::SelectionText)] : QColor();
}

QColor StateTable::linkColor() const
{
    return m_data ? m_data->sharedColors[size_t(StateTableColor::Link)] : QColor();
}

bool StateTable::operator==(const StateTable &other) const
{
    return m_focusWidth == other.m_focusWidth && m_borderWidth == other.m_borderWidth && (m_data == other.m_data
        || (m_data && other.m_data && m_data->states == other.m_data->states
            && m_data->sharedColors == other.m_data->sharedColors));
}

} // namespace InterActive
