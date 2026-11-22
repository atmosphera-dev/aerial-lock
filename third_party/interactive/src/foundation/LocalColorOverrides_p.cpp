// SPDX-License-Identifier: LGPL-3.0-or-later
#include "LocalColorOverrides_p.h"
#include "ColorMath_p.h"
#include "TokenValidation_p.h"
#include "AppearanceTrace_p.h"
#include "cpp/cam/cam.h"
#include <QHash>
#include <algorithm>
#include <cmath>
#include <limits>

namespace InterActive {

QColor LocalColorOverrides::Branch::evaluate(const BranchInputs &next,
    const std::function<QColor()> &calculate, const char *operation, const void *owner, quint64 revision)
{
    Q_UNUSED(operation);
    Q_UNUSED(owner);
    Q_UNUSED(revision);
    if (!valid || !(inputs == next)) {
        const TraceAggregate aggregated(TraceScope::AggLocalDerivation, 0, AppearanceWork::LocalDerivation);
        result = calculate();
        inputs = next;
        valid = true;
    }
    return result;
}

std::shared_ptr<const StateTableData> LocalColorOverrides::buildStateTable(
    const ControlColorValues &input, std::shared_ptr<const ControlFindings> findings,
    const StateTableSharedColors &sharedColors, const QColor &backdrop, bool invalidFocus,
    const StateTableRecipe &recipe)
{
    auto table = std::make_shared<StateTableData>();
    table->sourceValues = input;
    table->sourceFindings = findings;
    table->recipe = recipe;
    table->sharedColors = sharedColors;
    table->backdrop = backdrop;
    table->invalidFocus = invalidFocus;
    auto values = input;
    std::shared_ptr<ControlFindings> changedFindings;
    const auto finding = [&](size_t pair, const QVariantMap &diagnostic) {
        if (!changedFindings)
            changedFindings = std::make_shared<ControlFindings>(*findings);
        (*changedFindings)[pair].append(diagnostic);
    };
    namespace mcu = material_color_utilities;
    const QColor canvas = ColorMath::composite(backdrop, QColor(Qt::white));
    const auto visible = [&](const QColor &fill) { return ColorMath::composite(fill, canvas); };
    const auto cam = [&](const QColor &fill) { return mcu::CamFromInt(visible(fill).rgb()); };
    const auto readable = [&](const QColor &fill, const QColor &text) {
        const QColor surface = visible(fill);
        return ColorMath::contrastRatio(text, surface) >= recipe.target
            && ColorMath::contrastRatio(QColor::fromRgba(text.rgba()), QColor::fromRgb(surface.rgb())) >= recipe.target;
    };
    // Finite, palette-time search only. Cache the reservation CAMs for this
    // search; runtime interaction merely indexes the completed array below.
    const auto feedback = [&](const QColor &base, const QColor &text, qreal strength,
                              const QList<QColor> &reserved, const QString &path, size_t pair,
                              bool retainBase = false, bool report = true, const QColor &surround = {}, bool weaken = true) {
        const QColor feedbackCanvas = surround.isValid() ? ColorMath::composite(surround, QColor(Qt::white)) : canvas;
        const auto feedbackVisible = [&](const QColor &fill) { return ColorMath::composite(fill, feedbackCanvas); };
        const auto feedbackCam = [&](const QColor &fill) { return mcu::CamFromInt(feedbackVisible(fill).rgb()); };
        const auto feedbackReadable = [&](const QColor &fill) {
            const QColor surface = feedbackVisible(fill);
            return ColorMath::contrastRatio(text, surface) >= recipe.target
                && ColorMath::contrastRatio(QColor::fromRgba(text.rgba()), QColor::fromRgb(surface.rgb())) >= recipe.target;
        };
        QList<mcu::Cam> reservations;
        for (const auto &fill : reserved)
            reservations.append(feedbackCam(fill));
        const auto separation = [&](const QColor &fill) {
            const auto candidate = feedbackCam(fill);
            qreal minimum = std::numeric_limits<qreal>::infinity();
            for (const auto &other : reservations)
                minimum = std::min(minimum, std::sqrt(std::pow(candidate.jstar - other.jstar, 2)
                    + std::pow(candidate.astar - other.astar, 2) + std::pow(candidate.bstar - other.bstar, 2)));
            return minimum;
        };
        QColor best = base;
        qreal bestDistance = separation(base);
        bool bestReadable = feedbackReadable(base);
        if (retainBase && bestDistance >= 4 && bestReadable)
            return base;
        const QColor toward = ColorMath::composite(text, feedbackVisible(base));
        const QColor away = ColorMath::contrastRatio(text, QColor(Qt::white))
                >= ColorMath::contrastRatio(text, QColor(Qt::black)) ? QColor(Qt::white) : QColor(Qt::black);
        // Prefer the label direction, then the contrast-preserving endpoint.
        // Nine fixed proposals per direction, token strength through 3x.
        for (const QColor &endpoint : {toward, away}) {
            for (int step = 0; step <= 8; ++step) {
                const QColor candidate = ColorMath::blend(feedbackVisible(base), endpoint, strength * (1 + step / qreal(4)));
                if (!feedbackReadable(candidate))
                    continue;
                const qreal distance = separation(candidate);
                if (distance >= 4)
                    return candidate;
                if (!bestReadable || distance > bestDistance) {
                    best = candidate;
                    bestDistance = distance;
                    bestReadable = true;
                }
            }
        }
        if (weaken && strength > 0) {
            // Enumerate the union of actual Qt RGB8 channel transitions below
            // 1x. Linear blending on this opaque backdrop makes each channel
            // monotone: at most 765 boundaries / 766 constant-color intervals.
            // Keep both representable sides of each boundary, not uniform
            // samples that can miss interleaved channel transitions.
            struct Transition { qreal before, after; };
            QHash<QRgb, qreal> distances;
            const QColor origin = feedbackVisible(base);
            const qreal limit = std::min(strength, qreal(1));
            const qreal first = std::nextafter(qreal(0), limit);
            const qreal last = std::nextafter(limit, qreal(0));
            for (const QColor &endpoint : {toward, away}) {
                const auto proposal = [&](qreal amount) { return ColorMath::blend(origin, endpoint, amount); };
                const auto measured = [&](qreal amount) { return feedbackVisible(proposal(amount)).rgb(); };
                const auto channel = [](QRgb rgb, int index) {
                    return index == 0 ? qRed(rgb) : index == 1 ? qGreen(rgb) : qBlue(rgb);
                };
                QList<Transition> transitions;
                const QRgb start = measured(first), finish = measured(last);
                for (int index = 0; index < 3; ++index) {
                    const int from = channel(start, index), to = channel(finish, index);
                    const int direction = to > from ? 1 : -1;
                    for (int level = from; level != to;) {
                        level += direction;
                        qreal low = first, high = last;
                        for (int iteration = 0; iteration < 64; ++iteration) {
                            const qreal middle = low + (high - low) / 2;
                            const int value = channel(measured(middle), index);
                            if (direction > 0 ? value >= level : value <= level)
                                high = middle;
                            else
                                low = middle;
                        }
                        transitions.append({low, high});
                    }
                }
                std::sort(transitions.begin(), transitions.end(), [](const auto &a, const auto &b) {
                    return a.after < b.after;
                });
                transitions.erase(std::unique(transitions.begin(), transitions.end(), [](const auto &a, const auto &b) {
                    return a.after == b.after;
                }), transitions.end());
                const auto interval = [&](qreal weak, qreal strong) -> QColor {
                    if (weak > strong || measured(weak) != measured(strong))
                        return {}; // Never retry a non-monotone/mismatched interval.
                    const QRgb rgb = measured(strong);
                    auto distance = distances.constFind(rgb);
                    if (distance == distances.cend())
                        distance = distances.insert(rgb, separation(proposal(strong)));
                    if (distance.value() < 4)
                        return {};
                    for (qreal amount : {strong, weak}) {
                        const QColor candidate = proposal(amount);
                        if (feedbackReadable(candidate))
                            return candidate;
                    }
                    return {};
                };
                qreal strong = last;
                for (auto boundary = transitions.crbegin(); boundary != transitions.crend(); ++boundary) {
                    const QColor candidate = interval(boundary->after, strong);
                    if (candidate.isValid())
                        return candidate;
                    strong = boundary->before;
                }
                const QColor candidate = interval(first, strong);
                if (candidate.isValid())
                    return candidate;
            }
        }
        if (report) {
            auto diagnostic = TokenValidation::diagnostic(path, QStringLiteral("low-feedback"),
                QStringLiteral("Bounded state feedback could not satisfy both distinction and readability; retained a best-effort fill."));
            diagnostic.insert(QStringLiteral("distance"), bestDistance);
            diagnostic.insert(QStringLiteral("target"), 4.0);
            finding(pair, diagnostic);
        }
        return best;
    };
    const auto owned = [&](ControlColor property) { return recipe.owned.test(size_t(property)); };
    const auto put = [&](ControlColor property, const QColor &color) { values.set(property, color); };
    const auto diagnoseOwned = [&](ControlColor property, const QList<QColor> &reserved, size_t pair) {
        if (!owned(property))
            return;
        const auto candidate = cam(values.value(property));
        for (const auto &fill : reserved) {
            const auto other = cam(fill);
            const qreal distance = std::sqrt(std::pow(candidate.jstar - other.jstar, 2)
                + std::pow(candidate.astar - other.astar, 2) + std::pow(candidate.bstar - other.bstar, 2));
            if (distance >= 4)
                continue;
            auto diagnostic = TokenValidation::diagnostic(controlColorKey(property), QStringLiteral("low-feedback"),
                QStringLiteral("Explicit state color is retained below the CAM16-UCS distance target."));
            diagnostic.insert(QStringLiteral("distance"), distance);
            diagnostic.insert(QStringLiteral("target"), 4.0);
            finding(pair, diagnostic);
            break;
        }
    };
    const bool interactive = familyDescriptor(recipe.family).interactive;
    const bool selection = recipe.family == ControlFamily::Button || recipe.family == ControlFamily::Tool
        || recipe.family == ControlFamily::Delegate || recipe.family == ControlFamily::Tab
        || recipe.family == ControlFamily::Choice;
    if (recipe.family == ControlFamily::TabBar && !owned(ControlColor::backgroundColor)) {
        // The track separates its raised segment from the effective canvas.
        // Only this search uses that surround; text diagnostics keep backdrop.
        put(ControlColor::backgroundColor, feedback(values.value(ControlColor::backgroundColor),
            values.value(ControlColor::foregroundColor), recipe.hoverBlend,
            {values.value(ControlColor::selectedBackgroundColor),
                ColorMath::composite(sharedColors[size_t(StateTableColor::TabCanvas)], QColor(Qt::white))},
            QStringLiteral("tabTrackColor"), 0, true, true, sharedColors[size_t(StateTableColor::TabCanvas)]));
    }
    const QColor rest = values.value(ControlColor::backgroundColor);
    QColor text = values.value(ControlColor::foregroundColor);
    QList<QColor> hoverReservations{rest};
    if (recipe.family == ControlFamily::Tab)
        hoverReservations.append(values.value(ControlColor::selectedBackgroundColor));
    if (interactive) {
        if (!owned(ControlColor::foregroundColor) && !owned(ControlColor::hoverBackgroundColor)) {
            const QColor initial = feedback(rest, text, recipe.hoverBlend, hoverReservations, {}, 1, false, false, {}, false);
            const auto a = cam(rest), b = cam(initial);
            const qreal distance = std::sqrt(std::pow(a.jstar - b.jstar, 2)
                + std::pow(a.astar - b.astar, 2) + std::pow(a.bstar - b.bstar, 2));
            if (distance < 4) {
                // Finalize an unowned pair jointly when a floor-edge label
                // leaves no feedback headroom (including, but not limited to,
                // endpoint fills). Existing owned state surfaces constrain it.
                const QColor strongest = ColorMath::contrastRatio(QColor(Qt::black), visible(rest))
                    >= ColorMath::contrastRatio(QColor(Qt::white), visible(rest)) ? QColor(Qt::black) : QColor(Qt::white);
                QList<QColor> surfaces{rest, ColorMath::blend(visible(rest), strongest, 3 * recipe.hoverBlend)};
                if (owned(ControlColor::pressedBackgroundColor))
                    surfaces.append(values.value(ControlColor::pressedBackgroundColor));
                const QColor label = ColorMath::contrastingText(text, surfaces, canvas, LabelContrastTarget);
                if (std::all_of(surfaces.cbegin(), surfaces.cend(), [&](const QColor &fill) { return readable(fill, label); })) {
                    text = label;
                    put(ControlColor::foregroundColor, text);
                }
            }
        }
        if (!owned(ControlColor::hoverBackgroundColor))
            put(ControlColor::hoverBackgroundColor, feedback(rest, text, recipe.hoverBlend,
                hoverReservations, QStringLiteral("hoverBackgroundColor"), 1));
        diagnoseOwned(ControlColor::hoverBackgroundColor, {rest}, 1);
        // Inset consumers still select rest/hover during press. The button
        // family also supplies a real pressed fill to slider thumbs.
        if (!owned(ControlColor::pressedBackgroundColor))
            put(ControlColor::pressedBackgroundColor, feedback(rest, text, recipe.pressedBlend,
                {rest}, QStringLiteral("pressedBackgroundColor"), 2));
        diagnoseOwned(ControlColor::pressedBackgroundColor, {rest}, 2);
    }
    if (recipe.family == ControlFamily::Choice) {
        // A choice owns a small accent center, not a neutral navigation band.
        // Resolve its light pair once, preserving explicit local/application pairs.
        for (const auto pair : {std::pair{ControlColor::selectedBackgroundColor, ControlColor::selectedForegroundColor},
                 std::pair{ControlColor::inactiveBackgroundColor, ControlColor::inactiveForegroundColor}}) {
            if (!owned(pair.first) && !owned(pair.second))
                put(pair.second, QColor(Qt::white));
            if (!owned(pair.first))
                put(pair.first, ColorMath::contrastingText(recipe.accent,
                    {values.value(pair.second)}, canvas, LabelContrastTarget));
        }
    }
    QColor selectedHover = values.value(ControlColor::selectedBackgroundColor);
    if (selection) {
        const QColor hover = values.value(ControlColor::hoverBackgroundColor);
        if (!owned(ControlColor::selectedBackgroundColor))
            put(ControlColor::selectedBackgroundColor, feedback(selectedHover,
                values.value(ControlColor::selectedForegroundColor), recipe.pressedBlend,
                {rest, hover}, QStringLiteral("selectedBackgroundColor"), 3, true));
        const QColor selected = values.value(ControlColor::selectedBackgroundColor);
        selectedHover = feedback(selected, values.value(ControlColor::selectedForegroundColor), recipe.hoverBlend,
            {rest, hover, selected}, QStringLiteral("selectedHoverBackgroundColor"), 3);
        diagnoseOwned(ControlColor::selectedBackgroundColor, {rest, hover}, 3);
    }
    if (recipe.family == ControlFamily::MenuItem) {
        // The mapped accent/onAccent pair is already finished. Sparse local
        // selected/inactive pairs are installed before this common builder.
        selectedHover = values.value(ControlColor::selectedBackgroundColor);
    }
    if (rest.alpha() == 0 && !owned(ControlColor::disabledBackgroundColor))
        put(ControlColor::disabledBackgroundColor, QColor(Qt::transparent));
    if (recipe.coupleBorder && !owned(ControlColor::borderColor))
        put(ControlColor::borderColor, values.value(ControlColor::foregroundColor));

    // Project diagnostics for the actual generated pairs, not the old palette
    // seeds. Unchanged and explicitly owned pairs reuse their existing findings.
    for (size_t i = 0; familyDescriptor(recipe.family).textContrast && i < ControlDiagnosticPairs.size() - 1; ++i) {
        const auto [foreground, background] = ControlDiagnosticPairs[i];
        if (values.value(background) == input.value(background) && values.value(foreground) == input.value(foreground))
            continue;
        if (!changedFindings)
            changedFindings = std::make_shared<ControlFindings>(*findings);
        QVariantList contrast;
        TokenValidation::checkContrast(&contrast, controlColorKey(foreground) + QLatin1Char('/') + controlColorKey(background),
            values.value(foreground), visible(values.value(background)), recipe.target);
        // Keep feedback warnings already added to this pair.
        auto &pair = (*changedFindings)[i];
        for (qsizetype j = pair.size(); j-- > 0;)
            if (pair[j].toMap().value(QStringLiteral("code")) == QStringLiteral("low-contrast"))
                pair.removeAt(j);
        pair.append(contrast);
    }
    table->values = values;
    const bool choice = recipe.family == ControlFamily::Choice;
    const QColor on = values.value(ControlColor::selectedBackgroundColor);
    const QColor onMark = values.value(ControlColor::selectedForegroundColor);
    const QColor onPressed = choice ? feedback(on, onMark, recipe.pressedBlend,
        {on}, QStringLiteral("choicePressedCenterColor"), 3) : QColor();
    const QColor offTrack = owned(ControlColor::backgroundColor) ? rest
        : sharedColors[size_t(StateTableColor::ChoiceTrack)];
    const QColor offHover = !choice ? QColor() : owned(ControlColor::hoverBackgroundColor)
        ? values.value(ControlColor::hoverBackgroundColor) : feedback(offTrack, text, recipe.hoverBlend,
            {offTrack}, QStringLiteral("choiceHoverTrackColor"), 1);
    const QColor offPressed = !choice ? QColor() : owned(ControlColor::pressedBackgroundColor)
        ? values.value(ControlColor::pressedBackgroundColor) : feedback(offTrack, text, recipe.pressedBlend,
            {offTrack}, QStringLiteral("choicePressedTrackColor"), 2);
    const QColor disabledTrack = owned(ControlColor::disabledBackgroundColor)
        ? values.value(ControlColor::disabledBackgroundColor) : sharedColors[size_t(StateTableColor::ChoiceDisabledTrack)];
    const QColor disabledCenter = values.value(ControlColor::disabledForegroundColor);
    const QColor disabledMark = choice ? ColorMath::contrastingText(QColor(Qt::white),
        {disabledCenter}, canvas, LabelContrastTarget) : QColor();
    const bool slider = recipe.family == ControlFamily::Slider;
    const bool progress = recipe.family == ControlFamily::Progress;
    const bool scroll = recipe.family == ControlFamily::ScrollBar;
    const QColor scrollRest = values.value(ControlColor::foregroundColor);
    // Explicit thumb colors remain exact through interaction. The unowned
    // neutral companions are finished shared roles, never text feedback.
    const QColor scrollHover = owned(ControlColor::foregroundColor) ? scrollRest
        : sharedColors[size_t(StateTableColor::ScrollHover)];
    const QColor scrollPressed = owned(ControlColor::foregroundColor) ? scrollRest
        : sharedColors[size_t(StateTableColor::ScrollPressed)];
    const auto entry = [&](const QColor &fill, ControlColor foreground, bool disabled = false,
                           QColor center = {}, QColor track = {}, QColor scrollThumb = {}) {
        const QColor text = values.value(foreground);
        const QColor border = values.value(disabled ? ControlColor::disabledBorderColor : ControlColor::borderColor);
        const QColor focus = values.value(ControlColor::focusColor);
        const QColor surface = ColorMath::composite(choice ? sharedColors[size_t(StateTableColor::ChoiceGap)] : fill, backdrop);
        const QColor focusedBorder = invalidFocus ? border
            : ColorMath::contrastRatio(focus, surface) >= 3 ? focus : choice ? border : text;
        const QColor remainder = disabled ? values.value(ControlColor::disabledBackgroundColor)
            : slider ? sharedColors[size_t(StateTableColor::ValueRemainder)]
            : progress || scroll ? fill : values.value(ControlColor::borderColor);
        const QColor thumb = scroll ? (disabled ? values.value(ControlColor::disabledForegroundColor) : scrollThumb)
            : slider ? fill : QColor();
        const bool menuHighlight = recipe.family == ControlFamily::MenuItem && !disabled
            && (foreground == ControlColor::selectedForegroundColor || foreground == ControlColor::inactiveForegroundColor);
        const bool conditionalBorder = menuHighlight && ColorMath::contrastRatio(fill,
            sharedColors[size_t(StateTableColor::MenuPopup)]) < recipe.target;
        return StateColors(fill, values.value(foreground),
            border, focus,
            values.value(disabled ? ControlColor::disabledForegroundColor : ControlColor::fillColor),
            remainder,
            disabled ? values.value(ControlColor::disabledForegroundColor)
                     : sharedColors[size_t(StateTableColor::Placeholder)], focusedBorder,
             sharedColors[size_t(StateTableColor::ChoiceGap)], choice ? center : QColor(),
             !choice ? QColor() : disabled ? disabledMark
                 : foreground == ControlColor::inactiveForegroundColor ? values.value(foreground) : onMark,
              choice ? track : QColor(), thumb, conditionalBorder);
    };
    const auto ordinary = entry(rest, ControlColor::foregroundColor, false, on, offTrack, scrollRest);
    const auto hover = entry(values.value(ControlColor::hoverBackgroundColor), ControlColor::foregroundColor,
        false, selectedHover, offHover, scrollHover);
    const auto pressed = entry(values.value(ControlColor::pressedBackgroundColor), ControlColor::foregroundColor,
        false, onPressed, offPressed, scrollPressed);
    const auto selected = entry(on, ControlColor::selectedForegroundColor, false, on, offTrack, scrollRest);
    const auto inactive = entry(values.value(ControlColor::inactiveBackgroundColor), ControlColor::inactiveForegroundColor,
        false, values.value(ControlColor::inactiveBackgroundColor), offTrack, scrollRest);
    table->states = {ordinary, hover, pressed, selected,
        entry(selectedHover, ControlColor::selectedForegroundColor, false, selectedHover, offHover, scrollHover),
        recipe.family == ControlFamily::MenuItem ? selected : pressed,
        inactive, inactive,
        entry(values.value(ControlColor::disabledBackgroundColor), ControlColor::disabledForegroundColor,
            true, disabledCenter, disabledTrack)};
    // Choice feedback diagnostics above belong to the same immutable snapshot.
    table->findings = changedFindings ? changedFindings : std::move(findings);
    return table;
}

std::shared_ptr<const StateTableData> LocalColorOverrides::install(const PaletteState &palette,
    const ControlColorMapping &mapping, const std::shared_ptr<const StateTableData> &shared,
    const ControlColorValues &overrides, bool coupleBorder, const void *owner, quint64 revision)
{
    TraceSpan span(TraceScope::LocalInstall, owner);
    auto values = overrides;
    if (values.presence.none()) {
        m_resting.valid = m_hover.valid = m_pressed.valid = m_foreground.valid = m_selected.valid = m_inactive.valid = false;
        for (auto &pair : m_pairs)
            pair.valid = false;
        m_table.reset();
        return shared;
    }
    const auto name = [](ControlColor property) -> const QString & { return controlColorKey(property); };
    const auto explicitLocal = [&](ControlColor property) { return overrides.contains(property); };
    const auto effective = [&](ControlColor property) {
        return values.contains(property) ? values.value(property) : palette.color(mapping[size_t(property)]);
    };
    const auto put = [&](ControlColor property, const QColor &value) { values.set(property, value); };
    const QColor base = effective(ControlColor::backgroundColor);
    const QColor backdrop = shared->backdrop;
    const qreal target = palette.target;
    const qreal labelTarget = palette.labelTarget;
    const bool textContrast = familyDescriptor(shared->recipe.family).textContrast;
    const bool backgroundOwned = explicitLocal(ControlColor::backgroundColor);
    const bool foregroundOwned = explicitLocal(ControlColor::foregroundColor);
    if (backgroundOwned && textContrast) {
        const QColor resting = foregroundOwned ? effective(ControlColor::foregroundColor)
            : m_resting.evaluate({{base, backdrop}, labelTarget, 0, 1}, [&] {
                return ColorMath::contrastingText(base, {base}, backdrop, labelTarget);
            }, "resting-foreground", owner, revision);
        if (!explicitLocal(ControlColor::hoverBackgroundColor)) {
            put(ControlColor::hoverBackgroundColor, m_hover.evaluate({{base, resting, backdrop},
                labelTarget, palette.hoverBlend, quint8(foregroundOwned)}, [&] {
                    return ColorMath::feedbackSurface(base, resting, palette.hoverBlend);
                }, "hover-feedback", owner, revision));
        } else {
            m_hover.valid = false;
        }
        if (!explicitLocal(ControlColor::pressedBackgroundColor)) {
            put(ControlColor::pressedBackgroundColor, m_pressed.evaluate({{base, resting, backdrop},
                labelTarget, palette.pressedBlend, quint8(foregroundOwned)}, [&] {
                    return ColorMath::feedbackSurface(base, resting, palette.pressedBlend);
                }, "pressed-feedback", owner, revision));
        } else {
            m_pressed.valid = false;
        }
        if (foregroundOwned)
            m_resting.valid = false;
    } else {
        m_resting.valid = m_hover.valid = m_pressed.valid = false;
    }
    const bool hoverOwned = explicitLocal(ControlColor::hoverBackgroundColor);
    const bool pressedOwned = explicitLocal(ControlColor::pressedBackgroundColor);
    if (textContrast && !foregroundOwned && (backgroundOwned || hoverOwned || pressedOwned)) {
        const QColor hover = effective(ControlColor::hoverBackgroundColor);
        const QColor pressed = effective(ControlColor::pressedBackgroundColor);
        const quint8 ownership = quint8(backgroundOwned | (hoverOwned << 1) | (pressedOwned << 2));
        put(ControlColor::foregroundColor, m_foreground.evaluate({{base, hover, pressed, backdrop},
            labelTarget, 0, ownership}, [&] {
                return ColorMath::contrastingText(base, {base, hover, pressed}, backdrop, labelTarget);
            }, "ordinary-foreground", owner, revision));
    } else {
        m_foreground.valid = false;
    }
    const auto pairedText = [&](ControlColor background, ControlColor foreground, Branch &branch, const char *operation) {
        if (explicitLocal(background) && !explicitLocal(foreground)) {
            const QColor surface = effective(background);
            put(foreground, branch.evaluate({{surface, backdrop}, labelTarget, 0, 1}, [&] {
                return ColorMath::contrastingText(surface, {surface}, backdrop, labelTarget);
            }, operation, owner, revision));
        } else {
            branch.valid = false;
        }
    };
    pairedText(ControlColor::selectedBackgroundColor, ControlColor::selectedForegroundColor, m_selected, "selected-foreground");
    pairedText(ControlColor::inactiveBackgroundColor, ControlColor::inactiveForegroundColor, m_inactive, "inactive-foreground");
    if (coupleBorder && !explicitLocal(ControlColor::borderColor)
        && values.contains(ControlColor::foregroundColor))
        put(ControlColor::borderColor, effective(ControlColor::foregroundColor));
    if (explicitLocal(ControlColor::borderColor) && !explicitLocal(ControlColor::disabledBorderColor))
        put(ControlColor::disabledBorderColor, effective(ControlColor::borderColor));

    std::shared_ptr<ControlFindings> localFindings;
    for (size_t i = 0; textContrast && i < ControlDiagnosticPairs.size(); ++i) {
        const auto [foreground, background] = ControlDiagnosticPairs[i];
        const bool focus = i == 5;
        const QString path = name(foreground) + QLatin1Char('/') + (focus ? QStringLiteral("content") : name(background));
        const bool localText = values.contains(foreground);
        const bool localSurface = !focus && values.contains(background);
        const bool locallyMeasured = localText || localSurface;
        auto &pair = m_pairs[i];
        if (!locallyMeasured) {
            pair.valid = false;
            continue;
        }
        const QColor text = effective(foreground);
        const QColor surface = focus ? backdrop : effective(background);
        const qreal required = focus ? 3 : target;
        const PairInputs inputs{text, surface, backdrop, required, localText, localSurface};
        if (!pair.valid || !(pair.inputs == inputs)) {
            const TraceAggregate aggregated(TraceScope::AggLocalPairMeasure, quint16(i),
                AppearanceWork::LocalPairMeasurement);
            pair.findings.clear();
            TokenValidation::checkContrast(&pair.findings, path, text,
                focus ? backdrop : ColorMath::composite(surface, backdrop), required);
            pair.inputs = inputs;
            pair.valid = true;
        }
        if (!localFindings)
            localFindings = std::make_shared<ControlFindings>(*shared->sourceFindings);
        (*localFindings)[i] = pair.findings;
    }
    for (size_t i = 0; i < ControlColorCount; ++i) {
        if (!values.presence.test(i))
            values.set(ControlColor(i), shared->sourceValues.values[i]);
    }
    const std::shared_ptr<const ControlFindings> findings = localFindings ? localFindings : shared->sourceFindings;
    auto recipe = shared->recipe;
    recipe.owned |= overrides.presence;
    recipe.coupleBorder = coupleBorder && !explicitLocal(ControlColor::borderColor);
    if (!m_table || !(m_table->sourceValues == values)
        || !(m_table->recipe == recipe)
        || *m_table->sourceFindings != *findings
        || m_table->sharedColors != shared->sharedColors || m_table->backdrop != shared->backdrop
        || m_table->invalidFocus != shared->invalidFocus) {
        AppearanceTrace::work(AppearanceWork::PrivateStateTableBuild);
        m_table = buildStateTable(values, findings, shared->sharedColors, shared->backdrop, shared->invalidFocus, recipe);
    }
    return m_table;
}

} // namespace InterActive
