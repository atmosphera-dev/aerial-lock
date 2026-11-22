// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PaletteState_p.h"
#include "PaletteBuilder_p.h"
#include "ColorMath_p.h"
#include "TokenValidation_p.h"
#include "AppearanceTrace_p.h"
#include "LocalColorOverrides_p.h"
#include <QPointer>
#include <algorithm>

namespace InterActive {

void PropertyNotifications::initialize(QObject *object, const QStringList &names)
{
    properties = names;
    if (properties.isEmpty()) {
        const auto *meta = object->metaObject();
        for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
            const auto property = meta->property(i);
            if (property.hasNotifySignal() && !property.isConstant())
                properties.append(QString::fromLatin1(property.name()));
        }
    }
    const auto *meta = object->metaObject();
    for (const auto &name : properties) {
        const auto property = meta->property(meta->indexOfProperty(name.toLatin1().constData()));
        metadata.append({name, property});
        last.insert(name, property.read(object));
    }
}

void PropertyNotifications::installed(QObject *object)
{
    TraceSpan span(TraceScope::NotificationCompare, object, AppearanceWork::NotificationCompare);
    quint32 compared = 0;
    for (const auto &property : metadata) {
        ++compared;
        if (last.value(property.name) != property.metadata.read(object))
            pending.insert(property.name);
    }
    span.setAux(compared);
}

void PropertyNotifications::drain(QObject *object)
{
    TraceSpan span(TraceScope::NotificationDrain, object, AppearanceWork::NotificationDrain);
    quint32 emitted = 0;
    if (draining)
        return;
    draining = true;
    const QPointer<QObject> guard(object);
    while (!pending.isEmpty()) {
        // Property order is declaration order; nested signal order is not a contract.
        for (const auto &property : metadata) {
            if (!pending.remove(property.name))
                continue;
            const QVariant current = property.metadata.read(object);
            if (last.value(property.name) == current)
                continue;
            last.insert(property.name, current);
            ++emitted;
            property.metadata.notifySignal().invoke(object, Qt::DirectConnection);
            if (!guard)
                return;
        }
    }
    draining = false;
    span.setAux(emitted);
}

MetricRecord MetricRecord::from(const QVariantMap &values)
{
    MetricRecord result;
#define IA_READ_METRIC(name, ...) result.name = values.value(QStringLiteral(#name)).toDouble();
    IA_METRIC_TOKENS(IA_READ_METRIC)
#undef IA_READ_METRIC
    return result;
}

bool MetricRecord::operator==(const MetricRecord &other) const
{
#define IA_COMPARE_METRIC(name, ...) if (name != other.name) return false;
    IA_METRIC_TOKENS(IA_COMPARE_METRIC)
#undef IA_COMPARE_METRIC
    return true;
}

TypographyRecord TypographyRecord::from(const QVariantMap &values)
{
    TypographyRecord result;
    result.family = values.value(QStringLiteral("family")).toString();
#define IA_READ_SIZE(name, ...) result.name = values.value(QStringLiteral(#name)).toDouble();
    IA_TYPE_SIZE_TOKENS(IA_READ_SIZE)
#undef IA_READ_SIZE
#define IA_READ_WEIGHT(name, ...) result.name = values.value(QStringLiteral(#name)).toInt();
    IA_TYPE_WEIGHT_TOKENS(IA_READ_WEIGHT)
#undef IA_READ_WEIGHT
    return result;
}

bool TypographyRecord::operator==(const TypographyRecord &other) const
{
    if (family != other.family)
        return false;
#define IA_COMPARE_TYPE(name, ...) if (name != other.name) return false;
    IA_TYPE_SIZE_TOKENS(IA_COMPARE_TYPE)
    IA_TYPE_WEIGHT_TOKENS(IA_COMPARE_TYPE)
#undef IA_COMPARE_TYPE
    return true;
}

void AppearanceState::installNoncolors()
{
    metrics = MetricRecord::from(snapshot.tokens.value(QStringLiteral("metrics")).toMap());
    typography = TypographyRecord::from(snapshot.tokens.value(QStringLiteral("typography")).toMap());
}

