// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PaletteBuilder_p.h"
#include "ColorMath_p.h"
#include "TokenValidation_p.h"
#include "AirmailProfiles.h"
#include "AppearanceTrace_p.h"
#include "TokenDefinitions.h"
#include "cpp/cam/cam.h"
#include "cpp/cam/hct.h"
#include "cpp/utils/utils.h"
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>

namespace InterActive {
namespace {

namespace mcu = material_color_utilities;

bool explicitColor(const QSet<QString> &supplied, const QString &role)
{
    return supplied.contains(QStringLiteral("colors.") + role);
}

QString actionFor(const QString &role)
{
    return role == QLatin1String("accent") ? QStringLiteral("primary")
        : role == QLatin1String("error") ? QStringLiteral("destructive")
        : role == QLatin1String("warning") ? QStringLiteral("caution") : QString();
}

struct SemanticGroup {
    QString identity;
    QVariantMap colors;
    QSet<QString> dependent;
};

// One finalization path for both candidates and caller-owned semantic seeds.
// Dependency flags distinguish immutable pairs from pairs a candidate can repair.
SemanticGroup finalizeGroup(const QVariantMap &input, const QSet<QString> &supplied,
                            const QVariantMap &motion, const QString &role, const QColor &seed,
                            bool candidate, const QColor &canvas, const QColor &content,
                            qreal floor, qreal labelTarget)
{
    const TraceAggregate aggregated(TraceScope::AggFinalizeGroup, 0);
    SemanticGroup group{role, {}, {}};
    const auto color = [&](const QString &name) {
        return group.colors.contains(name) ? group.colors.value(name).value<QColor>() : input.value(name).value<QColor>();
    };
    const auto set = [&](const QString &name, const QColor &value, bool dependent) {
        group.colors.insert(name, explicitColor(supplied, name) ? input.value(name) : QVariant(value));
        if (dependent && !explicitColor(supplied, name))
            group.dependent.insert(name);
    };
    set(role, seed, candidate);
    const QString action = actionFor(role);
    if (!action.isEmpty()) {
        QString foreground = role;
        foreground[0] = foreground[0].toUpper();
        foreground.prepend(QStringLiteral("on"));
        const bool explicitText = explicitColor(supplied, foreground);
        QColor restingText = explicitText ? color(foreground)
            : ColorMath::contrastingText(color(role), {color(role)}, content, labelTarget);
        // The primary accent is exact even when absent/equal-to-default. Only
        // library-owned semantic companions may have their resting fill adjusted.
        if (candidate && color(role).alphaF() == 1 && restingText.alphaF() == 1
            && ColorMath::contrastRatio(restingText, color(role)) < floor) {
            set(role, ColorMath::contrastingText(color(role), {restingText}, content, floor + 0.1), true);
            if (!explicitText)
                restingText = ColorMath::contrastingText(color(role), {color(role)}, content, labelTarget);
        }
        for (const auto *state : {"Hover", "Pressed"}) {
            const QString strength = QLatin1String(state) == QLatin1String("Hover")
                ? QStringLiteral("hoverBlend") : QStringLiteral("pressedBlend");
            set(role + QLatin1String(state), ColorMath::feedbackSurface(color(role), restingText,
                motion.value(strength).toDouble()), candidate);
        }
        if (explicitText)
            set(foreground, color(foreground), false);
        else
            set(foreground, ColorMath::contrastingText(color(role),
                {color(role), color(role + QStringLiteral("Hover")), color(role + QStringLiteral("Pressed"))}, content, labelTarget), candidate);
        set(action + QStringLiteral("Surface"), color(role), group.dependent.contains(role));
        for (const auto *state : {"Hover", "Pressed"}) {
            const QString semanticState = role + QLatin1String(state);
            set(action + QLatin1String(state), color(semanticState), group.dependent.contains(semanticState));
        }
        const QString actionText = action + QStringLiteral("Text");
        if (explicitColor(supplied, actionText)) {
            set(actionText, color(actionText), false);
        } else if (!explicitText && color(action + QStringLiteral("Surface")) == color(role)
            && color(action + QStringLiteral("Hover")) == color(role + QStringLiteral("Hover"))
            && color(action + QStringLiteral("Pressed")) == color(role + QStringLiteral("Pressed"))) {
            // Aliases with identical effective states have the same derivation.
            // Reuse this call's result, including its full precision; no cache.
            set(actionText, color(foreground), candidate);
        } else {
            set(actionText, ColorMath::contrastingText(color(role),
                {color(action + QStringLiteral("Surface")), color(action + QStringLiteral("Hover")),
                 color(action + QStringLiteral("Pressed"))}, content, labelTarget), candidate);
        }
        set(action + QStringLiteral("Outline"), color(action + QStringLiteral("Text")),
            group.dependent.contains(action + QStringLiteral("Text")));
    }
    if (role != QLatin1String("accent")) {
        set(role + QStringLiteral("Surface"), ColorMath::blend(color(QStringLiteral("content")), color(role),
            motion.value(QStringLiteral("semanticBlend")).toDouble()), candidate);
        const QString messageText = role + QStringLiteral("Text");
        if (explicitColor(supplied, messageText))
            set(messageText, color(messageText), false);
        else
            set(messageText, ColorMath::contrastingText(color(role),
                {color(QStringLiteral("content")), color(role + QStringLiteral("Surface"))}, canvas, labelTarget), candidate);
    }
    return group;
}

struct PaletteFill {
    QString identity;
    QString role;
    QColor visible;
    mcu::Cam cam;
    bool dependent;
    bool resting;
    bool interactive;
};

PaletteFill paletteFill(const QString &identity, const QString &role, const QColor &color,
                        const QColor &content, bool dependent, bool resting, bool interactive)
{
    const QColor visible = QColor::fromRgb(ColorMath::composite(color, content).rgb());
    return {identity, role, visible, mcu::CamFromInt(visible.rgb()), dependent, resting, interactive};
}

QList<PaletteFill> groupFills(const SemanticGroup &group, const QSet<QString> &supplied, const QColor &content)
{
    QList<PaletteFill> fills;
    const auto append = [&](const QString &role, bool resting, bool interactive) {
        fills.append(paletteFill(group.identity, role, group.colors.value(role).value<QColor>(), content,
            group.dependent.contains(role), resting, interactive));
    };
    append(group.identity, true, false);
    const QString action = actionFor(group.identity);
    if (!action.isEmpty()) {
        append(action + QStringLiteral("Surface"), true, true);
        for (const auto *state : {"Hover", "Pressed"}) {
            append(action + QLatin1String(state), false, true);
            const QString semantic = group.identity + QLatin1String(state);
            if (explicitColor(supplied, semantic))
                append(semantic, false, true);
        }
    }
    return fills;
}

qreal fillDistance(const PaletteFill &a, const PaletteFill &b)
{
    return std::sqrt(std::pow(a.cam.jstar - b.cam.jstar, 2)
        + std::pow(a.cam.astar - b.cam.astar, 2) + std::pow(a.cam.bstar - b.cam.bstar, 2));
}

qreal separationTarget(const PaletteFill &a, const PaletteFill &b)
{
    if (a.identity == b.identity)
        return 0;
    if (a.resting && b.resting)
        return 12;
    return a.interactive && b.interactive ? 8 : 0;
}

struct CandidateChecks {
    bool separated = true;
    bool readable = true;
    bool readableFull = true;
    bool readableRgb8 = true;
    bool family = true;
    qreal minimumDistance = std::numeric_limits<qreal>::infinity();
    // Composited RGB8 tones of the reserved fills blocking a dependent fill:
    // nonzero separation target with final measured distance below it.
    QList<qreal> blockers;
};

bool chromaticFamily(const QColor &color, qreal anchor)
{
    const mcu::Hct actual(color.rgb());
    return actual.get_chroma() >= 8 && mcu::DiffDegrees(actual.get_hue(), anchor) <= 22;
}

CandidateChecks checkCandidate(const SemanticGroup &group, const QSet<QString> &supplied,
                              const QList<PaletteFill> &reserved, const QColor &canvas,
                              const QColor &content, qreal target, qreal anchor, bool neutral)
{
    const TraceAggregate aggregated(TraceScope::AggCheckCandidate, 0);
    CandidateChecks checks;
    const auto fills = groupFills(group, supplied, content);
    for (const auto &fill : fills) {
        if (fill.dependent) {
            checks.family &= neutral ? (fill.visible.red() == fill.visible.green() && fill.visible.green() == fill.visible.blue())
                : chromaticFamily(fill.visible, anchor);
        }
        for (const auto &fixed : reserved) {
            const qreal required = separationTarget(fill, fixed);
            // A fixed action surface can mask the candidate completely. Its
            // fixed collision is diagnosed once and never steers repair proposals.
            if (!fill.dependent || required == 0)
                continue;
            const qreal distance = fillDistance(fill, fixed);
            checks.minimumDistance = std::min(checks.minimumDistance, distance);
            if (distance < required) {
                checks.separated = false;
                // Repair proposals read the reserved fill's composited RGB8 tone.
                checks.blockers.append(mcu::LstarFromArgb(fixed.visible.rgb()));
            }
        }
    }
    const auto color = [&](const QString &name) { return group.colors.value(name).value<QColor>(); };
    const auto pair = [&](const QString &foreground, const QString &background, const QColor &surface) {
        if (!group.dependent.contains(foreground) && !group.dependent.contains(background))
            return;
        const QColor backdrop = background == QStringLiteral("content") || background == group.identity + QStringLiteral("Surface")
            ? canvas : content;
        const QColor visible = ColorMath::composite(surface, ColorMath::composite(backdrop, QColor(Qt::white)));
        const qreal full = ColorMath::contrastRatio(color(foreground), visible);
        const qreal rgb8 = ColorMath::contrastRatio(QColor::fromRgba(color(foreground).rgba()), QColor::fromRgb(visible.rgb()));
        checks.readableFull &= full >= target;
        checks.readableRgb8 &= rgb8 >= target;
        checks.readable &= checks.readableFull && checks.readableRgb8;
    };
    if (group.identity != QLatin1String("accent")) {
        pair(group.identity + QStringLiteral("Text"), QStringLiteral("content"), content);
        pair(group.identity + QStringLiteral("Text"), group.identity + QStringLiteral("Surface"), color(group.identity + QStringLiteral("Surface")));
    }
    const QString action = actionFor(group.identity);
    if (!action.isEmpty()) {
        QString foreground = group.identity;
        foreground[0] = foreground[0].toUpper();
        foreground.prepend(QStringLiteral("on"));
        for (const auto &state : {QString(), QStringLiteral("Hover"), QStringLiteral("Pressed")})
            pair(foreground, group.identity + state, color(group.identity + state));
        for (const auto *state : {"Surface", "Hover", "Pressed"})
            pair(action + QStringLiteral("Text"), action + QLatin1String(state), color(action + QLatin1String(state)));
    }
    return checks;
}

// Bounded direct recipe: one direct base proposal per branch at the analyzed
// accent tone, then at most two tone repairs derived from the direct proposal's
// frozen separation blockers. No hue rotation, no tone×chroma grid. Branch
// proposals are counted per role for the trial's three/six bound evidence.
SemanticGroup selectGroup(const QVariantMap &colors, const QSet<QString> &supplied, const QVariantMap &motion,
                          const QString &role, const mcu::Hct &seed, qreal chroma, qreal anchor,
                          const QList<PaletteFill> &reserved, const QColor &canvas, const QColor &content,
                          qreal floor, qreal labelTarget, QVariantList *diagnostics)
{
    const TraceSpan span(TraceScope::SelectGroup, nullptr);
    // A red-family primary occupies the danger family: danger stays true gray.
    const bool neutralDanger = role == QLatin1String("error") && chroma >= 12
        && mcu::DiffDegrees(seed.get_hue(), anchor) <= 20;
    const qreal seedTone = seed.get_tone();
    // The chroma floor retains semantic identity for gray/weakly chromatic
    // seeds; it is not a claim of exact chroma matching.
    const qreal requestChroma = std::max(chroma, qreal(12));
    const quint16 roleTag = role == QLatin1String("error") ? 1 : role == QLatin1String("warning") ? 2 : 3;

    struct Attempt {
        SemanticGroup group;
        CandidateChecks checks;
        qreal toneDistance = 0;     // final base-tone distance from the seed tone
        qreal actualChroma = 0;
        QRgb rgb = 0;               // final composited base RGB8
        bool valid = false;         // every acceptance gate passed
    };
    QList<Attempt> tried;
    SemanticGroup chosen;
    int proposals[2] = {0, 0};      // [chromatic, neutral]

    const auto requestColor = [&](bool neutral, qreal tone) {
        // Every request starts from the original H/C values, never from an
        // already gamut-mapped object whose chroma an earlier attempt reduced.
        return neutral ? QColor::fromRgb(mcu::IntFromLstar(tone))
                       : QColor::fromRgb(mcu::Hct(anchor, requestChroma, tone).ToInt());
    };
    const auto propose = [&](bool neutral, const QColor &base) {
        Attempt attempt;
        {
            const TraceAggregate proposal(TraceScope::AggPaletteProposal,
                                          quint16(roleTag * 2 + (neutral ? 1 : 0)));
            attempt.group = finalizeGroup(colors, supplied, motion, role, base, true, canvas, content, floor, labelTarget);
            attempt.checks = checkCandidate(attempt.group, supplied, reserved, canvas, content, floor, anchor, neutral);
        }
        ++proposals[neutral ? 1 : 0];
        const QColor finalBase = QColor::fromRgb(ColorMath::composite(
            attempt.group.colors.value(role).value<QColor>(), content).rgb());
        attempt.rgb = finalBase.rgb();
        attempt.toneDistance = std::abs(mcu::LstarFromArgb(attempt.rgb) - seedTone);
        attempt.actualChroma = mcu::Hct(attempt.rgb).get_chroma();
        attempt.valid = attempt.checks.family && attempt.checks.readable && attempt.checks.separated;
        tried.append(attempt);
    };
    // Closest final base tone to the seed tone; exact ties break to larger
    // actual base chroma, then lower RGB integer.
    const auto closer = [](const Attempt &a, const Attempt &b) {
        if (a.toneDistance != b.toneDistance)
            return a.toneDistance < b.toneDistance;
        if (a.actualChroma != b.actualChroma)
            return a.actualChroma > b.actualChroma;
        return a.rgb < b.rgb;
    };
    const auto branch = [&](bool neutral) {
        const QColor directRequest = requestColor(neutral, seedTone);
        propose(neutral, directRequest);
        const Attempt direct = tried.constLast();
        if (direct.valid) {
            chosen = direct.group;
            return true;
        }
        // Tone repairs read the direct proposal's frozen blocker range; with no
        // separation blockers they step a fixed 16 tones from the seed tone.
        qreal lower = seedTone - 16, upper = seedTone + 16;
        if (!direct.checks.blockers.isEmpty()) {
            const auto range = std::minmax_element(direct.checks.blockers.cbegin(), direct.checks.blockers.cend());
            lower = *range.first - 16;
            upper = *range.second + 16;
        }
        lower = std::clamp(lower, qreal(0), qreal(100));
        upper = std::clamp(upper, qreal(0), qreal(100));
        QSet<qreal> requestTones{seedTone};
        QSet<QRgb> generated{directRequest.rgb()};
        QList<int> feasible;
        for (const qreal tone : {lower, upper}) {
            // Duplicated request tones or generated RGB values buy no attempts.
            if (requestTones.contains(tone))
                continue;
            requestTones.insert(tone);
            const QColor request = requestColor(neutral, tone);
            if (generated.contains(request.rgb()))
                continue;
            generated.insert(request.rgb());
            propose(neutral, request);
            if (tried.constLast().valid)
                feasible.append(tried.size() - 1);
        }
        if (feasible.isEmpty())
            return false;
        int best = feasible.constFirst();
        for (const int index : feasible)
            if (closer(tried.at(index), tried.at(best)))
                best = index;
        chosen = tried.at(best).group;
        return true;
    };

    bool accepted = branch(neutralDanger);
    if (!accepted && !neutralDanger)
        accepted = branch(true);
    if (accepted) {
        AppearanceTrace::paletteRecipe({PaletteRecipeRole(roleTag - 1), proposals[0], proposals[1],
            int(tried.size()), false});
        return chosen;
    }

    // Exhaustion is not rejection: retain a deterministic best-effort attempt
    // (family-valid first, then maximum minimum separation, then the declared
    // final-tone/chroma/RGB tie-break) and report the bounded failure honestly.
    Q_ASSERT(!tried.isEmpty());
    int best = 0;
    for (int i = 1; i < tried.size(); ++i) {
        const Attempt &a = tried.at(i), &b = tried.at(best);
        if (a.checks.family != b.checks.family) {
            if (a.checks.family)
                best = i;
        } else if (a.checks.minimumDistance != b.checks.minimumDistance) {
            if (a.checks.minimumDistance > b.checks.minimumDistance)
                best = i;
        } else if (closer(a, b)) {
            best = i;
        }
    }
    chosen = tried.at(best).group;
    QStringList failedGates;
    if (!tried.at(best).checks.family)
        failedGates.append(QStringLiteral("family"));
    if (!tried.at(best).checks.separated)
        failedGates.append(QStringLiteral("separated"));
    if (!tried.at(best).checks.readableFull)
        failedGates.append(QStringLiteral("readable-full"));
    if (!tried.at(best).checks.readableRgb8)
        failedGates.append(QStringLiteral("readable-rgb8"));
    const int maximum = neutralDanger ? 3 : 6;
    auto entry = TokenValidation::diagnostic(QStringLiteral("tokens.colors.") + role,
        QStringLiteral("palette-generation-exhausted"),
        QStringLiteral("Bounded palette generation tried %1 of at most %2 base proposals for %3 without "
                       "passing every gate; retained a deterministic best-effort candidate failing: %4. "
                       "These bounded proposals failed; this is not proof that no feasible palette exists.")
            .arg(tried.size()).arg(maximum).arg(role, failedGates.join(QStringLiteral(", "))));
    entry.insert(QStringLiteral("role"), role);
    entry.insert(QStringLiteral("tried"), tried.size());
    entry.insert(QStringLiteral("maximum"), maximum);
    entry.insert(QStringLiteral("failedGates"), failedGates);
    diagnostics->append(entry);
    AppearanceTrace::paletteRecipe({PaletteRecipeRole(roleTag - 1), proposals[0], proposals[1],
        int(tried.size()), true});
    return chosen;
}

void diagnoseCollisions(const QList<PaletteFill> &fills, QVariantList *diagnostics)
{
    QSet<QString> seen;
    for (qsizetype i = 0; i < fills.size(); ++i) {
        for (qsizetype j = i + 1; j < fills.size(); ++j) {
            const auto &a = fills[i], &b = fills[j];
            const qreal target = separationTarget(a, b);
            if (target == 0)
                continue;
            const qreal distance = fillDistance(a, b);
            if (distance >= target)
                continue;
            const QString key = a.identity + QLatin1Char('/') + b.identity + QLatin1Char('/')
                + QString::number(a.visible.rgb()) + QLatin1Char('/') + QString::number(b.visible.rgb());
            if (seen.contains(key))
                continue;
            seen.insert(key);
            const QString first = QStringLiteral("tokens.colors.") + a.role;
            const QString second = QStringLiteral("tokens.colors.") + b.role;
            auto entry = TokenValidation::diagnostic(first + QLatin1Char('/') + b.role, QStringLiteral("palette-collision"),
                QStringLiteral("Palette distinction distance %1 between %2 and %3 is below the %4 target; explicit colors are retained. This is a palette design measurement, not text contrast.")
                    .arg(distance, 0, 'f', 3).arg(first, second).arg(target));
            entry.insert(QStringLiteral("paths"), QStringList{first, second});
            entry.insert(QStringLiteral("distance"), distance);
            entry.insert(QStringLiteral("target"), target);
            diagnostics->append(entry);
        }
    }
}

} // namespace

ResolvedAppearance PaletteBuilder::prepare(const QVariantMap &input, const QString &fontFamily)
{
    auto result = TokenValidation::parseEnvelope(input);
    if (!result.accepted)
        return result;
    result.tokens = AirmailProfiles::profile(result.scheme, fontFamily);
    if (result.contrast == QLatin1String("high")) {
        const bool dark = result.scheme == QLatin1String("dark");
        auto colors = result.tokens.value(QStringLiteral("colors")).toMap();
        const QColor text(dark ? Qt::white : Qt::black);
        for (const auto *name : {"text", "secondaryText", "placeholderText", "outline", "separator"})
            colors.insert(QString::fromLatin1(name), text);
        colors.insert(QStringLiteral("selection"), QColor(dark ? "#363A40" : "#DDE2E7"));
        colors.insert(QStringLiteral("inactiveSelection"), QColor(dark ? "#303237" : "#E7EAEE"));
        result.tokens.insert(QStringLiteral("colors"), colors);
    }
    TokenValidation::applyTokens(result, input.value(QStringLiteral("tokens")).toMap());
    return result;
}

ResolvedAppearance PaletteBuilder::calculatePalette(ResolvedAppearance result)
{
    const TraceSpan span(TraceScope::PaletteCalculate, nullptr);
    // Hue anchors always come from the selected profile, before explicit input.
    const auto semanticHueSeeds = AirmailProfiles::profile(result.scheme, {}).value(QStringLiteral("colors")).toMap();
    auto colors = result.tokens.value(QStringLiteral("colors")).toMap();
    auto motion = result.tokens.value(QStringLiteral("motion")).toMap();
    const auto color = [&](const QString &name) { return colors.value(name).value<QColor>(); };
    const auto derive = [&](const QString &name, const QColor &value) {
        if (!result.explicitTokens.contains(QStringLiteral("colors.") + name))
            colors.insert(name, value);
    };
    const qreal target = result.contrast == QLatin1String("high") ? 7 : 5;
    const qreal labelTarget = LabelContrastTarget;
    const QColor canvas = color(QStringLiteral("canvas"));
    const QColor content = ColorMath::composite(color(QStringLiteral("content")), ColorMath::composite(canvas, QColor(Qt::white)));
    QMap<QString, SemanticGroup> finalGroups;
    QList<PaletteFill> reserved;
    const auto install = [&](const SemanticGroup &group) {
        for (auto it = group.colors.cbegin(); it != group.colors.cend(); ++it)
            colors.insert(it.key(), it.value());
        finalGroups.insert(group.identity, group);
        reserved.append(groupFills(group, result.explicitTokens, content));
    };
    install(finalizeGroup(colors, result.explicitTokens, motion, QStringLiteral("accent"),
        color(QStringLiteral("accent")), false, canvas, content, target, labelTarget));
    for (const auto *role : {"error", "warning", "success"}) {
        const QString name = QString::fromLatin1(role);
        if (explicitColor(result.explicitTokens, name))
            install(finalizeGroup(colors, result.explicitTokens, motion, name, color(name), false, canvas, content, target, labelTarget));
    }
    // Reserve state-only inputs from all still-implicit groups before selecting
    // danger: even a later caution hover participates, without using its placeholder base.
    for (const auto *role : {"error", "warning"}) {
        const QString name = QString::fromLatin1(role), action = actionFor(name);
        if (finalGroups.contains(name))
            continue;
        for (const auto &state : {name + QStringLiteral("Hover"), name + QStringLiteral("Pressed"),
                 action + QStringLiteral("Surface"), action + QStringLiteral("Hover"), action + QStringLiteral("Pressed")}) {
            if (explicitColor(result.explicitTokens, state))
                reserved.append(paletteFill(name, state, color(state), content, false,
                    state.endsWith(QLatin1String("Surface")), true));
        }
    }
    const QColor visibleAccent = QColor::fromRgb(ColorMath::composite(color(QStringLiteral("accent")), content).rgb());
    const mcu::Hct paletteSeed(visibleAccent.rgb());
    const qreal seedChroma = visibleAccent.red() == visibleAccent.green() && visibleAccent.green() == visibleAccent.blue()
        ? 0 : paletteSeed.get_chroma();
    for (const auto *role : {"error", "warning", "success"}) {
        const QString name = QString::fromLatin1(role);
        if (!finalGroups.contains(name)) {
            const qreal anchor = mcu::Hct(semanticHueSeeds.value(name).value<QColor>().rgb()).get_hue();
            install(selectGroup(colors, result.explicitTokens, motion, name, paletteSeed, seedChroma,
                anchor, reserved, canvas, content, target, labelTarget, &result.diagnostics));
        }
    }
    QList<PaletteFill> finalFills;
    for (const auto *role : {"accent", "error", "warning", "success"})
        finalFills.append(groupFills(finalGroups.value(QString::fromLatin1(role)), result.explicitTokens, content));
    diagnoseCollisions(finalFills, &result.diagnostics);
    if (result.explicitTokens.contains(QStringLiteral("colors.control"))) {
        derive(QStringLiteral("controlHover"), ColorMath::blend(color(QStringLiteral("control")), color(QStringLiteral("text")), motion.value(QStringLiteral("hoverBlend")).toDouble()));
        derive(QStringLiteral("controlPressed"), ColorMath::blend(color(QStringLiteral("control")), color(QStringLiteral("text")), motion.value(QStringLiteral("pressedBlend")).toDouble()));
    }
    if (!explicitColor(result.explicitTokens, QStringLiteral("focus"))) {
        // One accent-hued focus role must read on every neutral focus surface,
        // not just fields. Colored/local fills retain the state-table fallback.
        QList<QColor> focusSurfaces{content, ColorMath::composite(canvas, QColor(Qt::white))};
        for (const auto *role : {"popup", "control", "controlHover", "controlPressed",
                 "selection", "inactiveSelection"})
            focusSurfaces.append(ColorMath::composite(color(QString::fromLatin1(role)), content));
        derive(QStringLiteral("focus"), ColorMath::contrastingText(color(QStringLiteral("accent")),
            focusSurfaces, content, 3));
    }
    derive(QStringLiteral("accentText"), ColorMath::contrastingText(color(QStringLiteral("accent")), {color(QStringLiteral("content"))}, canvas, labelTarget));
    derive(QStringLiteral("link"), color(QStringLiteral("accentText")));
    result.tokens.insert(QStringLiteral("colors"), colors);

    const auto check = [&](const QString &foreground, const QString &background, qreal required, bool onContent = false) {
        const QColor backdrop = background == QLatin1String("canvas") ? QColor(Qt::white) : onContent ? content : canvas;
        TokenValidation::checkContrast(&result.diagnostics, QStringLiteral("tokens.colors.") + foreground + QLatin1Char('/') + background,
                      color(foreground), ColorMath::composite(color(background), backdrop), required);
    };
    for (const auto *surface : {"canvas", "content", "popup", "control", "controlHover", "controlPressed", "selection", "inactiveSelection"})
        check(QStringLiteral("text"), QString::fromLatin1(surface), target);
    for (const auto *text : {"secondaryText", "placeholderText", "link", "accentText", "errorText", "warningText", "successText"})
        check(QString::fromLatin1(text), QStringLiteral("content"), target);
    for (const auto *role : {"error", "warning", "success"}) {
        const QString name = QString::fromLatin1(role);
        check(name + QStringLiteral("Text"), name + QStringLiteral("Surface"), target);
    }
    for (const auto *background : {"accent", "accentHover", "accentPressed"})
        check(QStringLiteral("onAccent"), QString::fromLatin1(background), target, true);
    for (const auto *background : {"error", "errorHover", "errorPressed"})
        check(QStringLiteral("onError"), QString::fromLatin1(background), target, true);
    for (const auto *background : {"warning", "warningHover", "warningPressed"})
        check(QStringLiteral("onWarning"), QString::fromLatin1(background), target, true);
    for (const auto *role : {"primary", "destructive", "caution"}) {
        const QString name = QString::fromLatin1(role);
        for (const auto *state : {"Surface", "Hover", "Pressed"})
            check(name + QStringLiteral("Text"), name + QString::fromLatin1(state), target, true);
        check(name + QStringLiteral("Outline"), name + QStringLiteral("Surface"), 3, true);
    }
    check(QStringLiteral("tooltipText"), QStringLiteral("tooltipBackground"), target);
    check(QStringLiteral("inverseText"), QStringLiteral("inverseSurface"), target);
    check(QStringLiteral("inverseSecondary"), QStringLiteral("inverseSurface"), target);
    check(QStringLiteral("outline"), QStringLiteral("content"), 3);
    check(QStringLiteral("focus"), QStringLiteral("content"), 3);
    if (canvas.alphaF() < 1)
        result.diagnostics.append(TokenValidation::diagnostic(QStringLiteral("tokens.colors.canvas"), QStringLiteral("translucent-canvas"),
                                            QStringLiteral("Contrast uses white behind a translucent canvas; verify the actual host background.")));
    return result;
}

void PaletteBuilder::resolveNoncolors(ResolvedAppearance *prepared,
                                  const ResolvedAppearance *previousInput,
                                  const ResolvedAppearance *previousOutput)
{
    auto &result = *prepared;
    const auto unchanged = [&](const QString &group) {
        return previousInput && previousOutput
            && result.tokens.value(group) == previousInput->tokens.value(group);
    };
    const QString metricGroup = QStringLiteral("metrics");
    if (unchanged(metricGroup) && result.density == previousInput->density
        && result.contrast == previousInput->contrast) {
        result.tokens.insert(metricGroup, previousOutput->tokens.value(metricGroup));
    } else {
        auto metrics = result.tokens.value(metricGroup).toMap();
        if (result.density == QLatin1String("touch")) {
            for (const auto *role : {"controlHeight", "toolHeight", "rowHeight", "targetSize"}) {
                const QString name = QString::fromLatin1(role);
                metrics.insert(name, std::max(qreal(44), metrics.value(name).toDouble()));
            }
        }
        if (result.contrast == QLatin1String("high")) {
            metrics.insert(QStringLiteral("borderWidth"), std::max(qreal(2), metrics.value(QStringLiteral("borderWidth")).toDouble()));
            metrics.insert(QStringLiteral("focusWidth"), std::max(qreal(3), metrics.value(QStringLiteral("focusWidth")).toDouble()));
        }
        result.tokens.insert(metricGroup, metrics);
    }
    const QString typeGroup = QStringLiteral("typography");
    if (unchanged(typeGroup) && result.fontScale == previousInput->fontScale) {
        result.tokens.insert(typeGroup, previousOutput->tokens.value(typeGroup));
    } else {
        auto typography = result.tokens.value(typeGroup).toMap();
#define IA_SCALE_SIZE(name, defaultValue, min, max) typography.insert(QStringLiteral(#name), typography.value(QStringLiteral(#name)).toDouble() * result.fontScale);
        IA_TYPE_SIZE_TOKENS(IA_SCALE_SIZE)
#undef IA_SCALE_SIZE
        result.tokens.insert(typeGroup, typography);
    }
    const QString motionGroup = QStringLiteral("motion");
    if (unchanged(motionGroup) && result.reducedMotion == previousInput->reducedMotion) {
        result.tokens.insert(motionGroup, previousOutput->tokens.value(motionGroup));
    } else {
        auto motion = result.tokens.value(motionGroup).toMap();
        if (result.reducedMotion) {
            for (const auto *name : {"feedbackDuration", "popupDuration", "busyDuration"})
                motion.insert(QString::fromLatin1(name), 0);
        }
        result.tokens.insert(motionGroup, motion);
    }
    const QString iconGroup = QStringLiteral("icons");
    if (unchanged(iconGroup))
        result.tokens.insert(iconGroup, previousOutput->tokens.value(iconGroup));
}

} // namespace InterActive
