// SPDX-License-Identifier: LGPL-3.0-or-later
#include "SeparatorAppearance.h"
#include "PaletteState_p.h"
#include "TokenValidation_p.h"

namespace InterActive {

SeparatorAppearance::SeparatorAppearance(QObject *parent) : QObject(parent), m_notified(foregroundColor()) {}

QColor SeparatorAppearance::foregroundColor() const
{
    return m_override ? *m_override : m_source ? m_source->colors()->separator()
        : PaletteState::fallback()->color(ColorRole::separator);
}

void SeparatorAppearance::setSource(Appearance *source)
{
    if (m_source == source)
        return;
    disconnect(m_changed);
    disconnect(m_destroyed);
    m_source = source;
    if (source) {
        m_changed = connect(source->colors(), &ColorTokens::separatorChanged, this, &SeparatorAppearance::publish);
        m_destroyed = connect(source, &QObject::destroyed, this, [this] {
            m_source = nullptr;
            const QPointer<SeparatorAppearance> guard(this);
            emit sourceChanged();
            if (guard)
                publish();
        });
    }
    const QPointer<SeparatorAppearance> guard(this);
    emit sourceChanged();
    if (guard)
        publish();
}

void SeparatorAppearance::setForegroundColor(const QColor &color)
{
    QVariant normalized;
    QVariantList diagnostics;
    if (TokenValidation::validate(QStringLiteral("colors"), QStringLiteral("separator"), color,
            &normalized, &diagnostics, QStringLiteral("foregroundColor"))) {
        m_override = normalized.value<QColor>();
        m_diagnostics.clear();
    } else {
        m_diagnostics = diagnostics;
    }
    publish();
}

void SeparatorAppearance::resetForegroundColor()
{
    m_override.reset();
    m_diagnostics.clear();
    publish();
}

void SeparatorAppearance::publish()
{
    if (m_publishing)
        return;
    m_publishing = true;
    const QPointer<SeparatorAppearance> guard(this);
    while (m_notified != foregroundColor() || m_notifiedDiagnostics != m_diagnostics) {
        if (m_notified != foregroundColor()) {
            m_notified = foregroundColor();
            emit foregroundColorChanged();
            if (!guard)
                return;
        }
        if (m_notifiedDiagnostics != m_diagnostics) {
            m_notifiedDiagnostics = m_diagnostics;
            emit diagnosticsChanged();
            if (!guard)
                return;
        }
    }
    m_publishing = false;
}

} // namespace InterActive