namespace {
constexpr ControlColorMapping neutralRoles(ColorRole background,
    ColorRole hover = ColorRole::controlHover, ColorRole pressed = ColorRole::controlPressed,
    ColorRole foreground = ColorRole::text)
{
    return {background, ColorRole::accent, foreground, hover, pressed, ColorRole::disabledSurface,
        ColorRole::disabledText, ColorRole::inactiveSelection, ColorRole::text,
        ColorRole::selection, ColorRole::text, ColorRole::outline, ColorRole::outline, ColorRole::focus};
}

constexpr std::array<FamilyDescriptor, ControlFamilyCount> Families{{
    {"button", neutralRoles(ColorRole::control), &MetricRecord::controlHeight, &MetricRecord::controlRadius,
        &MetricRecord::horizontalPadding, &MetricRecord::verticalPadding, &TypographyRecord::controlSize, true, false},
    {"tool", neutralRoles(ColorRole::transparent), &MetricRecord::toolHeight, &MetricRecord::controlRadius,
        &MetricRecord::horizontalPadding, &MetricRecord::verticalPadding, &TypographyRecord::controlSize, true, false},
    {"field", neutralRoles(ColorRole::content), &MetricRecord::controlHeight, &MetricRecord::controlRadius,
        &MetricRecord::horizontalPadding, &MetricRecord::verticalPadding, &TypographyRecord::controlSize, true, false},
    {"surface", neutralRoles(ColorRole::content), nullptr, &MetricRecord::controlRadius,
        &MetricRecord::horizontalPadding, &MetricRecord::verticalPadding, &TypographyRecord::controlSize, false, false},
    {"popup", neutralRoles(ColorRole::popup), nullptr, &MetricRecord::popupRadius,
        &MetricRecord::popupPadding, &MetricRecord::popupPadding, &TypographyRecord::controlSize, false, false},
    {"label", neutralRoles(ColorRole::transparent), nullptr, &MetricRecord::controlRadius,
        &MetricRecord::horizontalPadding, &MetricRecord::verticalPadding, &TypographyRecord::controlSize, false, true},
    {"delegate", neutralRoles(ColorRole::transparent), &MetricRecord::rowHeight, &MetricRecord::controlRadius,
        &MetricRecord::horizontalPadding, &MetricRecord::verticalPadding, &TypographyRecord::controlSize, true, false},
    {"tab", neutralRoles(ColorRole::transparent), &MetricRecord::rowHeight, &MetricRecord::controlRadius,
        &MetricRecord::horizontalPadding, &MetricRecord::verticalPadding, &TypographyRecord::controlSize, true, false},
    {"choice", neutralRoles(ColorRole::transparent), &MetricRecord::controlHeight, &MetricRecord::controlRadius,
        &MetricRecord::verticalPadding, &MetricRecord::verticalPadding, &TypographyRecord::controlSize, true, false},
    {"tooltip", neutralRoles(ColorRole::tooltipBackground, ColorRole::tooltipBackground,
        ColorRole::tooltipBackground, ColorRole::tooltipText), nullptr, &MetricRecord::controlRadius,
        &MetricRecord::horizontalPadding, &MetricRecord::verticalPadding, &TypographyRecord::supportingSize, false, false},
    {"menuItem", neutralRoles(ColorRole::transparent), &MetricRecord::rowHeight, &MetricRecord::controlRadius,
        &MetricRecord::horizontalPadding, &MetricRecord::verticalPadding, &TypographyRecord::controlSize, true, false, true},
    {"tabBar", neutralRoles(ColorRole::control), nullptr, &MetricRecord::controlRadius,
        &MetricRecord::spacingXS, &MetricRecord::spacingXS, &TypographyRecord::controlSize, false, false},
    {"slider", neutralRoles(ColorRole::control), &MetricRecord::controlHeight, &MetricRecord::controlRadius,
        &MetricRecord::horizontalPadding, &MetricRecord::verticalPadding, &TypographyRecord::controlSize, true, false},
    {"progress", neutralRoles(ColorRole::controlPressed, ColorRole::controlPressed, ColorRole::controlPressed), nullptr, &MetricRecord::controlRadius,
        &MetricRecord::horizontalPadding, &MetricRecord::verticalPadding, &TypographyRecord::controlSize, false, false},
    {"scrollBar", neutralRoles(ColorRole::transparent, ColorRole::transparent, ColorRole::transparent, ColorRole::controlPressed),
        nullptr, &MetricRecord::controlRadius, &MetricRecord::horizontalPadding, &MetricRecord::verticalPadding,
        &TypographyRecord::controlSize, false, false, false, false}
}};
}

const FamilyDescriptor &familyDescriptor(ControlFamily family)
{
    return Families[size_t(family)];
}

