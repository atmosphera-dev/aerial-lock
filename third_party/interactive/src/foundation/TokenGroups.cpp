// SPDX-License-Identifier: LGPL-3.0-or-later
#include "TokenGroups.h"
#include "AppearanceTrace_p.h"
#include "PaletteState_p.h"
#include <QPointer>

namespace InterActive {

TokenGroup::TokenGroup(QObject *parent) : QObject(parent), m_notifications(new PropertyNotifications) {}
TokenGroup::~TokenGroup() = default;

void TokenGroup::install(const QVariantMap &values)
{
    m_values = values;
    m_notifications->installed(this);
}

void TokenGroup::initializeNotifications()
{
    m_notifications->initialize(this);
}

void TokenGroup::publish()
{
    TraceSpan span(TraceScope::GroupPublish, this);
    QPointer<TokenGroup> guard(this);
    m_notifications->drain(this);
    if (guard)
        emit changed();
}

#define IA_COLOR_GETTER(name, light, dark) \
    QColor ColorTokens::name() const { return m_palette ? m_palette->color(ColorRole::name) \
        : m_values.value(QStringLiteral(#name)).value<QColor>(); }
IA_COLOR_TOKENS(IA_COLOR_GETTER)
#undef IA_COLOR_GETTER

} // namespace InterActive
