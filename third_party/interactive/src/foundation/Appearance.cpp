// SPDX-License-Identifier: LGPL-3.0-or-later
#include "Appearance.h"
#include "AppearanceSource_p.h"
#include "AppearanceTrace_p.h"
#include "PaletteBuilder_p.h"
#include "PaletteState_p.h"
#include <QFont>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QPointer>

namespace InterActive {

QString Appearance::initialFontFamily()
{
    const QString family = qobject_cast<QGuiApplication *>(QCoreApplication::instance())
        ? QGuiApplication::font().family() : QString();
    return family.isEmpty() ? QStringLiteral("Sans Serif") : family;
}

Appearance::Appearance(QObject *parent)
    : QObject(parent), m_fontFamily(initialFontFamily()),
      m_prepared(PaletteBuilder::prepare({}, m_fontFamily)),
      m_colors(new ColorTokens(this)), m_metrics(new MetricTokens(this)),
      m_typography(new TypographyTokens(this)), m_motion(new MotionTokens(this)), m_icons(new IconTokens(this))
{
    auto *source = new AppearanceSource(this);
    auto state = std::make_shared<AppearanceState>();
    state->revision = 1;
    state->palette = PaletteState::calculate(m_prepared, {}, this, state->revision);
    state->snapshot = m_prepared;
    state->snapshot.tokens.insert(QStringLiteral("colors"), state->palette->values);
    state->snapshot.diagnostics.append(state->palette->diagnostics);
    PaletteBuilder::resolveNoncolors(&state->snapshot);
    state->installNoncolors();
    source->install(state);
    m_resolved = state->snapshot;
    m_diagnostics = m_resolved.diagnostics;
    m_colors->m_palette = state->palette;
    m_colors->install(m_resolved.tokens.value(QStringLiteral("colors")).toMap());
    m_metrics->install(m_resolved.tokens.value(QStringLiteral("metrics")).toMap());
    m_typography->install(m_resolved.tokens.value(QStringLiteral("typography")).toMap());
    m_motion->install(m_resolved.tokens.value(QStringLiteral("motion")).toMap());
    m_icons->install(m_resolved.tokens.value(QStringLiteral("icons")).toMap());
    for (auto *group : {static_cast<TokenGroup *>(m_colors), static_cast<TokenGroup *>(m_metrics),
             static_cast<TokenGroup *>(m_typography), static_cast<TokenGroup *>(m_motion), static_cast<TokenGroup *>(m_icons)})
        group->initializeNotifications();
    source->notifications.initialize(this, {QStringLiteral("scheme"), QStringLiteral("density"),
        QStringLiteral("contrast"), QStringLiteral("fontScale"), QStringLiteral("reducedMotion")});
}

Appearance::~Appearance() = default;

Appearance *Appearance::create(QQmlEngine *engine, QJSEngine *)
{
    return new Appearance(engine);
}

void Appearance::setInput(const QVariantMap &input)
{
    TraceAttempt attempt(this);
    auto *source = AppearanceSource::from(this);
    const quint64 revisionBefore = source->state() ? source->state()->revision : 0;
    auto prepared = PaletteBuilder::prepare(input, m_fontFamily);
    const QPointer<Appearance> guard(this);
    if (!prepared.accepted) {
        attempt.finish(TraceOutcome::Rejected, revisionBefore, revisionBefore);
        const auto rejected = prepared.diagnostics;
        const bool changed = m_diagnostics != rejected;
        m_diagnostics = rejected;
        if (changed)
            emit diagnosticsChanged();
        if (guard)
            emit inputRejected(rejected);
        return;
    }
    const auto before = source->state();
    auto state = std::make_shared<AppearanceState>();
    state->revision = before->revision + 1;
    const auto inputs = PaletteInputs::from(prepared);
    {
        TraceSpan compare(TraceScope::PaletteKeyCompare, this);
        if (inputs == PaletteInputs::from(m_prepared)) {
            state->palette = before->palette;
            AppearanceTrace::counter(TraceScope::PaletteReuse, this, 1);
        } else {
            state->palette = PaletteState::calculate(prepared, before->palette, this, state->revision);
        }
    }
    auto candidate = prepared;
    {
        TraceSpan noncolors(TraceScope::ResolveNoncolors, this);
        PaletteBuilder::resolveNoncolors(&candidate, &m_prepared, &before->snapshot);
    }
    candidate.tokens.insert(QStringLiteral("colors"), state->palette->values);
    candidate.diagnostics.append(state->palette->diagnostics);
    const bool inputChangedValue = m_input != input;
    const bool diagnosticChange = m_diagnostics != candidate.diagnostics;
    if (!inputChangedValue && !diagnosticChange && m_resolved.tokens == candidate.tokens) {
        attempt.finish(TraceOutcome::NoOp, revisionBefore, revisionBefore);
        return;
    }

    const bool colorChange = m_colors->values() != candidate.tokens.value(QStringLiteral("colors")).toMap();
    const bool metricChange = m_metrics->values() != candidate.tokens.value(QStringLiteral("metrics")).toMap();
    const bool typeChange = m_typography->values() != candidate.tokens.value(QStringLiteral("typography")).toMap();
    const bool motionChange = m_motion->values() != candidate.tokens.value(QStringLiteral("motion")).toMap();
    const bool iconChange = m_icons->values() != candidate.tokens.value(QStringLiteral("icons")).toMap();
    state->snapshot = candidate;
    state->installNoncolors();
    const auto update = PaletteUpdate::between(before, state);
    m_input = input;
    m_prepared = std::move(prepared);
    source->install(state);
    m_resolved = std::move(candidate);
    m_diagnostics = m_resolved.diagnostics;
    m_colors->m_palette = state->palette;
    attempt.finish(TraceOutcome::Accepted, revisionBefore, state->revision);
    // Install every group before any notification. The completed update owns
    // immutable references even if an earlier observer installs a newer revision.
    {
        TraceSpan install(TraceScope::Install, this);
        m_colors->install(m_resolved.tokens.value(QStringLiteral("colors")).toMap());
        m_metrics->install(m_resolved.tokens.value(QStringLiteral("metrics")).toMap());
        m_typography->install(m_resolved.tokens.value(QStringLiteral("typography")).toMap());
        m_motion->install(m_resolved.tokens.value(QStringLiteral("motion")).toMap());
        m_icons->install(m_resolved.tokens.value(QStringLiteral("icons")).toMap());
    }
    source->notifications.installed(this);
    {
        TraceSpan dispatch(TraceScope::AggregateDispatch, this);
        emit resolvedChanged();
        if (!guard)
            return;
        source->publish(update);
        if (!guard)
            return;
    }
    source->notifications.drain(this);
    if (!guard)
        return;
    if (colorChange)
        m_colors->publish();
    if (!guard)
        return;
    if (metricChange)
        m_metrics->publish();
    if (!guard)
        return;
    if (typeChange)
        m_typography->publish();
    if (!guard)
        return;
    if (motionChange)
        m_motion->publish();
    if (!guard)
        return;
    if (iconChange)
        m_icons->publish();
    if (!guard)
        return;
    if (inputChangedValue)
        emit inputChanged();
    if (!guard)
        return;
    if (diagnosticChange)
        emit diagnosticsChanged();
}

} // namespace InterActive