std::optional<ControlFamily> controlFamily(const QString &family)
{
    for (size_t i = 0; i < Families.size(); ++i) {
        if (family == QLatin1String(Families[i].name))
            return ControlFamily(i);
    }
    return std::nullopt;
}

const char *controlColorName(ControlColor color)
{
#define IA_COLOR_NAME(type, name, upper, group, token) #name,
    static constexpr const char *names[]{IA_CONTROL_COLOR_PROPERTIES(IA_COLOR_NAME)};
#undef IA_COLOR_NAME
    return names[size_t(color)];
}

const QString &controlColorKey(ControlColor color)
{
    static const auto names = [] {
        std::array<QString, ControlColorCount> result;
        for (size_t i = 0; i < result.size(); ++i)
            result[i] = QString::fromLatin1(controlColorName(ControlColor(i)));
        return result;
    }();
    return names[size_t(color)];
}

ControlColorMapping controlColorMapping(ControlFamily family, Presentation::Tone tone,
                                       Presentation::Validation validation, bool dark)
{
    // Finite selectors only: the finished colors already belong to the palette.
    static const auto mappings = [] {
        std::array<std::array<ControlColorMapping, 4>, ControlFamilyCount> all{};
        for (size_t f = 0; f < all.size(); ++f) {
            const auto &descriptor = Families[f];
            const auto &neutral = descriptor.colors;
            all[f][0] = neutral;
            const std::array<std::array<ColorRole, 6>, 3> semantic{{
                {ColorRole::primarySurface, ColorRole::primaryText, ColorRole::primaryHover,
                    ColorRole::primaryPressed, ColorRole::primaryOutline, ColorRole::accentText},
                {ColorRole::destructiveSurface, ColorRole::destructiveText, ColorRole::destructiveHover,
                    ColorRole::destructivePressed, ColorRole::destructiveOutline, ColorRole::errorText},
                {ColorRole::cautionSurface, ColorRole::cautionText, ColorRole::cautionHover,
                    ColorRole::cautionPressed, ColorRole::cautionOutline, ColorRole::warningText}}};
            for (size_t t = 0; t < semantic.size(); ++t) {
                auto colored = neutral;
                const auto &roles = semantic[t];
                if (descriptor.usesSemanticText(Presentation::Tone(int(Presentation::Primary) + int(t)))) {
                    colored[size_t(ControlColor::foregroundColor)] = roles[5];
                } else {
                    colored[size_t(ControlColor::backgroundColor)] = roles[0];
                    colored[size_t(ControlColor::foregroundColor)] = roles[1];
                    colored[size_t(ControlColor::hoverBackgroundColor)] = roles[2];
                    colored[size_t(ControlColor::pressedBackgroundColor)] = roles[3];
                    colored[size_t(ControlColor::selectedBackgroundColor)] = roles[3];
                    colored[size_t(ControlColor::selectedForegroundColor)] = roles[1];
                    colored[size_t(ControlColor::inactiveBackgroundColor)] = roles[0];
                    colored[size_t(ControlColor::inactiveForegroundColor)] = roles[1];
                    colored[size_t(ControlColor::borderColor)] = roles[4];
                }
                all[f][t + 1] = colored;
            }
        }
        return all;
    }();
    const size_t t = tone == Presentation::Primary ? 1 : tone == Presentation::Destructive ? 2
        : tone == Presentation::Caution ? 3 : 0;
    auto mapping = mappings[size_t(family)][t];
    if ((family == ControlFamily::Tab || family == ControlFamily::TabBar) && t == 0) {
        const auto segment = dark ? ColorRole::controlHover : ColorRole::content;
        mapping[size_t(ControlColor::selectedBackgroundColor)] = segment;
        mapping[size_t(ControlColor::inactiveBackgroundColor)] = segment;
    }
    if (family == ControlFamily::MenuItem || family == ControlFamily::Choice) {
        mapping[size_t(ControlColor::selectedBackgroundColor)] = ColorRole::accent;
        mapping[size_t(ControlColor::selectedForegroundColor)] = ColorRole::onAccent;
        mapping[size_t(ControlColor::inactiveBackgroundColor)] = ColorRole::accent;
        mapping[size_t(ControlColor::inactiveForegroundColor)] = ColorRole::onAccent;
    }
    if (validation != Presentation::None) {
        const auto role = validation == Presentation::Invalid ? ColorRole::errorText : ColorRole::warningText;
        mapping[size_t(ControlColor::borderColor)] = role;
        mapping[size_t(ControlColor::disabledBorderColor)] = role;
    }
    return mapping;
}

