// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ControlAppearance.h"
#include "AppearanceSource_p.h"
#include "AirmailProfiles.h"
#include "AppearanceTrace_p.h"
#include "FontEnvironment_p.h"
#include "LocalColorOverrides_p.h"
#include "TokenValidation_p.h"
#include <QFontMetricsF>
#include <algorithm>
#include <cmath>

namespace InterActive {

struct ControlAppearancePrivate {
    struct Defaults {
        MetricRecord metrics;
        TypographyRecord typography;
        QString density;
    };
    explicit ControlAppearancePrivate(Defaults defaults) : defaults(std::move(defaults)) {}
    struct Diagnostics {
        ControlInputErrors invalidInputs;
        std::shared_ptr<const ControlFindings> findings;
        bool operator==(const Diagnostics &other) const
        {
            return invalidInputs == other.invalidInputs
                && (findings == other.findings || (findings && other.findings && *findings == *other.findings));
        }
        bool operator!=(const Diagnostics &other) const { return !(*this == other); }
    };
    enum class Notification {
        source, family, tone, validation,
#define IA_NOTIFY_INDEX(type, name, upper, group, token) name,
        IA_CONTROL_PROPERTIES(IA_NOTIFY_INDEX)
#undef IA_NOTIFY_INDEX
        resolvedFont, headingPixelSize, headingWeight, stateTable, Count
    };
    struct NotifiedValues {
        Appearance *source = nullptr;
        ControlFamily family = ControlFamily::Button;
        Presentation::Tone tone = Presentation::Auto;
        Presentation::Validation validation = Presentation::None;
#define IA_NOTIFIED_VALUE(type, name, upper, group, token) type name{};
        IA_CONTROL_PROPERTIES(IA_NOTIFIED_VALUE)
#undef IA_NOTIFIED_VALUE
        QFont resolvedFont;
        qreal headingPixelSize = 0;
        int headingWeight = 0;
        StateTable stateTable;
    } last;
    struct FontInputs {
        QString family;
        int pixelSize;
        int weight;
        quint64 generation;
        bool operator==(const FontInputs &other) const
        {
            return family == other.family && pixelSize == other.pixelSize
                && weight == other.weight && generation == other.generation;
        }
    };
    struct HeightInputs {
        qreal floor, padding, target, textHeight;
        bool operator==(const HeightInputs &other) const
        {
            return floor == other.floor && padding == other.padding
                && target == other.target && textHeight == other.textHeight;
        }
    };
    const Defaults defaults;
    ControlFamily family = ControlFamily::Button;
    QPointer<AppearanceSource> source;
    AppearanceStatePtr sourceState;
    std::shared_ptr<const PaletteState> palette = PaletteState::fallback();
    ControlColorMapping mapping = controlColorMapping(ControlFamily::Button, Presentation::Auto, Presentation::None);
    LocalColorOverrides local;
    std::shared_ptr<const StateTableData> table;
    ControlColorValues colorOverrides;
    ControlNoncolorOverrides noncolorOverrides;
    ControlNoncolorValues values;
    ControlInputErrors invalidInputs;
    std::bitset<size_t(Notification::Count)> pending;
    bool notificationsInitialized = false;
    bool drainingNotifications = false;
    FontEnvironment *environment = nullptr;
    std::optional<FontInputs> fontInputs;
    std::optional<HeightInputs> heightInputs;
    qreal textHeight = 0;
    quint64 revision = 0;
    Diagnostics diagnostics, lastDiagnostics;
    mutable QVariantList diagnosticsCache;
    mutable bool diagnosticsCacheValid = false;
    bool drainingDiagnostics = false;
};

namespace {
ControlAppearancePrivate::Defaults fallbackNoncolors()
{
    const auto tokens = AirmailProfiles::profile(QStringLiteral("light"), Appearance::initialFontFamily());
    return {MetricRecord::from(tokens.value(QStringLiteral("metrics")).toMap()),
        TypographyRecord::from(tokens.value(QStringLiteral("typography")).toMap()), QStringLiteral("compact")};
}
}

ControlAppearance::ControlAppearance(QObject *parent)
    : QObject(parent), m_private(new ControlAppearancePrivate(fallbackNoncolors()))
{
    m_private->environment = FontEnvironment::shared();
    connect(m_private->environment, &FontEnvironment::invalidated, this, [this] {
        // Environment signals can arrive inside a source's aggregate callback,
        // before completed() reaches this facade. Do not mark that newer source
        // installed while leaving its dependent color branches/notifications old.
        if (m_source && m_private->sourceState)
            resolve(PaletteUpdate::between(m_private->sourceState, m_private->source->state()), true);
        else
            resolveParts(false, true, false);
    });
    resolve();
    initializeNotifications();
    m_private->lastDiagnostics = m_private->diagnostics;
}

ControlAppearance::~ControlAppearance() = default;

QString ControlAppearance::family() const
{
    return QString::fromLatin1(familyDescriptor(m_private->family).name);
}

QVariantList ControlAppearance::diagnostics() const
{
    auto &state = *m_private;
    if (!state.diagnosticsCacheValid) {
        AppearanceTrace::work(AppearanceWork::ControlDiagnosticsBuild);
        QVariantList diagnostics;
        // Public input diagnostics retain their original lexical property order.
        static const auto errorOrder = [] {
            std::array<std::pair<const char *, ControlInput>, size_t(ControlInput::Count)> names{{
#define IA_INPUT_NAME(type, name, upper, group, token) {#name, ControlInput::name},
                IA_CONTROL_PROPERTIES(IA_INPUT_NAME)
#undef IA_INPUT_NAME
                {"family", ControlInput::family}, {"tone", ControlInput::tone}, {"validation", ControlInput::validation}}};
            std::sort(names.begin(), names.end(), [](const auto &a, const auto &b) { return qstrcmp(a.first, b.first) < 0; });
            return names;
        }();
        for (const auto &entry : errorOrder) {
            if (state.diagnostics.invalidInputs.contains(entry.second))
                diagnostics.append(state.diagnostics.invalidInputs.value(entry.second));
        }
        if (state.diagnostics.findings) {
            for (const auto &findings : *state.diagnostics.findings)
                diagnostics.append(findings);
        }
        state.diagnosticsCache = std::move(diagnostics);
        state.diagnosticsCacheValid = true;
    }
    return state.diagnosticsCache;
}

#define IA_COLOR_GETTER(type, name, upper, group, token) \
    type ControlAppearance::name() const { return m_private->table->values.value(ControlColor::name); }
IA_CONTROL_COLOR_PROPERTIES(IA_COLOR_GETTER)
#undef IA_COLOR_GETTER
#define IA_NONCOLOR_GETTER(type, name, upper, group, token) \
    type ControlAppearance::name() const { return std::get<type>(m_private->values[size_t(ControlNoncolor::name)]); }
IA_CONTROL_NONCOLOR_PROPERTIES(IA_NONCOLOR_GETTER)
#undef IA_NONCOLOR_GETTER

StateTable ControlAppearance::stateTable() const
{
    const auto &metrics = m_private->sourceState ? m_private->sourceState->metrics : m_private->defaults.metrics;
    return StateTable(m_private->table, metrics.focusWidth, metrics.borderWidth);
}

qreal ControlAppearance::headingPixelSize() const
{
    const auto &state = *m_private;
    const auto &typography = state.sourceState ? state.sourceState->typography : state.defaults.typography;
    return state.noncolorOverrides.contains(ControlNoncolor::fontPixelSize)
        ? fontPixelSize() : typography.headingSize;
}

int ControlAppearance::headingWeight() const
{
    const auto &state = *m_private;
    const auto &typography = state.sourceState ? state.sourceState->typography : state.defaults.typography;
    return state.noncolorOverrides.contains(ControlNoncolor::fontWeight)
        ? fontWeight() : typography.emphasisWeight;
}

void ControlAppearance::setSource(Appearance *source)
{
    if (m_source == source)
        return;
    disconnect(m_sourceChanged);
    disconnect(m_sourceDestroyed);
    m_source = source;
    m_private->source = source ? AppearanceSource::from(source) : nullptr;
    if (source) {
        m_sourceChanged = connect(m_private->source, &AppearanceSource::completed, this, [this, source](const PaletteUpdate &update) {
            if (m_source == source)
                resolve(update);
        });
        m_sourceDestroyed = connect(source, &QObject::destroyed, this, [this] {
            m_source = nullptr;
            resolve();
        });
    }
    resolve();
}

void ControlAppearance::setFamily(const QString &family)
{
    const auto parsed = controlFamily(family);
    if (!parsed) {
        m_private->invalidInputs.set(ControlInput::family, TokenValidation::diagnostic(QStringLiteral("family"),
            QStringLiteral("invalid-value"), QStringLiteral("Unknown family; previous family retained.")));
        resolve();
        return;
    }
    const bool hadError = m_private->invalidInputs.reset(ControlInput::family);
    if (m_private->family == *parsed && !hadError)
        return;
    m_private->family = *parsed;
    resolve();
}

void ControlAppearance::setTone(Presentation::Tone tone)
{
    if (tone < Presentation::Auto || tone > Presentation::Caution) {
        m_private->invalidInputs.set(ControlInput::tone, TokenValidation::diagnostic(QStringLiteral("tone"),
            QStringLiteral("invalid-value"), QStringLiteral("Unknown tone; previous tone retained.")));
        resolve();
        return;
    }
    const bool hadError = m_private->invalidInputs.reset(ControlInput::tone);
    if (m_tone == tone && !hadError)
        return;
    m_tone = tone;
    resolve();
}

void ControlAppearance::setValidation(Presentation::Validation validation)
{
    if (validation < Presentation::None || validation > Presentation::Invalid) {
        m_private->invalidInputs.set(ControlInput::validation, TokenValidation::diagnostic(QStringLiteral("validation"),
            QStringLiteral("invalid-value"), QStringLiteral("Unknown validation; previous value retained.")));
        resolve();
        return;
    }
    const bool hadError = m_private->invalidInputs.reset(ControlInput::validation);
    if (m_validation == validation && !hadError)
        return;
    m_validation = validation;
    resolve();
}

#define IA_LOCAL_METHODS(type, name, upper, group, token, record, Index) \
    void ControlAppearance::set##upper(const type &value) \
    { \
        auto &state = *m_private; \
        QVariant normalized; \
        QVariantList diagnostics; \
        if (!TokenValidation::validate(QStringLiteral(#group), QStringLiteral(#token), QVariant::fromValue(value), \
                &normalized, &diagnostics, QStringLiteral(#name), true)) { \
            state.invalidInputs.set(ControlInput::name, diagnostics.first().toMap()); \
            resolve(); \
            return; \
        } \
        const bool hadError = state.invalidInputs.reset(ControlInput::name); \
        const decltype(state.record)::Value typed = normalized.value<type>(); \
        if (state.record.contains(Index::name) && state.record.value(Index::name) == typed && !hadError) \
            return; \
        state.record.set(Index::name, typed); \
        resolve(); \
    } \
    void ControlAppearance::reset##upper() \
    { \
        const bool removed = m_private->record.reset(Index::name); \
        const bool hadError = m_private->invalidInputs.reset(ControlInput::name); \
        if (removed || hadError) \
            resolve(); \
    }
#define IA_COLOR_METHODS(type, name, upper, group, token) \
    IA_LOCAL_METHODS(type, name, upper, group, token, colorOverrides, ControlColor)
IA_CONTROL_COLOR_PROPERTIES(IA_COLOR_METHODS)
#undef IA_COLOR_METHODS
#define IA_NONCOLOR_METHODS(type, name, upper, group, token) \
    IA_LOCAL_METHODS(type, name, upper, group, token, noncolorOverrides, ControlNoncolor)
IA_CONTROL_NONCOLOR_PROPERTIES(IA_NONCOLOR_METHODS)
#undef IA_NONCOLOR_METHODS
#undef IA_LOCAL_METHODS

void ControlAppearance::resetOverrides()
{
    auto &state = *m_private;
    if (state.colorOverrides.presence.none() && state.noncolorOverrides.presence.none() && state.invalidInputs.presence.none())
        return;
    state.colorOverrides.presence.reset();
    state.noncolorOverrides.presence.reset();
    state.invalidInputs.presence.reset();
    resolve();
}

void ControlAppearance::resolve()
{
    resolveParts(true, true, true);
}

void ControlAppearance::resolve(const PaletteUpdate &update, bool forceNoncolors)
{
    auto &state = *m_private;
    const auto newest = m_source ? m_private->source->state() : AppearanceStatePtr();
    if (!newest || !state.sourceState) {
        resolve();
        return;
    }
    // A completed signal may be obsolete after a nested source update. Diff
    // against what this facade actually installed, never trust a stale mask.
    const auto effective = state.sourceState == update.before && newest == update.after
        ? update : PaletteUpdate::between(state.sourceState, newest);
    ColorRoleMask dependencies;
    for (const auto role : state.mapping)
        dependencies.set(size_t(role));
    dependencies.set(size_t(ColorRole::content));
    dependencies.set(size_t(ColorRole::canvas));
    if (state.family == ControlFamily::Field) {
        dependencies.set(size_t(ColorRole::placeholderText));
        dependencies.set(size_t(ColorRole::onAccent));
    }
    if (state.family == ControlFamily::Label)
        dependencies.set(size_t(ColorRole::link));
    if (state.family == ControlFamily::Slider)
        dependencies.set(size_t(ColorRole::controlPressed));
    if (state.family == ControlFamily::ScrollBar) {
        dependencies.set(size_t(ColorRole::outline));
        dependencies.set(size_t(ColorRole::secondaryText));
    }
    if (state.family == ControlFamily::Choice) {
        dependencies.set(size_t(ColorRole::control));
        dependencies.set(size_t(ColorRole::disabledSurface));
    }
    if (state.family == ControlFamily::MenuItem) {
        dependencies.set(size_t(ColorRole::popup));
        // The menu reuses the finished Primary table, including its ownership
        // sensitive foreground/outline coupling and action alias presence.
        for (const auto role : controlColorMapping(ControlFamily::Button, Presentation::Primary,
                 Presentation::None, state.palette->dark))
            dependencies.set(size_t(role));
        dependencies.set(size_t(ColorRole::accentHover));
        dependencies.set(size_t(ColorRole::accentPressed));
        dependencies.set(size_t(ColorRole::onAccent));
    }
    if (state.family == ControlFamily::Tab) {
        dependencies.set(size_t(ColorRole::control));
        dependencies.set(size_t(ColorRole::controlHover));
        dependencies.set(size_t(ColorRole::text));
    }
    const auto &before = *state.palette;
    const auto &after = *newest->palette;
    const bool colors = ((effective.changedRoles | effective.changedPresence) & dependencies).any()
        || ((state.family == ControlFamily::Tab || state.family == ControlFamily::TabBar) && before.dark != after.dark)
        || before.target != after.target || before.labelTarget != after.labelTarget
        || before.hoverBlend != after.hoverBlend || before.pressedBlend != after.pressedBlend;
    const bool noncolors = forceNoncolors || (effective.changedGroups & (MetricsGroup | TypographyGroup));
    resolveParts(colors, noncolors, false);
}

void ControlAppearance::resolveParts(bool colors, bool noncolors, bool selectors)
{
    TraceSpan span(TraceScope::FacadeResolve, this, AppearanceWork::FacadeUpdate);
    auto &state = *m_private;
    // Own the installed source reference across every callback. Read the newest
    // source state, not the earlier dispatch mask, if an observer reentered it.
    state.sourceState = m_source ? state.source->state() : AppearanceStatePtr();
    state.palette = state.sourceState ? state.sourceState->palette : PaletteState::fallback();
    state.revision = state.sourceState ? state.sourceState->revision : state.revision + 1;
    const auto &descriptor = familyDescriptor(state.family);
    if (colors) {
        AppearanceTrace::work(AppearanceWork::ColorResolution);
        state.mapping = controlColorMapping(state.family, m_tone, m_validation, state.palette->dark);
        const bool colored = m_tone == Presentation::Primary || m_tone == Presentation::Destructive || m_tone == Presentation::Caution;
        const bool coupleBorder = state.family != ControlFamily::MenuItem && colored && !descriptor.usesSemanticText(m_tone) && m_validation == Presentation::None
            && !state.palette->explicitRole(state.mapping[size_t(ControlColor::borderColor)]);
        const auto shared = state.palette->stateTable(state.family, m_tone, m_validation);
        if (state.colorOverrides.presence.none()) {
            state.table = shared;
        } else {
            state.table = state.local.install(*state.palette, state.mapping, shared,
                state.colorOverrides, coupleBorder, this, state.revision);
        }
    }

    if (noncolors) {
        const auto &metrics = state.sourceState ? state.sourceState->metrics : state.defaults.metrics;
        const auto &typography = state.sourceState ? state.sourceState->typography : state.defaults.typography;
        const auto &density = state.sourceState ? state.sourceState->snapshot.density : state.defaults.density;
        ControlNoncolorValues candidate;
#define IA_NONCOLOR_DEFAULT(type, name, upper, group, token) \
        candidate[size_t(ControlNoncolor::name)] = group.token;
        IA_CONTROL_NONCOLOR_PROPERTIES(IA_NONCOLOR_DEFAULT)
#undef IA_NONCOLOR_DEFAULT
        candidate[size_t(ControlNoncolor::controlRadius)] = metrics.*descriptor.radius;
        candidate[size_t(ControlNoncolor::horizontalPadding)] = metrics.*descriptor.horizontalPadding;
        candidate[size_t(ControlNoncolor::verticalPadding)] = metrics.*descriptor.verticalPadding;
        candidate[size_t(ControlNoncolor::fontPixelSize)] = typography.*descriptor.fontSize;
        candidate[size_t(ControlNoncolor::minimumHeight)] = descriptor.height ? metrics.*descriptor.height : qreal(0);
        for (size_t i = 0; i < candidate.size(); ++i) {
            if (state.noncolorOverrides.presence.test(i))
                candidate[i] = state.noncolorOverrides.values[i];
        }
        const auto &requestedFamily = std::get<QString>(candidate[size_t(ControlNoncolor::fontFamily)]);
        const int pixelSize = std::max(1, qRound(std::get<qreal>(candidate[size_t(ControlNoncolor::fontPixelSize)])));
        const int weight = std::get<int>(candidate[size_t(ControlNoncolor::fontWeight)]);
        const ControlAppearancePrivate::FontInputs fontInputs{requestedFamily, pixelSize, weight, state.environment->generation()};
        if (!state.fontInputs || !(*state.fontInputs == fontInputs)) {
            AppearanceTrace::work(AppearanceWork::FontConstruction);
            QFont font;
            font.setFamily(requestedFamily);
            font.setPixelSize(pixelSize);
            font.setWeight(static_cast<QFont::Weight>(weight));
            AppearanceTrace::work(AppearanceWork::FontMeasurement);
            state.textHeight = std::ceil(QFontMetricsF(font).height());
            m_font = font;
            state.fontInputs = fontInputs;
        }
        // Keep the requested floor, never yesterday's derived output, in the key.
        const qreal requestedFloor = std::get<qreal>(candidate[size_t(ControlNoncolor::minimumHeight)]);
        const qreal padding = std::get<qreal>(candidate[size_t(ControlNoncolor::verticalPadding)]);
        const qreal targetFloor = descriptor.interactive && density == QLatin1String("touch") ? metrics.targetSize : 0;
        const ControlAppearancePrivate::HeightInputs heightInputs{requestedFloor, padding, targetFloor, state.textHeight};
        qreal minimum = state.heightInputs ? minimumHeight() : 0;
        if (!state.heightInputs || !(*state.heightInputs == heightInputs)) {
            AppearanceTrace::work(AppearanceWork::GeometryCalculation);
            minimum = std::max({requestedFloor, state.textHeight + 2 * padding, targetFloor});
            state.heightInputs = heightInputs;
        }
        candidate[size_t(ControlNoncolor::minimumHeight)] = minimum;
        state.values = std::move(candidate);
    }
    const ControlAppearancePrivate::Diagnostics diagnostics{state.invalidInputs, state.table->findings};
    if (state.diagnostics != diagnostics) {
        state.diagnostics = diagnostics;
        state.diagnosticsCacheValid = false;
    }
    if (colors || noncolors || selectors)
        installNotifications();
    const QPointer<ControlAppearance> guard(this);
    emit resolvedChanged();
    if (guard)
        drainNotifications();
    if (guard)
        drainDiagnostics();
}

#define IA_FACADE_SELECTORS(X) \
    X(source, m_source.data()) \
    X(family, m_private->family) \
    X(tone, m_tone) \
    X(validation, m_validation)

void ControlAppearance::initializeNotifications()
{
    auto &state = *m_private;
#define IA_INITIALIZE(name, current) state.last.name = current;
    IA_FACADE_SELECTORS(IA_INITIALIZE)
#define IA_INITIALIZE_PROPERTY(type, name, upper, group, token) IA_INITIALIZE(name, name())
    IA_CONTROL_PROPERTIES(IA_INITIALIZE_PROPERTY)
#undef IA_INITIALIZE_PROPERTY
    IA_INITIALIZE(resolvedFont, m_font)
    IA_INITIALIZE(headingPixelSize, headingPixelSize())
    IA_INITIALIZE(headingWeight, headingWeight())
    IA_INITIALIZE(stateTable, stateTable())
#undef IA_INITIALIZE
    state.notificationsInitialized = true;
}

void ControlAppearance::installNotifications()
{
    auto &state = *m_private;
    if (!state.notificationsInitialized)
        return;
    TraceSpan span(TraceScope::NotificationCompare, this, AppearanceWork::NotificationCompare);
    using Notification = ControlAppearancePrivate::Notification;
#define IA_COMPARE(name, current) \
    if (state.last.name != (current)) state.pending.set(size_t(Notification::name));
    IA_FACADE_SELECTORS(IA_COMPARE)
#define IA_COMPARE_PROPERTY(type, name, upper, group, token) IA_COMPARE(name, name())
    IA_CONTROL_PROPERTIES(IA_COMPARE_PROPERTY)
#undef IA_COMPARE_PROPERTY
    IA_COMPARE(resolvedFont, m_font)
    IA_COMPARE(headingPixelSize, headingPixelSize())
    IA_COMPARE(headingWeight, headingWeight())
    IA_COMPARE(stateTable, stateTable())
#undef IA_COMPARE
    span.setAux(quint32(Notification::Count));
}

void ControlAppearance::drainNotifications()
{
    auto &state = *m_private;
    TraceSpan span(TraceScope::NotificationDrain, this, AppearanceWork::NotificationDrain);
    if (!state.notificationsInitialized || state.drainingNotifications)
        return;
    state.drainingNotifications = true;
    const QPointer<ControlAppearance> guard(this);
    quint32 emitted = 0;
    using Notification = ControlAppearancePrivate::Notification;
    const auto notify = [&](Notification property, auto &last, const auto &current, auto signal) {
        state.pending.reset(size_t(property));
        if (last == current)
            return true;
        last = current;
        ++emitted;
        (this->*signal)();
        return bool(guard);
    };
    while (state.pending.any()) {
        // Preserve declaration order, re-reading each pending property after
        // callbacks. Nested installs add obligations to this same drain.
#define IA_NOTIFY(name, current) \
        if (state.pending.test(size_t(Notification::name)) \
            && !notify(Notification::name, state.last.name, current, &ControlAppearance::name##Changed)) return;
        IA_FACADE_SELECTORS(IA_NOTIFY)
#define IA_NOTIFY_PROPERTY(type, name, upper, group, token) IA_NOTIFY(name, name())
        IA_CONTROL_PROPERTIES(IA_NOTIFY_PROPERTY)
#undef IA_NOTIFY_PROPERTY
        IA_NOTIFY(resolvedFont, m_font)
        IA_NOTIFY(headingPixelSize, headingPixelSize())
        IA_NOTIFY(headingWeight, headingWeight())
        IA_NOTIFY(stateTable, stateTable())
#undef IA_NOTIFY
    }
    state.drainingNotifications = false;
    span.setAux(emitted);
}
#undef IA_FACADE_SELECTORS

void ControlAppearance::drainDiagnostics()
{
    auto &state = *m_private;
    if (state.drainingDiagnostics)
        return;
    state.drainingDiagnostics = true;
    const QPointer<ControlAppearance> guard(this);
    while (state.lastDiagnostics != state.diagnostics) {
        state.lastDiagnostics = state.diagnostics;
        emit diagnosticsChanged();
        if (!guard)
            return;
    }
    state.drainingDiagnostics = false;
}

} // namespace InterActive