PaletteInputs PaletteInputs::from(const ResolvedAppearance &prepared)
{
    PaletteInputs input;
    input.scheme = prepared.scheme;
    input.contrast = prepared.contrast;
    input.colors = prepared.tokens.value(QStringLiteral("colors")).toMap();
    for (const auto &key : prepared.explicitTokens) {
        if (key.startsWith(QLatin1String("colors.")))
            input.presence.insert(key);
    }
    const auto motion = prepared.tokens.value(QStringLiteral("motion")).toMap();
    input.hoverBlend = motion.value(QStringLiteral("hoverBlend")).toDouble();
    input.pressedBlend = motion.value(QStringLiteral("pressedBlend")).toDouble();
    input.semanticBlend = motion.value(QStringLiteral("semanticBlend")).toDouble();
    return input;
}

bool PaletteInputs::operator==(const PaletteInputs &other) const
{
    return scheme == other.scheme && contrast == other.contrast && colors == other.colors
        && presence == other.presence && hoverBlend == other.hoverBlend
        && pressedBlend == other.pressedBlend && semanticBlend == other.semanticBlend;
}

QVariantList PaletteState::pairFindings(ColorRole foreground, ColorRole background,
                                       const QString &path, bool focus) const
{
    const auto found = std::find_if(pairs.cbegin(), pairs.cend(), [&](const DiagnosticPair &pair) {
        return pair.foreground == foreground && pair.background == background && pair.focus == focus;
    });
    Q_ASSERT(found != pairs.cend());
    QVariantList results;
    for (const auto &finding : found->findings) {
        auto entry = finding.toMap();
        entry.insert(QStringLiteral("path"), path);
        results.append(entry);
    }
    return results;
}

std::shared_ptr<const ControlFindings> PaletteState::controlFindings(const ControlColorMapping &mapping) const
{
    const auto found = m_controlFindings.find(mapping);
    if (found != m_controlFindings.end())
        return found->second;
    auto findings = std::make_shared<ControlFindings>();
    for (size_t i = 0; i < ControlDiagnosticPairs.size(); ++i) {
        const auto [foreground, background] = ControlDiagnosticPairs[i];
        const bool focus = i == 5;
        const QString path = controlColorKey(foreground) + QLatin1Char('/')
            + (focus ? QStringLiteral("content") : controlColorKey(background));
        (*findings)[i] = pairFindings(mapping[size_t(foreground)],
            focus ? ColorRole::content : mapping[size_t(background)], path, focus);
    }
    m_controlFindings.emplace(mapping, findings);
    return findings;
}

std::shared_ptr<const StateTableData> PaletteState::stateTable(ControlFamily family,
    Presentation::Tone tone, Presentation::Validation validation) const
{
    const size_t t = tone == Presentation::Primary ? 1 : tone == Presentation::Destructive ? 2
        : tone == Presentation::Caution ? 3 : 0;
    auto &table = m_stateTables[(size_t(family) * 4 + t) * 3 + size_t(validation)];
    if (!table) {
        AppearanceTrace::work(AppearanceWork::StateTableBuild);
        const auto mapping = controlColorMapping(family, tone, validation, dark);
        StateTableRecipe recipe;
        recipe.family = family;
        recipe.target = target;
        recipe.hoverBlend = hoverBlend;
        recipe.pressedBlend = pressedBlend;
        recipe.accent = color(ColorRole::accent);
        recipe.onAccent = color(ColorRole::onAccent);
        recipe.coupleBorder = family != ControlFamily::MenuItem && t != 0 && !familyDescriptor(family).usesSemanticText(tone)
            && validation == Presentation::None && !explicitRole(mapping[size_t(ControlColor::borderColor)]);
        ControlColorValues values;
        for (size_t i = 0; i < ControlColorCount; ++i) {
            values.set(ControlColor(i), color(mapping[i]));
            recipe.owned.set(i, explicitRole(mapping[i]));
        }
        if (family == ControlFamily::Choice) {
            // Choice keeps its separately derived accent center recipe.
            recipe.owned.reset(size_t(ControlColor::selectedBackgroundColor));
            recipe.owned.reset(size_t(ControlColor::inactiveBackgroundColor));
        }
        // An explicit semantic state also owns its action alias unless the
        // caller supplied that action independently.
        for (const auto pair : {std::pair{ColorRole::primaryHover, ColorRole::accentHover},
                 std::pair{ColorRole::primaryPressed, ColorRole::accentPressed},
                 std::pair{ColorRole::destructiveHover, ColorRole::errorHover},
                 std::pair{ColorRole::destructivePressed, ColorRole::errorPressed},
                 std::pair{ColorRole::cautionHover, ColorRole::warningHover},
                 std::pair{ColorRole::cautionPressed, ColorRole::warningPressed}}) {
            for (size_t i = 0; i < ControlColorCount; ++i)
                if (mapping[i] == pair.first && explicitRole(pair.second))
                    recipe.owned.set(i);
        }
        StateTableSharedColors sharedColors;
        if (family == ControlFamily::TabBar)
            sharedColors[size_t(StateTableColor::TabCanvas)] = color(ColorRole::canvas);
        if (family == ControlFamily::MenuItem) {
            sharedColors[size_t(StateTableColor::MenuPopup)] = ColorMath::composite(color(ColorRole::popup), backdrop);
            values.set(ControlColor::borderColor,
                stateTable(ControlFamily::Button, Presentation::Primary, Presentation::None)
                    ->states[size_t(Presentation::Rest)].borderColor());
        }
        if (family == ControlFamily::Field) {
            sharedColors[size_t(StateTableColor::Placeholder)] = color(ColorRole::placeholderText);
            sharedColors[size_t(StateTableColor::Selection)] = color(ColorRole::accent);
            sharedColors[size_t(StateTableColor::SelectionText)] = color(ColorRole::onAccent);
        }
        if (family == ControlFamily::Label)
            sharedColors[size_t(StateTableColor::Link)] = color(ColorRole::link);
        if (family == ControlFamily::Choice) {
            sharedColors[size_t(StateTableColor::ChoiceTrack)] = color(ColorRole::control);
            sharedColors[size_t(StateTableColor::ChoiceDisabledTrack)] = color(ColorRole::disabledSurface);
            sharedColors[size_t(StateTableColor::ChoiceGap)] = color(ColorRole::transparent);
        }
        if (family == ControlFamily::Slider)
            sharedColors[size_t(StateTableColor::ValueRemainder)] = color(ColorRole::controlPressed);
        if (family == ControlFamily::ScrollBar) {
            sharedColors[size_t(StateTableColor::ScrollHover)] = color(ColorRole::outline);
            sharedColors[size_t(StateTableColor::ScrollPressed)] = color(ColorRole::secondaryText);
        }
        // Transparent tabs are painted on their group's track, not content.
        // The group table finalizes that shared neutral surface only once.
        const QColor stateBackdrop = family == ControlFamily::Tab
            ? ColorMath::composite(stateTable(ControlFamily::TabBar, Presentation::Neutral,
                Presentation::None)->values.value(ControlColor::backgroundColor), backdrop) : backdrop;
        // A position/extent thumb is not text. Do not apply text readability
        // diagnostics to its deliberately quiet neutral role.
        const auto findings = familyDescriptor(family).textContrast ? controlFindings(mapping)
            : std::make_shared<const ControlFindings>();
        table = LocalColorOverrides::buildStateTable(values, findings, sharedColors,
            stateBackdrop, family == ControlFamily::Field && validation == Presentation::Invalid, recipe);
    }
    return table;
}

PaletteUpdate PaletteUpdate::between(const AppearanceStatePtr &before, const AppearanceStatePtr &after)
{
    PaletteUpdate update{before, after, {}, {}};
    if (before->palette != after->palette) {
        for (size_t i = 0; i < ColorRoleCount; ++i) {
            update.changedRoles.set(i, before->palette->colors[i] != after->palette->colors[i]);
            update.changedPresence.set(i, before->palette->presence.test(i) != after->palette->presence.test(i));
        }
    }
    const auto &old = before->snapshot;
    const auto &next = after->snapshot;
    if (old.density != next.density || !(before->metrics == after->metrics))
        update.changedGroups |= MetricsGroup;
    if (!(before->typography == after->typography))
        update.changedGroups |= TypographyGroup;
    if (old.tokens.value(QStringLiteral("motion")) != next.tokens.value(QStringLiteral("motion")))
        update.changedGroups |= MotionGroup;
    if (old.tokens.value(QStringLiteral("icons")) != next.tokens.value(QStringLiteral("icons")))
        update.changedGroups |= IconsGroup;
    return update;
}

std::shared_ptr<const PaletteState> PaletteState::calculate(const ResolvedAppearance &prepared,
    const std::shared_ptr<const PaletteState> &previous, const void *owner, quint64 revision)
{
    Q_UNUSED(revision);
    TraceSpan span(TraceScope::PaletteBuild, owner, AppearanceWork::SharedPaletteBuild);
    auto input = prepared;
    input.diagnostics.clear();
    const auto output = PaletteBuilder::calculatePalette(std::move(input));
    auto state = std::make_shared<PaletteState>();
    state->values = output.tokens.value(QStringLiteral("colors")).toMap();
    state->diagnostics = output.diagnostics;
#define IA_INSTALL_COLOR(name, light, dark) \
    state->colors[size_t(ColorRole::name)] = state->values.value(QStringLiteral(#name)).value<QColor>(); \
    state->presence.set(size_t(ColorRole::name), prepared.explicitTokens.contains(QStringLiteral("colors." #name)));
    IA_COLOR_TOKENS(IA_INSTALL_COLOR)
#undef IA_INSTALL_COLOR
    state->backdrop = ColorMath::composite(state->color(ColorRole::content), state->color(ColorRole::canvas));
    state->dark = prepared.scheme == QLatin1String("dark");
    state->target = prepared.contrast == QLatin1String("high") ? 7 : 5;
    const auto motion = prepared.tokens.value(QStringLiteral("motion")).toMap();
    state->hoverBlend = motion.value(QStringLiteral("hoverBlend")).toDouble();
    state->pressedBlend = motion.value(QStringLiteral("pressedBlend")).toDouble();
    const auto addPair = [&](ColorRole foreground, ColorRole background, bool focus = false) {
        const auto descriptorMatches = [&](const DiagnosticPair &pair) {
            return pair.foreground == foreground && pair.background == background && pair.focus == focus;
        };
        if (std::any_of(state->pairs.cbegin(), state->pairs.cend(), descriptorMatches))
            return;
        DiagnosticPair pair{foreground, background, focus, {}};
        const qreal required = focus ? 3 : state->target;
        bool reused = false;
        if (previous && previous->backdrop == state->backdrop && (focus || previous->target == state->target)
            && previous->color(foreground) == state->color(foreground)
            && previous->color(background) == state->color(background)) {
            const auto old = std::find_if(previous->pairs.cbegin(), previous->pairs.cend(), descriptorMatches);
            if (old != previous->pairs.cend()) {
                pair.findings = old->findings;
                reused = true;
            }
        }
        if (!reused) {
            AppearanceTrace::counter(TraceScope::PaletteBuild, owner, 2,
                AppearanceWork::SharedPairMeasurement); // measured pair
            TokenValidation::checkContrast(&pair.findings, {}, state->color(foreground),
                focus ? state->backdrop : ColorMath::composite(state->color(background), state->backdrop), required);
        } else {
            AppearanceTrace::counter(TraceScope::PaletteBuild, owner, 1); // reused pair
        }
        state->pairs.append(std::move(pair));
    };
    for (size_t f = 0; f < ControlFamilyCount; ++f) {
        for (const auto tone : {Presentation::Neutral, Presentation::Primary, Presentation::Destructive, Presentation::Caution}) {
            const auto roles = controlColorMapping(ControlFamily(f), tone, Presentation::None, state->dark);
            for (const auto surface : {ControlColor::backgroundColor, ControlColor::hoverBackgroundColor,
                     ControlColor::pressedBackgroundColor})
                addPair(roles[size_t(ControlColor::foregroundColor)], roles[size_t(surface)]);
            addPair(roles[size_t(ControlColor::selectedForegroundColor)], roles[size_t(ControlColor::selectedBackgroundColor)]);
            addPair(roles[size_t(ControlColor::inactiveForegroundColor)], roles[size_t(ControlColor::inactiveBackgroundColor)]);
        }
    }
    addPair(ColorRole::focus, ColorRole::content, true);
    return state;
}

const std::shared_ptr<const PaletteState> &PaletteState::fallback()
{
    // Pure constants only. Font/application/engine state is deliberately absent.
    static const auto state = calculate(PaletteBuilder::prepare({}, QStringLiteral("Sans Serif")), {}, nullptr, 0);
    return state;
}

} // namespace InterActive
