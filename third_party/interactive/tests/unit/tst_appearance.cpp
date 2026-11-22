// SPDX-License-Identifier: LGPL-3.0-or-later
#include "Appearance.h"
#include "AirmailProfiles.h"
#include "ControlAppearance.h"
#include "SeparatorAppearance.h"
#include "ScreenGeometry.h"
#include "TokenSchema.h"
#include "AppearanceTrace_p.h"
#include "TestTraceSetup.h"
#include "cpp/cam/cam.h"
#include "cpp/cam/hct.h"
#include "cpp/contrast/contrast.h"
#include "cpp/utils/utils.h"
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QList>
#include <QMetaEnum>
#include <QMetaProperty>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>
#include <functional>

using namespace InterActive;

namespace {

using WorkRecorder = AppearanceWorkRecorder;

QVariantMap tokenInput(const QString &group, const QVariantMap &values)
{
    return {{QStringLiteral("tokens"), QVariantMap{{group, values}}}};
}

// Project typed per-role recipe work into the existing assertion context.
class PaletteWorkRecorder {
public:
    struct Line {
        QString role;
        int chromatic = 0;
        int neutral = 0;
        int tried = 0;
        bool exhausted = false;
    };
    void clear() { work.clear(); }
    QList<Line> lines() const
    {
        QList<Line> result;
        for (const auto &entry : work.paletteRecipes()) {
            const char *role = entry.role == PaletteRecipeRole::Error ? "error"
                : entry.role == PaletteRecipeRole::Warning ? "warning" : "success";
            result.append({QString::fromLatin1(role), entry.chromatic, entry.neutral, entry.tried, entry.exhausted});
        }
        return result;
    }
private:
    AppearanceWorkRecorder work;
};

bool hasDiagnostic(const QVariantList &diagnostics, const QString &code, const QString &path = {})
{
    for (const auto &value : diagnostics) {
        const auto entry = value.toMap();
        if (entry.value(QStringLiteral("code")) == code
            && (path.isEmpty() || entry.value(QStringLiteral("path")) == path))
            return true;
    }
    return false;
}

QColor opaqueOver(const QColor &foreground, const QColor &background)
{
    const qreal alpha = foreground.alphaF();
    return QColor::fromRgbF(foreground.redF() * alpha + background.redF() * (1 - alpha),
                           foreground.greenF() * alpha + background.greenF() * (1 - alpha),
                           foreground.blueF() * alpha + background.blueF() * (1 - alpha));
}

// Independent sRGB oracle: do not validate the resolver with its own contrast math.
qreal rgbLuminance(const QColor &color)
{
    const auto linear = [](qreal channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

qreal rgbContrast(const QColor &foreground, const QColor &background, const QColor &canvas = QColor(Qt::white))
{
    const QColor surface = opaqueOver(background, opaqueOver(canvas, QColor(Qt::white)));
    const qreal a = rgbLuminance(opaqueOver(foreground, surface));
    const qreal b = rgbLuminance(surface);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

QColor rgbFeedback(const QColor &background, const QColor &foreground, qreal amount)
{
    const qreal endpoint = rgbContrast(foreground, QColor(Qt::white)) > rgbContrast(foreground, QColor(Qt::black)) ? 1 : 0;
    const auto mix = [amount](qreal from, qreal to) { return from + (to - from) * amount; };
    return QColor::fromRgbF(mix(background.redF(), endpoint), mix(background.greenF(), endpoint),
                           mix(background.blueF(), endpoint), mix(background.alphaF(), 1));
}

namespace mcu = material_color_utilities;

qreal visibleDistance(const QColor &a, const QColor &b, const QColor &backdrop)
{
    const auto first = mcu::CamFromInt(opaqueOver(a, backdrop).rgb());
    const auto second = mcu::CamFromInt(opaqueOver(b, backdrop).rgb());
    return std::sqrt(std::pow(first.jstar - second.jstar, 2)
        + std::pow(first.astar - second.astar, 2) + std::pow(first.bstar - second.bstar, 2));
}

// Independent RGB/compositing/CAM oracle for the bounded table-time recipe.
QColor stateFeedback(const QColor &base, const QColor &text, qreal amount, const QColor &backdrop,
                     qreal floor, QList<QColor> reserved = {}, bool retain = false)
{
    if (reserved.isEmpty())
        reserved.append(base);
    const auto distance = [&](const QColor &fill) {
        qreal result = std::numeric_limits<qreal>::infinity();
        for (const auto &other : reserved)
            result = std::min(result, visibleDistance(fill, other, backdrop));
        return result;
    };
    const auto readable = [&](const QColor &fill) {
        const QColor visible = opaqueOver(fill, backdrop);
        return rgbContrast(text, visible) >= floor
            && rgbContrast(QColor::fromRgba(text.rgba()), QColor::fromRgb(visible.rgb())) >= floor;
    };
    if (retain && readable(base) && distance(base) >= 4)
        return base;
    QColor best = base;
    qreal bestDistance = distance(base);
    bool bestReadable = readable(base);
    const QColor visible = opaqueOver(base, backdrop);
    const QColor toward = opaqueOver(text, visible);
    const QColor away(rgbContrast(text, QColor(Qt::white)) >= rgbContrast(text, QColor(Qt::black)) ? Qt::white : Qt::black);
    for (const auto &endpoint : {toward, away}) {
        for (int i = 0; i <= 8; ++i) {
            const qreal strength = std::clamp(amount * (1 + i / 4.0), 0.0, 1.0);
            const auto mix = [&](qreal a, qreal b) { return a + (b - a) * strength; };
            const QColor candidate = QColor::fromRgbF(mix(visible.redF(), endpoint.redF()),
                mix(visible.greenF(), endpoint.greenF()), mix(visible.blueF(), endpoint.blueF()));
            if (!readable(candidate))
                continue;
            const qreal measured = distance(candidate);
            if (measured >= 4)
                return candidate;
            if (!bestReadable || measured > bestDistance) {
                best = candidate;
                bestDistance = measured;
                bestReadable = true;
            }
        }
    }
    return best;
}

// Independent uniform 16-bit strength scan, not a replica of production's
// transition union. It can miss sub-1/65536 strength intervals; default and
// colored fixtures below compare the strongest RGB8 result it observes.
QColor sampledWeakerFeedback(const QColor &base, const QColor &text, qreal strength,
    const QColor &backdrop, qreal floor, const QList<QColor> &reserved)
{
    const QColor visible = opaqueOver(base, backdrop);
    const QColor toward = opaqueOver(text, visible);
    const QColor away(rgbContrast(text, QColor(Qt::white)) >= rgbContrast(text, QColor(Qt::black)) ? Qt::white : Qt::black);
    for (const auto &endpoint : {toward, away}) {
        for (int step = 65535; step > 0; --step) {
            const qreal amount = strength * step / 65536;
            const auto mix = [&](qreal a, qreal b) { return a + (b - a) * amount; };
            const QColor candidate = QColor::fromRgbF(mix(visible.redF(), endpoint.redF()),
                mix(visible.greenF(), endpoint.greenF()), mix(visible.blueF(), endpoint.blueF()));
            if (rgbContrast(text, candidate, backdrop) < floor
                || rgbContrast(QColor::fromRgba(text.rgba()), QColor::fromRgb(candidate.rgb()), backdrop) < floor)
                continue;
            if (std::all_of(reserved.cbegin(), reserved.cend(), [&](const QColor &other) {
                    return visibleDistance(candidate, other, backdrop) >= 4;
                }))
                return candidate;
        }
    }
    return {};
}

void verifyFeedbackOutcome(const QColor &actual, const QColor &primary, const QColor &text,
    const QList<QColor> &reserved, const QColor &backdrop, qreal floor, const QVariantList &diagnostics)
{
    const auto passes = [&](const QColor &candidate) {
        return rgbContrast(text, candidate, backdrop) >= floor
            && rgbContrast(QColor::fromRgba(text.rgba()), QColor::fromRgb(opaqueOver(candidate, backdrop).rgb())) >= floor
            && std::all_of(reserved.cbegin(), reserved.cend(), [&](const QColor &other) {
                return visibleDistance(candidate, other, backdrop) >= 4;
            });
    };
    if (passes(primary)) {
        QCOMPARE(actual, primary); // Every old success stays exact.
    } else if (passes(actual)) {
        QVERIFY(!hasDiagnostic(diagnostics, QStringLiteral("low-feedback"), QStringLiteral("selectedHoverBackgroundColor")));
    } else {
        QCOMPARE(actual, primary); // Still-infeasible best effort stays exact too.
        QVERIFY(hasDiagnostic(diagnostics, QStringLiteral("low-feedback"), QStringLiteral("selectedHoverBackgroundColor")));
    }
}

bool gray(const QColor &color)
{
    return color.red() == color.green() && color.green() == color.blue();
}

QVariantMap paletteInput(const QString &scheme, const QString &contrast, const QVariantMap &colors)
{
    return {{QStringLiteral("scheme"), scheme}, {QStringLiteral("contrast"), contrast},
        {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), colors}}}};
}

void verifyPaletteSeparation(const QVariantMap &colors, const QString &context)
{
    const auto color = [&](const QString &role) { return colors.value(role).value<QColor>(); };
    const QColor backdrop = opaqueOver(color(QStringLiteral("content")),
        opaqueOver(color(QStringLiteral("canvas")), QColor(Qt::white)));
    const QList<QStringList> resting{
        {QStringLiteral("accent"), QStringLiteral("primarySurface")},
        {QStringLiteral("error"), QStringLiteral("destructiveSurface")},
        {QStringLiteral("warning"), QStringLiteral("cautionSurface")}, {QStringLiteral("success")}};
    const QStringList actions{QStringLiteral("primary"), QStringLiteral("destructive"), QStringLiteral("caution")};
    for (qsizetype i = 0; i < resting.size(); ++i) {
        for (qsizetype j = i + 1; j < resting.size(); ++j) {
            for (const auto &a : resting[i]) {
                for (const auto &b : resting[j]) {
                    const qreal distance = visibleDistance(color(a), color(b), backdrop);
                    QVERIFY2(distance >= 12, qPrintable(QStringLiteral("%1 %2/%3 distance=%4 target=12")
                        .arg(context, a, b).arg(distance, 0, 'g', 12)));
                }
            }
        }
    }
    for (qsizetype i = 0; i < actions.size(); ++i) {
        for (qsizetype j = i + 1; j < actions.size(); ++j) {
            for (const auto *a : {"Surface", "Hover", "Pressed"}) {
                for (const auto *b : {"Surface", "Hover", "Pressed"}) {
                    const QString first = actions[i] + QLatin1String(a), second = actions[j] + QLatin1String(b);
                    const qreal distance = visibleDistance(color(first), color(second), backdrop);
                    QVERIFY2(distance >= 8, qPrintable(QStringLiteral("%1 %2/%3 distance=%4 target=8")
                        .arg(context, first, second).arg(distance, 0, 'g', 12)));
                }
            }
        }
    }
}

void verifyReadableContrast(const QColor &text, const QColor &background, const QColor &canvas, qreal target,
                            const QString &context = {})
{
    const QColor surface = opaqueOver(background, opaqueOver(canvas, QColor(Qt::white)));
    const qreal full = rgbContrast(text, surface);
    const qreal rgb8 = rgbContrast(QColor::fromRgba(text.rgba()), QColor::fromRgb(surface.rgb()));
    const QString details = QStringLiteral("%1 text=%2 surface=%3 full=%4 rgb8=%5 target=%6")
        .arg(context, text.name(QColor::HexArgb), surface.name(QColor::HexArgb))
        .arg(full, 0, 'g', 10).arg(rgb8, 0, 'g', 10).arg(target);
    QVERIFY2(full >= target, qPrintable(details));
    QVERIFY2(rgb8 >= target, qPrintable(details));
}

void verifyContrastDiagnostic(const QVariantList &diagnostics, const QString &path,
                              const QColor &foreground, const QColor &background,
                              const QColor &canvas, qreal target)
{
    const qreal ratio = rgbContrast(foreground, background, canvas);
    const QString context = QStringLiteral("%1 foreground=%2 background=%3 backdrop=%4 ratio=%5 target=%6")
        .arg(path, foreground.name(QColor::HexArgb), background.name(QColor::HexArgb), canvas.name(QColor::HexArgb))
        .arg(ratio, 0, 'g', 10).arg(target);
    QVERIFY2(hasDiagnostic(diagnostics, QStringLiteral("low-contrast"), path) == (ratio + 0.001 < target), qPrintable(context));
    for (const auto &value : diagnostics) {
        const auto entry = value.toMap();
        if (entry.value(QStringLiteral("code")) != QStringLiteral("low-contrast")
            || entry.value(QStringLiteral("path")) != path)
            continue;
        QCOMPARE(entry.value(QStringLiteral("severity")).toString(), QStringLiteral("warning"));
        QVERIFY(!entry.value(QStringLiteral("message")).toString().isEmpty());
        QCOMPARE(entry.value(QStringLiteral("target")).toDouble(), target);
        QVERIFY2(std::abs(entry.value(QStringLiteral("ratio")).toDouble() - ratio) < 0.001,
                 qPrintable(context + QStringLiteral(" reportedRatio=%1").arg(entry.value(QStringLiteral("ratio")).toDouble(), 0, 'g', 10)));
    }
}

void verifyDerivedContrast(const QColor &text, const QList<QColor> &backgrounds, const QColor &canvas, qreal target,
                           const QString &context = {})
{
    qreal minimum = 21, blackMinimum = 21, whiteMinimum = 21;
    QStringList ratios;
    for (const QColor &background : backgrounds) {
        const qreal ratio = rgbContrast(text, background, canvas);
        minimum = std::min(minimum, ratio);
        blackMinimum = std::min(blackMinimum, rgbContrast(QColor(Qt::black), background, canvas));
        whiteMinimum = std::min(whiteMinimum, rgbContrast(QColor(Qt::white), background, canvas));
        ratios.append(QStringLiteral("%1:%2").arg(background.name(QColor::HexArgb)).arg(ratio, 0, 'g', 10));
    }
    const QString details = QStringLiteral("%1 text=%2 backdrop=%3 target=%4 minimum=%5 blackMinimum=%6 whiteMinimum=%7 surfaces=[%8]")
        .arg(context, text.name(QColor::HexArgb), canvas.name(QColor::HexArgb))
        .arg(target).arg(minimum, 0, 'g', 10).arg(blackMinimum, 0, 'g', 10).arg(whiteMinimum, 0, 'g', 10)
        .arg(ratios.join(QStringLiteral(", ")));
    const qreal endpointMinimum = std::max(blackMinimum, whiteMinimum);
    if (endpointMinimum >= target)
        QVERIFY2(minimum >= target, qPrintable(details));
    else
        QVERIFY2(minimum + 0.001 >= endpointMinimum, qPrintable(details));
}

} // namespace

class AppearanceTests : public QObject
{
    Q_OBJECT
private slots:
    void screenGeometryLifetime();
    void defaultsAndSchemaCompleteness();
    void numericSchemaBounds();
    void pressInsetTokens();
    void partialInputReplacesSnapshot();
    void invalidFields_data();
    void invalidFields();
    void unknownFieldsAndStrictEnvelopeFields();
    void rejectedEnvelope_data();
    void rejectedEnvelope();
    void accentRelationships();
    void derivedLabelTarget();
    void contrastingText_data();
    void contrastingText();
    void multipleBackgrounds();
    void feedbackSurface();
    void semanticTextRoles();
    void contrastDiagnostics();
    void highContrast_data();
    void highContrast();
    void reducedMotionAndDensity();
    void atomicNotifications();
    void engineLocalSingletons();
    void qmlPublicApiAndReset();
    void localDefaultsAndFamilies();
    void localOverridesAndLiveReset();
    void allLocalPropertiesAreResettable();
    void localDerivationAndDiagnostics();
    void localPurple();
    void fontsAndMinimumHeight();
    void semanticPresentation();
    void solidRecipes_data();
    void solidRecipes();
    void semanticBorderOverrides();
    void solidOverrides();
    void solidBackdropConsistency_data();
    void solidBackdropConsistency();
    void applicationStateOverrides();
    void localStateOverrides();
    void sourceLifetimeAndReplacement();
    void localAtomicNotification();
    void semanticPaletteFixtures_data();
    void semanticPaletteFixtures();
    void semanticPaletteResponse();
    void semanticPaletteOwnership();
    void semanticPaletteReservations();
    void semanticPaletteAlpha();
    void semanticPaletteDeterminism();
    void semanticPaletteImmutablePairs();
    void directRecipeBounds();
    void directRecipeRepairTones();
    void directRecipeExhaustion();
    void contrastingTextFastPath();
    void sharedPaletteWork_data();
    void sharedPaletteWork();
    void mixedFacadeWorkDoesNotScale();
    void sharedFontEnvironmentDoesNotScale();
    void fontEnvironmentDuringSourceUpdate();
    void lazyControlDiagnostics();
    void propertySpecificNotifications();
    void headingTypographyRoles();
    void localWorkIsolation();
    void fontAndGeometryWork();
    void reentrantNotifications();
    void deletionDuringPublication();
    void preciseAtomicCallbacks();
    void equalSourceAndOwnershipNotifications();
    void localDependencyChanges();
    void familyDescriptorContract();
    void typedFacadeNotificationOrder();
    void typedFacadePendingReversion();
    void stateTableEquivalence();
    void stateTableSharingAndLifetime();
    void stateTableQmlBinding();
    void stateTableNotifications();
    void supplementaryStateRoles();
    void focusedBorderInputs();
    void neutralFocusBorder();
    void stateFeedbackMatrix();
    void menuHighlightPairs();
    void menuHighlightLiveDependencies();
    void tabModeAndSeparatorContracts();
    void tabTrackSurround();
    void weakerFeedback();
    void valueAndScrollRoles();
    void stateSweepDoesNoFoundationWork();
    void appearanceMetaObjectIsPublic();
};

void AppearanceTests::appearanceMetaObjectIsPublic()
{
    const auto &meta = Appearance::staticMetaObject;
    for (int i = meta.methodOffset(); i < meta.methodCount(); ++i) {
        const auto method = meta.method(i);
        QVERIFY(method.name() != QByteArrayLiteral("completed"));
        for (const auto &type : method.parameterTypes())
            QVERIFY2(!type.contains("PaletteUpdate"), type.constData());
    }
    QVERIFY(meta.indexOfSignal("resolvedChanged()") >= 0);
}

void AppearanceTests::stateFeedbackMatrix()
{
    Appearance source;
    for (const auto *scheme : {"light", "dark"}) {
        for (const auto *contrast : {"normal", "high"}) {
            source.setInput(paletteInput(QLatin1String(scheme), QLatin1String(contrast), {}));
            const qreal floor = QLatin1String(contrast) == QLatin1String("high") ? 7 : 5;
            const QColor contentBackdrop = opaqueOver(source.colors()->content(),
                opaqueOver(source.colors()->canvas(), QColor(Qt::white)));
            ControlAppearance track;
            track.setSource(&source);
            track.setFamily(QStringLiteral("tabBar"));
            for (const auto *family : {"button", "tool", "field", "delegate", "tab", "choice", "menuItem"}) {
                const QColor backdrop = QLatin1String(family) == QLatin1String("tab")
                    ? opaqueOver(track.backgroundColor(), contentBackdrop) : contentBackdrop;
                ControlAppearance control;
                control.setSource(&source);
                control.setFamily(QLatin1String(family));
                for (const auto tone : {Presentation::Neutral, Presentation::Primary, Presentation::Destructive, Presentation::Caution}) {
                    control.setTone(tone);
                    const QString context = QStringLiteral("%1/%2/%3/%4").arg(QLatin1String(scheme), QLatin1String(contrast),
                        QLatin1String(family)).arg(int(tone));
                    const auto check = [&](bool local) {
                        const auto table = control.stateTable();
                        const auto rest = table.colors(Presentation::Rest);
                        const auto hover = table.colors(Presentation::Hover);
                        const qreal distance = visibleDistance(rest.backgroundColor(), hover.backgroundColor(), backdrop);
                        QVERIFY2(distance >= 4, qPrintable(context + QStringLiteral(" rest/hover=%1 local=%2").arg(distance).arg(local)));
                        verifyReadableContrast(hover.foregroundColor(), hover.backgroundColor(), backdrop, floor, context);
                        {
                            const auto pressed = table.colors(Presentation::Pressed);
                            QVERIFY2(visibleDistance(rest.backgroundColor(), pressed.backgroundColor(), backdrop) >= 4, qPrintable(context));
                            verifyReadableContrast(pressed.foregroundColor(), pressed.backgroundColor(), backdrop, floor, context);
                        }
                        if (QLatin1String(family) != QLatin1String("field") && QLatin1String(family) != QLatin1String("menuItem")) {
                            const Presentation::State states[]{Presentation::Rest, Presentation::Hover,
                                Presentation::Selected, Presentation::SelectedHover};
                            for (size_t i = 0; i < std::size(states); ++i) {
                                const auto a = table.colors(states[i]);
                                verifyReadableContrast(a.foregroundColor(), a.backgroundColor(), backdrop, floor, context);
                                for (size_t j = i + 1; j < std::size(states); ++j) {
                                    const qreal separation = visibleDistance(a.backgroundColor(), table.colors(states[j]).backgroundColor(), backdrop);
                                    QVERIFY2(separation >= 4, qPrintable(context + QStringLiteral(" %1/%2=%3").arg(int(states[i])).arg(int(states[j])).arg(separation)));
                                }
                            }
                        }
                        if (rest.backgroundColor().alpha() == 0)
                            QCOMPARE(table.colors(Presentation::Disabled).backgroundColor().alpha(), 0);
                    };
                    check(false);
                    if (QLatin1String(family) == QLatin1String("menuItem")) {
                        const auto band = control.stateTable().colors(Presentation::Selected);
                        QCOMPARE(band.backgroundColor(), source.colors()->accent());
                        QCOMPARE(band.foregroundColor(), source.colors()->onAccent());
                        verifyReadableContrast(band.foregroundColor(), band.backgroundColor(), backdrop, 7, context);
                        QCOMPARE(control.stateTable().colors(Presentation::SelectedHover), control.stateTable().colors(Presentation::Selected));
                    }
                    for (const QColor &fill : {QColor(Qt::white), QColor(Qt::black)}) {
                        control.setBackgroundColor(fill);
                        check(true);
                        QCOMPARE(control.stateTable().colors(Presentation::Rest).backgroundColor(), fill);
                        control.resetOverrides();
                    }
                    const QColor exact("#80445566");
                    control.setHoverBackgroundColor(exact);
                    control.setPressedBackgroundColor(exact);
                    control.setSelectedBackgroundColor(exact);
                    control.setDisabledBackgroundColor(exact);
                    QCOMPARE(control.stateTable().colors(Presentation::Hover).backgroundColor(), exact);
                    QCOMPARE(control.stateTable().colors(Presentation::Pressed).backgroundColor(), exact);
                    QCOMPARE(control.stateTable().colors(Presentation::Selected).backgroundColor(), exact);
                    QCOMPARE(control.stateTable().colors(Presentation::Disabled).backgroundColor(), exact);
                    const auto retained = control.stateTable();
                    control.setFontPixelSize(control.fontPixelSize() + 1);
                    QCOMPARE(control.stateTable(), retained);
                    control.resetOverrides();
                }
            }
        }
    }
    source.setInput(tokenInput(QStringLiteral("motion"), {{QStringLiteral("hoverBlend"), 0}}));
    ControlAppearance impossible;
    impossible.setSource(&source);
    QVERIFY(hasDiagnostic(impossible.diagnostics(), QStringLiteral("low-feedback")));
}

void AppearanceTests::menuHighlightPairs()
{
    Appearance source;
    ControlAppearance control;
    control.setSource(&source);
    control.setFamily(QStringLiteral("menuItem"));
    for (const auto *scheme : {"light", "dark"}) {
        for (const auto *contrast : {"normal", "high"}) {
            for (const QColor &requested : {QColor(), QColor("#F5D90A"),
                     QColor(QLatin1String(scheme) == QLatin1String("light") ? "#F2F3F5" : "#303237"), QColor("#7A1F1F")}) {
                const QVariantMap overrides = requested.isValid() ? QVariantMap{{QStringLiteral("accent"), requested}} : QVariantMap{};
                source.setInput(paletteInput(QLatin1String(scheme), QLatin1String(contrast), overrides));
                const QColor accent = source.colors()->accent();
                if (requested.isValid())
                    QCOMPARE(accent, requested);
                const QColor backdrop = opaqueOver(source.colors()->content(), source.colors()->canvas());
                const QColor popup = opaqueOver(source.colors()->popup(), backdrop);
                ControlAppearance primary;
                primary.setSource(&source);
                primary.setTone(Presentation::Primary);
                const auto border = primary.stateTable().colors(Presentation::Rest).borderColor();
                const qreal width = rgbContrast(accent, popup) < (QLatin1String(contrast) == QLatin1String("high") ? 7 : 5)
                    ? source.metrics()->borderWidth() : 0;
                for (const auto tone : {Presentation::Neutral, Presentation::Primary, Presentation::Caution, Presentation::Destructive}) {
                    control.setTone(tone);
                    const auto band = control.stateTable().colors(Presentation::Selected);
                    QCOMPARE(band.backgroundColor(), accent);
                    QCOMPARE(band.foregroundColor(), source.colors()->onAccent());
                    QCOMPARE(band.borderColor(), border);
                    QCOMPARE(band.borderWidth(), width);
                    verifyReadableContrast(band.foregroundColor(), band.backgroundColor(), backdrop, 7);
                    for (const auto state : {Presentation::SelectedHover, Presentation::SelectedPressed,
                             Presentation::Inactive, Presentation::InactiveHover}) {
                        const auto entry = control.stateTable().colors(state);
                        QCOMPARE(entry.backgroundColor(), band.backgroundColor());
                        QCOMPARE(entry.foregroundColor(), band.foregroundColor());
                        QCOMPARE(entry.borderWidth(), width);
                        QCOMPARE(entry.borderColor(), border);
                    }
                    QCOMPARE(control.stateTable().colors(Presentation::Rest).borderWidth(), 0);
                    QCOMPARE(control.stateTable().colors(Presentation::Disabled).borderWidth(), 0);
                    control.setBorderColor(QColor("#123456")); // Private and shared builders agree.
                    QCOMPARE(control.stateTable().colors(Presentation::Selected).backgroundColor(), band.backgroundColor());
                    QCOMPARE(control.stateTable().colors(Presentation::Selected).foregroundColor(), band.foregroundColor());
                    QCOMPARE(control.stateTable().colors(Presentation::Selected).borderColor(), QColor("#123456"));
                    control.resetOverrides();
                    control.setBackgroundColor(QColor("#e0e0e0")); // Unrelated private branch must not darken the highlight.
                    QCOMPARE(control.stateTable().colors(Presentation::Selected).backgroundColor(), accent);
                    QCOMPARE(control.stateTable().colors(Presentation::Selected).foregroundColor(), source.colors()->onAccent());
                    control.resetOverrides();
                    control.setSelectedBackgroundColor(accent); // Equal assignments still own their pair.
                    control.setSelectedForegroundColor(source.colors()->onAccent());
                    control.setInactiveBackgroundColor(accent);
                    control.setInactiveForegroundColor(source.colors()->onAccent());
                    QCOMPARE(control.stateTable().colors(Presentation::Selected), band);
                    QCOMPARE(control.stateTable().colors(Presentation::Inactive), band);
                    control.resetOverrides();
                    control.setSelectedBackgroundColor(QColor("#abcd12"));
                    QCOMPARE(control.selectedBackgroundColor(), QColor("#abcd12"));
                    verifyReadableContrast(control.selectedForegroundColor(), control.selectedBackgroundColor(), backdrop, 7);
                    QCOMPARE(control.stateTable().colors(Presentation::Selected).borderWidth(),
                        rgbContrast(control.selectedBackgroundColor(), popup) < (QLatin1String(contrast) == QLatin1String("high") ? 7 : 5)
                            ? source.metrics()->borderWidth() : 0);
                    control.setSelectedForegroundColor(QColor("#123456"));
                    QCOMPARE(control.selectedForegroundColor(), QColor("#123456"));
                    control.setInactiveBackgroundColor(QColor("#f0f0f0"));
                    verifyReadableContrast(control.inactiveForegroundColor(), control.inactiveBackgroundColor(), backdrop, 7);
                    QCOMPARE(control.stateTable().colors(Presentation::Inactive).borderWidth(),
                        rgbContrast(control.inactiveBackgroundColor(), popup) < (QLatin1String(contrast) == QLatin1String("high") ? 7 : 5)
                            ? source.metrics()->borderWidth() : 0);
                    control.setInactiveForegroundColor(QColor("#f0f0f0"));
                    QCOMPARE(control.inactiveForegroundColor(), QColor("#f0f0f0"));
                    QVERIFY(hasDiagnostic(control.diagnostics(), QStringLiteral("low-contrast"),
                        QStringLiteral("inactiveForegroundColor/inactiveBackgroundColor")));
                    control.resetOverrides();
                    QCOMPARE(control.stateTable().colors(Presentation::Selected), band);
                    control.setSelectedForegroundColor(QColor("#aaaaaa"));
                    QCOMPARE(control.selectedBackgroundColor(), accent);
                    QCOMPARE(control.selectedForegroundColor(), QColor("#aaaaaa"));
                    control.resetOverrides();
                }
            }
        }
    }
}

void AppearanceTests::menuHighlightLiveDependencies()
{
    Appearance source;
    ControlAppearance menu, primary;
    menu.setSource(&source);
    menu.setFamily(QStringLiteral("menuItem"));
    primary.setSource(&source);
    primary.setTone(Presentation::Primary);
    auto colors = source.colors()->values(); // Freeze unrelated roles to isolate each dependency.
    colors.insert(QStringLiteral("accent"), QColor("#F2F3F5"));
    colors.insert(QStringLiteral("onAccent"), QColor(Qt::black));
    auto install = [&] { source.setInput(tokenInput(QStringLiteral("colors"), colors)); };
    install();
    QSignalSpy changed(&menu, &ControlAppearance::stateTableChanged);
    const auto checkBorder = [&] {
        QCOMPARE(menu.stateTable().colors(Presentation::Selected).borderColor(),
            primary.stateTable().colors(Presentation::Rest).borderColor());
    };
    checkBorder();
    colors.insert(QStringLiteral("popup"), QColor(Qt::black));
    install();
    QCOMPARE(menu.stateTable().colors(Presentation::Selected).borderWidth(), 0);
    QVERIFY(changed.count() > 0);
    changed.clear();
    colors.insert(QStringLiteral("popup"), QColor("#F2F3F5"));
    install();
    QCOMPARE(menu.stateTable().colors(Presentation::Selected).borderWidth(), source.metrics()->borderWidth());
    QVERIFY(changed.count() > 0);
    changed.clear();
    colors.insert(QStringLiteral("primaryOutline"), QColor("#123456"));
    install();
    checkBorder();
    QCOMPARE(menu.stateTable().colors(Presentation::Selected).borderColor(), QColor("#123456"));
    QVERIFY(changed.count() > 0);
    // Presence alone participates in Primary's outline coupling, even when
    // the finished values happen to remain equal.
    colors.remove(QStringLiteral("primaryOutline"));
    install();
    checkBorder();
    const auto coupled = menu.stateTable();
    WorkRecorder work;
    work.clear();
    colors.insert(QStringLiteral("primaryOutline"), source.colors()->primaryOutline());
    install();
    checkBorder();
    QCOMPARE(work.count(AppearanceWork::ColorResolution), 2);
    QCOMPARE(menu.stateTable(), coupled); // Equal-valued ownership still refreshes both consumers.
    changed.clear();
    colors.insert(QStringLiteral("accent"), QColor("#F5D90A"));
    install();
    QCOMPARE(menu.stateTable().colors(Presentation::Selected).backgroundColor(), QColor("#F5D90A"));
    QVERIFY(changed.count() > 0);
    changed.clear();
    colors.insert(QStringLiteral("onAccent"), QColor("#111111"));
    install();
    QCOMPARE(menu.stateTable().colors(Presentation::Selected).backgroundColor(), QColor("#F5D90A"));
    QCOMPARE(menu.stateTable().colors(Presentation::Selected).foregroundColor(), QColor("#111111"));
    QVERIFY(changed.count() > 0);
    menu.setSelectedBackgroundColor(menu.selectedBackgroundColor());
    menu.setSelectedForegroundColor(menu.selectedForegroundColor());
    menu.setInactiveBackgroundColor(menu.inactiveBackgroundColor());
    menu.setInactiveForegroundColor(menu.inactiveForegroundColor());
    colors.insert(QStringLiteral("accent"), QColor("#7A1F1F"));
    colors.insert(QStringLiteral("onAccent"), QColor(Qt::white));
    install();
    QCOMPARE(menu.selectedBackgroundColor(), QColor("#F5D90A"));
    QCOMPARE(menu.inactiveBackgroundColor(), QColor("#F5D90A"));
    menu.resetOverrides();
    QCOMPARE(menu.selectedBackgroundColor(), QColor("#7A1F1F"));
    QCOMPARE(menu.inactiveForegroundColor(), QColor(Qt::white));
    colors.insert(QStringLiteral("popup"), QColor("#7A1F1F"));
    install();
    menu.setBorderColor(menu.borderColor()); // Keep a real private branch during metric projection.
    const auto retained = menu.stateTable();
    changed.clear();
    work.clear();
    source.setInput({{QStringLiteral("tokens"), QVariantMap{
        {QStringLiteral("colors"), colors}, {QStringLiteral("metrics"), QVariantMap{{QStringLiteral("borderWidth"), 3}}}}}});
    QCOMPARE(menu.stateTable().colors(Presentation::Selected).borderWidth(), 3);
    QCOMPARE(retained.colors(Presentation::Selected).borderWidth(), 1);
    QVERIFY(changed.count() > 0);
    QCOMPARE(work.count(AppearanceWork::SharedPaletteBuild), 0);
    QCOMPARE(work.count(AppearanceWork::StateTableBuild), 0);
    QCOMPARE(work.count(AppearanceWork::PrivateStateTableBuild), 0);
    work.clear();
    for (int i = int(Presentation::Rest); i <= int(Presentation::Disabled); ++i)
        menu.stateTable().colors(Presentation::State(i));
    QCOMPARE(work.count(AppearanceWork::SharedPaletteBuild), 0);
    QCOMPARE(work.count(AppearanceWork::StateTableBuild), 0);
    QCOMPARE(work.count(AppearanceWork::PrivateStateTableBuild), 0);
    work.clear();
    menu.setFontPixelSize(menu.fontPixelSize() + 1);
    QCOMPARE(work.count(AppearanceWork::SharedPaletteBuild), 0);
    QCOMPARE(work.count(AppearanceWork::StateTableBuild), 0);
    QCOMPARE(work.count(AppearanceWork::PrivateStateTableBuild), 0);
}

void AppearanceTests::weakerFeedback()
{
    Appearance source;
    ControlAppearance tab, track;
    tab.setSource(&source);
    tab.setFamily(QStringLiteral("tab"));
    track.setSource(&source);
    track.setFamily(QStringLiteral("tabBar"));
    for (const auto *contrast : {"normal", "high"}) {
        source.setInput({{QStringLiteral("contrast"), QLatin1String(contrast)}});
        const auto selected = tab.stateTable().colors(Presentation::Selected);
        const auto hover = tab.stateTable().colors(Presentation::SelectedHover);
        const QColor backdrop = track.backgroundColor();
        for (const auto &reserved : {tab.backgroundColor(), tab.hoverBackgroundColor(), selected.backgroundColor()})
            QVERIFY(visibleDistance(hover.backgroundColor(), reserved, backdrop) >= 4);
        verifyReadableContrast(hover.foregroundColor(), hover.backgroundColor(), backdrop,
            source.contrast() == QLatin1String("high") ? 7 : 5);
        QVERIFY(rgbLuminance(hover.backgroundColor()) > rgbLuminance(backdrop));
        QVERIFY(tab.diagnostics().isEmpty());
        const QList<QColor> reserved{tab.backgroundColor(), tab.hoverBackgroundColor(), selected.backgroundColor()};
        const QColor sampled = sampledWeakerFeedback(selected.backgroundColor(), selected.foregroundColor(),
            source.motion()->hoverBlend(), backdrop, source.contrast() == QLatin1String("high") ? 7 : 5, reserved);
        QVERIFY(sampled.isValid());
        QCOMPARE(hover.backgroundColor().rgb(), sampled.rgb());
        qInfo().noquote() << contrast << "selected-hover" << hover.backgroundColor().name();
    }
    source.setInput({});
    tab.setSelectedBackgroundColor(QColor("#fffeff"));
    tab.setSelectedForegroundColor(QColor("#25282d"));
    const QList<QColor> reserved{tab.backgroundColor(), tab.hoverBackgroundColor(), tab.selectedBackgroundColor()};
    const QColor primary = stateFeedback(tab.selectedBackgroundColor(), tab.selectedForegroundColor(),
        source.motion()->hoverBlend(), track.backgroundColor(), 5, reserved);
    QVERIFY(std::any_of(reserved.cbegin(), reserved.cend(), [&](const QColor &other) {
        return visibleDistance(primary, other, track.backgroundColor()) < 4;
    }));
    const QColor sampled = sampledWeakerFeedback(tab.selectedBackgroundColor(), tab.selectedForegroundColor(),
        source.motion()->hoverBlend(), track.backgroundColor(), 5, reserved);
    QVERIFY(sampled.isValid());
    QCOMPARE(tab.stateTable().colors(Presentation::SelectedHover).backgroundColor().rgb(), sampled.rgb());
    tab.setSelectedBackgroundColor(QColor(Qt::white));
    tab.setSelectedForegroundColor(QColor(Qt::white)); // Neither ray can be readable at these strengths.
    const QColor impossible = stateFeedback(tab.selectedBackgroundColor(), tab.selectedForegroundColor(),
        source.motion()->hoverBlend(), track.backgroundColor(), 5,
        {tab.backgroundColor(), tab.hoverBackgroundColor(), tab.selectedBackgroundColor()});
    QCOMPARE(tab.stateTable().colors(Presentation::SelectedHover).backgroundColor(), impossible);
    QVERIFY(hasDiagnostic(tab.diagnostics(), QStringLiteral("low-feedback"), QStringLiteral("selectedHoverBackgroundColor")));
    ControlAppearance button;
    button.setSource(&source);
    source.setInput({{QStringLiteral("contrast"), QStringLiteral("high")}});
    button.setBackgroundColor(QColor(Qt::white));
    // The original stronger headroom repair still owns label finalization.
    const QColor backdrop = source.colors()->content();
    const QColor base(Qt::white);
    const QColor seed = TokenSchema::contrastingText(base, {base}, backdrop, 7);
    const auto mix = [](qreal a, qreal b) { return a + (b - a) * .24; };
    const QColor strongestFill = QColor::fromRgbF(mix(base.redF(), 0), mix(base.greenF(), 0), mix(base.blueF(), 0));
    const QColor expectedLabel = TokenSchema::contrastingText(seed, {base, strongestFill}, backdrop, 7);
    QVERIFY(expectedLabel != seed);
    QCOMPARE(button.foregroundColor(), expectedLabel);
}

void AppearanceTests::tabTrackSurround()
{
    Appearance source;
    ControlAppearance track, local, tab;
    for (auto *control : {&track, &local, &tab})
        control->setSource(&source);
    track.setFamily(QStringLiteral("tabBar"));
    local.setFamily(QStringLiteral("tabBar"));
    tab.setFamily(QStringLiteral("tab"));
    local.setForegroundColor(source.colors()->text()); // Genuine sparse private branch.
    for (const auto *scheme : {"light", "dark"}) {
        for (const auto *contrast : {"normal", "high"}) {
            source.setInput(paletteInput(QLatin1String(scheme), QLatin1String(contrast), {}));
            const QColor canvas = opaqueOver(source.colors()->canvas(), QColor(Qt::white));
            const QColor selected = QLatin1String(scheme) == QLatin1String("dark")
                ? source.colors()->controlHover() : source.colors()->content();
            const QColor expected = stateFeedback(source.colors()->control(), source.colors()->text(),
                source.motion()->hoverBlend(), canvas, source.contrast() == QLatin1String("high") ? 7 : 5,
                {selected, source.colors()->canvas()}, true);
            QCOMPARE(track.backgroundColor(), expected);
            QVERIFY(visibleDistance(track.backgroundColor(), canvas, canvas) >= 4);
            QVERIFY(visibleDistance(track.backgroundColor(), selected, canvas) >= 4);
            if (QLatin1String(scheme) == QLatin1String("dark"))
                QCOMPARE(track.backgroundColor(), source.colors()->control());
            qInfo().noquote() << scheme << contrast << "track" << track.backgroundColor().name()
                << "hover" << tab.hoverBackgroundColor().name() << "pressed" << tab.pressedBackgroundColor().name()
                << "focus" << tab.stateTable().colors(Presentation::Rest).focusedBorderColor().name()
                << "hover-focus" << tab.stateTable().colors(Presentation::Hover).focusedBorderColor().name();
        }
    }
    source.setInput({});
    local.setForegroundColor(source.colors()->text());
    QCOMPARE(local.backgroundColor(), track.backgroundColor());
    const auto oldLocal = local.stateTable();
    const QColor oldTrack = local.backgroundColor();
    source.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("canvas"), QColor("#dedfe1")}}));
    QVERIFY(local.stateTable() != oldLocal);
    QVERIFY(local.backgroundColor() != oldTrack);
    QCOMPARE(local.backgroundColor(), track.backgroundColor());
    local.resetOverrides();
    QCOMPARE(local.stateTable(), track.stateTable());
    const auto contentBefore = track.stateTable();
    source.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("canvas"), QColor("#dedfe1")},
        {QStringLiteral("content"), QColor("#f8f8fa")}}));
    QVERIFY(track.stateTable() != contentBefore);
    for (const QColor &exact : {track.backgroundColor(), QColor("#80445566"), QColor(Qt::transparent)}) {
        local.setBackgroundColor(exact);
        source.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("control"), exact}}));
        QCOMPARE(track.backgroundColor(), exact);
        QCOMPARE(local.backgroundColor(), exact);
        local.resetBackgroundColor();
        QCOMPARE(local.backgroundColor(), exact);
    }
    source.setInput({});
    QCOMPARE(local.backgroundColor(), track.backgroundColor());

    source.setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}});
    const QColor seed = source.colors()->control();
    const qreal alpha = .9;
    const QColor translucent = QColor::fromRgbF((seed.redF() - (1 - alpha)) / alpha,
        (seed.greenF() - (1 - alpha)) / alpha, (seed.blueF() - (1 - alpha)) / alpha, alpha);
    source.setInput(paletteInput(QStringLiteral("dark"), QStringLiteral("normal"),
        {{QStringLiteral("canvas"), translucent}}));
    const QColor visibleCanvas = opaqueOver(source.colors()->canvas(), QColor(Qt::white));
    QVERIFY(visibleDistance(seed, visibleCanvas, visibleCanvas) < .1);
    local.setSelectedBackgroundColor(QColor(Qt::white));
    for (auto *control : {&track, &local}) {
        QVERIFY(visibleDistance(control->backgroundColor(), visibleCanvas, visibleCanvas) >= 4);
        verifyReadableContrast(control->foregroundColor(), control->backgroundColor(), visibleCanvas, 5);
    }
}

void AppearanceTests::tabModeAndSeparatorContracts()
{
    Appearance source;
    const auto frozenColors = source.colors()->values();
    source.setInput(paletteInput(QStringLiteral("light"), QStringLiteral("normal"), frozenColors));
    ControlAppearance tab, track;
    tab.setSource(&source);
    tab.setFamily(QStringLiteral("tab"));
    track.setSource(&source);
    track.setFamily(QStringLiteral("tabBar"));
    const auto light = tab.stateTable();
    QSignalSpy tabChanged(&tab, &ControlAppearance::stateTableChanged);
    QSignalSpy trackChanged(&track, &ControlAppearance::stateTableChanged);
    source.setInput(paletteInput(QStringLiteral("dark"), QStringLiteral("normal"), frozenColors));
    QCOMPARE(source.colors()->values(), frozenColors);
    QVERIFY(tab.stateTable() != light); // Mode is a selector even when every role is unchanged.
    QVERIFY(tabChanged.count() > 0);
    QVERIFY(trackChanged.count() > 0);

    Appearance lineSource;
    WorkRecorder work;
    SeparatorAppearance line;
    line.setSource(&lineSource);
    QCOMPARE(line.foregroundColor(), lineSource.colors()->separator());
    line.setForegroundColor(line.foregroundColor()); // An equal assignment is still owned.
    lineSource.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("separator"), QColor("green")}}));
    QCOMPARE(line.foregroundColor(), frozenColors.value(QStringLiteral("separator")).value<QColor>());
    line.resetOverrides();
    QCOMPARE(line.foregroundColor(), QColor("green"));
    line.setForegroundColor(QColor());
    QCOMPARE(line.foregroundColor(), QColor("green"));
    QVERIFY(!line.diagnostics().isEmpty());
    line.resetForegroundColor();
    QVERIFY(line.diagnostics().isEmpty());
    QCOMPARE(work.count(AppearanceWork::FontConstruction), 0);
    QCOMPARE(work.count(AppearanceWork::FontMeasurement), 0);
    QCOMPARE(work.count(AppearanceWork::GeometryCalculation), 0);
    QCOMPARE(work.count(AppearanceWork::StateTableBuild), 0);
    QCOMPARE(work.count(AppearanceWork::PrivateStateTableBuild), 0);
}

void AppearanceTests::stateTableEquivalence()
{
    // Independent state and focused-border oracle, expressed in template flags.
    const auto verify = [](const ControlAppearance &control) {
        struct Flags { Presentation::State state; bool enabled, down, hover, selected, active; };
        const Flags cases[]{
            {Presentation::Rest, true, false, false, false, true},
            {Presentation::Hover, true, false, true, false, true},
            {Presentation::Pressed, true, true, false, false, true},
            {Presentation::Selected, true, false, false, true, true},
            {Presentation::SelectedHover, true, false, true, true, true},
            {Presentation::SelectedPressed, true, true, true, true, true},
            {Presentation::Inactive, true, false, false, true, false},
            {Presentation::InactiveHover, true, false, true, true, false},
            {Presentation::Disabled, false, true, true, true, false}};
        for (const auto &flags : cases) {
            const auto entry = control.stateTable().colors(flags.state);
            const auto *source = control.source();
            QColor backdrop = opaqueOver(source->colors()->content(),
                opaqueOver(source->colors()->canvas(), QColor(Qt::white)));
            if (control.family() == QLatin1String("tab")) {
                ControlAppearance track;
                track.setSource(control.source());
                track.setFamily(QStringLiteral("tabBar"));
                backdrop = opaqueOver(track.backgroundColor(), backdrop);
            }
            const bool selectable = control.family() == QLatin1String("button") || control.family() == QLatin1String("tool")
                || control.family() == QLatin1String("delegate") || control.family() == QLatin1String("tab")
                || control.family() == QLatin1String("choice");
            const QColor background = flags.state == Presentation::SelectedHover && selectable
                ? stateFeedback(control.selectedBackgroundColor(), control.selectedForegroundColor(), source->motion()->hoverBlend(),
                    backdrop, source->contrast() == QLatin1String("high") ? 7 : 5,
                    {control.backgroundColor(), control.hoverBackgroundColor(), control.selectedBackgroundColor()})
                : !flags.enabled ? control.disabledBackgroundColor()
                : flags.down ? control.pressedBackgroundColor()
                : flags.selected ? (flags.active ? control.selectedBackgroundColor() : control.inactiveBackgroundColor())
                : flags.hover ? control.hoverBackgroundColor() : control.backgroundColor();
            const QColor foreground = !flags.enabled ? control.disabledForegroundColor()
                : flags.down ? control.foregroundColor()
                : flags.selected ? (flags.active ? control.selectedForegroundColor() : control.inactiveForegroundColor())
                : control.foregroundColor();
            if (flags.state == Presentation::SelectedHover && selectable)
                verifyFeedbackOutcome(entry.backgroundColor(), background, foreground,
                    {control.backgroundColor(), control.hoverBackgroundColor(), control.selectedBackgroundColor()},
                    backdrop, source->contrast() == QLatin1String("high") ? 7 : 5, control.diagnostics());
            else
                QCOMPARE(entry.backgroundColor(), background);
            QCOMPARE(entry.foregroundColor(), foreground);
            QCOMPARE(entry.borderColor(), flags.enabled ? control.borderColor() : control.disabledBorderColor());
            QCOMPARE(entry.focusColor(), control.focusColor());
            const bool invalidField = control.family() == QLatin1String("field")
                && control.validation() == Presentation::Invalid;
            const QColor expectedFocus = invalidField ? entry.borderColor()
                : rgbContrast(control.focusColor(), control.family() == QLatin1String("choice")
                    ? QColor(Qt::transparent) : entry.backgroundColor(), backdrop) >= 3 ? control.focusColor()
                    : control.family() == QLatin1String("choice") ? entry.borderColor() : foreground;
            QCOMPARE(entry.focusedBorderColor(), expectedFocus);
            QCOMPARE(entry.focusedBorderWidth(), source->metrics()->focusWidth());
            QCOMPARE(entry.fillColor(), flags.enabled ? control.fillColor() : control.disabledForegroundColor());
            QCOMPARE(entry.trackColor(), flags.enabled ? control.borderColor() : control.disabledBackgroundColor());
        }
    };
    Appearance appearance;
    for (const auto *scheme : {"light", "dark"}) {
        for (const auto *contrast : {"normal", "high"}) {
            appearance.setInput({{QStringLiteral("scheme"), QLatin1String(scheme)},
                {QStringLiteral("contrast"), QLatin1String(contrast)}});
            for (const auto *family : {"button", "tool", "field", "surface", "popup", "label",
                     "delegate", "tab", "choice", "tooltip"}) {
                ControlAppearance control;
                control.setSource(&appearance);
                control.setFamily(QLatin1String(family));
                for (const auto tone : {Presentation::Auto, Presentation::Neutral, Presentation::Primary,
                         Presentation::Destructive, Presentation::Caution}) {
                    control.setTone(tone);
                    for (const auto validation : {Presentation::None, Presentation::Warning, Presentation::Invalid}) {
                        control.setValidation(validation);
                        verify(control);
                        control.setBackgroundColor(QColor("#8657b8")); // derived ordinary states/text/border
                        verify(control);
                        control.resetOverrides();
                        control.setHoverBackgroundColor(QColor("#303040")); // partially inherited ordinary pair
                        control.setPressedBackgroundColor(QColor("#202030"));
                        verify(control);
                        control.resetOverrides();
                        control.setSelectedBackgroundColor(QColor("#153b57")); // independently derived pairs
                        control.setInactiveBackgroundColor(QColor("#421b35"));
                        control.setDisabledBackgroundColor(QColor("#595966"));
                        control.setDisabledForegroundColor(QColor("#eeeeee"));
                        control.setBorderColor(QColor("#449955"));
                        control.setFocusColor(QColor("#5599cc"));
                        verify(control);
                        control.resetOverrides();
                        int colorIndex = 0;
#define IA_STATE_OVERRIDE(type, name, upper, group, token) \
                        control.set##upper(QColor::fromHsv((colorIndex++ * 23) % 360, 120, 190).toRgb());
                        IA_CONTROL_COLOR_PROPERTIES(IA_STATE_OVERRIDE)
#undef IA_STATE_OVERRIDE
                        verify(control); // every explicit pair is retained, even when low contrast
                        control.resetOverrides();
                        verify(control);
                    }
                }
            }
        }
    }
}

void AppearanceTests::neutralFocusBorder()
{
    Appearance source;
    for (const auto *scheme : {"light", "dark"}) {
        for (const auto *contrast : {"normal", "high"}) {
            const QVariantMap input{{QStringLiteral("scheme"), QLatin1String(scheme)},
                {QStringLiteral("contrast"), QLatin1String(contrast)}};
            source.setInput(input);
            const auto check = [&] {
                const QColor focus = source.colors()->focus();
                const QColor backdrop = opaqueOver(source.colors()->content(),
                    opaqueOver(source.colors()->canvas(), QColor(Qt::white)));
                for (const auto *role : {"content", "popup", "canvas", "control", "controlHover",
                         "controlPressed", "selection", "inactiveSelection"}) {
                    const auto fill = source.colors()->values().value(QLatin1String(role)).value<QColor>();
                    QVERIFY2(rgbContrast(focus, fill, backdrop) >= 3, role);
                }
                for (const auto *family : {"button", "tool", "field", "delegate", "tab"}) {
                    ControlAppearance control;
                    control.setSource(&source);
                    control.setFamily(QLatin1String(family));
                    ControlAppearance track;
                    track.setSource(&source);
                    track.setFamily(QStringLiteral("tabBar"));
                    const QColor familyBackdrop = control.family() == QLatin1String("tab")
                        ? opaqueOver(track.backgroundColor(), backdrop) : backdrop;
                    for (int i = Presentation::Rest; i < Presentation::Disabled; ++i) {
                        const auto entry = control.stateTable().colors(Presentation::State(i));
                        const QColor expected = rgbContrast(focus, entry.backgroundColor(), familyBackdrop) >= 3
                            ? focus : entry.foregroundColor();
                        QCOMPARE(entry.focusedBorderColor(), expected);
                        QVERIFY(rgbContrast(expected, entry.backgroundColor(), familyBackdrop) >= 3);
                    }
                    // Local colors still use the entry's foreground when shared
                    // focus is unreadable on the explicitly chosen fill.
                    control.setBackgroundColor(focus);
                    const auto local = control.stateTable().colors(Presentation::Rest);
                    QCOMPARE(local.focusedBorderColor(), local.foregroundColor());
                }
            };
            check();
            // Neutral input changes must participate in the shared derivation.
            auto overridden = input;
            overridden.insert(QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), QVariantMap{
                {QStringLiteral("control"), QColor(scheme == QLatin1String("light") ? "#cccccc" : "#555555")}}}});
            source.setInput(overridden);
            check();
            auto tokens = overridden.value(QStringLiteral("tokens")).toMap();
            auto colors = tokens.value(QStringLiteral("colors")).toMap();
            colors.insert(QStringLiteral("focus"), QColor("#80ff0000"));
            tokens.insert(QStringLiteral("colors"), colors);
            overridden.insert(QStringLiteral("tokens"), tokens);
            source.setInput(overridden);
            QCOMPARE(source.colors()->focus(), QColor("#80ff0000"));
        }
    }
}

void AppearanceTests::focusedBorderInputs()
{
    Appearance source;
    ControlAppearance control;
    control.setSource(&source);
    control.setFamily(QStringLiteral("field"));
    control.setBackgroundColor(QColor(Qt::transparent)); // Exercise the private builder's backdrop key.
    control.setFocusColor(QColor("#777777"));
    const auto check = [&] {
        const auto entry = control.stateTable().colors(Presentation::Rest);
        const QColor backdrop = opaqueOver(source.colors()->content(),
            opaqueOver(source.colors()->canvas(), QColor(Qt::white)));
        const auto expected = control.family() == QLatin1String("field")
            && control.validation() == Presentation::Invalid ? control.borderColor()
            : rgbContrast(control.focusColor(), entry.backgroundColor(), backdrop) >= 3
                ? control.focusColor() : entry.foregroundColor();
        QCOMPARE(entry.focusedBorderColor(), expected);
        QCOMPARE(entry.focusedBorderWidth(), source.metrics()->focusWidth());
    };
    check();
    source.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("content"), QColor("#777777")}}));
    check();
    control.setValidation(Presentation::Invalid);
    check();
    control.setBorderColor(QColor("red")); // Explicit invalid outline still wins.
    check();
    control.setFamily(QStringLiteral("button"));
    check();
    control.setFamily(QStringLiteral("field"));
    check();
    const auto retained = control.stateTable();
    QSignalSpy changed(&control, &ControlAppearance::stateTableChanged);
    WorkRecorder work;
    source.setInput({{QStringLiteral("tokens"), QVariantMap{
        {QStringLiteral("colors"), QVariantMap{{QStringLiteral("content"), QColor("#777777")}}},
        {QStringLiteral("metrics"), QVariantMap{{QStringLiteral("focusWidth"), 5}}}}}});
    QCOMPARE(changed.count(), 1);
    QCOMPARE(control.stateTable().colors(Presentation::Rest).focusedBorderWidth(), qreal(5));
    QCOMPARE(retained.colors(Presentation::Rest).focusedBorderWidth(), qreal(2));
    QCOMPARE(work.count(AppearanceWork::StateTableBuild), 0);
    QCOMPARE(work.count(AppearanceWork::PrivateStateTableBuild), 0);
    check();
    source.setInput({});
    control.resetOverrides();
    check();
}

void AppearanceTests::stateTableSharingAndLifetime()
{
    StateTable retained;
    QColor before;
    {
        Appearance appearance;
        ControlAppearance first, second;
        first.setSource(&appearance);
        second.setSource(&appearance);
        WorkRecorder work;
        first.setFamily(QStringLiteral("choice"));
        QCOMPARE(work.count(AppearanceWork::StateTableBuild), 1);
        second.setFamily(QStringLiteral("choice"));
        QCOMPARE(work.count(AppearanceWork::StateTableBuild), 1);
        second.setTone(Presentation::Neutral); // Auto is the same memoization slot
        QCOMPARE(work.count(AppearanceWork::StateTableBuild), 1);
        QCOMPARE(work.count(AppearanceWork::PrivateStateTableBuild), 0);
        QCOMPARE(first.stateTable(), second.stateTable());
        retained = first.stateTable();
        before = retained.colors(Presentation::Rest).backgroundColor();
        first.setBackgroundColor(QColor("purple"));
        QCOMPARE(work.count(AppearanceWork::PrivateStateTableBuild), 1);
        QVERIFY(first.stateTable() != second.stateTable());
        QCOMPARE(second.stateTable().colors(Presentation::Rest).backgroundColor(), before);
        QCOMPARE(retained.colors(Presentation::Rest).backgroundColor(), before);
        const auto privateSnapshot = first.stateTable();
        work.clear();
        first.setSpacing(17);
        first.setFontPixelSize(20);
        first.setBackgroundColor(QColor()); // invalid input affects diagnostics only
        QCOMPARE(work.count(AppearanceWork::StateTableBuild), 0);
        QCOMPARE(work.count(AppearanceWork::PrivateStateTableBuild), 0);
        QCOMPARE(first.stateTable(), privateSnapshot);
        first.resetOverrides();
        QCOMPARE(first.stateTable(), second.stateTable());
        QCOMPARE(work.count(AppearanceWork::StateTableBuild), 0);
        QCOMPARE(work.count(AppearanceWork::PrivateStateTableBuild), 0);
        work.clear();
        for (int i = 0; i < 100; ++i) {
            for (int state = Presentation::Rest; state <= Presentation::Disabled; ++state)
                QVERIFY(first.stateTable().colors(Presentation::State(state)).backgroundColor().isValid());
        }
        QCOMPARE(work.count(AppearanceWork::ColorResolution), 0);
        QCOMPARE(work.count(AppearanceWork::LocalDerivation), 0);
        QCOMPARE(work.count(AppearanceWork::LocalPairMeasurement), 0);
        QCOMPARE(work.count(AppearanceWork::StateTableBuild), 0);
        QCOMPARE(work.count(AppearanceWork::PrivateStateTableBuild), 0);
        // The public default/invalid index is safe and never indexes past the array.
        QVERIFY(!StateTable().colors(Presentation::Rest).backgroundColor().isValid());
        QVERIFY(!first.stateTable().colors(Presentation::State(-1)).backgroundColor().isValid());
        QVERIFY(!first.stateTable().colors(Presentation::State(9)).backgroundColor().isValid());
    }
    QCOMPARE(retained.colors(Presentation::Rest).backgroundColor(), before);
}

void AppearanceTests::stateTableQmlBinding()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import InterActive.Foundation
        ControlAppearance {
            source: Appearance
            property int currentState: Presentation.Rest
            readonly property color observedBackground: stateTable.colors(currentState).backgroundColor
            readonly property color observedForeground: stateTable.colors(currentState).foregroundColor
            readonly property color observedBorder: stateTable.colors(currentState).borderColor
            readonly property color observedFocus: stateTable.colors(currentState).focusColor
        }
    )", QUrl(QStringLiteral("inmemory:/state-table.qml")));
    QVERIFY(QTest::qWaitFor([&] { return !component.isLoading(); }, 10000));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *control = qobject_cast<ControlAppearance *>(object.get());
    QVERIFY(control);
    const auto check = [&] {
        const auto entry = control->stateTable().colors(Presentation::State(control->property("currentState").toInt()));
        QCOMPARE(control->property("observedBackground").value<QColor>(), entry.backgroundColor());
        QCOMPARE(control->property("observedForeground").value<QColor>(), entry.foregroundColor());
        QCOMPARE(control->property("observedBorder").value<QColor>(), entry.borderColor());
        QCOMPARE(control->property("observedFocus").value<QColor>(), entry.focusColor());
    };
    check();
    control->setBackgroundColor(QColor("purple"));
    check(); // A binding must update on a table change without a state change.
    for (int state = Presentation::Rest; state <= Presentation::Disabled; ++state) {
        QVERIFY(control->setProperty("currentState", state));
        check();
    }
    control->source()->setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}});
    check();
    control->resetOverrides();
    control->setTone(Presentation::Primary);
    control->setValidation(Presentation::Invalid);
    check();
}

void AppearanceTests::stateTableNotifications()
{
    ControlAppearance control;
    QSignalSpy changed(&control, &ControlAppearance::stateTableChanged);
    const StateTable original = control.stateTable();
    // Equal foreground ownership leaves all state colors equal. Equal base
    // ownership would legitimately rederive the neutral hover/pressed recipe.
    control.setForegroundColor(control.foregroundColor());
    QCOMPARE(changed.count(), 0); // Ownership alone does not change effective colors.
    control.resetForegroundColor();
    QCOMPARE(changed.count(), 0);
    control.setSpacing(17);
    QCOMPARE(changed.count(), 0);
    control.setFontPixelSize(20); // Font/geometry still are not in the state table.
    control.setControlRadius(9);
    QCOMPARE(changed.count(), 0);
    // S4 adds the fill role: its table consumers must be notified exactly once.
    control.setFillColor(QColor("red"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(control.stateTable().colors(Presentation::Rest).fillColor(), QColor("red"));
    QCOMPARE(control.stateTable().colors(Presentation::Disabled).fillColor(), control.disabledForegroundColor());
    changed.clear();
    control.setBackgroundColor(QColor("purple"));
    QCOMPARE(changed.count(), 1);
    connect(&control, &ControlAppearance::stateTableChanged, &control, [&] {
        if (control.stateTable().colors(Presentation::Rest).backgroundColor() == QColor("blue"))
            control.setBackgroundColor(QColor("green"));
    });
    control.setBackgroundColor(QColor("blue"));
    QCOMPARE(changed.count(), 3); // Nested table installs retain their notification obligation.
    QCOMPARE(control.stateTable().colors(Presentation::Rest).backgroundColor(), QColor("green"));
    QVERIFY(control.stateTable() != original);
}

void AppearanceTests::valueAndScrollRoles()
{
    Appearance source;
    ControlAppearance slider, progress, scroll, peer;
    for (auto *control : {&slider, &progress, &scroll, &peer})
        control->setSource(&source);
    slider.setFamily(QStringLiteral("slider"));
    progress.setFamily(QStringLiteral("progress"));
    scroll.setFamily(QStringLiteral("scrollBar"));
    peer.setFamily(QStringLiteral("scrollBar"));
    for (const auto *scheme : {"light", "dark"}) {
        for (const auto *contrast : {"normal", "high"}) {
            source.setInput(paletteInput(QLatin1String(scheme), QLatin1String(contrast), {}));
            const auto *colors = source.colors();
            const QColor backdrop = opaqueOver(colors->content(), colors->canvas());
            const auto rest = scroll.stateTable().colors(Presentation::Rest);
            const auto hover = scroll.stateTable().colors(Presentation::Hover);
            const auto pressed = scroll.stateTable().colors(Presentation::Pressed);
            QCOMPARE(rest.thumbColor(), colors->controlPressed());
            QCOMPARE(hover.thumbColor(), colors->outline());
            QCOMPARE(pressed.thumbColor(), colors->secondaryText());
            QVERIFY(visibleDistance(rest.thumbColor(), hover.thumbColor(), backdrop) >= 4.0);
            QVERIFY(visibleDistance(rest.thumbColor(), pressed.thumbColor(), backdrop) >= 4.0);
            QVERIFY(rest.thumbColor() != colors->text());
            QVERIFY(scroll.diagnostics().isEmpty()); // Decorative thumb, not unreadable text.
            QCOMPARE(scroll.stateTable(), peer.stateTable());
            for (const auto state : {Presentation::Rest, Presentation::Hover, Presentation::Pressed}) {
                QCOMPARE(scroll.stateTable().colors(state).trackColor().alpha(), 0);
                const auto knob = slider.stateTable().colors(state);
                QCOMPARE(knob.trackColor(), colors->controlPressed());
                QVERIFY(knob.trackColor() != colors->outline());
                QVERIFY(knob.trackColor() != colors->text());
                QCOMPARE(knob.fillColor(), colors->accent());
                QCOMPARE(knob.thumbColor(), knob.backgroundColor());
                QCOMPARE(progress.stateTable().colors(state).trackColor(), colors->controlPressed());
            }
            const auto sliderRest = slider.stateTable().colors(Presentation::Rest);
            QVERIFY(visibleDistance(sliderRest.thumbColor(), slider.stateTable().colors(Presentation::Hover).thumbColor(), backdrop) >= 4);
            QCOMPARE(slider.stateTable().colors(Presentation::Disabled).fillColor(), slider.disabledForegroundColor());
            QCOMPARE(slider.stateTable().colors(Presentation::Disabled).trackColor(), slider.disabledBackgroundColor());
            QCOMPARE(scroll.stateTable().colors(Presentation::Disabled).thumbColor(), scroll.disabledForegroundColor());
        }
    }
    QSignalSpy sliderChanged(&slider, &ControlAppearance::stateTableChanged);
    QSignalSpy scrollChanged(&scroll, &ControlAppearance::stateTableChanged);
    source.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("controlPressed"), QColor("purple")}}));
    QVERIFY(sliderChanged.count() > 0);
    QVERIFY(scrollChanged.count() > 0);
    QCOMPARE(slider.stateTable().colors(Presentation::Rest).trackColor(), QColor("purple"));
    QCOMPARE(progress.stateTable().colors(Presentation::Rest).trackColor(), QColor("purple"));
    scroll.setForegroundColor(QColor("green"));
    progress.setBackgroundColor(QColor("yellow"));
    slider.setBorderColor(QColor("red"));
    for (const auto state : {Presentation::Rest, Presentation::Hover, Presentation::Pressed})
        QCOMPARE(scroll.stateTable().colors(state).thumbColor(), QColor("green"));
    QCOMPARE(progress.stateTable().colors(Presentation::Rest).trackColor(), QColor("yellow"));
    QCOMPARE(slider.stateTable().colors(Presentation::Rest).trackColor(), QColor("purple"));
    const WorkRecorder recorder;
    for (int i = 0; i < 100; ++i) {
        const auto state = Presentation::State(i % 9);
        QVERIFY(scroll.stateTable().colors(state).thumbColor().isValid());
        QVERIFY(slider.stateTable().colors(state).thumbColor().isValid());
    }
    QCOMPARE(recorder.count(AppearanceWork::StateTableBuild), quint64(0));
    QCOMPARE(recorder.count(AppearanceWork::PrivateStateTableBuild), quint64(0));
    scroll.resetForegroundColor();
    QCOMPARE(scroll.stateTable(), peer.stateTable());
    scroll.setBackgroundColor(QColor("yellow"));
    scroll.setHoverBackgroundColor(QColor("red"));
    QCOMPARE(scroll.stateTable().colors(Presentation::Rest).trackColor(), QColor("yellow"));
    QCOMPARE(scroll.stateTable().colors(Presentation::Hover).trackColor(), QColor("red"));
    QCOMPARE(scroll.stateTable().colors(Presentation::Rest).thumbColor(), peer.stateTable().colors(Presentation::Rest).thumbColor());
    QCOMPARE(scroll.stateTable().colors(Presentation::Hover).thumbColor(), peer.stateTable().colors(Presentation::Hover).thumbColor());
    progress.resetBackgroundColor();
    QCOMPARE(progress.stateTable().colors(Presentation::Rest).trackColor(), QColor("purple"));
}

void AppearanceTests::supplementaryStateRoles()
{
    Appearance source;
    ControlAppearance field, label;
    field.setSource(&source);
    field.setFamily(QStringLiteral("field"));
    label.setSource(&source);
    label.setFamily(QStringLiteral("label"));
    QSignalSpy fieldChanged(&field, &ControlAppearance::stateTableChanged);
    QSignalSpy labelChanged(&label, &ControlAppearance::stateTableChanged);
    const auto verify = [&] {
        QCOMPARE(field.stateTable().selectionColor(), source.colors()->accent());
        QCOMPARE(field.stateTable().selectedTextColor(), source.colors()->onAccent());
        QCOMPARE(label.stateTable().linkColor(), source.colors()->link());
        for (int i = Presentation::Rest; i <= Presentation::Disabled; ++i) {
            const bool disabled = i == Presentation::Disabled;
            const auto colors = field.stateTable().colors(Presentation::State(i));
            QCOMPARE(colors.fillColor(), disabled ? field.disabledForegroundColor() : field.fillColor());
            QCOMPARE(colors.trackColor(), disabled ? field.disabledBackgroundColor() : field.borderColor());
            QCOMPARE(colors.placeholderTextColor(), disabled ? field.disabledForegroundColor() : source.colors()->placeholderText());
        }
    };
    verify();
    field.setBackgroundColor(QColor("purple")); // Also exercise private-table invalidation.
    verify();
    fieldChanged.clear();
    labelChanged.clear();
    source.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("placeholderText"), QColor("green")},
        {QStringLiteral("onAccent"), QColor("yellow")}, {QStringLiteral("link"), QColor("red")}}));
    QCOMPARE(fieldChanged.count(), 1);
    QCOMPARE(labelChanged.count(), 1);
    verify();
    source.setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}});
    verify();
    field.setFillColor(QColor("blue")); // Field selection deliberately ignores local fill overrides.
    verify();
    field.resetOverrides();
    verify();
}

void AppearanceTests::stateSweepDoesNoFoundationWork()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import InterActive.Foundation
        Item {
            property bool available: true
            property bool down: false
            property bool hovered: false
            property bool selected: false
            property bool active: true
            property bool inset: false
            ControlAppearance { id: facade; source: Appearance }
            ControlAppearance { id: choiceFacade; objectName: "choiceFacade"; source: Appearance; family: "choice" }
            ControlBackground {
                id: surface
                resolvedAppearance: facade
                controlEnabled: parent.available
                down: parent.down; hovered: parent.hovered
                selected: parent.selected; active: parent.active; pressInset: parent.inset
            }
            ChoiceIndicator {
                id: indicator
                resolvedAppearance: choiceFacade
                controlEnabled: parent.available
                down: parent.down; hovered: parent.hovered
                checked: parent.selected; active: parent.active
            }
            readonly property color observed: surface.color
            readonly property color foreground: surface.foregroundColor
            readonly property color observedChoice: indicator.color
            readonly property color choiceForeground: indicator.markColor
            readonly property color choiceCenter: indicator.mark.color
        }
    )", QUrl(QStringLiteral("inmemory:/state-sweep.qml")));
    QVERIFY(QTest::qWaitFor([&] { return !component.isLoading(); }, 10000));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *facade = object->findChild<ControlAppearance *>();
    QVERIFY(facade);
    auto *choiceFacade = object->findChild<ControlAppearance *>(QStringLiteral("choiceFacade"));
    QVERIFY(choiceFacade);
    WorkRecorder work;
    facade->setSpacing(facade->spacing() + 1);
    QCOMPARE(work.count(AppearanceWork::FacadeUpdate), 1); // Positive control for the scope recorder.
    work.clear();
    for (int flags = 0; flags < 64; ++flags) {
        const bool enabled = flags & 1, down = flags & 2, hover = flags & 4;
        const bool selected = flags & 8, active = flags & 16, inset = flags & 32;
        object->setProperty("available", enabled);
        object->setProperty("down", down);
        object->setProperty("hovered", hover);
        object->setProperty("selected", selected);
        object->setProperty("active", active);
        object->setProperty("inset", inset);
        const QColor background = enabled && selected && active && hover && (!down || inset)
            ? facade->stateTable().colors(Presentation::SelectedHover).backgroundColor()
            : !enabled ? facade->disabledBackgroundColor()
            : down && !inset ? facade->pressedBackgroundColor()
            : selected ? (active ? facade->selectedBackgroundColor() : facade->inactiveBackgroundColor())
            : hover ? facade->hoverBackgroundColor() : facade->backgroundColor();
        const QColor foreground = !enabled ? facade->disabledForegroundColor()
            : down && !inset ? facade->foregroundColor()
            : selected ? (active ? facade->selectedForegroundColor() : facade->inactiveForegroundColor())
            : facade->foregroundColor();
        QCOMPARE(object->property("observed").value<QColor>(), background);
        QCOMPARE(object->property("foreground").value<QColor>(), foreground);
        const auto choiceState = !enabled ? Presentation::Disabled
            : down ? (selected ? Presentation::SelectedPressed : Presentation::Pressed)
            : selected ? (active ? (hover ? Presentation::SelectedHover : Presentation::Selected)
                : (hover ? Presentation::InactiveHover : Presentation::Inactive))
            : hover ? Presentation::Hover : Presentation::Rest;
        const auto choiceColors = choiceFacade->stateTable().colors(choiceState);
        const QColor choiceForeground = selected ? choiceColors.choiceMarkColor() : choiceColors.foregroundColor();
        QCOMPARE(object->property("observedChoice").value<QColor>(), QColor(Qt::transparent));
        QCOMPARE(object->property("choiceForeground").value<QColor>(), choiceForeground);
        QCOMPARE(object->property("choiceCenter").value<QColor>(), selected
            ? choiceColors.choiceCenterColor() : choiceColors.backgroundColor());
    }
    QCOMPARE(work.count(AppearanceWork::FacadeUpdate), 0);
    QCOMPARE(work.count(AppearanceWork::ColorResolution), 0);
    QCOMPARE(work.count(AppearanceWork::LocalDerivation), 0);
    QCOMPARE(work.count(AppearanceWork::LocalPairMeasurement), 0);
    QCOMPARE(work.count(AppearanceWork::StateTableBuild), 0);
    QCOMPARE(work.count(AppearanceWork::PrivateStateTableBuild), 0);
}

void AppearanceTests::familyDescriptorContract()
{
    struct Family {
        const char *name;
        const char *background;
        qreal height;
        bool interactive;
    };
    const Family families[]{
        {"button", "control", 100, true}, {"tool", "transparent", 110, true},
        {"field", "content", 100, true}, {"surface", "content", 0, false},
        {"popup", "popup", 0, false}, {"label", "transparent", 0, false},
        {"delegate", "transparent", 120, true}, {"tab", "transparent", 120, true},
        {"choice", "transparent", 100, true}, {"tooltip", "tooltipBackground", 0, false}};
    Appearance appearance;
    for (const auto *scheme : {"light", "dark"}) {
        for (const auto *density : {"compact", "touch"}) {
            appearance.setInput({{QStringLiteral("scheme"), QString::fromLatin1(scheme)},
                {QStringLiteral("density"), QString::fromLatin1(density)},
                {QStringLiteral("tokens"), QVariantMap{
                    {QStringLiteral("metrics"), QVariantMap{{QStringLiteral("controlHeight"), 100},
                        {QStringLiteral("toolHeight"), 110}, {QStringLiteral("rowHeight"), 120},
                        {QStringLiteral("targetSize"), 200}, {QStringLiteral("controlRadius"), 5},
                        {QStringLiteral("popupRadius"), 9}, {QStringLiteral("horizontalPadding"), 3},
                        {QStringLiteral("verticalPadding"), 7}, {QStringLiteral("popupPadding"), 11},
                        {QStringLiteral("spacing"), 13}}},
                    {QStringLiteral("typography"), QVariantMap{{QStringLiteral("controlSize"), 14},
                        {QStringLiteral("supportingSize"), 10}, {QStringLiteral("controlWeight"), 450}}}}}});
            const auto colors = appearance.colors()->values();
            const auto color = [&](const QString &role) { return colors.value(role).value<QColor>(); };
            for (const auto &family : families) {
                ControlAppearance control;
                control.setSource(&appearance);
                control.setFamily(QLatin1String(family.name));
                QCOMPARE(control.family(), QLatin1String(family.name));
                const bool popup = control.family() == QLatin1String("popup");
                const bool tooltip = control.family() == QLatin1String("tooltip");
                const bool label = control.family() == QLatin1String("label");
                QCOMPARE(control.controlRadius(), popup ? 9.0 : 5.0);
                QCOMPARE(control.horizontalPadding(), popup ? 11.0 : control.family() == QLatin1String("choice") ? 7.0 : 3.0);
                QCOMPARE(control.verticalPadding(), popup ? 11.0 : 7.0);
                QCOMPARE(control.spacing(), 13.0);
                QCOMPARE(control.fontPixelSize(), tooltip ? 10.0 : 14.0);
                QCOMPARE(control.fontWeight(), 450);
                const qreal target = family.interactive && qstrcmp(density, "touch") == 0 ? 200 : 0;
                QCOMPARE(control.minimumHeight(), std::max({family.height, target,
                    std::ceil(QFontMetricsF(control.resolvedFont()).height()) + 2 * control.verticalPadding()}));
                for (const auto tone : {Presentation::Auto, Presentation::Neutral, Presentation::Primary,
                         Presentation::Destructive, Presentation::Caution}) {
                    control.setTone(tone);
                    const QString semantic = tone == Presentation::Primary ? QStringLiteral("primary")
                        : tone == Presentation::Destructive ? QStringLiteral("destructive")
                        : tone == Presentation::Caution ? QStringLiteral("caution") : QString();
                    const bool colored = !semantic.isEmpty();
                    const QString background = colored && !label ? semantic + QStringLiteral("Surface") : QString::fromLatin1(family.background);
                    const QString foreground = colored ? (label
                        ? tone == Presentation::Primary ? QStringLiteral("accentText")
                            : tone == Presentation::Destructive ? QStringLiteral("errorText") : QStringLiteral("warningText")
                        : semantic + QStringLiteral("Text")) : tooltip ? QStringLiteral("tooltipText") : QStringLiteral("text");
                    const QString hover = colored && !label ? semantic + QStringLiteral("Hover")
                        : tooltip ? QStringLiteral("tooltipBackground") : QStringLiteral("controlHover");
                    const QString pressed = colored && !label ? semantic + QStringLiteral("Pressed")
                        : tooltip ? QStringLiteral("tooltipBackground") : QStringLiteral("controlPressed");
                    for (const auto validation : {Presentation::None, Presentation::Warning, Presentation::Invalid}) {
                        control.setValidation(validation);
                        QCOMPARE(control.backgroundColor(), color(background));
                        QCOMPARE(control.foregroundColor(), color(foreground));
                        QColor backdrop = opaqueOver(appearance.colors()->content(), opaqueOver(appearance.colors()->canvas(), QColor(Qt::white)));
                        const bool tab = control.family() == QLatin1String("tab");
                        ControlAppearance track;
                        if (tab) {
                            track.setSource(&appearance);
                            track.setFamily(QStringLiteral("tabBar"));
                            backdrop = opaqueOver(track.backgroundColor(), backdrop);
                        }
                        const QColor segmentSeed = color(qstrcmp(scheme, "dark") == 0
                            ? QStringLiteral("controlHover") : QStringLiteral("content"));
                        const QColor selectedSeed = tab && !colored ? segmentSeed
                            : color(colored && !label ? semantic + QStringLiteral("Pressed") : QStringLiteral("selection"));
                        const QColor expectedHover = family.interactive
                            ? stateFeedback(color(background), color(foreground), appearance.motion()->hoverBlend(), backdrop, 5,
                                tab ? QList<QColor>{color(background), selectedSeed} : QList<QColor>{color(background)})
                            : color(hover);
                        QCOMPARE(control.hoverBackgroundColor(), expectedHover);
                        QCOMPARE(control.pressedBackgroundColor(), family.interactive
                            ? stateFeedback(color(background), color(foreground), appearance.motion()->pressedBlend(), backdrop, 5) : color(pressed));
                        const bool selectable = family.interactive && control.family() != QLatin1String("field");
                        const bool choice = control.family() == QLatin1String("choice");
                        const QColor choiceSeed = TokenSchema::contrastingText(appearance.colors()->accent(), {QColor(Qt::white)}, backdrop, 7);
                        QCOMPARE(control.selectedBackgroundColor(), choice
                            ? stateFeedback(choiceSeed, QColor(Qt::white), appearance.motion()->pressedBlend(), backdrop, 5,
                                {color(background), expectedHover}, true) : selectable
                            ? stateFeedback(selectedSeed, control.selectedForegroundColor(), appearance.motion()->pressedBlend(), backdrop, 5,
                                {color(background), expectedHover}, true) : selectedSeed);
                        QCOMPARE(control.inactiveBackgroundColor(), choice ? choiceSeed
                            : tab && !colored ? segmentSeed
                            : color(colored && !label ? semantic + QStringLiteral("Surface") : QStringLiteral("inactiveSelection")));
                        QCOMPARE(control.selectedForegroundColor(), choice ? QColor(Qt::white)
                            : color(colored && !label ? foreground : QStringLiteral("text")));
                        QCOMPARE(control.inactiveForegroundColor(), control.selectedForegroundColor());
                        const QString border = validation == Presentation::Invalid ? QStringLiteral("errorText")
                            : validation == Presentation::Warning ? QStringLiteral("warningText")
                            : colored && !label ? semantic + QStringLiteral("Outline") : QStringLiteral("outline");
                        QCOMPARE(control.borderColor(), color(border));
                        QCOMPARE(control.disabledBorderColor(), color(validation == Presentation::None ? QStringLiteral("outline") : border));
                        QCOMPARE(control.disabledBackgroundColor(), color(background).alpha() == 0
                            ? QColor(Qt::transparent) : appearance.colors()->disabledSurface());
                        QCOMPARE(control.disabledForegroundColor(), appearance.colors()->disabledText());
                        QCOMPARE(control.focusColor(), appearance.colors()->focus());
                        QCOMPARE(control.fillColor(), appearance.colors()->accent());
                    }
                }
            }
        }
    }
}

void AppearanceTests::typedFacadeNotificationOrder()
{
    ControlAppearance control;
    QStringList names{QStringLiteral("source"), QStringLiteral("family"), QStringLiteral("tone"), QStringLiteral("validation")};
#define IA_TEST_NAME(type, name, upper, group, token) names.append(QStringLiteral(#name));
    IA_CONTROL_PROPERTIES(IA_TEST_NAME)
#undef IA_TEST_NAME
    names.append(QStringLiteral("resolvedFont"));
    names.append(QStringLiteral("diagnostics"));
    QStringList observed;
    connect(&control, &ControlAppearance::resolvedChanged, this, [&] { observed.append(QStringLiteral("resolvedChanged")); });
#define IA_TEST_OBSERVE(name) \
    connect(&control, &ControlAppearance::name##Changed, this, [&] { observed.append(QStringLiteral(#name "Changed")); });
    IA_TEST_OBSERVE(source)
    IA_TEST_OBSERVE(family)
    IA_TEST_OBSERVE(tone)
    IA_TEST_OBSERVE(validation)
#define IA_TEST_PROPERTY(type, name, upper, group, token) IA_TEST_OBSERVE(name)
    IA_CONTROL_PROPERTIES(IA_TEST_PROPERTY)
#undef IA_TEST_PROPERTY
    IA_TEST_OBSERVE(resolvedFont)
    IA_TEST_OBSERVE(diagnostics)
#undef IA_TEST_OBSERVE
    const auto snapshot = [&] {
        QVariantMap values;
        for (const auto &name : names)
            values.insert(name, control.property(name.toLatin1().constData()));
        return values;
    };
    const auto check = [&](const std::function<void()> &change) {
        const auto before = snapshot();
        observed.clear();
        change();
        const auto after = snapshot();
        QStringList expected{QStringLiteral("resolvedChanged")};
        for (const auto &name : names) {
            if (before.value(name) != after.value(name))
                expected.append(name + QStringLiteral("Changed"));
        }
        QCOMPARE(observed, expected);
    };
    auto appearance = std::make_unique<Appearance>();
    check([&] { control.setSource(appearance.get()); });
    check([&] { control.setFamily(QStringLiteral("popup")); });
    check([&] { control.setTone(Presentation::Primary); });
    check([&] { control.setValidation(Presentation::Invalid); });
    check([&] { appearance->setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}, {QStringLiteral("fontScale"), 1.5}}); });
    check([&] { control.setFamily(QStringLiteral(" Popup ")); });
    check([&] { control.setFamily(QStringLiteral("popup")); });
    check([&] { control.setSpacing(-1); });
    check([&] { control.resetSpacing(); });
    check([&] { appearance.reset(); });
}

void AppearanceTests::typedFacadePendingReversion()
{
    Appearance appearance;
    appearance.setInput({{QStringLiteral("fontScale"), 2},
        {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("metrics"), QVariantMap{{QStringLiteral("controlRadius"), 10}}}}}});
    ControlAppearance control;
    const qreal initialSize = control.fontPixelSize(), initialRadius = control.controlRadius();
    const auto reenter = connect(&control, &ControlAppearance::sourceChanged, this, [&] {
        control.setFontPixelSize(initialSize);
        control.setControlRadius(initialRadius);
    });
    QSignalSpy size(&control, &ControlAppearance::fontPixelSizeChanged);
    QSignalSpy radius(&control, &ControlAppearance::controlRadiusChanged);
    QSignalSpy font(&control, &ControlAppearance::resolvedFontChanged);
    control.setSource(&appearance);
    QCOMPARE(control.fontPixelSize(), initialSize);
    QCOMPARE(control.controlRadius(), initialRadius);
    QCOMPARE(size.count(), 0);
    QCOMPARE(radius.count(), 0);
    QCOMPARE(font.count(), 0);
    disconnect(reenter);
    QSignalSpy family(&control, &ControlAppearance::familyChanged);
    connect(&control, &ControlAppearance::spacingChanged, this, [&] { control.setFamily(QStringLiteral("label")); });
    control.setSpacing(17);
    QCOMPARE(control.family(), QStringLiteral("label"));
    QCOMPARE(family.count(), 1); // A nested obligation earlier than the current drain position.
}

void AppearanceTests::preciseAtomicCallbacks()
{
    Appearance appearance;
    ControlAppearance facade;
    facade.setSource(&appearance);
    facade.setTone(Presentation::Primary);
    const QVariantMap input{{QStringLiteral("scheme"), QStringLiteral("dark")},
        {QStringLiteral("fontScale"), 2}, {QStringLiteral("reducedMotion"), true},
        {QStringLiteral("tokens"), QVariantMap{
            {QStringLiteral("colors"), QVariantMap{{QStringLiteral("accent"), QColor("#8657b8")}}},
            {QStringLiteral("metrics"), QVariantMap{{QStringLiteral("controlRadius"), 10}}}}}};
    int globalObservations = 0, localObservations = 0;
    const auto global = [&] {
        ++globalObservations;
        QCOMPARE(appearance.input(), input);
        QCOMPARE(appearance.scheme(), QStringLiteral("dark"));
        QCOMPARE(appearance.fontScale(), 2.0);
        QCOMPARE(appearance.colors()->content(), QColor("#27292d"));
        QCOMPARE(appearance.colors()->accent(), QColor("#8657b8"));
        QCOMPARE(appearance.metrics()->controlRadius(), 10.0);
        QCOMPARE(appearance.typography()->controlSize(), 26.0);
        QCOMPARE(appearance.motion()->feedbackDuration(), 0);
        QCOMPARE(appearance.diagnostics(), appearance.snapshot().diagnostics);
    };
    const auto local = [&] {
        ++localObservations;
        QCOMPARE(facade.backgroundColor(), appearance.colors()->primarySurface());
        QCOMPARE(facade.foregroundColor(), appearance.colors()->primaryText());
        QCOMPARE(facade.borderColor(), appearance.colors()->primaryOutline());
        QCOMPARE(facade.fontPixelSize(), 26.0);
        QCOMPARE(facade.resolvedFont().pixelSize(), 26);
        QCOMPARE(facade.controlRadius(), 10.0);
        QVERIFY(facade.minimumHeight() >= std::ceil(QFontMetricsF(facade.resolvedFont()).height()) + 2 * facade.verticalPadding());
    };
#define IA_OBSERVE_COLOR(name, light, dark) connect(appearance.colors(), &ColorTokens::name##Changed, this, global);
    IA_COLOR_TOKENS(IA_OBSERVE_COLOR)
#undef IA_OBSERVE_COLOR
#define IA_OBSERVE_METRIC(name, value, low, high) connect(appearance.metrics(), &MetricTokens::name##Changed, this, global);
    IA_METRIC_TOKENS(IA_OBSERVE_METRIC)
#undef IA_OBSERVE_METRIC
    connect(&appearance, &Appearance::schemeChanged, this, global);
    connect(&appearance, &Appearance::fontScaleChanged, this, global);
    connect(&appearance, &Appearance::reducedMotionChanged, this, global);
    connect(appearance.typography(), &TypographyTokens::controlSizeChanged, this, global);
    connect(appearance.motion(), &MotionTokens::feedbackDurationChanged, this, global);
    connect(&appearance, &Appearance::resolvedChanged, this, global);
#define IA_OBSERVE_LOCAL(type, name, upper, group, token) connect(&facade, &ControlAppearance::name##Changed, this, local);
    IA_CONTROL_PROPERTIES(IA_OBSERVE_LOCAL)
#undef IA_OBSERVE_LOCAL
    connect(&facade, &ControlAppearance::resolvedFontChanged, this, local);
    connect(&facade, &ControlAppearance::diagnosticsChanged, this, local);
    connect(&facade, &ControlAppearance::resolvedChanged, this, local);
    appearance.setInput(input);
    QVERIFY(globalObservations > 5);
    QVERIFY(localObservations > 5);
}

void AppearanceTests::equalSourceAndOwnershipNotifications()
{
    Appearance first, second;
    ControlAppearance facade;
    facade.setSource(&first);
    facade.setTone(Presentation::Primary);
    facade.setBackgroundColor(QColor("#8657b8"));
    const QColor localText = facade.foregroundColor();
    QSignalSpy source(&facade, &ControlAppearance::sourceChanged);
    QSignalSpy snapshots(&facade, &ControlAppearance::resolvedChanged);
    QSignalSpy background(&facade, &ControlAppearance::backgroundColorChanged);
    QSignalSpy text(&facade, &ControlAppearance::foregroundColorChanged);
    QSignalSpy font(&facade, &ControlAppearance::resolvedFontChanged);
    WorkRecorder work;
    facade.setSource(&second);
    QCOMPARE(source.count(), 1);
    QCOMPARE(snapshots.count(), 1);
    QCOMPARE(background.count(), 0);
    QCOMPARE(text.count(), 0);
    QCOMPARE(font.count(), 0);
    QCOMPARE(work.count(AppearanceWork::LocalDerivation), 0);
    QCOMPARE(work.count(AppearanceWork::FontMeasurement), 0);
    const QColor inheritedOutline = second.colors()->primaryOutline();
    second.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("primaryOutline"), inheritedOutline}}));
    QCOMPARE(facade.borderColor(), inheritedOutline);
    QCOMPARE(facade.foregroundColor(), localText);
    QCOMPARE(work.count(AppearanceWork::LocalDerivation), 0);
    second.setInput({});
    QCOMPARE(facade.borderColor(), localText);
    QCOMPARE(background.count(), 0);
    QCOMPARE(text.count(), 0);
    facade.setForegroundColor(localText);
    facade.setForegroundColor(QColor());
    QVERIFY(hasDiagnostic(facade.diagnostics(), QStringLiteral("invalid-value"), QStringLiteral("foregroundColor")));
    QCOMPARE(facade.foregroundColor(), localText);
    facade.resetForegroundColor();
    QVERIFY(!hasDiagnostic(facade.diagnostics(), QStringLiteral("invalid-value"), QStringLiteral("foregroundColor")));
    QCOMPARE(facade.foregroundColor(), localText);
    facade.resetOverrides();
    QCOMPARE(facade.backgroundColor(), second.colors()->primarySurface());
}

void AppearanceTests::localDependencyChanges()
{
    Appearance appearance;
    ControlAppearance facade;
    facade.setSource(&appearance);
    facade.setTone(Presentation::Primary);
    facade.setBackgroundColor(QColor::fromRgbF(.41, .23, .62, .57));
    facade.setSelectedBackgroundColor(QColor("#111122"));
    facade.setInactiveBackgroundColor(QColor("#ddeeff"));
    WorkRecorder work;
    const auto assertPairs = [&](qreal target) {
        const QColor backdrop = opaqueOver(appearance.colors()->content(), opaqueOver(appearance.colors()->canvas(), QColor(Qt::white)));
        for (const auto *surface : {"backgroundColor", "hoverBackgroundColor", "pressedBackgroundColor"})
            verifyContrastDiagnostic(facade.diagnostics(), QStringLiteral("foregroundColor/") + QLatin1String(surface),
                facade.foregroundColor(), facade.property(surface).value<QColor>(), backdrop, target);
        verifyContrastDiagnostic(facade.diagnostics(), QStringLiteral("selectedForegroundColor/selectedBackgroundColor"),
            facade.selectedForegroundColor(), facade.selectedBackgroundColor(), backdrop, target);
        verifyContrastDiagnostic(facade.diagnostics(), QStringLiteral("inactiveForegroundColor/inactiveBackgroundColor"),
            facade.inactiveForegroundColor(), facade.inactiveBackgroundColor(), backdrop, target);
    };
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("content"), QColor("#27292d")},
        {QStringLiteral("canvas"), QColor("#202124")}}));
    QVERIFY(work.count(AppearanceWork::LocalDerivation) >= 3);
    assertPairs(5);
    work.clear();
    const QList<QColor> normalLabels{facade.foregroundColor(), facade.selectedForegroundColor(), facade.inactiveForegroundColor()};
    auto input = appearance.input();
    input.insert(QStringLiteral("contrast"), QStringLiteral("high"));
    appearance.setInput(input);
    QCOMPARE(work.count(AppearanceWork::LocalDerivation), 0); // Label target remains 7; only the diagnostic floor changes.
    QCOMPARE((QList<QColor>{facade.foregroundColor(), facade.selectedForegroundColor(), facade.inactiveForegroundColor()}), normalLabels);
    QVERIFY(work.count(AppearanceWork::LocalPairMeasurement) >= 3);
    assertPairs(7);
    work.clear();
    const QColor selected = facade.selectedForegroundColor();
    auto tokens = input.value(QStringLiteral("tokens")).toMap();
    tokens.insert(QStringLiteral("motion"), QVariantMap{{QStringLiteral("hoverBlend"), .27}, {QStringLiteral("pressedBlend"), .39}});
    input.insert(QStringLiteral("tokens"), tokens);
    appearance.setInput(input);
    QVERIFY(work.count(AppearanceWork::LocalDerivation) > 0);
    QCOMPARE(facade.selectedForegroundColor(), selected);
    assertPairs(7);
    work.clear();
    auto colors = tokens.value(QStringLiteral("colors")).toMap();
    colors.insert(QStringLiteral("accent"), QColor("#20845f"));
    tokens.insert(QStringLiteral("colors"), colors);
    input.insert(QStringLiteral("tokens"), tokens);
    appearance.setInput(input);
    QCOMPARE(work.count(AppearanceWork::LocalDerivation), 0);
    QCOMPARE(work.count(AppearanceWork::FontMeasurement), 0);
    QCOMPARE(work.count(AppearanceWork::GeometryCalculation), 0);
    assertPairs(7);
}

void AppearanceTests::sharedPaletteWork_data()
{
    QTest::addColumn<int>("controls");
    QTest::newRow("one") << 1;
    QTest::newRow("hundred") << 100;
    QTest::newRow("thousand") << 1000;
}

void AppearanceTests::sharedPaletteWork()
{
    QFETCH(int, controls);
    Appearance appearance;
    WorkRecorder work;
    std::vector<std::unique_ptr<ControlAppearance>> facades;
    for (int i = 0; i < controls; ++i) {
        auto facade = std::make_unique<ControlAppearance>();
        facade->setSource(&appearance);
        facade->setTone(Presentation::Primary);
        facades.push_back(std::move(facade));
    }
    QVERIFY(work.count(AppearanceWork::SharedPaletteBuild) <= 1); // immutable fallback, once only
    QCOMPARE(work.count(AppearanceWork::LocalDerivation), 0);
    QCOMPARE(work.count(AppearanceWork::LocalPairMeasurement), 0);
    work.clear();
    const auto input = tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QColor("#8657b8")}});
    appearance.setInput(input);
    QCOMPARE(work.count(AppearanceWork::SharedPaletteBuild), 1);
    QCOMPARE(work.count(AppearanceWork::LocalDerivation), 0);
    QCOMPARE(work.count(AppearanceWork::LocalPairMeasurement), 0);
    QCOMPARE(work.count(AppearanceWork::FontConstruction), 0);
    QCOMPARE(work.count(AppearanceWork::FontMeasurement), 0);
    QCOMPARE(work.count(AppearanceWork::GeometryCalculation), 0);
    for (const auto &facade : facades) {
        QCOMPARE(facade->backgroundColor(), appearance.colors()->primarySurface());
        const QColor backdrop = opaqueOver(appearance.colors()->content(), opaqueOver(appearance.colors()->canvas(), QColor(Qt::white)));
        QCOMPARE(facade->selectedBackgroundColor(), stateFeedback(appearance.colors()->primaryPressed(), facade->selectedForegroundColor(),
            appearance.motion()->pressedBlend(), backdrop, 5, {facade->backgroundColor(), facade->hoverBackgroundColor()}, true));
        QCOMPARE(facade->inactiveForegroundColor(), appearance.colors()->primaryText());
    }
    work.clear();
    appearance.setInput(input);
    QCOMPARE(work.count(AppearanceWork::SharedPaletteBuild), 0);
    appearance.setInput({{QStringLiteral("schemaVersion"), 2}});
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("rejected-input")));
    appearance.setInput(input);
    QVERIFY(!hasDiagnostic(appearance.diagnostics(), QStringLiteral("rejected-input")));
    QCOMPARE(work.count(AppearanceWork::SharedPaletteBuild), 0);
    auto unrelated = input;
    unrelated.insert(QStringLiteral("fontScale"), 1.5);
    unrelated.insert(QStringLiteral("density"), QStringLiteral("touch"));
    unrelated.insert(QStringLiteral("reducedMotion"), true);
    appearance.setInput(unrelated);
    QCOMPARE(work.count(AppearanceWork::SharedPaletteBuild), 0);
    QVERIFY(work.count(AppearanceWork::FontMeasurement) > 0);
    QVERIFY(work.count(AppearanceWork::GeometryCalculation) > 0);
}

void AppearanceTests::mixedFacadeWorkDoesNotScale()
{
    const QStringList families{QStringLiteral("button"), QStringLiteral("tool"),
        QStringLiteral("field"), QStringLiteral("choice"), QStringLiteral("delegate"),
        QStringLiteral("delegate")}; // IAMenuItem also uses the delegate family.
    const Presentation::Tone tones[]{Presentation::Neutral, Presentation::Primary,
        Presentation::Caution, Presentation::Destructive};
    const AppearanceWork operations[]{AppearanceWork::SharedPaletteBuild, AppearanceWork::LocalDerivation,
        AppearanceWork::LocalPairMeasurement, AppearanceWork::FontMeasurement,
        AppearanceWork::GeometryCalculation, AppearanceWork::StateTableBuild};
    QList<int> baselineAccent, baselineMetrics;
    for (const int count : {10, 100}) {
        Appearance appearance;
        std::vector<std::unique_ptr<ControlAppearance>> facades;
        QSet<QString> combinations;
        for (int i = 0; i < count; ++i) {
            // Both populations use exactly the same ten selector combinations.
            const int combination = i % 10;
            auto facade = std::make_unique<ControlAppearance>();
            facade->setFamily(families[combination % families.size()]);
            facade->setTone(tones[combination % 4]);
            facade->setValidation(Presentation::Validation(combination % 3));
            facade->setSource(&appearance);
            QCOMPARE(facade->family(), families[combination % families.size()]);
            combinations.insert(facade->family() + QString::number(facade->tone())
                + QString::number(facade->validation()));
            facades.push_back(std::move(facade));
        }
        WorkRecorder work; // Exclude construction and selector initialization.
        auto input = tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QColor("#8657b8")}});
        appearance.setInput(input);
        QCOMPARE(work.count(AppearanceWork::SharedPaletteBuild), 1);
        QVERIFY(work.count(AppearanceWork::StateTableBuild) > 0);
        QVERIFY(work.count(AppearanceWork::StateTableBuild) <= combinations.size());
        QCOMPARE(work.count(AppearanceWork::PrivateStateTableBuild), 0);
        QCOMPARE(work.count(AppearanceWork::LocalDerivation), 0);
        QCOMPARE(work.count(AppearanceWork::LocalPairMeasurement), 0);
        QCOMPARE(work.count(AppearanceWork::FontMeasurement), 0);
        QCOMPARE(work.count(AppearanceWork::GeometryCalculation), 0);
        QList<int> accentCounts, metricsCounts;
        for (const auto operation : operations)
            accentCounts.append(work.count(operation));
        work.clear();
        auto tokens = input.value(QStringLiteral("tokens")).toMap();
        // Radius changes exercise metrics resolution without requiring per-control
        // height recalculation, which master intentionally still performs for height inputs.
        tokens.insert(QStringLiteral("metrics"), QVariantMap{{QStringLiteral("controlRadius"), 9}});
        input.insert(QStringLiteral("tokens"), tokens);
        appearance.setInput(input);
        QCOMPARE(work.count(AppearanceWork::ColorResolution), 0);
        QCOMPARE(work.count(AppearanceWork::StateTableBuild), 0);
        QCOMPARE(work.count(AppearanceWork::PrivateStateTableBuild), 0);
        for (const auto operation : operations)
            metricsCounts.append(work.count(operation));
        for (const auto &facade : facades)
            QCOMPARE(facade->controlRadius(), 9.0);
        for (int i = 0; i < accentCounts.size(); ++i) {
            qInfo().nospace() << "G1 N=" << count << " operation=" << AppearanceTrace::workName(operations[i])
                             << " accent=" << accentCounts[i] << " metrics=" << metricsCounts[i];
        }
        if (count == 10) {
            baselineAccent = accentCounts;
            baselineMetrics = metricsCounts;
        } else {
            QCOMPARE(accentCounts, baselineAccent);
            QCOMPARE(metricsCounts, baselineMetrics);
        }
    }
}

void AppearanceTests::sharedFontEnvironmentDoesNotScale()
{
    for (const int count : {10, 100}) {
        QQmlEngine first, second;
        auto *source = first.singletonInstance<Appearance *>("InterActive.Foundation", "Appearance");
        auto *other = second.singletonInstance<Appearance *>("InterActive.Foundation", "Appearance");
        QVERIFY(source);
        QVERIFY(other);
        std::vector<std::unique_ptr<ControlAppearance>> facades;
        for (int i = 0; i < count; ++i) {
            auto facade = std::make_unique<ControlAppearance>();
            facade->setSource(i % 2 ? source : other);
            facades.push_back(std::move(facade));
        }
        int environments = 0;
        for (auto *child : qApp->children()) {
            if (qstrcmp(child->metaObject()->className(), "InterActive::FontEnvironment") == 0)
                ++environments;
        }
        QCOMPARE(environments, 1); // Each shared environment installs exactly one app filter.
        source->setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}});
        QCOMPARE(facades[0]->backgroundColor(), other->colors()->control());
        QCOMPARE(facades[1]->backgroundColor(), source->colors()->control());
        QCOMPARE(other->scheme(), QStringLiteral("light"));
    }
}

void AppearanceTests::lazyControlDiagnostics()
{
    Appearance appearance;
    ControlAppearance control;
    control.setSource(&appearance);
    QSignalSpy diagnostics(&control, &ControlAppearance::diagnosticsChanged);
    WorkRecorder work;
    appearance.setInput(tokenInput(QStringLiteral("colors"), {
        {QStringLiteral("text"), QColor("#ffffff")},
        {QStringLiteral("control"), QColor("#ffffff")}}));
    QCOMPARE(diagnostics.count(), 1);
    QCOMPARE(work.count(AppearanceWork::ControlDiagnosticsBuild), 0);
    const auto findings = control.diagnostics();
    QVERIFY(hasDiagnostic(findings, QStringLiteral("low-contrast"), QStringLiteral("foregroundColor/backgroundColor")));
    QCOMPARE(work.count(AppearanceWork::ControlDiagnosticsBuild), 1);
    QCOMPARE(control.diagnostics(), findings);
    QCOMPARE(work.count(AppearanceWork::ControlDiagnosticsBuild), 1);
    auto input = appearance.input();
    auto tokens = input.value(QStringLiteral("tokens")).toMap();
    tokens.insert(QStringLiteral("metrics"), QVariantMap{{QStringLiteral("controlRadius"), 9}});
    input.insert(QStringLiteral("tokens"), tokens);
    appearance.setInput(input);
    QCOMPARE(control.diagnostics(), findings);
    QCOMPARE(work.count(AppearanceWork::ControlDiagnosticsBuild), 1);
    QCOMPARE(diagnostics.count(), 1);
    // Nested diagnostic updates retain their notification obligations without
    // forcing a read of the flattened public list.
    connect(&control, &ControlAppearance::diagnosticsChanged, &control, [&] {
        if (control.controlRadius() == 9)
            control.setControlRadius(-1);
    });
    control.setForegroundColor(QColor("#000000"));
    QVERIFY(hasDiagnostic(control.diagnostics(), QStringLiteral("invalid-value"), QStringLiteral("controlRadius")));
}

void AppearanceTests::fontEnvironmentDuringSourceUpdate()
{
    Appearance appearance;
    ControlAppearance control;
    control.setSource(&appearance);
    control.setTone(Presentation::Primary);
    control.setHoverBackgroundColor(QColor("#303040"));
    const QFont original = QGuiApplication::font();
    const auto restore = qScopeGuard([&] { QGuiApplication::setFont(original); });
    QFont modified = original;
    modified.setItalic(!original.italic());
    connect(&appearance, &Appearance::resolvedChanged, &control, [&] {
        QGuiApplication::setFont(modified);
    });
    QSignalSpy fill(&control, &ControlAppearance::fillColorChanged);
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QColor("#FFDF00")}}));
    QCOMPARE(fill.count(), 1);
    QCOMPARE(control.fillColor(), appearance.colors()->accent());
    QCOMPARE(control.resolvedFont().italic(), modified.italic());
    ControlAppearance expected;
    expected.setSource(&appearance);
    expected.setTone(Presentation::Primary);
    expected.setHoverBackgroundColor(QColor("#303040"));
    QCOMPARE(control.foregroundColor(), expected.foregroundColor());
    QCOMPARE(control.diagnostics(), expected.diagnostics());
}

void AppearanceTests::propertySpecificNotifications()
{
    Appearance appearance;
    ControlAppearance control;
    control.setSource(&appearance);
    const auto specific = [](QObject *object) {
        const auto *meta = object->metaObject();
        for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
            const auto property = meta->property(i);
            if (property.isConstant())
                continue;
            QVERIFY(property.hasNotifySignal());
            QCOMPARE(property.notifySignal().name(), QByteArray(property.name()) + "Changed");
        }
    };
    specific(&appearance);
    specific(appearance.colors());
    specific(appearance.metrics());
    specific(appearance.typography());
    specific(appearance.motion());
    specific(appearance.icons());
    specific(&control);
    QSignalSpy neutral(&control, &ControlAppearance::backgroundColorChanged);
    QSignalSpy font(&control, &ControlAppearance::resolvedFontChanged);
    QSignalSpy size(&control, &ControlAppearance::fontPixelSizeChanged);
    QSignalSpy height(&control, &ControlAppearance::minimumHeightChanged);
    QSignalSpy scale(&appearance, &Appearance::fontScaleChanged);
    QSignalSpy globalNeutral(appearance.colors(), &ColorTokens::contentChanged);
    QSignalSpy accent(appearance.colors(), &ColorTokens::accentChanged);
    QSignalSpy fill(&control, &ControlAppearance::fillColorChanged);
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QColor("#8657b8")}}));
    QCOMPARE(accent.count(), 1);
    QCOMPARE(fill.count(), 1);
    QCOMPARE(neutral.count(), 0);
    QCOMPARE(font.count(), 0);
    QCOMPARE(size.count(), 0);
    QCOMPARE(height.count(), 0);
    QCOMPARE(scale.count(), 0);
    QCOMPARE(globalNeutral.count(), 0);
    QSignalSpy snapshots(&control, &ControlAppearance::resolvedChanged);
    control.setBackgroundColor(control.backgroundColor());
    QCOMPARE(snapshots.count(), 1);
    QCOMPARE(neutral.count(), 0);
    control.resetBackgroundColor();
    QCOMPARE(snapshots.count(), 2);
    QCOMPARE(neutral.count(), 0);
}

void AppearanceTests::headingTypographyRoles()
{
    Appearance appearance;
    ControlAppearance control;
    control.setSource(&appearance);
    QCOMPARE(control.headingPixelSize(), appearance.typography()->headingSize());
    QCOMPARE(control.headingWeight(), appearance.typography()->emphasisWeight());
    QSignalSpy size(&control, &ControlAppearance::headingPixelSizeChanged);
    QSignalSpy weight(&control, &ControlAppearance::headingWeightChanged);
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QColor("#8657b8")}}));
    QCOMPARE(size.count(), 0);
    QCOMPARE(weight.count(), 0);
    // Equal-valued ordinary overrides also own the corresponding title role.
    control.setFontPixelSize(control.fontPixelSize());
    control.setFontWeight(control.fontWeight());
    QCOMPARE(control.headingPixelSize(), 13.0);
    QCOMPARE(control.headingWeight(), 400);
    QCOMPARE(size.count(), 1);
    QCOMPARE(weight.count(), 1);
    appearance.setInput(tokenInput(QStringLiteral("typography"), {
        {QStringLiteral("headingSize"), 24}, {QStringLiteral("emphasisWeight"), 700}}));
    QCOMPARE(control.headingPixelSize(), 13.0);
    QCOMPARE(control.headingWeight(), 400);
    QCOMPARE(size.count(), 1);
    QCOMPARE(weight.count(), 1);
    control.resetOverrides();
    QCOMPARE(control.headingPixelSize(), 24.0);
    QCOMPARE(control.headingWeight(), 700);
    QCOMPARE(size.count(), 2);
    QCOMPARE(weight.count(), 2);
    control.setSource(nullptr);
    QCOMPARE(control.headingPixelSize(), 16.0);
    QCOMPARE(control.headingWeight(), 600);
}

void AppearanceTests::localWorkIsolation()
{
    Appearance appearance;
    ControlAppearance local;
    local.setSource(&appearance);
    local.setTone(Presentation::Primary);
    local.setBackgroundColor(QColor("#8657b8"));
    const auto ordinary = [&] {
        return QVariantList{local.backgroundColor(), local.hoverBackgroundColor(), local.pressedBackgroundColor(),
            local.foregroundColor(), local.borderColor()};
    };
    const auto before = ordinary();
    WorkRecorder work;
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QColor("#20845f")}}));
    QCOMPARE(ordinary(), before);
    QCOMPARE(work.count(AppearanceWork::LocalDerivation), 0);
    QCOMPARE(local.fillColor(), appearance.colors()->accent());
    const QColor backdrop = opaqueOver(appearance.colors()->content(), opaqueOver(appearance.colors()->canvas(), QColor(Qt::white)));
    QCOMPARE(local.selectedBackgroundColor(), stateFeedback(appearance.colors()->primaryPressed(), local.selectedForegroundColor(),
        appearance.motion()->pressedBlend(), backdrop, 5, {local.backgroundColor(), local.hoverBackgroundColor()}, true));
    QCOMPARE(local.inactiveBackgroundColor(), appearance.colors()->primarySurface());
    local.setBackgroundColor(QColor("#101020"));
    QVERIFY(ordinary() != before);
    QVERIFY(work.count(AppearanceWork::LocalDerivation) > 0);
    local.resetOverrides();
    local.setHoverBackgroundColor(QColor("#303040"));
    work.clear();
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QColor("#FFDF00")}}));
    QCOMPARE(work.count(AppearanceWork::LocalDerivation), 1);
    local.setSelectedBackgroundColor(QColor("#111122"));
    const QColor selectedText = local.selectedForegroundColor();
    work.clear();
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QColor("#5C9CF5")}}));
    QCOMPARE(local.selectedForegroundColor(), selectedText);
    QCOMPARE(work.count(AppearanceWork::LocalDerivation), 1); // partially inherited ordinary pair only
}

void AppearanceTests::fontAndGeometryWork()
{
    Appearance appearance;
    ControlAppearance control;
    control.setSource(&appearance);
    control.setFontPixelSize(13.1);
    WorkRecorder work;
    QSignalSpy font(&control, &ControlAppearance::resolvedFontChanged);
    QSignalSpy size(&control, &ControlAppearance::fontPixelSizeChanged);
    control.setFontPixelSize(13.2);
    QCOMPARE(size.count(), 1);
    QCOMPARE(font.count(), 0);
    QCOMPARE(work.count(AppearanceWork::FontConstruction), 0);
    QCOMPARE(work.count(AppearanceWork::FontMeasurement), 0);
    QCOMPARE(work.count(AppearanceWork::GeometryCalculation), 0);
    control.setVerticalPadding(12);
    QCOMPARE(work.count(AppearanceWork::FontMeasurement), 0);
    QCOMPARE(work.count(AppearanceWork::GeometryCalculation), 1);
    work.clear();
    control.setHorizontalPadding(17);
    control.setControlRadius(9);
    control.setSpacing(19);
    QCOMPARE(work.count(AppearanceWork::FontMeasurement), 0);
    QCOMPARE(work.count(AppearanceWork::GeometryCalculation), 0);
    control.setMinimumHeight(80);
    QCOMPARE(control.minimumHeight(), 80.0);
    control.setMinimumHeight(0);
    QVERIFY(control.minimumHeight() < 80);
    const QString captured = control.fontFamily();
    const QFont original = QGuiApplication::font();
    const auto restore = qScopeGuard([&] { QGuiApplication::setFont(original); });
    QFont modified = original;
    modified.setItalic(!original.italic());
    work.clear();
    QGuiApplication::setFont(modified);
    QVERIFY(work.count(AppearanceWork::FontConstruction) > 0);
    QVERIFY(work.count(AppearanceWork::FontMeasurement) > 0);
    QCOMPARE(control.fontFamily(), captured);
    QCOMPARE(control.resolvedFont().italic(), modified.italic());
}

void AppearanceTests::reentrantNotifications()
{
    Appearance appearance;
    bool nested = false;
    const auto early = connect(&appearance, &Appearance::resolvedChanged, this, [&] {
        if (nested)
            return;
        nested = true;
        auto next = appearance.input();
        next.insert(QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"),
            QVariantMap{{QStringLiteral("accent"), QColor("#8657b8")}}}});
        appearance.setInput(next);
    });
    ControlAppearance late, later;
    late.setSource(&appearance);
    later.setSource(&appearance);
    QSignalSpy lateFont(&late, &ControlAppearance::resolvedFontChanged);
    QSignalSpy laterFont(&later, &ControlAppearance::resolvedFontChanged);
    QSignalSpy globalSize(appearance.typography(), &TypographyTokens::controlSizeChanged);
    QSignalSpy aggregate(&late, &ControlAppearance::resolvedChanged);
    appearance.setInput({{QStringLiteral("fontScale"), 2}});
    QCOMPARE(late.fontPixelSize(), 26.0);
    QCOMPARE(later.resolvedFont().pixelSize(), 26);
    QCOMPARE(late.fillColor(), QColor("#8657b8"));
    QCOMPARE(lateFont.count(), 1);
    QCOMPARE(laterFont.count(), 1);
    QCOMPARE(globalSize.count(), 1);
    QCOMPARE(aggregate.count(), 2);
    disconnect(early);
    const auto localObserver = connect(&late, &ControlAppearance::fontPixelSizeChanged, this, [&] {
        auto next = appearance.input();
        auto tokens = next.value(QStringLiteral("tokens")).toMap();
        tokens.insert(QStringLiteral("colors"), QVariantMap{{QStringLiteral("accent"), QColor("#20845f")}});
        next.insert(QStringLiteral("tokens"), tokens);
        appearance.setInput(next);
    });
    appearance.setInput({{QStringLiteral("fontScale"), 1.5}});
    QCOMPARE(late.resolvedFont().pixelSize(), 20);
    QCOMPARE(later.fontPixelSize(), 19.5);
    QCOMPARE(late.fillColor(), QColor("#20845f"));
    disconnect(localObserver);
}

void AppearanceTests::deletionDuringPublication()
{
    ControlAppearance facade;
    facade.setBackgroundColor(QColor("#8657b8"));
    auto source = std::make_unique<Appearance>();
    facade.setSource(source.get());
    QPointer<Appearance> sourceGuard(source.get());
    connect(source.get(), &Appearance::resolvedChanged, this, [&] { source.reset(); });
    sourceGuard->setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}});
    QVERIFY(!sourceGuard);
    QVERIFY(!facade.source());
    QCOMPARE(facade.backgroundColor(), QColor("#8657b8"));
    Appearance appearance;
    auto doomed = std::make_unique<ControlAppearance>();
    doomed->setSource(&appearance);
    QPointer<ControlAppearance> guard(doomed.get());
    connect(doomed.get(), &ControlAppearance::fillColorChanged, this, [&] { doomed.reset(); });
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QColor("#20845f")}}));
    QVERIFY(!guard);
}

void AppearanceTests::semanticPaletteFixtures_data()
{
    QTest::addColumn<QString>("scheme");
    QTest::addColumn<QString>("contrast");
    QTest::addColumn<QColor>("accent");
    QTest::addColumn<bool>("ordinary");
    const QStringList fixtures{QStringLiteral("#5C9CF5"), QStringLiteral("#FFDF00"), QStringLiteral("#49335F"),
        QStringLiteral("#8628C8"), QStringLiteral("#E53935"), QStringLiteral("#60252A"), QStringLiteral("#F5B8B8"),
        QStringLiteral("#D99B24"), QStringLiteral("#36A56A"), QStringLiteral("#27B8CC"),
        QStringLiteral("#000000"), QStringLiteral("#808080"), QStringLiteral("#FFFFFF")};
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        for (const auto &contrast : {QStringLiteral("normal"), QStringLiteral("high")}) {
            for (const auto &fixture : fixtures)
                QTest::newRow(qPrintable(scheme + QLatin1Char('-') + contrast + QLatin1Char('-') + fixture))
                    << scheme << contrast << QColor(fixture) << (fixture != QLatin1String("#000000")
                        && fixture != QLatin1String("#808080") && fixture != QLatin1String("#FFFFFF"));
        }
    }
}

void AppearanceTests::semanticPaletteFixtures()
{
    QFETCH(QString, scheme);
    QFETCH(QString, contrast);
    QFETCH(QColor, accent);
    QFETCH(bool, ordinary);
    Appearance appearance;
    appearance.setInput(paletteInput(scheme, contrast, {}));
    const auto before = appearance.snapshot().tokens;
    const auto input = paletteInput(scheme, contrast, {{QStringLiteral("accent"), accent}});
    QElapsedTimer timer;
    timer.start();
    appearance.setInput(input);
    const qint64 elapsed = timer.nsecsElapsed();
    QCOMPARE(appearance.input(), input);
    QCOMPARE(appearance.colors()->accent(), accent);
    const auto tokens = appearance.snapshot().tokens;
    const auto colors = appearance.colors()->values();
    const auto priorColors = before.value(QStringLiteral("colors")).toMap();
    for (const auto *role : {"canvas", "content", "popup", "text", "secondaryText", "placeholderText", "disabledText",
             "disabledSurface", "control", "controlHover", "controlPressed", "selection", "inactiveSelection",
             "outline", "separator", "tooltipBackground", "tooltipText", "inverseSurface", "inverseText",
             "inverseSecondary", "scrim", "shadow", "transparent"})
        QCOMPARE(colors.value(QString::fromLatin1(role)), priorColors.value(QString::fromLatin1(role)));
    for (const auto *group : {"metrics", "typography", "motion", "icons"})
        QCOMPARE(tokens.value(QString::fromLatin1(group)), before.value(QString::fromLatin1(group)));
    const qreal target = contrast == QLatin1String("high") ? 7 : 5;
    const QColor backdrop = opaqueOver(appearance.colors()->content(), opaqueOver(appearance.colors()->canvas(), QColor(Qt::white)));
    const auto color = [&](const QString &role) { return colors.value(role).value<QColor>(); };
    const auto anchors = AirmailProfiles::profile(scheme, QStringLiteral("Test Sans")).value(QStringLiteral("colors")).toMap();
    QVariantMap measurements;
    for (const auto *role : {"error", "warning", "success"}) {
        const QString name = QString::fromLatin1(role);
        const QColor fill = color(name);
        const mcu::Hct actual(fill.rgb());
        QCOMPARE(fill.alpha(), 255);
        if (!gray(fill)) {
            QVERIFY(actual.get_chroma() >= 8);
            QVERIFY(mcu::DiffDegrees(actual.get_hue(), mcu::Hct(anchors.value(name).value<QColor>().rgb()).get_hue()) <= 22);
        }
        verifyReadableContrast(color(name + QStringLiteral("Text")), color(QStringLiteral("content")),
            color(QStringLiteral("canvas")), target, name + QStringLiteral("Text/content"));
        verifyReadableContrast(color(name + QStringLiteral("Text")), color(name + QStringLiteral("Surface")),
            color(QStringLiteral("canvas")), target, name + QStringLiteral("Text/message"));
        measurements.insert(name, QVariantMap{{QStringLiteral("rgb"), fill.name()},
            {QStringLiteral("hue"), actual.get_hue()}, {QStringLiteral("chroma"), actual.get_chroma()},
            {QStringLiteral("tone"), actual.get_tone()}});
    }
    const mcu::Hct seed(accent.rgb());
    const mcu::Hct dangerAnchor(anchors.value(QStringLiteral("error")).value<QColor>().rgb());
    if (seed.get_chroma() >= 12 && mcu::DiffDegrees(seed.get_hue(), dangerAnchor.get_hue()) <= 20)
        QVERIFY2(gray(color(QStringLiteral("error"))), "Red primary requires true neutral danger");
    for (const auto *prefix : {"destructive", "caution"}) {
        const QString name = QString::fromLatin1(prefix);
        QCOMPARE(color(name + QStringLiteral("Outline")), color(name + QStringLiteral("Text")));
        const QString semantic = name == QLatin1String("destructive") ? QStringLiteral("error") : QStringLiteral("warning");
        const qreal anchor = mcu::Hct(anchors.value(semantic).value<QColor>().rgb()).get_hue();
        for (const auto *state : {"Surface", "Hover", "Pressed"}) {
            const QColor visible = opaqueOver(color(name + QLatin1String(state)), backdrop);
            if (gray(color(semantic))) {
                QVERIFY(gray(visible));
            } else {
                const mcu::Hct actual(visible.rgb());
                QVERIFY(actual.get_chroma() >= 8);
                QVERIFY2(mcu::DiffDegrees(actual.get_hue(), anchor) <= 22,
                    qPrintable(name + QLatin1String(state) + QStringLiteral(" hueDistance=%1")
                        .arg(mcu::DiffDegrees(actual.get_hue(), anchor), 0, 'g', 12)));
            }
            verifyReadableContrast(color(name + QStringLiteral("Text")), color(name + QLatin1String(state)), backdrop, target, name);
        }
    }
    if (ordinary) {
        verifyPaletteSeparation(colors, accent.name() + QLatin1Char('-') + contrast);
        QVERIFY2(!hasDiagnostic(appearance.diagnostics(), QStringLiteral("palette-collision")),
            qPrintable(QJsonDocument::fromVariant(appearance.diagnostics()).toJson(QJsonDocument::Compact)));
    } else {
        // Extremes retain exact caller input, with measured warnings when needed.
        for (const auto &value : appearance.diagnostics()) {
            const auto entry = value.toMap();
            if (entry.value(QStringLiteral("code")) == QLatin1String("palette-collision")) {
                QVERIFY(entry.value(QStringLiteral("distance")).toDouble() < entry.value(QStringLiteral("target")).toDouble());
                QCOMPARE(entry.value(QStringLiteral("severity")).toString(), QStringLiteral("warning"));
            }
        }
    }
    QVariantList distances, ratios;
    const QStringList bases{QStringLiteral("accent"), QStringLiteral("error"), QStringLiteral("warning"), QStringLiteral("success")};
    for (qsizetype i = 0; i < bases.size(); ++i)
        for (qsizetype j = i + 1; j < bases.size(); ++j)
            distances.append(QVariantMap{{QStringLiteral("paths"), QStringList{bases[i], bases[j]}},
                {QStringLiteral("distance"), visibleDistance(color(bases[i]), color(bases[j]), backdrop)},
                {QStringLiteral("target"), 12}});
    const QStringList actions{QStringLiteral("primary"), QStringLiteral("destructive"), QStringLiteral("caution")};
    for (qsizetype i = 0; i < actions.size(); ++i) {
        for (const auto *state : {"Surface", "Hover", "Pressed"}) {
            const QString path = actions[i] + QLatin1String(state);
            const QColor visible = opaqueOver(color(path), backdrop);
            const QColor text = color(actions[i] + QStringLiteral("Text"));
            ratios.append(QVariantMap{{QStringLiteral("paths"), QStringList{actions[i] + QStringLiteral("Text"), path}},
                {QStringLiteral("full"), rgbContrast(text, visible)},
                {QStringLiteral("rgb8"), rgbContrast(QColor::fromRgba(text.rgba()), QColor::fromRgb(visible.rgb()))},
                {QStringLiteral("target"), target}});
            for (qsizetype j = i + 1; j < actions.size(); ++j)
                for (const auto *otherState : {"Surface", "Hover", "Pressed"}) {
                    const QString other = actions[j] + QLatin1String(otherState);
                    distances.append(QVariantMap{{QStringLiteral("paths"), QStringList{path, other}},
                        {QStringLiteral("distance"), visibleDistance(color(path), color(other), backdrop)},
                        {QStringLiteral("target"), QLatin1String(state) == QLatin1String("Surface")
                            && QLatin1String(otherState) == QLatin1String("Surface") ? 12 : 8}});
                }
        }
    }
    QVariantMap channels;
    for (auto it = colors.cbegin(); it != colors.cend(); ++it) {
        const QColor value = it.value().value<QColor>();
        channels.insert(it.key(), QVariantList{value.redF(), value.greenF(), value.blueF(), value.alphaF()});
    }
    qInfo().noquote() << "IA_PALETTE" << QJsonDocument::fromVariant(QVariantMap{
        {QStringLiteral("scheme"), scheme}, {QStringLiteral("contrast"), contrast}, {QStringLiteral("accent"), accent.name()},
        {QStringLiteral("resolved"), measurements}, {QStringLiteral("elapsedNs"), elapsed},
        {QStringLiteral("colors"), colors}, {QStringLiteral("channels"), channels},
        {QStringLiteral("distances"), distances}, {QStringLiteral("ratios"), ratios},
        {QStringLiteral("diagnostics"), appearance.diagnostics()}}).toJson(QJsonDocument::Compact);
}

void AppearanceTests::semanticPaletteResponse()
{
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        for (const auto &contrast : {QStringLiteral("normal"), QStringLiteral("high")}) {
            const auto resolve = [&](const QColor &accent) {
                return TokenSchema::resolve(paletteInput(scheme, contrast, {{QStringLiteral("accent"), accent}}),
                    QStringLiteral("Test Sans")).tokens.value(QStringLiteral("colors")).toMap();
            };
            const auto bright = resolve(QColor("#FFDF00")), dim = resolve(QColor("#49335F"));
            const auto lighter = resolve(QColor::fromRgb(mcu::Hct(280, 24, 80).ToInt()));
            const auto darker = resolve(QColor::fromRgb(mcu::Hct(280, 24, 25).ToInt()));
            const auto vivid = resolve(QColor::fromRgb(mcu::Hct(280, 70, 65).ToInt()));
            const auto muted = resolve(QColor::fromRgb(mcu::Hct(280, 15, 65).ToInt()));
            for (const auto *role : {"error", "warning", "success"}) {
                const QString name = QString::fromLatin1(role);
                const auto hct = [&](const QVariantMap &colors) { return mcu::Hct(colors.value(name).value<QColor>().rgb()); };
                QVERIFY(hct(bright).get_tone() > hct(dim).get_tone());
                QVERIFY(hct(lighter).get_tone() > hct(darker).get_tone());
                QVERIFY(hct(vivid).get_chroma() > hct(muted).get_chroma());
            }
            Appearance defaults;
            defaults.setInput(paletteInput(scheme, contrast, {}));
            const auto noAccent = defaults.colors()->values();
            defaults.setInput(paletteInput(scheme, contrast, {{QStringLiteral("accent"), QColor("#5C9CF5")}}));
            QCOMPARE(defaults.colors()->values(), noAccent);
            QVERIFY(defaults.snapshot().explicitTokens.contains(QStringLiteral("colors.accent")));
            QVERIFY(noAccent.value(QStringLiteral("warning")).value<QColor>() != QColor("#F5A742")
                || noAccent.value(QStringLiteral("error")).value<QColor>() != QColor("#E06C75"));
        }
    }
}

void AppearanceTests::semanticPaletteOwnership()
{
    Appearance appearance;
    const QVariantMap generated = appearance.colors()->values();
    const QStringList roles{QStringLiteral("error"), QStringLiteral("warning"), QStringLiteral("success")};
    for (int mask = 1; mask < 8; ++mask) {
        QVariantMap supplied;
        for (int i = 0; i < roles.size(); ++i)
            if (mask & (1 << i))
                supplied.insert(roles[i], QColor::fromRgbF(0.15327 + i * 0.1, 0.72361, 0.53923, 0.43719));
        for (const QColor &accent : {QColor("#FFDF00"), QColor("#49335F"), QColor("#5C9CF5")}) {
            auto colors = supplied;
            colors.insert(QStringLiteral("accent"), accent);
            const auto input = paletteInput(QStringLiteral("light"), QStringLiteral("normal"), colors);
            appearance.setInput(input);
            QCOMPARE(appearance.input(), input);
            for (auto it = supplied.cbegin(); it != supplied.cend(); ++it)
                QCOMPARE(appearance.colors()->values().value(it.key()), it.value());
            appearance.setInput(paletteInput(QStringLiteral("light"), QStringLiteral("normal"), supplied));
            for (auto it = supplied.cbegin(); it != supplied.cend(); ++it)
                QCOMPARE(appearance.colors()->values().value(it.key()), it.value());
        }
    }
    for (const auto &role : roles) {
        appearance.setInput(tokenInput(QStringLiteral("colors"), {{role, generated.value(role)},
            {QStringLiteral("accent"), QColor("#FFDF00")}}));
        QCOMPARE(appearance.colors()->values().value(role), generated.value(role));
    }
    appearance.setInput({});
    QCOMPARE(appearance.colors()->values(), generated);
}

void AppearanceTests::semanticPaletteReservations()
{
    Appearance appearance;
    const QColor accent("#5C9CF5");
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("destructiveSurface"), accent}}));
    QCOMPARE(appearance.colors()->accent(), accent);
    QCOMPARE(appearance.colors()->destructiveSurface(), accent);
    QVERIFY(!gray(appearance.colors()->error()));
    QVERIFY(visibleDistance(appearance.colors()->error(), accent, appearance.colors()->content()) >= 12);
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("palette-collision")));
    // A later, still-implicit caution state constrains danger's candidate.
    const QColor reserved = appearance.colors()->error();
    for (const auto *state : {"cautionHover", "cautionPressed"}) {
        appearance.setInput(tokenInput(QStringLiteral("colors"), {{QString::fromLatin1(state), reserved}}));
        QCOMPARE(appearance.colors()->values().value(QString::fromLatin1(state)).value<QColor>(), reserved);
        for (const auto *danger : {"destructiveSurface", "destructiveHover", "destructivePressed"})
            QVERIFY(visibleDistance(appearance.colors()->values().value(QString::fromLatin1(danger)).value<QColor>(),
                reserved, appearance.colors()->content()) >= 8);
    }
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("error"), accent},
        {QStringLiteral("warning"), accent}, {QStringLiteral("success"), accent}}));
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("palette-collision")));
    QSet<QString> pairs;
    for (const auto &value : appearance.diagnostics()) {
        const auto entry = value.toMap();
        if (entry.value(QStringLiteral("code")) != QLatin1String("palette-collision"))
            continue;
        QVERIFY(!pairs.contains(entry.value(QStringLiteral("path")).toString()));
        pairs.insert(entry.value(QStringLiteral("path")).toString());
        const QStringList paths = entry.value(QStringLiteral("paths")).toStringList();
        QCOMPARE(paths.size(), 2);
        const auto colorAt = [&](const QString &path) {
            return appearance.colors()->values().value(path.mid(QStringLiteral("tokens.colors.").size())).value<QColor>();
        };
        const qreal measured = visibleDistance(colorAt(paths[0]), colorAt(paths[1]), appearance.colors()->content());
        QCOMPARE(entry.value(QStringLiteral("distance")).toDouble(), measured);
        QVERIFY(measured < entry.value(QStringLiteral("target")).toDouble());
        QVERIFY(entry.value(QStringLiteral("target")).toDouble() == 12 || entry.value(QStringLiteral("target")).toDouble() == 8);
    }
}

void AppearanceTests::semanticPaletteAlpha()
{
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        for (const auto &contrast : {QStringLiteral("normal"), QStringLiteral("high")}) {
            const QColor canvas = QColor::fromRgbF(0.17, 0.25, 0.37, 0.61);
            const QColor content = QColor::fromRgbF(0.84, 0.78, 0.69, 0.43);
            for (const qreal alpha : {0.0, 0.37139}) {
                const QColor accent = QColor::fromRgbF(0.91, 0.19, 0.31, alpha);
                const QColor visible = opaqueOver(accent, opaqueOver(content, opaqueOver(canvas, QColor(Qt::white))));
                const QVariantMap backdrop{{QStringLiteral("canvas"), canvas}, {QStringLiteral("content"), content}};
                auto transparentInput = backdrop, opaqueInput = backdrop;
                transparentInput.insert(QStringLiteral("accent"), accent);
                opaqueInput.insert(QStringLiteral("accent"), QColor::fromRgb(visible.rgb()));
                const auto transparentResult = TokenSchema::resolve(paletteInput(scheme, contrast, transparentInput), QStringLiteral("Sans"));
                const auto colors = transparentResult.tokens.value(QStringLiteral("colors")).toMap();
                // Equivalent visible seeds can have different live primary feedback
                // after an alpha blend. Hold those constraints equal as well before
                // isolating the analysis seed's effect on semantic generation.
                for (const auto *role : {"primarySurface", "primaryHover", "primaryPressed", "accentHover", "accentPressed"})
                    opaqueInput.insert(QString::fromLatin1(role), colors.value(QString::fromLatin1(role)));
                const auto opaqueResult = TokenSchema::resolve(paletteInput(scheme, contrast, opaqueInput), QStringLiteral("Sans"));
                QCOMPARE(colors.value(QStringLiteral("accent")).value<QColor>(), accent);
                for (const auto *role : {"error", "warning", "success"}) {
                    const QString name = QString::fromLatin1(role);
                    QCOMPARE(colors.value(name).value<QColor>().alpha(), 255);
                    QCOMPARE(colors.value(name), opaqueResult.tokens.value(QStringLiteral("colors")).toMap().value(name));
                }
            }
        }
    }
}

void AppearanceTests::semanticPaletteDeterminism()
{
    Appearance first, second;
    QList<QVariantMap> snapshots;
    QList<QVariantMap> inputs;
    const qreal anchor = mcu::Hct(QColor("#E06C75").rgb()).get_hue();
    for (const qreal offset : {-20.1, -20.01, -19.99, -19.9, 0.0, 19.9, 19.99, 20.01, 20.1})
        inputs.append(tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QColor::fromRgb(mcu::Hct(anchor + offset, 50, 65).ToInt())}}));
    int notifications = 0;
    connect(&first, &Appearance::resolvedChanged, this, [&] {
        ++notifications;
        const auto expected = TokenSchema::resolve(first.input(), first.typography()->family());
        QCOMPARE(first.snapshot().tokens, expected.tokens);
        QCOMPARE(first.diagnostics(), expected.diagnostics);
    });
    for (const auto &input : inputs) {
        first.setInput(input);
        snapshots.append(first.snapshot().tokens);
        const int before = notifications;
        first.setInput(input);
        QCOMPARE(notifications, before);
        QCOMPARE(second.colors()->accent(), QColor("#5C9CF5"));
    }
    for (qsizetype i = inputs.size() - 1; i >= 0; --i) {
        second.setInput(inputs[i]);
        QCOMPARE(second.snapshot().tokens, snapshots[i]);
    }
}

void AppearanceTests::semanticPaletteImmutablePairs()
{
    Appearance appearance;
    const QVariantMap fixedPair{{QStringLiteral("destructiveHover"), QColor(Qt::white)},
        {QStringLiteral("destructiveText"), QColor(Qt::white)}};
    appearance.setInput(tokenInput(QStringLiteral("colors"), fixedPair));
    QCOMPARE(appearance.colors()->destructiveHover(), QColor(Qt::white));
    QCOMPARE(appearance.colors()->destructiveText(), QColor(Qt::white));
    QVERIFY(!gray(appearance.colors()->error()));
    verifyReadableContrast(appearance.colors()->destructiveText(), appearance.colors()->destructiveSurface(),
        appearance.colors()->content(), 5, QStringLiteral("repairable normal beside immutable white/white hover"));
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("low-contrast"),
        QStringLiteral("tokens.colors.destructiveText/destructiveHover")));

    // A derived shared foreground on black/white fixed states is still
    // candidate-dependent. Exhaustion is bounded and must be honestly diagnosed.
    const QVariantMap impossible{{QStringLiteral("destructiveSurface"), QColor(Qt::black)},
        {QStringLiteral("destructiveHover"), QColor(Qt::white)}, {QStringLiteral("destructivePressed"), QColor(Qt::black)}};
    QSignalSpy notifications(&appearance, &Appearance::resolvedChanged);
    QElapsedTimer timer;
    timer.start();
    appearance.setInput(tokenInput(QStringLiteral("colors"), impossible));
    const qint64 elapsed = timer.nsecsElapsed();
    QCOMPARE(notifications.size(), 1);
    for (auto it = impossible.cbegin(); it != impossible.cend(); ++it)
        QCOMPARE(appearance.colors()->values().value(it.key()), it.value());
    for (const auto *state : {"Surface", "Hover", "Pressed"}) {
        const QString role = QStringLiteral("destructive") + QLatin1String(state);
        verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.destructiveText/") + role,
            appearance.colors()->destructiveText(), appearance.colors()->values().value(role).value<QColor>(),
            appearance.colors()->content(), 5);
        QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("low-contrast"),
            QStringLiteral("tokens.colors.destructiveText/") + role));
    }
    qInfo().noquote() << "IA_EXHAUSTED_INPUT" << QJsonDocument::fromVariant(QVariantMap{
        {QStringLiteral("elapsedNs"), elapsed}, {QStringLiteral("diagnostics"), appearance.diagnostics()}}).toJson(QJsonDocument::Compact);
    const auto complete = appearance.snapshot().tokens;
    appearance.setInput(appearance.input());
    QCOMPARE(notifications.size(), 1);
    QCOMPARE(appearance.snapshot().tokens, complete);
}

void AppearanceTests::directRecipeBounds()
{
    PaletteWorkRecorder recorder;
    const QStringList fixtures{QStringLiteral("#5C9CF5"), QStringLiteral("#FFDF00"), QStringLiteral("#49335F"),
        QStringLiteral("#8628C8"), QStringLiteral("#E53935"), QStringLiteral("#60252A"), QStringLiteral("#F5B8B8"),
        QStringLiteral("#D99B24"), QStringLiteral("#36A56A"), QStringLiteral("#27B8CC"),
        QStringLiteral("#000000"), QStringLiteral("#808080"), QStringLiteral("#FFFFFF")};
    // Ordinary required cases stay within three proposals per branch, six per
    // role, and never exhaust. The recorded black/gray/white fixture exception
    // covers separation-class findings only: those fixtures may exhaust with
    // `separated` as the sole failed gate; family and readability exhaustion is
    // not excused anywhere.
    const QStringList separationException{QStringLiteral("#000000"), QStringLiteral("#808080"), QStringLiteral("#FFFFFF")};
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        for (const auto &contrast : {QStringLiteral("normal"), QStringLiteral("high")}) {
            for (const auto &fixture : fixtures) {
                const auto input = paletteInput(scheme, contrast, {{QStringLiteral("accent"), QColor(fixture)}});
                recorder.clear();
                const auto result = TokenSchema::resolve(input, QStringLiteral("Test Sans"));
                QVERIFY2(!recorder.lines().isEmpty(), "the recipe must record per-role proposal counts");
                // The honest report rides along: gates that failed, per role.
                QHash<QString, QStringList> exhaustedGates;
                QStringList allDiagnostics;
                for (const auto &value : result.diagnostics) {
                    const auto entry = value.toMap();
                    allDiagnostics.append(entry.value(QStringLiteral("code")).toString()
                        + QLatin1Char('@') + entry.value(QStringLiteral("path")).toString());
                    if (entry.value(QStringLiteral("code")) == QStringLiteral("palette-generation-exhausted")) {
                        exhaustedGates.insert(entry.value(QStringLiteral("role")).toString(),
                                              entry.value(QStringLiteral("failedGates")).toStringList());
                        allDiagnostics.append(QStringLiteral("failedGates-type=%1")
                            .arg(QString::fromLatin1(entry.value(QStringLiteral("failedGates")).metaType().name())));
                    }
                }
                for (const auto &line : recorder.lines()) {
                    const QString context = QStringLiteral("%1-%2-%3 role=%4 chromatic=%5 neutral=%6 tried=%7 exhaustedGates=[%8] keys=[%9] diagnostics=[%10]")
                        .arg(scheme, contrast, fixture, line.role).arg(line.chromatic).arg(line.neutral).arg(line.tried)
                        .arg(exhaustedGates.value(line.role).join(QLatin1Char('+')), exhaustedGates.keys().join(QLatin1Char('|')),
                             allDiagnostics.join(QLatin1Char(',')));
                    QVERIFY2(line.chromatic <= 3, qPrintable(context));
                    QVERIFY2(line.neutral <= 3, qPrintable(context));
                    QVERIFY2(line.tried <= 6, qPrintable(context));
                    QCOMPARE(line.tried, line.chromatic + line.neutral);
                    if (line.exhausted) {
                        QVERIFY2(separationException.contains(fixture), qPrintable(context));
                        QVERIFY2(exhaustedGates.contains(line.role),
                                 qPrintable(context + QStringLiteral(" roleHex=%1 contains=%2")
                                     .arg(QString::fromLatin1(line.role.toUtf8().toHex()))
                                     .arg(exhaustedGates.contains(line.role))));
                        QVERIFY2(exhaustedGates.value(line.role) == QStringList{QStringLiteral("separated")},
                                 qPrintable(context));
                    }
                }
            }
        }
    }
    // A red-family primary runs the neutral-only danger branch.
    recorder.clear();
    TokenSchema::resolve(paletteInput(QStringLiteral("light"), QStringLiteral("normal"),
        {{QStringLiteral("accent"), QColor("#E53935")}}), QStringLiteral("Test Sans"));
    bool sawNeutralDanger = false;
    for (const auto &line : recorder.lines()) {
        QVERIFY(line.chromatic <= 3 && line.neutral <= 3 && line.tried <= 6);
        if (line.role == QLatin1String("error")) {
            QCOMPARE(line.chromatic, 0);
            QVERIFY(line.neutral >= 1);
            sawNeutralDanger = true;
        }
    }
    QVERIFY(sawNeutralDanger);
    // Exhaustion rides the same bound: the impossible black/white state case.
    recorder.clear();
    TokenSchema::resolve(tokenInput(QStringLiteral("colors"), {{QStringLiteral("destructiveSurface"), QColor(Qt::black)},
        {QStringLiteral("destructiveHover"), QColor(Qt::white)}, {QStringLiteral("destructivePressed"), QColor(Qt::black)}}),
        QStringLiteral("Test Sans"));
    bool sawExhausted = false;
    for (const auto &line : recorder.lines()) {
        QVERIFY(line.chromatic <= 3 && line.neutral <= 3 && line.tried <= 6);
        QCOMPARE(line.tried, line.chromatic + line.neutral);
        sawExhausted |= line.exhausted;
    }
    QVERIFY(sawExhausted);
}

void AppearanceTests::directRecipeRepairTones()
{
    const QString scheme = QStringLiteral("light"), contrast = QStringLiteral("normal");
    const QColor accent("#5C9CF5");
    const auto profile = AirmailProfiles::profile(scheme, QStringLiteral("Test Sans"))
        .value(QStringLiteral("colors")).toMap();
    const QColor content = opaqueOver(profile.value(QStringLiteral("content")).value<QColor>(),
        opaqueOver(profile.value(QStringLiteral("canvas")).value<QColor>(), QColor(Qt::white)));
    const QColor visible = opaqueOver(accent, content);
    const mcu::Hct seed(visible.rgb());
    const qreal requestChroma = std::max(visible.red() == visible.green() && visible.green() == visible.blue()
        ? 0.0 : seed.get_chroma(), 12.0);
    const qreal anchor = mcu::Hct(profile.value(QStringLiteral("error")).value<QColor>().rgb()).get_hue();
    const qreal tone = seed.get_tone();
    const auto expected = [&](qreal requestTone) {
        return QColor::fromRgb(mcu::Hct(anchor, requestChroma, std::clamp(requestTone, 0.0, 100.0)).ToInt());
    };
    const auto resolvedError = [&](const QVariantMap &extra) {
        QVariantMap colors = extra;
        colors.insert(QStringLiteral("accent"), accent);
        const auto result = TokenSchema::resolve(paletteInput(scheme, contrast, colors), QStringLiteral("Test Sans"));
        return result.tokens.value(QStringLiteral("colors")).toMap().value(QStringLiteral("error")).value<QColor>();
    };
    // The unblocked direct proposal is accepted as-is.
    const QColor direct = resolvedError(QVariantMap{});
    QCOMPARE(direct, expected(tone));
    // A reserved state fill blocking the direct proposal from above selects the
    // nearer lower repair at min(blocker tones) - 16.
    const QColor above = expected(tone + 4);
    const QColor repairedAbove = resolvedError({{QStringLiteral("cautionHover"), above}});
    QCOMPARE(repairedAbove, expected(mcu::LstarFromArgb(above.rgb()) - 16));
    QVERIFY(repairedAbove != direct);
    // A blocker below selects the nearer higher repair at max(blocker tones) + 16.
    const QColor below = expected(tone - 4);
    const QColor repairedBelow = resolvedError({{QStringLiteral("cautionHover"), below}});
    QCOMPARE(repairedBelow, expected(mcu::LstarFromArgb(below.rgb()) + 16));
    QVERIFY(repairedBelow != direct);
    // Fixed-versus-fixed collisions are diagnosed but cannot steer repairs: an
    // explicit errorHover equal to the primary surface leaves the base untouched.
    const QVariantMap colliding{{QStringLiteral("accent"), accent}, {QStringLiteral("errorHover"), accent}};
    const auto collided = TokenSchema::resolve(paletteInput(scheme, contrast, colliding), QStringLiteral("Test Sans"));
    QCOMPARE(collided.tokens.value(QStringLiteral("colors")).toMap()
        .value(QStringLiteral("error")).value<QColor>(), direct);
    QVERIFY(hasDiagnostic(collided.diagnostics, QStringLiteral("palette-collision")));
}

void AppearanceTests::directRecipeExhaustion()
{
    const QVariantMap impossible{{QStringLiteral("destructiveSurface"), QColor(Qt::black)},
        {QStringLiteral("destructiveHover"), QColor(Qt::white)}, {QStringLiteral("destructivePressed"), QColor(Qt::black)}};
    const auto findExhaustion = [](const QVariantList &diagnostics, const QString &role) {
        for (const auto &value : diagnostics) {
            const auto entry = value.toMap();
            if (entry.value(QStringLiteral("code")) == QStringLiteral("palette-generation-exhausted")
                && entry.value(QStringLiteral("path")) == QStringLiteral("tokens.colors.") + role)
                return entry;
        }
        return QVariantMap{};
    };
    const auto result = TokenSchema::resolve(tokenInput(QStringLiteral("colors"), impossible), QStringLiteral("Test Sans"));
    const auto error = findExhaustion(result.diagnostics, QStringLiteral("error"));
    QVERIFY2(!error.isEmpty(), "bounded generation must report its exhaustion");
    QVERIFY(findExhaustion(result.diagnostics, QStringLiteral("warning")).isEmpty());
    QVERIFY(findExhaustion(result.diagnostics, QStringLiteral("success")).isEmpty());
    QCOMPARE(error.value(QStringLiteral("severity")).toString(), QStringLiteral("warning"));
    QCOMPARE(error.value(QStringLiteral("role")).toString(), QStringLiteral("error"));
    QCOMPARE(error.value(QStringLiteral("maximum")).toInt(), 6);
    const int tried = error.value(QStringLiteral("tried")).toInt();
    QVERIFY(tried >= 1 && tried <= 6);
    const auto failedGates = error.value(QStringLiteral("failedGates")).toStringList();
    QVERIFY(failedGates.contains(QStringLiteral("readable-full")));
    QVERIFY(failedGates.contains(QStringLiteral("readable-rgb8")));
    // Explicit colors are retained and the best-effort result is deterministic.
    const auto colors = result.tokens.value(QStringLiteral("colors")).toMap();
    for (auto it = impossible.cbegin(); it != impossible.cend(); ++it)
        QCOMPARE(colors.value(it.key()), it.value());
    const auto repeat = TokenSchema::resolve(tokenInput(QStringLiteral("colors"), impossible), QStringLiteral("Test Sans"));
    QCOMPARE(repeat.tokens, result.tokens);
    QCOMPARE(repeat.diagnostics, result.diagnostics);
    QVERIFY(hasDiagnostic(result.diagnostics, QStringLiteral("low-contrast")));
    // A red primary keeps exhausted danger true gray within the three-proposal bound.
    QVariantMap redInput = impossible;
    redInput.insert(QStringLiteral("accent"), QColor("#E53935"));
    const auto red = TokenSchema::resolve(paletteInput(QStringLiteral("light"), QStringLiteral("normal"), redInput),
                                          QStringLiteral("Test Sans"));
    const auto redError = findExhaustion(red.diagnostics, QStringLiteral("error"));
    QVERIFY(!redError.isEmpty());
    QCOMPARE(redError.value(QStringLiteral("maximum")).toInt(), 3);
    QVERIFY(redError.value(QStringLiteral("tried")).toInt() <= 3);
    const QColor redDanger = red.tokens.value(QStringLiteral("colors")).toMap()
        .value(QStringLiteral("error")).value<QColor>();
    QVERIFY(gray(redDanger));
}

void AppearanceTests::contrastingTextFastPath()
{
    const QColor white(Qt::white), black(Qt::black);
    // The dark exterior solution is one algebraic HCT request, judged as the
    // actual quantized color.
    const QColor seed("#E06C75");
    const mcu::Hct hct(seed.rgb());
    const qreal darker = mcu::Darker(100, 5);
    QVERIFY(darker >= 0);
    const QColor text = TokenSchema::contrastingText(seed, {white}, white, 5);
    QCOMPARE(text, QColor::fromRgb(mcu::Hct(hct.get_hue(), hct.get_chroma(), darker).ToInt()));
    QVERIFY(rgbContrast(text, white) >= 5);
    // The light exterior solution against a dark background (the seed itself is
    // not readable on it, so the fast path serves the algebraic proposal).
    const QColor darkSeed("#40609f");
    const QColor darkBackground("#101214");
    QVERIFY(rgbContrast(darkSeed, darkBackground, black) < 5);
    const mcu::Hct darkHct(darkSeed.rgb());
    const qreal lighter = mcu::Lighter(mcu::LstarFromY(rgbLuminance(darkBackground) * 100), 5);
    QVERIFY(lighter >= 0);
    QCOMPARE(TokenSchema::contrastingText(darkSeed, {darkBackground}, black, 5),
             QColor::fromRgb(mcu::Hct(darkHct.get_hue(), darkHct.get_chroma(), lighter).ToInt()));
    // Endpoint: MCU's safety margin rejects Darker although literal black meets
    // the target; the endpoint is validated directly and wins.
    const qreal edgeChannels = 0.4857;
    const QColor edge = QColor::fromRgbF(edgeChannels, edgeChannels, edgeChannels);
    QVERIFY(mcu::Darker(mcu::LstarFromY(rgbLuminance(edge) * 100), 5) < 0);
    QVERIFY(rgbContrast(black, edge) >= 5);
    QCOMPARE(TokenSchema::contrastingText(edge, {edge}, white, 5), black);
    // A background whose RGB8 rounding alone drops black below the target makes
    // the pair infeasible: a full-precision pass is never rounded up into a
    // readable acceptance, and the best effort maximizes the minimum ratio.
    QColor tricky;
    for (int r = 1214; r <= 1268 && !tricky.isValid(); r += 2)
        for (int g = 1214; g <= 1268 && !tricky.isValid(); g += 2)
            for (int b = 1214; b <= 1268 && !tricky.isValid(); b += 2) {
                const QColor candidate = QColor::fromRgbF(r / 2550.0, g / 2550.0, b / 2550.0);
                if (rgbContrast(black, candidate) >= 5
                    && rgbContrast(black, QColor::fromRgb(candidate.rgb())) < 5)
                    tricky = candidate;
            }
    QVERIFY2(tricky.isValid(), "expected a full-pass/RGB8-fail background in the scanned range");
    const QColor best = TokenSchema::contrastingText(tricky, {tricky}, white, 5);
    QCOMPARE(best, black);
    QVERIFY(rgbContrast(best, tricky) >= 5);
    QVERIFY(rgbContrast(best, QColor::fromRgb(tricky.rgb())) < 5);
    // Gray seeds stay true gray through the fast path.
    const QColor grayText = TokenSchema::contrastingText(QColor("#808080"), {white}, white, 5);
    QVERIFY(gray(grayText));
    QVERIFY(rgbContrast(grayText, white) >= 5);
    // Mixed dark/light backgrounds still reach the general fallback's maximin.
    const QColor mixed = TokenSchema::contrastingText(seed, {black, white}, white, 7);
    const qreal minimum = std::min(rgbContrast(mixed, black), rgbContrast(mixed, white));
    QVERIFY(minimum >= 4.5);
    QVERIFY(minimum <= std::sqrt(21.0) + 0.001);
}

void AppearanceTests::screenGeometryLifetime()
{
    ScreenGeometry geometry;
    QCOMPARE(geometry.window(), nullptr);
    QCOMPARE(geometry.availableGeometry(), QRect());
    QSignalSpy windowChanged(&geometry, &ScreenGeometry::windowChanged);
    QSignalSpy geometryChanged(&geometry, &ScreenGeometry::availableGeometryChanged);
    auto first = std::make_unique<QQuickWindow>();
    QQuickWindow second;
    QVERIFY(first->screen());
    geometry.setWindow(first.get());
    QCOMPARE(geometry.availableGeometry(), first->screen()->availableGeometry());
    QCOMPARE(windowChanged.count(), 1);
    QCOMPARE(geometryChanged.count(), 1);
    geometry.setWindow(first.get());
    QCOMPARE(windowChanged.count(), 1);
    geometry.setWindow(&second);
    QCOMPARE(geometry.window(), &second);
    QCOMPARE(geometry.availableGeometry(), second.screen()->availableGeometry());
    geometry.setWindow(first.get());
    first.reset();
    QCOMPARE(geometry.window(), nullptr);
    QCOMPARE(geometry.availableGeometry(), QRect());
    QCOMPARE(windowChanged.count(), 4);
    QCOMPARE(geometryChanged.count(), 2);
}

void AppearanceTests::defaultsAndSchemaCompleteness()
{
    Appearance appearance;
    QCOMPARE(appearance.scheme(), QStringLiteral("light"));
    QCOMPARE(appearance.density(), QStringLiteral("compact"));
    QCOMPARE(appearance.contrast(), QStringLiteral("normal"));
    QCOMPARE(appearance.fontScale(), 1.0);
    QVERIFY(!appearance.reducedMotion());
    QVERIFY(appearance.input().isEmpty());
    QCOMPARE(appearance.colors()->canvas(), QColor("#F4F5F7"));
    QCOMPARE(appearance.colors()->text(), QColor("#25282D"));
    QCOMPARE(appearance.colors()->accent(), QColor("#5C9CF5"));
    verifyPaletteSeparation(appearance.colors()->values(), QStringLiteral("default"));
    QCOMPARE(appearance.metrics()->controlHeight(), 30.0);
    QCOMPARE(appearance.metrics()->toolHeight(), 28.0);
    QCOMPARE(appearance.metrics()->controlRadius(), 4.0);
    QCOMPARE(appearance.typography()->controlSize(), 13.0);
    QCOMPARE(appearance.typography()->family(), QGuiApplication::font().family());
    QCOMPARE(appearance.motion()->feedbackDuration(), 120);
    QCOMPARE(appearance.motion()->popupDuration(), 150);
    QCOMPARE(appearance.motion()->hoverBlend(), 0.08);
    QCOMPARE(appearance.motion()->pressedBlend(), 0.16);
    QVERIFY(appearance.diagnostics().isEmpty());

    const auto schema = TokenSchema::definitions();
    const QMap<QString, TokenGroup *> groups{
        {QStringLiteral("colors"), appearance.colors()}, {QStringLiteral("metrics"), appearance.metrics()},
        {QStringLiteral("typography"), appearance.typography()}, {QStringLiteral("motion"), appearance.motion()},
        {QStringLiteral("icons"), appearance.icons()}};
    QCOMPARE(schema.keys(), groups.keys());
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        const auto rawProfile = AirmailProfiles::profile(scheme, QStringLiteral("Test Sans"));
        QCOMPARE(rawProfile.keys(), schema.keys());
        appearance.setInput({{QStringLiteral("scheme"), scheme}});
        QVERIFY(appearance.diagnostics().isEmpty());
        for (auto group = schema.cbegin(); group != schema.cend(); ++group) {
            const auto entries = group.value().toMap();
            const auto rawValues = rawProfile.value(group.key()).toMap();
            const auto values = groups.value(group.key())->values();
            QCOMPARE(rawValues.keys(), entries.keys());
            QCOMPARE(values.keys(), entries.keys());
            auto *object = groups.value(group.key());
            QCOMPARE(object->metaObject()->propertyCount() - object->metaObject()->propertyOffset(), entries.size());
            for (auto field = entries.cbegin(); field != entries.cend(); ++field) {
                QVariant normalized;
                QVariantList diagnostics;
                QVERIFY2(TokenSchema::validate(group.key(), field.key(), rawValues.value(field.key()), &normalized, &diagnostics), qPrintable(field.key()));
                QVERIFY(diagnostics.isEmpty());
                const int index = object->metaObject()->indexOfProperty(field.key().toUtf8().constData());
                QVERIFY2(index >= 0, qPrintable(field.key()));
                const auto property = object->metaObject()->property(index);
                QVERIFY(property.isReadable());
                QVERIFY(!property.isWritable());
                QVERIFY(property.hasNotifySignal());
                QCOMPARE(property.read(object), values.value(field.key()));
            }
        }
    }
    QCOMPARE(appearance.colors()->canvas(), QColor("#202124"));
    QCOMPARE(appearance.colors()->popup(), QColor("#303237"));
    QCOMPARE(appearance.colors()->inverseSurface(), QColor("#1F2022"));
    QCOMPARE(appearance.icons()->check(), QUrl(QStringLiteral("qrc:/qt/qml/InterActive/Foundation/icons/check.svg")));
    for (const auto *removed : {"tonalSurfaceBlend", "tonalHoverBlend", "tonalPressedBlend", "tonalOutlineBlend"}) {
        QVERIFY(!schema.value(QStringLiteral("motion")).toMap().contains(QString::fromLatin1(removed)));
        QCOMPARE(appearance.motion()->metaObject()->indexOfProperty(removed), -1);
        appearance.setInput(tokenInput(QStringLiteral("motion"), {{QString::fromLatin1(removed), 0.1}}));
        QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("unknown-field"),
                              QStringLiteral("tokens.motion.") + QLatin1String(removed)));
    }
}

void AppearanceTests::numericSchemaBounds()
{
    const auto schema = TokenSchema::definitions();
    for (auto group = schema.cbegin(); group != schema.cend(); ++group) {
        const auto fields = group.value().toMap();
        for (auto field = fields.cbegin(); field != fields.cend(); ++field) {
            const auto spec = field.value().toMap();
            const QString type = spec.value(QStringLiteral("type")).toString();
            if (type != QLatin1String("integer") && type != QLatin1String("real"))
                continue;
            const double minimum = spec.value(QStringLiteral("minimum")).toDouble();
            const double maximum = spec.value(QStringLiteral("maximum")).toDouble();
            QVariant normalized;
            QVERIFY(TokenSchema::validate(group.key(), field.key(), minimum, &normalized, nullptr));
            QVERIFY(TokenSchema::validate(group.key(), field.key(), maximum, &normalized, nullptr));
            const QVariantList invalid{minimum - 1, maximum + 1, true, QStringLiteral("1"),
                                       std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()};
            for (const auto &value : invalid) {
                QVariantList diagnostics;
                QVERIFY(!TokenSchema::validate(group.key(), field.key(), value, &normalized, &diagnostics));
                QCOMPARE(diagnostics.size(), 1);
                QVERIFY(hasDiagnostic(diagnostics, QStringLiteral("invalid-value")));
            }
            if (type == QLatin1String("integer"))
                QVERIFY(!TokenSchema::validate(group.key(), field.key(), minimum + 0.5, &normalized, nullptr));
        }
    }
}

void AppearanceTests::pressInsetTokens()
{
    Appearance appearance;
    for (const auto *scheme : {"light", "dark"}) {
        for (const auto *density : {"compact", "touch"}) {
            appearance.setInput({{QStringLiteral("scheme"), QString::fromLatin1(scheme)},
                                 {QStringLiteral("density"), QString::fromLatin1(density)}});
            QCOMPARE(appearance.metrics()->pressInsetDepth(), 8.0);
            QCOMPARE(appearance.metrics()->pressContentShift(), 1.0);
            QCOMPARE(appearance.motion()->pressInsetOpacity(), 0.28);
        }
    }

    struct Token {
        QString group;
        QString name;
        qreal defaultValue;
        qreal maximum;
        TokenGroup *output;
    };
    const QList<Token> tokens{
        {QStringLiteral("metrics"), QStringLiteral("pressInsetDepth"), 8, 64, appearance.metrics()},
        {QStringLiteral("metrics"), QStringLiteral("pressContentShift"), 1, 8, appearance.metrics()},
        {QStringLiteral("motion"), QStringLiteral("pressInsetOpacity"), 0.28, 1, appearance.motion()}};
    const auto schema = TokenSchema::definitions();
    for (const auto &token : tokens) {
        const auto spec = schema.value(token.group).toMap().value(token.name).toMap();
        QCOMPARE(spec.value(QStringLiteral("type")).toString(), QStringLiteral("real"));
        QCOMPARE(spec.value(QStringLiteral("minimum")).toDouble(), 0.0);
        QCOMPARE(spec.value(QStringLiteral("maximum")).toDouble(), token.maximum);
        const auto name = token.name.toUtf8();
        const int signalIndex = token.output->metaObject()->indexOfSignal((name + "Changed()").constData());
        QVERIFY(signalIndex >= 0);
        QSignalSpy changed(token.output, token.output->metaObject()->method(signalIndex));
        QVERIFY(changed.isValid());
        for (const qreal value : {qreal(0), token.maximum, token.maximum / 2}) {
            appearance.setInput(tokenInput(token.group, {{token.name, value}}));
            QCOMPARE(token.output->property(name.constData()).toDouble(), value);
            QVERIFY(!hasDiagnostic(appearance.diagnostics(), QStringLiteral("invalid-value")));
        }
        QCOMPARE(changed.count(), 3);
        for (const qreal value : {qreal(-0.01), token.maximum + 0.01}) {
            appearance.setInput(tokenInput(token.group, {{token.name, value}}));
            QCOMPARE(token.output->property(name.constData()).toDouble(), token.defaultValue);
            QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("invalid-value"),
                                  QStringLiteral("tokens.") + token.group + QLatin1Char('.') + token.name));
        }
        appearance.setInput({});
        QCOMPARE(token.output->property(name.constData()).toDouble(), token.defaultValue);
    }
}

void AppearanceTests::partialInputReplacesSnapshot()
{
    Appearance appearance;
    appearance.setInput({{QStringLiteral("scheme"), QStringLiteral("dark")},
                         {QStringLiteral("tokens"), QVariantMap{
                             {QStringLiteral("colors"), QVariantMap{{QStringLiteral("accent"), QStringLiteral("#8657b8")}}},
                             {QStringLiteral("metrics"), QVariantMap{{QStringLiteral("controlRadius"), 9}}},
                             {QStringLiteral("typography"), QVariantMap{{QStringLiteral("controlSize"), 21}}}}}});
    QCOMPARE(appearance.colors()->accent(), QColor("#8657b8"));
    QCOMPARE(appearance.metrics()->controlRadius(), 9.0);
    QCOMPARE(appearance.typography()->controlSize(), 21.0);
    appearance.setInput(tokenInput(QStringLiteral("metrics"), {{QStringLiteral("spacingS"), 11}}));
    QCOMPARE(appearance.scheme(), QStringLiteral("light"));
    QCOMPARE(appearance.colors()->accent(), QColor("#5C9CF5"));
    QCOMPARE(appearance.metrics()->controlRadius(), 4.0);
    QCOMPARE(appearance.typography()->controlSize(), 13.0);
    QCOMPARE(appearance.metrics()->spacingS(), 11.0);
    appearance.setInput({});
    QCOMPARE(appearance.metrics()->spacingS(), 8.0);
    QVERIFY(appearance.input().isEmpty());
}

void AppearanceTests::invalidFields_data()
{
    QTest::addColumn<QString>("group");
    QTest::addColumn<QString>("field");
    QTest::addColumn<QVariant>("value");
    QTest::newRow("negative") << QStringLiteral("metrics") << QStringLiteral("controlRadius") << QVariant(-1);
    QTest::newRow("nan") << QStringLiteral("metrics") << QStringLiteral("spacingS") << QVariant(std::numeric_limits<double>::quiet_NaN());
    QTest::newRow("infinity") << QStringLiteral("typography") << QStringLiteral("controlSize") << QVariant(std::numeric_limits<double>::infinity());
    QTest::newRow("boolean-number") << QStringLiteral("metrics") << QStringLiteral("spacingM") << QVariant(true);
    QTest::newRow("string-number") << QStringLiteral("metrics") << QStringLiteral("controlHeight") << QVariant(QStringLiteral("40"));
    QTest::newRow("huge-number") << QStringLiteral("motion") << QStringLiteral("feedbackDuration") << QVariant(1000000000);
    QTest::newRow("fractional-integer") << QStringLiteral("motion") << QStringLiteral("popupDuration") << QVariant(120.5);
    QTest::newRow("invalid-color") << QStringLiteral("colors") << QStringLiteral("accent") << QVariant(QStringLiteral("not-a-color"));
    QTest::newRow("number-color") << QStringLiteral("colors") << QStringLiteral("accent") << QVariant(123);
    QTest::newRow("invalid-qcolor") << QStringLiteral("colors") << QStringLiteral("text") << QVariant(QColor());
    QTest::newRow("empty-family") << QStringLiteral("typography") << QStringLiteral("family") << QVariant(QStringLiteral("  "));
    QTest::newRow("number-family") << QStringLiteral("typography") << QStringLiteral("family") << QVariant(42);
    QTest::newRow("relative-icon") << QStringLiteral("icons") << QStringLiteral("check") << QVariant(QStringLiteral("check.svg"));
    QTest::newRow("bad-icon-type") << QStringLiteral("icons") << QStringLiteral("check") << QVariant(false);
    QTest::newRow("bad-icon-scheme") << QStringLiteral("icons") << QStringLiteral("check") << QVariant(QStringLiteral("javascript:bad()"));
}

void AppearanceTests::invalidFields()
{
    QFETCH(QString, group);
    QFETCH(QString, field);
    QFETCH(QVariant, value);
    Appearance appearance;
    const QVariant expected = appearance.snapshot().tokens.value(group).toMap().value(field);
    QSignalSpy rejected(&appearance, &Appearance::inputRejected);
    auto input = tokenInput(group, {{field, value}});
    auto tokens = input.value(QStringLiteral("tokens")).toMap();
    auto metrics = tokens.value(QStringLiteral("metrics")).toMap();
    metrics.insert(QStringLiteral("popupRadius"), 15);
    tokens.insert(QStringLiteral("metrics"), metrics);
    input.insert(QStringLiteral("tokens"), tokens);
    appearance.setInput(input);
    QCOMPARE(rejected.count(), 0);
    QCOMPARE(appearance.snapshot().tokens.value(group).toMap().value(field), expected);
    QCOMPARE(appearance.metrics()->popupRadius(), 15.0);
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("invalid-value"), QStringLiteral("tokens.") + group + QLatin1Char('.') + field));
}

void AppearanceTests::unknownFieldsAndStrictEnvelopeFields()
{
    Appearance appearance;
    appearance.setInput({{QStringLiteral("mystery"), true}, {QStringLiteral("scheme"), 1},
                         {QStringLiteral("density"), QStringLiteral("dense")},
                         {QStringLiteral("contrast"), QStringLiteral("extreme")},
                         {QStringLiteral("fontScale"), QStringLiteral("2")},
                         {QStringLiteral("reducedMotion"), 1},
                         {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("unknown"), false},
                             {QStringLiteral("colors"), QVariantMap{{QStringLiteral("mystery"), QStringLiteral("red")}}}}}});
    QCOMPARE(appearance.scheme(), QStringLiteral("light"));
    QCOMPARE(appearance.density(), QStringLiteral("compact"));
    QCOMPARE(appearance.fontScale(), 1.0);
    QVERIFY(!appearance.reducedMotion());
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("unknown-field"), QStringLiteral("mystery")));
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("unknown-field"), QStringLiteral("tokens.unknown")));
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("unknown-field"), QStringLiteral("tokens.colors.mystery")));
    for (const auto &value : appearance.diagnostics()) {
        const auto entry = value.toMap();
        QVERIFY(entry.contains(QStringLiteral("path")));
        QVERIFY(entry.contains(QStringLiteral("code")));
        QVERIFY(entry.contains(QStringLiteral("severity")));
        QVERIFY(entry.contains(QStringLiteral("message")));
    }
    appearance.setInput({});
    QVERIFY(appearance.diagnostics().isEmpty());
}

void AppearanceTests::rejectedEnvelope_data()
{
    QTest::addColumn<QVariantMap>("input");
    QTest::newRow("unsupported-version") << QVariantMap{{QStringLiteral("schemaVersion"), 2}};
    QTest::newRow("string-version") << QVariantMap{{QStringLiteral("schemaVersion"), QStringLiteral("1")}};
    QTest::newRow("boolean-version") << QVariantMap{{QStringLiteral("schemaVersion"), true}};
    QTest::newRow("fractional-version") << QVariantMap{{QStringLiteral("schemaVersion"), 1.1}};
    QTest::newRow("tokens-scalar") << QVariantMap{{QStringLiteral("tokens"), QStringLiteral("bad")}};
    QTest::newRow("tokens-array") << QVariantMap{{QStringLiteral("tokens"), QVariantList{1, 2}}};
    QTest::newRow("tokens-null") << QVariantMap{{QStringLiteral("tokens"), QVariant()}};
    QTest::newRow("group-scalar") << QVariantMap{{QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), 12}}}};
    QTest::newRow("group-array") << QVariantMap{{QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), QVariantList{1}}}}};
    QTest::newRow("group-null") << QVariantMap{{QStringLiteral("tokens"), QVariantMap{{QStringLiteral("metrics"), QVariant()}}}};
}

void AppearanceTests::rejectedEnvelope()
{
    QFETCH(QVariantMap, input);
    Appearance appearance;
    appearance.setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}, {QStringLiteral("fontScale"), 1.5}});
    const auto previousInput = appearance.input();
    const auto previousTokens = appearance.snapshot().tokens;
    QSignalSpy rejected(&appearance, &Appearance::inputRejected);
    QSignalSpy resolved(&appearance, &Appearance::resolvedChanged);
    QSignalSpy inputChanged(&appearance, &Appearance::inputChanged);
    QSignalSpy colorsChanged(appearance.colors(), &TokenGroup::changed);
    appearance.setInput(input);
    QCOMPARE(rejected.count(), 1);
    QCOMPARE(resolved.count(), 0);
    QCOMPARE(inputChanged.count(), 0);
    QCOMPARE(colorsChanged.count(), 0);
    QCOMPARE(appearance.input(), previousInput);
    QCOMPARE(appearance.snapshot().tokens, previousTokens);
    QCOMPARE(appearance.scheme(), QStringLiteral("dark"));
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("rejected-input")));
    QCOMPARE(rejected.first().first().toList(), appearance.diagnostics());
    appearance.setInput(previousInput);
    QVERIFY(!hasDiagnostic(appearance.diagnostics(), QStringLiteral("rejected-input")));
    QCOMPARE(appearance.snapshot().tokens, previousTokens);
}

void AppearanceTests::accentRelationships()
{
    Appearance appearance;
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QStringLiteral("#ffed00")}}));
    QVERIFY(appearance.colors()->onAccent() != QColor(Qt::black));
    QVERIFY(appearance.colors()->onAccent() != QColor(Qt::white));
    QVERIFY(appearance.colors()->onAccent().red() > appearance.colors()->onAccent().blue());
    QVERIFY(rgbContrast(appearance.colors()->focus(), appearance.colors()->content()) >= 3);
    QVERIFY(rgbContrast(appearance.colors()->link(), appearance.colors()->content()) >= 5);
    const QColor baseText = TokenSchema::contrastingText(appearance.colors()->accent(), {appearance.colors()->accent()},
                                                       appearance.colors()->content(), 7);
    QCOMPARE(appearance.colors()->accentHover(), rgbFeedback(appearance.colors()->accent(), baseText, 0.08));
    QCOMPARE(appearance.colors()->accentPressed(), rgbFeedback(appearance.colors()->accent(), baseText, 0.16));
    for (const auto &background : {appearance.colors()->accent(), appearance.colors()->accentHover(), appearance.colors()->accentPressed()})
        verifyReadableContrast(appearance.colors()->onAccent(), background, appearance.colors()->content(), 7);
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QColor("#101020")}}));
    QVERIFY(rgbContrast(appearance.colors()->onAccent(), appearance.colors()->accent()) >= 5);
    QVERIFY(appearance.colors()->onAccent().blue() > appearance.colors()->onAccent().red());
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QStringLiteral("#ffed00")},
        {QStringLiteral("onAccent"), QStringLiteral("white")}, {QStringLiteral("accentHover"), QStringLiteral("#aabbcc")}}));
    QCOMPARE(appearance.colors()->onAccent(), QColor(Qt::white));
    QCOMPARE(appearance.colors()->accentHover(), QColor("#aabbcc"));
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("low-contrast"), QStringLiteral("tokens.colors.onAccent/accent")));
}

void AppearanceTests::derivedLabelTarget()
{
    const QColor black(Qt::black), white(Qt::white);
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        for (const QColor fill : {QColor("#ffed00"), QColor("#101020"), QColor("#808080")}) {
            QList<QColor> normalLabels;
            for (const auto &contrast : {QStringLiteral("normal"), QStringLiteral("high")}) {
                Appearance appearance;
                // Pin identical state surfaces to isolate label derivation from
                // the mode-dependent companion floor and feedback recipe.
                QVariantMap supplied;
                for (const auto *role : {"accent", "error", "warning"}) {
                    const QString name = QString::fromLatin1(role);
                    for (const auto &state : {QString(), QStringLiteral("Hover"), QStringLiteral("Pressed")})
                        supplied.insert(name + state, fill);
                }
                appearance.setInput(paletteInput(scheme, contrast, supplied));
                ControlAppearance local;
                local.setSource(&appearance);
                local.setBackgroundColor(fill);
                const qreal floor = contrast == QLatin1String("high") ? 7 : 5;
                QCOMPARE(local.hoverBackgroundColor(), stateFeedback(fill, local.foregroundColor(), appearance.motion()->hoverBlend(),
                    appearance.colors()->content(), floor));
                QCOMPARE(local.pressedBackgroundColor(), stateFeedback(fill, local.foregroundColor(), appearance.motion()->pressedBlend(),
                    appearance.colors()->content(), floor));
                local.setHoverBackgroundColor(fill);
                local.setPressedBackgroundColor(fill);
                local.setSelectedBackgroundColor(fill);
                local.setInactiveBackgroundColor(fill);
                const auto *colors = appearance.colors();
                const QList<QColor> labels{colors->onAccent(), colors->onError(), colors->onWarning(),
                    colors->primaryText(), colors->destructiveText(), colors->cautionText(),
                    local.foregroundColor(), local.selectedForegroundColor(), local.inactiveForegroundColor()};
                QCOMPARE(colors->accent(), fill);
                QCOMPARE(colors->primarySurface(), fill);
                QCOMPARE(colors->primaryHover(), fill);
                QCOMPARE(colors->primaryPressed(), fill);
                QCOMPARE(local.backgroundColor(), fill);
                const qreal strongest = std::max(rgbContrast(black, fill), rgbContrast(white, fill));
                for (const QColor &label : labels) {
                    QCOMPARE(label, TokenSchema::contrastingText(fill, {fill}, colors->content(), 7));
                    if (fill == QColor("#808080")) {
                        QVERIFY(strongest < 7);
                        QVERIFY(std::abs(rgbContrast(label, fill) - strongest) < 0.001);
                        QCOMPARE(label, black);
                    } else {
                        verifyReadableContrast(label, fill, colors->content(), 7);
                        // Closest passing retains a hue-related proposal where
                        // one passes; strongest-only would choose an endpoint.
                        QVERIFY(label != black && label != white);
                    }
                }
                verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.primaryText/primarySurface"),
                    colors->primaryText(), fill, colors->content(), floor);
                verifyContrastDiagnostic(local.diagnostics(), QStringLiteral("foregroundColor/backgroundColor"),
                    local.foregroundColor(), fill, colors->content(), floor);
                if (contrast == QLatin1String("normal"))
                    normalLabels = labels;
                else
                    QCOMPARE(labels, normalLabels);
            }
        }
    }
}

void AppearanceTests::contrastingText_data()
{
    QTest::addColumn<QColor>("seed");
    QTest::addColumn<QList<QColor>>("backgrounds");
    QTest::addColumn<QColor>("canvas");
    const QList<QColor> seeds{QColor("#5C9CF5"), QColor("#F5A742"), QColor("#E06C75"),
        QColor("#8657b8"), QColor("#ffed00"), QColor("#20845f"), QColor("#805C9CF5"),
        QColor("#80E06C75"), QColor("#808080"), QColor("#80808080"), QColor(Qt::black), QColor(Qt::white),
        QColor::fromHsv(210, 160, 190)};
    for (const QColor &seed : seeds) {
        const QByteArray name = seed.name(QColor::HexArgb).toLatin1() + '-' + QByteArray::number(seed.spec());
        QTest::newRow((name + "-light").constData()) << seed
            << QList<QColor>{QColor(Qt::white), QColor("#e0e4e8")} << QColor(Qt::white);
        QTest::newRow((name + "-dark").constData()) << seed
            << QList<QColor>{QColor("#101214"), QColor("#25282d")} << QColor(Qt::black);
    }
    QTest::newRow("alpha-surfaces-light") << QColor("#805C9CF5")
        << QList<QColor>{QColor("#405C9CF5"), QColor("#605C9CF5"), QColor(Qt::transparent)} << QColor(Qt::white);
    QTest::newRow("alpha-surfaces-dark") << QColor("#80E06C75")
        << QList<QColor>{QColor("#20E06C75"), QColor("#40E06C75"), QColor(Qt::transparent)} << QColor("#101214");
    QTest::newRow("translucent-canvas-white-reference") << QColor("#8657b8")
        << QList<QColor>{QColor("#20ffffff"), QColor(Qt::transparent)} << QColor("#20ffffff");
}

void AppearanceTests::contrastingText()
{
    QFETCH(QColor, seed);
    QFETCH(QList<QColor>, backgrounds);
    QFETCH(QColor, canvas);
    const QColor text = TokenSchema::contrastingText(seed, backgrounds, canvas);
    QVERIFY(text.isValid());
    QCOMPARE(text.spec(), QColor::Rgb);
    QCOMPARE(text.alpha(), 255);
    QCOMPARE(text, QColor::fromRgba(text.rgba()));
    for (const QColor &background : backgrounds)
        QVERIFY2(rgbContrast(text, background, canvas) >= 4.5,
                 qPrintable(text.name() + QLatin1Char('/') + background.name(QColor::HexArgb)));
    if (seed.red() == seed.green() && seed.green() == seed.blue()) {
        QCOMPARE(text.red(), text.green());
        QCOMPARE(text.green(), text.blue());
    } else {
        QVERIFY(text != QColor(Qt::black));
        QVERIFY(text != QColor(Qt::white));
        if (seed.blue() > seed.red())
            QVERIFY(text.blue() > text.red());
        else if (seed.red() > seed.blue())
            QVERIFY(text.red() > text.blue());
        if (seed.green() > std::max(seed.red(), seed.blue()))
            QVERIFY(text.green() > std::max(text.red(), text.blue()));
    }
    const QColor readable("#1b4f8a");
    QCOMPARE(TokenSchema::contrastingText(readable, {QColor(Qt::white)}, QColor(Qt::white)), readable);
}

void AppearanceTests::multipleBackgrounds()
{
    const QColor black(Qt::black), white(Qt::white);
    for (const QColor &seed : {QColor("#5C9CF5"), QColor("#E06C75"), QColor("#808080")}) {
        const QColor readable = TokenSchema::contrastingText(seed, {black, white}, white);
        QVERIFY(rgbContrast(readable, black) >= 4.5);
        QVERIFY(rgbContrast(readable, white) >= 4.5);
        QVERIFY(readable != black && readable != white);
        const QColor reversed = TokenSchema::contrastingText(seed, {white, black}, white);
        QVERIFY(rgbContrast(reversed, black) >= 4.5);
        QVERIFY(rgbContrast(reversed, white) >= 4.5);

        const QColor best = TokenSchema::contrastingText(seed, {black, white}, white, 7);
        QCOMPARE(best.spec(), QColor::Rgb);
        QCOMPARE(best.alpha(), 255);
        const qreal minimum = std::min(rgbContrast(best, black), rgbContrast(best, white));
        // sqrt(21) is the continuous maximin bound for black and white, not 7.
        QVERIFY(minimum >= 4.5);
        QVERIFY(minimum <= std::sqrt(21.0) + 0.001);
        QVERIFY(std::sqrt(21.0) - minimum < 0.05);
        if (seed.red() == seed.blue()) {
            QCOMPARE(best.red(), best.green());
            QCOMPARE(best.green(), best.blue());
        }
        QVariantList diagnostics;
        TokenSchema::checkContrast(&diagnostics, QStringLiteral("text/black"), best, black, 7);
        TokenSchema::checkContrast(&diagnostics, QStringLiteral("text/white"), best, white, 7);
        QCOMPARE(diagnostics.size(), 2);
        verifyContrastDiagnostic(diagnostics, QStringLiteral("text/black"), best, black, white, 7);
        verifyContrastDiagnostic(diagnostics, QStringLiteral("text/white"), best, white, white, 7);
    }
    const QColor translucent("#805C9CF5");
    const QColor onWhite = TokenSchema::contrastingText(QColor("#5C9CF5"), {translucent}, white);
    const QColor onBlack = TokenSchema::contrastingText(QColor("#5C9CF5"), {translucent}, black);
    QVERIFY(rgbContrast(onWhite, translucent, white) >= 4.5);
    QVERIFY(rgbContrast(onBlack, translucent, black) >= 4.5);
    QVERIFY(onWhite != onBlack);
    QVERIFY(rgbContrast(onWhite, translucent, black) < 4.5);
    QVERIFY(rgbContrast(onBlack, translucent, white) < 4.5);
}

void AppearanceTests::feedbackSurface()
{
    for (const QColor &background : {QColor("#5C9CF5"), QColor("#F5A742"), QColor("#E06C75"),
                                     QColor("#8657b8"), QColor("#805C9CF5")}) {
        for (const QColor &foreground : {QColor(Qt::black), QColor(Qt::white), QColor("#301020"),
                                         QColor("#fff0ff"), QColor("#808080"), QColor("#80301020")}) {
            for (const qreal amount : {0.0, 0.04, 0.08, 0.12, 0.16, 1.0}) {
                const QColor actual = TokenSchema::feedbackSurface(background, foreground, amount);
                QCOMPARE(actual, rgbFeedback(background, foreground, amount));
            }
        }
    }
}

void AppearanceTests::semanticTextRoles()
{
    Appearance appearance;
    ControlAppearance label;
    label.setSource(&appearance);
    label.setFamily(QStringLiteral("label"));
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        for (const auto &contrast : {QStringLiteral("normal"), QStringLiteral("high")}) {
            appearance.setInput({{QStringLiteral("scheme"), scheme}, {QStringLiteral("contrast"), contrast}});
            const auto *colors = appearance.colors();
            const qreal target = contrast == QLatin1String("high") ? 7 : 5;
            for (const auto *role : {"accent", "error", "warning", "success"}) {
                const QString name = QString::fromLatin1(role);
                const QString textRole = name + QStringLiteral("Text");
                const QColor seed = colors->values().value(name).value<QColor>();
                const QColor text = colors->values().value(textRole).value<QColor>();
                QVERIFY(text.isValid());
                QCOMPARE(text.alpha(), 255);
                QVERIFY(text != QColor(Qt::black) && text != QColor(Qt::white));
                QVERIFY(rgbContrast(text, colors->content(), colors->canvas()) >= target);
                QVERIFY(seed.blue() > seed.red() ? text.blue() > text.red() : text.red() > text.blue());
                QList<QColor> backgrounds{colors->content()};
                verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.") + textRole + QStringLiteral("/content"),
                                         text, colors->content(), colors->canvas(), target);
                if (name != QLatin1String("accent")) {
                    const QString surfaceRole = name + QStringLiteral("Surface");
                    const QColor surface = colors->values().value(surfaceRole).value<QColor>();
                    backgrounds.append(surface);
                    QVERIFY(rgbContrast(text, surface, colors->canvas()) >= target);
                    verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.") + textRole + QLatin1Char('/') + surfaceRole,
                                             text, surface, colors->canvas(), target);
                }
                QCOMPARE(text, TokenSchema::contrastingText(seed, backgrounds, colors->canvas(), 7));
                verifyDerivedContrast(text, backgrounds, colors->canvas(), 7, textRole);
            }
            label.setTone(Presentation::Primary);
            QCOMPARE(label.foregroundColor(), colors->accentText());
            label.setTone(Presentation::Destructive);
            QCOMPARE(label.foregroundColor(), colors->errorText());
            label.setTone(Presentation::Caution);
            QCOMPARE(label.foregroundColor(), colors->warningText());
            QCOMPARE(label.backgroundColor(), colors->transparent());
            QCOMPARE(label.fillColor(), colors->accent());
        }
    }
    const QVariantMap explicitText{{QStringLiteral("accentText"), QColor("#123456")},
        {QStringLiteral("errorText"), QColor("#654321")}, {QStringLiteral("warningText"), QColor("#543210")},
        {QStringLiteral("successText"), QColor("#234510")}, {QStringLiteral("focus"), QColor("purple")},
        {QStringLiteral("link"), QColor("purple")}};
    appearance.setInput(tokenInput(QStringLiteral("colors"), explicitText));
    for (auto field = explicitText.cbegin(); field != explicitText.cend(); ++field)
        QCOMPARE(appearance.colors()->values().value(field.key()), field.value());
    QCOMPARE(label.foregroundColor(), QColor("#543210"));
    appearance.setInput({});
    QCOMPARE(label.foregroundColor(), appearance.colors()->warningText());
}

void AppearanceTests::contrastDiagnostics()
{
    QCOMPARE(TokenSchema::contrastRatio(QColor(Qt::black), QColor(Qt::white)), 21.0);
    QCOMPARE(TokenSchema::contrastRatio(QColor(Qt::transparent), QColor(Qt::black)), 1.0);
    const QColor gray("#777777");
    QVERIFY(rgbContrast(gray, QColor(Qt::black)) >= 4.5);
    QVERIFY(rgbContrast(gray, QColor(Qt::black)) < 5);
    QVariantList diagnostics;
    TokenSchema::checkContrast(&diagnostics, QStringLiteral("gray/black"), gray, QColor(Qt::black));
    QVERIFY(diagnostics.isEmpty()); // Direct callers still default to 4.5, not the resolver's 5.
    TokenSchema::checkContrast(&diagnostics, QStringLiteral("gray/black"), gray, QColor(Qt::black), 5);
    verifyContrastDiagnostic(diagnostics, QStringLiteral("gray/black"), gray, QColor(Qt::black), QColor(Qt::white), 5);
    diagnostics.clear();
    TokenSchema::checkContrast(&diagnostics, QStringLiteral("gray/white"), gray, QColor(Qt::white));
    verifyContrastDiagnostic(diagnostics, QStringLiteral("gray/white"), gray, QColor(Qt::white), QColor(Qt::white), 4.5);
    Appearance appearance;
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("text"), QStringLiteral("white")}}));
    QCOMPARE(appearance.colors()->text(), QColor(Qt::white));
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("low-contrast"), QStringLiteral("tokens.colors.text/content")));
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("text"), QStringLiteral("#1025282d")}}));
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("low-contrast")));
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("canvas"), QStringLiteral("transparent")}}));
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("translucent-canvas")));
    for (const QColor text : {QColor(Qt::black), QColor("#666666")}) {
        appearance.setInput(tokenInput(QStringLiteral("colors"),
            {{QStringLiteral("canvas"), QColor("#80000000")}, {QStringLiteral("text"), text}}));
        verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.text/canvas"),
                                 text, QColor("#80000000"), QColor(Qt::white), 5);
    }
}

void AppearanceTests::highContrast_data()
{
    QTest::addColumn<QString>("scheme");
    QTest::newRow("light") << QStringLiteral("light");
    QTest::newRow("dark") << QStringLiteral("dark");
}

void AppearanceTests::highContrast()
{
    QFETCH(QString, scheme);
    Appearance appearance;
    const auto defaultTokens = appearance.snapshot().tokens;
    const QVariantMap normalInput{{QStringLiteral("scheme"), scheme}, {QStringLiteral("contrast"), QStringLiteral("normal")}};
    const QVariantMap highInput{{QStringLiteral("scheme"), scheme}, {QStringLiteral("contrast"), QStringLiteral("high")}};
    appearance.setInput(normalInput);
    const auto normalTokens = appearance.snapshot().tokens;
    appearance.setInput(highInput);
    QCOMPARE(appearance.contrast(), QStringLiteral("high"));
    QCOMPARE(appearance.metrics()->borderWidth(), 2.0);
    QCOMPARE(appearance.metrics()->focusWidth(), 3.0);
    const QColor neutralText(scheme == QLatin1String("dark") ? Qt::white : Qt::black);
    for (const auto *role : {"text", "secondaryText", "placeholderText", "outline", "separator"})
        QCOMPARE(appearance.colors()->values().value(QString::fromLatin1(role)).value<QColor>(), neutralText);
    QCOMPARE(appearance.colors()->selection().alphaF(), 1.0f);
    QVERIFY(rgbContrast(appearance.colors()->text(), appearance.colors()->selection()) >= 7);
    QVERIFY(rgbContrast(appearance.colors()->outline(), appearance.colors()->content()) >= 3);
    const auto highTokens = appearance.snapshot().tokens;
    const auto highColors = appearance.colors()->values();
    QCOMPARE(appearance.colors()->accent(), QColor("#5C9CF5"));
    verifyPaletteSeparation(highColors, QStringLiteral("high default"));
    for (const auto *prefix : {"primary", "caution", "destructive"}) {
        const QString textRole = QString::fromLatin1(prefix) + QStringLiteral("Text");
        const QColor text = highColors.value(textRole).value<QColor>();
        for (const auto *state : {"Surface", "Hover", "Pressed"}) {
            const QString backgroundRole = QString::fromLatin1(prefix) + QLatin1String(state);
            const QColor background = highColors.value(backgroundRole).value<QColor>();
            verifyReadableContrast(text, background, appearance.colors()->canvas(), 7, textRole + QLatin1Char('/') + backgroundRole);
        }
    }
    QVERIFY(appearance.diagnostics().isEmpty());

    ControlAppearance localRose;
    localRose.setSource(&appearance);
    localRose.setTone(Presentation::Destructive);
    localRose.setBackgroundColor(QColor("#E06C75"));
    QCOMPARE(localRose.backgroundColor(), QColor("#E06C75"));
    QVERIFY(hasDiagnostic(localRose.diagnostics(), QStringLiteral("low-contrast"), QStringLiteral("foregroundColor/backgroundColor")));
    verifyContrastDiagnostic(localRose.diagnostics(), QStringLiteral("foregroundColor/backgroundColor"),
                             localRose.foregroundColor(), localRose.backgroundColor(), appearance.colors()->content(), 7);
    localRose.resetBackgroundColor();
    QCOMPARE(localRose.backgroundColor(), appearance.colors()->destructiveSurface());
    QCOMPARE(localRose.foregroundColor(), appearance.colors()->destructiveText());
    QVERIFY(localRose.diagnostics().isEmpty());

    appearance.setInput(normalInput);
    QCOMPARE(appearance.contrast(), QStringLiteral("normal"));
    QCOMPARE(appearance.snapshot().tokens, normalTokens);
    QVERIFY(appearance.diagnostics().isEmpty());
    appearance.setInput(highInput);
    QCOMPARE(appearance.snapshot().tokens, highTokens);
    QVERIFY(appearance.diagnostics().isEmpty());

    // Old default fills remain exact when the application explicitly supplies them.
    auto explicitInput = highInput;
    explicitInput.insert(QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), QVariantMap{
        {QStringLiteral("accent"), QColor("#5C9CF5")}, {QStringLiteral("warning"), QColor("#F5A742")},
        {QStringLiteral("error"), QColor("#E06C75")}}}});
    appearance.setInput(explicitInput);
    QCOMPARE(appearance.input(), explicitInput);
    QCOMPARE(appearance.colors()->accent(), QColor("#5C9CF5"));
    QCOMPARE(appearance.colors()->warning(), QColor("#F5A742"));
    QCOMPARE(appearance.colors()->error(), QColor("#E06C75"));
    QCOMPARE(appearance.colors()->destructiveSurface(), QColor("#E06C75"));
    const QColor rose = appearance.colors()->destructiveSurface();
    QVERIFY(rose != highColors.value(QStringLiteral("error")).value<QColor>());
    QVERIFY(std::max(rgbContrast(QColor(Qt::black), rose), rgbContrast(QColor(Qt::white), rose)) < 7);
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("low-contrast"), QStringLiteral("tokens.colors.destructiveText/destructiveSurface")));
    verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.destructiveText/destructiveSurface"),
                             appearance.colors()->destructiveText(), rose, appearance.colors()->canvas(), 7);
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("low-contrast"), QStringLiteral("tokens.colors.onError/error")));
    verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.onError/error"),
                             appearance.colors()->onError(), rose, appearance.colors()->canvas(), 7);
    for (const auto *prefix : {"primary", "caution"}) {
        const auto values = appearance.colors()->values();
        const QColor text = values.value(QString::fromLatin1(prefix) + QStringLiteral("Text")).value<QColor>();
        for (const auto *state : {"Surface", "Hover", "Pressed"}) {
            const QString role = QString::fromLatin1(prefix) + QLatin1String(state);
            const QColor background = values.value(role).value<QColor>();
            verifyReadableContrast(text, background, appearance.colors()->canvas(), 7, role);
            verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.") + QLatin1String(prefix)
                + QStringLiteral("Text/") + role, text, background, appearance.colors()->canvas(), 7);
        }
    }
    appearance.setInput(highInput);
    QCOMPARE(appearance.snapshot().tokens, highTokens);
    QVERIFY(appearance.diagnostics().isEmpty());
    appearance.setInput({{QStringLiteral("contrast"), QStringLiteral("high")},
                         {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), QVariantMap{
                             {QStringLiteral("text"), QStringLiteral("#eeeeee")}}},
                             {QStringLiteral("metrics"), QVariantMap{{QStringLiteral("borderWidth"), 0}, {QStringLiteral("focusWidth"), 0}}}}}});
    QCOMPARE(appearance.colors()->text(), QColor("#eeeeee"));
    QCOMPARE(appearance.metrics()->borderWidth(), 2.0);
    QCOMPARE(appearance.metrics()->focusWidth(), 3.0);
    QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("low-contrast")));
    appearance.setInput({});
    QVERIFY(appearance.input().isEmpty());
    QCOMPARE(appearance.contrast(), QStringLiteral("normal"));
    QCOMPARE(appearance.snapshot().tokens, defaultTokens);
    QVERIFY(appearance.diagnostics().isEmpty());
}

void AppearanceTests::reducedMotionAndDensity()
{
    Appearance appearance;
    appearance.setInput({{QStringLiteral("density"), QStringLiteral("touch")}, {QStringLiteral("fontScale"), 2},
                         {QStringLiteral("reducedMotion"), true}, {QStringLiteral("tokens"), QVariantMap{
                             {QStringLiteral("motion"), QVariantMap{{QStringLiteral("feedbackDuration"), 400}, {QStringLiteral("busyDuration"), 900}}},
                             {QStringLiteral("metrics"), QVariantMap{{QStringLiteral("controlHeight"), 20}, {QStringLiteral("targetSize"), 10}}}}}});
    QCOMPARE(appearance.motion()->feedbackDuration(), 0);
    QCOMPARE(appearance.motion()->popupDuration(), 0);
    QCOMPARE(appearance.motion()->busyDuration(), 0);
    QCOMPARE(appearance.motion()->tooltipDelay(), 500);
    QCOMPARE(appearance.motion()->hoverBlend(), 0.08);
    QCOMPARE(appearance.metrics()->controlHeight(), 44.0);
    QCOMPARE(appearance.metrics()->toolHeight(), 44.0);
    QCOMPARE(appearance.metrics()->rowHeight(), 44.0);
    QCOMPARE(appearance.metrics()->targetSize(), 44.0);
    QCOMPARE(appearance.typography()->controlSize(), 26.0);
    QCOMPARE(appearance.typography()->headingSize(), 32.0);
    appearance.setInput({});
    QCOMPARE(appearance.typography()->controlSize(), 13.0);
    QCOMPARE(appearance.metrics()->controlHeight(), 30.0);
    QCOMPARE(appearance.motion()->feedbackDuration(), 120);
}

void AppearanceTests::atomicNotifications()
{
    Appearance appearance;
    const auto input = QVariantMap{{QStringLiteral("scheme"), QStringLiteral("dark")}, {QStringLiteral("fontScale"), 2},
                                  {QStringLiteral("reducedMotion"), true},
                                  {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("metrics"), QVariantMap{{QStringLiteral("controlRadius"), 11}}}}}};
    int observations = 0;
    const auto observe = [&] {
        ++observations;
        QCOMPARE(appearance.input(), input);
        QCOMPARE(appearance.scheme(), QStringLiteral("dark"));
        QCOMPARE(appearance.colors()->canvas(), QColor("#202124"));
        QCOMPARE(appearance.metrics()->controlRadius(), 11.0);
        QCOMPARE(appearance.typography()->controlSize(), 26.0);
        QCOMPARE(appearance.motion()->feedbackDuration(), 0);
    };
    connect(&appearance, &Appearance::resolvedChanged, this, observe);
    connect(&appearance, &Appearance::inputChanged, this, observe);
    connect(appearance.colors(), &TokenGroup::changed, this, observe);
    connect(appearance.metrics(), &TokenGroup::changed, this, observe);
    connect(appearance.typography(), &TokenGroup::changed, this, observe);
    connect(appearance.motion(), &TokenGroup::changed, this, observe);
    QSignalSpy resolved(&appearance, &Appearance::resolvedChanged);
    appearance.setInput(input);
    QCOMPARE(resolved.count(), 1);
    QCOMPARE(observations, 6);
    appearance.setInput(input);
    QCOMPARE(resolved.count(), 1);
    QCOMPARE(observations, 6);
}

void AppearanceTests::engineLocalSingletons()
{
    QQmlEngine first;
    QQmlEngine second;
    auto *a = first.singletonInstance<Appearance *>("InterActive.Foundation", "Appearance");
    auto *b = second.singletonInstance<Appearance *>("InterActive.Foundation", "Appearance");
    QVERIFY(a);
    QVERIFY(b);
    QVERIFY(a != b);
    QVERIFY(a->colors() != b->colors());
    QCOMPARE(first.singletonInstance<Appearance *>("InterActive.Foundation", "Appearance"), a);
    a->setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}});
    QCOMPARE(a->scheme(), QStringLiteral("dark"));
    QCOMPARE(b->scheme(), QStringLiteral("light"));
    QCOMPARE(b->colors()->canvas(), QColor("#F4F5F7"));
    b->setInput(tokenInput(QStringLiteral("metrics"), {{QStringLiteral("controlRadius"), 12}}));
    QCOMPARE(a->metrics()->controlRadius(), 4.0);
    QCOMPARE(b->metrics()->controlRadius(), 12.0);
}

void AppearanceTests::qmlPublicApiAndReset()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQml
        import InterActive.Foundation
        ControlAppearance {
            source: Appearance
            family: "button"
            tone: Presentation.Primary
            validation: Presentation.Invalid
            Component.onCompleted: {
                Appearance.input = { schemaVersion: 1, scheme: "dark", tokens: { metrics: { controlRadius: 9 } } }
                backgroundColor = "purple"
            }
            function resetBackground() { backgroundColor = undefined }
            function useCaution() { tone = Presentation.Caution; validation = Presentation.None }
        }
    )", QUrl(QStringLiteral("inmemory:/appearance.qml")));
    const auto initialStatus = component.status();
    const auto componentDetails = [&] {
        const auto statuses = QMetaEnum::fromType<QQmlComponent::Status>();
        QStringList errors;
        for (const auto &error : component.errors())
            errors.append(error.toString());
        return QStringLiteral("url=%1; initialStatus=%2; status=%3; importPaths=[%4]; errors=[%5]")
            .arg(component.url().toString(), QString::fromLatin1(statuses.valueToKey(initialStatus)),
                 QString::fromLatin1(statuses.valueToKey(component.status())),
                 engine.importPathList().join(QStringLiteral(", ")), errors.join(QLatin1Char('\n')));
    };
    // setData() can return while import loading is still pending. Stop waiting
    // on Error as well as Ready so real loading errors fail immediately.
    QVERIFY2(QTest::qWaitFor([&] { return !component.isLoading(); }, 10000), qPrintable(componentDetails()));
    QVERIFY2(component.isReady(), qPrintable(componentDetails()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(componentDetails()));
    auto *control = qobject_cast<ControlAppearance *>(object.get());
    QVERIFY(control);
    QCOMPARE(control->backgroundColor(), QColor("purple"));
    QCOMPARE(control->source()->scheme(), QStringLiteral("dark"));
    QCOMPARE(control->controlRadius(), 9.0);
    QVERIFY(QMetaObject::invokeMethod(control, "resetBackground"));
    QCOMPARE(control->backgroundColor(), control->source()->colors()->primarySurface());
    QCOMPARE(control->foregroundColor(), control->source()->colors()->primaryText());
    QCOMPARE(control->borderColor(), control->source()->colors()->errorText());
    QCOMPARE(control->tone(), Presentation::Primary);
    QCOMPARE(control->validation(), Presentation::Invalid);
    control->setValidation(Presentation::None);
    QCOMPARE(control->borderColor(), control->source()->colors()->primaryOutline());
    QVERIFY(QMetaObject::invokeMethod(control, "useCaution"));
    QCOMPARE(control->tone(), Presentation::Caution);
    QCOMPARE(control->backgroundColor(), control->source()->colors()->cautionSurface());
    QCOMPARE(control->foregroundColor(), control->source()->colors()->cautionText());
    QCOMPARE(control->borderColor(), control->source()->colors()->cautionOutline());
}

void AppearanceTests::localDefaultsAndFamilies()
{
    ControlAppearance control;
    Appearance appearance;
    QVERIFY(!control.source());
    QCOMPARE(control.backgroundColor(), appearance.colors()->control());
    QCOMPARE(control.foregroundColor(), appearance.colors()->text());
    QCOMPARE(control.disabledForegroundColor(), appearance.colors()->disabledText());
    QCOMPARE(control.inactiveForegroundColor(), appearance.colors()->text());
    QVERIFY(control.minimumHeight() >= appearance.metrics()->controlHeight());
    QVERIFY(!control.fontFamily().isEmpty());
    QVERIFY(control.resolvedFont().pixelSize() > 0);
    QVERIFY(control.diagnostics().isEmpty());
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        appearance.setInput({{QStringLiteral("scheme"), scheme}});
        control.setSource(&appearance);
        for (const auto *family : {"button", "tool", "field", "surface", "popup", "label", "delegate", "tab", "choice", "tooltip"}) {
            control.setFamily(QString::fromLatin1(family));
            QVERIFY(control.backgroundColor().isValid());
            QVERIFY(control.foregroundColor().isValid());
            QVERIFY(control.hoverBackgroundColor().isValid());
            QVERIFY(control.pressedBackgroundColor().isValid());
            QVERIFY(control.minimumHeight() > 0);
            QVERIFY2(control.diagnostics().isEmpty(), family);
        }
    }
    control.setFamily(QStringLiteral("field"));
    QCOMPARE(control.backgroundColor(), appearance.colors()->content());
    control.setFamily(QStringLiteral("popup"));
    QCOMPARE(control.backgroundColor(), appearance.colors()->popup());
    QCOMPARE(control.controlRadius(), appearance.metrics()->popupRadius());
    control.setFamily(QStringLiteral("tooltip"));
    QCOMPARE(control.foregroundColor(), appearance.colors()->tooltipText());
    QCOMPARE(control.fontPixelSize(), appearance.typography()->supportingSize());
}

void AppearanceTests::localOverridesAndLiveReset()
{
    Appearance appearance;
    ControlAppearance customized, sibling;
    customized.setSource(&appearance);
    sibling.setSource(&appearance);
    const QColor initialBackground = customized.backgroundColor();
    const qreal initialRadius = customized.controlRadius();
    customized.setBackgroundColor(initialBackground);
    customized.setControlRadius(initialRadius);
    customized.setFontPixelSize(customized.fontPixelSize());
    customized.setFontWeight(customized.fontWeight());
    appearance.setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}, {QStringLiteral("fontScale"), 2},
                         {QStringLiteral("tokens"), QVariantMap{
                             {QStringLiteral("metrics"), QVariantMap{{QStringLiteral("controlRadius"), 12}}},
                             {QStringLiteral("typography"), QVariantMap{{QStringLiteral("controlWeight"), 600}}}}}});
    QCOMPARE(customized.backgroundColor(), initialBackground);
    QCOMPARE(customized.controlRadius(), initialRadius);
    QCOMPARE(customized.fontPixelSize(), 13.0);
    QCOMPARE(customized.fontWeight(), 400);
    QCOMPARE(sibling.backgroundColor(), QColor("#35373B"));
    QCOMPARE(sibling.controlRadius(), 12.0);
    QCOMPARE(sibling.fontPixelSize(), 26.0);
    QCOMPARE(sibling.fontWeight(), 600);
    QCOMPARE(customized.disabledForegroundColor(), appearance.colors()->disabledText());
    customized.resetBackgroundColor();
    QCOMPARE(customized.backgroundColor(), sibling.backgroundColor());
    QCOMPARE(customized.hoverBackgroundColor(), sibling.hoverBackgroundColor());
    customized.resetOverrides();
    QCOMPARE(customized.controlRadius(), sibling.controlRadius());
    QCOMPARE(customized.fontPixelSize(), sibling.fontPixelSize());
    QCOMPARE(customized.resolvedFont(), sibling.resolvedFont());
    QCOMPARE(appearance.scheme(), QStringLiteral("dark"));
    appearance.setInput({});
    QCOMPARE(customized.backgroundColor(), QColor("#F0F2F4"));
    QCOMPARE(customized.controlRadius(), 4.0);
}

void AppearanceTests::allLocalPropertiesAreResettable()
{
    Appearance appearance;
    ControlAppearance control;
    control.setSource(&appearance);
    const auto *meta = control.metaObject();
#define IA_TEST_RESET(type, name, upper, group, token) \
    { \
        const auto property = meta->property(meta->indexOfProperty(#name)); \
        QVERIFY(property.isWritable()); \
        QVERIFY(property.isResettable()); \
        const QVariant value = property.read(&control); \
        QVERIFY(property.write(&control, value)); \
        QVERIFY(property.reset(&control)); \
        QCOMPARE(property.read(&control), value); \
    }
    IA_CONTROL_PROPERTIES(IA_TEST_RESET)
#undef IA_TEST_RESET
}

void AppearanceTests::localDerivationAndDiagnostics()
{
    Appearance appearance;
    ControlAppearance control;
    control.setSource(&appearance);
    control.setBackgroundColor(QColor("#101020"));
    for (const auto &background : {control.backgroundColor(), control.hoverBackgroundColor(), control.pressedBackgroundColor()})
        verifyReadableContrast(control.foregroundColor(), background, appearance.colors()->content(), 5);
    QCOMPARE(control.hoverBackgroundColor(), stateFeedback(control.backgroundColor(), control.foregroundColor(), 0.08,
        appearance.colors()->content(), 5));
    QCOMPARE(control.pressedBackgroundColor(), stateFeedback(control.backgroundColor(), control.foregroundColor(), 0.16,
        appearance.colors()->content(), 5));
    QCOMPARE(control.disabledBackgroundColor(), appearance.colors()->disabledSurface());
    QCOMPARE(control.inactiveBackgroundColor(), appearance.colors()->inactiveSelection());
    control.setHoverBackgroundColor(QColor("#303040"));
    control.setForegroundColor(QColor("#101020"));
    QVERIFY(hasDiagnostic(control.diagnostics(), QStringLiteral("low-contrast"), QStringLiteral("foregroundColor/backgroundColor")));
    control.setBackgroundColor(QColor("#111122"));
    QCOMPARE(control.foregroundColor(), QColor("#101020"));
    QCOMPARE(control.hoverBackgroundColor(), QColor("#303040"));
    control.resetForegroundColor();
    for (const auto &background : {control.backgroundColor(), control.hoverBackgroundColor(), control.pressedBackgroundColor()})
        QVERIFY(rgbContrast(control.foregroundColor(), background) >= 5);
    control.setSelectedBackgroundColor(QColor("#111122"));
    QVERIFY(rgbContrast(control.selectedForegroundColor(), control.selectedBackgroundColor()) >= 5);
    control.setControlRadius(8);
    control.setControlRadius(-1);
    QCOMPARE(control.controlRadius(), 8.0);
    QVERIFY(hasDiagnostic(control.diagnostics(), QStringLiteral("invalid-value"), QStringLiteral("controlRadius")));
    control.setFontPixelSize(std::numeric_limits<double>::quiet_NaN());
    QVERIFY(std::isfinite(control.fontPixelSize()));
    QVERIFY(hasDiagnostic(control.diagnostics(), QStringLiteral("invalid-value"), QStringLiteral("fontPixelSize")));
    control.setBackgroundColor(QColor());
    QCOMPARE(control.backgroundColor(), QColor("#111122"));
    control.resetOverrides();
    QCOMPARE(control.controlRadius(), appearance.metrics()->controlRadius());
    QVERIFY(control.diagnostics().isEmpty());
    control.setFamily(QStringLiteral("unknown"));
    QCOMPARE(control.family(), QStringLiteral("button"));
    QVERIFY(hasDiagnostic(control.diagnostics(), QStringLiteral("invalid-value"), QStringLiteral("family")));
    control.setFamily(QStringLiteral("button"));
    QVERIFY(control.diagnostics().isEmpty());
}

void AppearanceTests::localPurple()
{
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        for (const auto &contrast : {QStringLiteral("normal"), QStringLiteral("high")}) {
            const bool high = contrast == QLatin1String("high");
            const qreal target = high ? 7 : 5;
            const QColor purple(high ? "#6A3D99" : "#8657b8");
            Appearance appearance;
            const QVariantMap input{{QStringLiteral("scheme"), scheme}, {QStringLiteral("contrast"), contrast}};
            appearance.setInput(input);
            ControlAppearance control, sibling;
            control.setSource(&appearance);
            sibling.setSource(&appearance);
            const QColor backdrop = opaqueOver(appearance.colors()->content(), opaqueOver(appearance.colors()->canvas(), QColor(Qt::white)));
            control.setBackgroundColor(purple);
            QCOMPARE(control.backgroundColor(), purple);
            QCOMPARE(control.hoverBackgroundColor(), stateFeedback(purple, control.foregroundColor(), appearance.motion()->hoverBlend(), backdrop, target));
            QCOMPARE(control.pressedBackgroundColor(), stateFeedback(purple, control.foregroundColor(), appearance.motion()->pressedBlend(), backdrop, target));
            QVERIFY(rgbLuminance(control.foregroundColor()) > rgbLuminance(purple));
            QVERIFY(visibleDistance(purple, control.hoverBackgroundColor(), backdrop) >= 4);
            for (const auto *state : {"backgroundColor", "hoverBackgroundColor", "pressedBackgroundColor"}) {
                const QColor background = control.property(state).value<QColor>();
                verifyReadableContrast(control.foregroundColor(), background, backdrop, target, QLatin1String(state));
                verifyContrastDiagnostic(control.diagnostics(), QStringLiteral("foregroundColor/") + QLatin1String(state),
                                         control.foregroundColor(), background, backdrop, target);
            }
            // P2 keeps the shared label while trading contrast down to the
            // selected readability floor for perceptible feedback.
            QVERIFY(control.diagnostics().isEmpty());
            QCOMPARE(control.resolvedFont(), sibling.resolvedFont());
            for (const auto *property : {"controlRadius", "minimumHeight", "horizontalPadding", "verticalPadding", "spacing"})
                QCOMPARE(control.property(property), sibling.property(property));
            control.resetBackgroundColor();
            for (const auto *property : {"backgroundColor", "hoverBackgroundColor", "pressedBackgroundColor", "foregroundColor"})
                QCOMPARE(control.property(property), sibling.property(property));
            QCOMPARE(appearance.input(), input);
        }
    }
}

void AppearanceTests::fontsAndMinimumHeight()
{
    Appearance appearance;
    ControlAppearance control;
    control.setSource(&appearance);
    control.setFontFamily(QStringLiteral("Monospace"));
    control.setFontPixelSize(25.5);
    control.setFontWeight(600);
    control.setVerticalPadding(9);
    control.setMinimumHeight(1);
    QCOMPARE(control.resolvedFont().family(), QStringLiteral("Monospace"));
    QCOMPARE(control.resolvedFont().pixelSize(), 26);
    QCOMPARE(int(control.resolvedFont().weight()), 600);
    QVERIFY(control.minimumHeight() >= std::ceil(QFontMetricsF(control.resolvedFont()).height()) + 18);
    appearance.setInput({{QStringLiteral("fontScale"), 2}, {QStringLiteral("density"), QStringLiteral("touch")}});
    QCOMPARE(control.fontPixelSize(), 25.5);
    QCOMPARE(control.resolvedFont().pixelSize(), 26);
    QVERIFY(control.minimumHeight() >= 44);
    control.resetOverrides();
    QCOMPARE(control.fontPixelSize(), 26.0);
    QCOMPARE(control.fontFamily(), appearance.typography()->family());
    QCOMPARE(control.fontWeight(), appearance.typography()->controlWeight());
    QVERIFY(control.minimumHeight() >= std::ceil(QFontMetricsF(control.resolvedFont()).height()) + 2 * control.verticalPadding());
    appearance.setInput({{QStringLiteral("fontScale"), 3}, {QStringLiteral("tokens"), QVariantMap{
        {QStringLiteral("typography"), QVariantMap{{QStringLiteral("controlSize"), 128}}}}}});
    const qreal largestDefault = control.fontPixelSize();
    const qreal largestHeight = control.minimumHeight();
    QCOMPARE(largestDefault, 384.0);
    control.setFontPixelSize(largestDefault);
    control.setMinimumHeight(largestHeight);
    appearance.setInput({});
    QCOMPARE(control.fontPixelSize(), largestDefault);
    QCOMPARE(control.minimumHeight(), largestHeight);
    QVERIFY(!hasDiagnostic(control.diagnostics(), QStringLiteral("invalid-value")));
}

void AppearanceTests::semanticPresentation()
{
    Appearance appearance;
    ControlAppearance control;
    control.setSource(&appearance);
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        appearance.setInput({{QStringLiteral("scheme"), scheme}});
        control.setValidation(Presentation::None);
        control.setTone(Presentation::Primary);
        QCOMPARE(control.backgroundColor(), appearance.colors()->primarySurface());
        QCOMPARE(control.foregroundColor(), appearance.colors()->primaryText());
        QCOMPARE(control.selectedForegroundColor(), appearance.colors()->primaryText());
        QCOMPARE(control.inactiveForegroundColor(), appearance.colors()->primaryText());
        QCOMPARE(control.borderColor(), appearance.colors()->primaryOutline());
        control.setTone(Presentation::Caution);
        QCOMPARE(control.backgroundColor(), appearance.colors()->cautionSurface());
        QCOMPARE(control.foregroundColor(), appearance.colors()->cautionText());
        QCOMPARE(control.borderColor(), appearance.colors()->cautionOutline());
        QCOMPARE(control.validation(), Presentation::None);
        control.setTone(Presentation::Destructive);
        QCOMPARE(control.backgroundColor(), appearance.colors()->destructiveSurface());
        QCOMPARE(control.foregroundColor(), appearance.colors()->destructiveText());
        QCOMPARE(control.borderColor(), appearance.colors()->destructiveOutline());
        control.setValidation(Presentation::Invalid);
        QCOMPARE(control.borderColor(), appearance.colors()->errorText());
        QCOMPARE(control.disabledBorderColor(), appearance.colors()->errorText());
        QVERIFY(rgbContrast(control.borderColor(), appearance.colors()->content(), appearance.colors()->canvas()) >= 5);
        QVERIFY(rgbContrast(control.borderColor(), appearance.colors()->errorSurface(), appearance.colors()->canvas()) >= 5);
        QCOMPARE(control.focusColor(), appearance.colors()->focus());
        control.setValidation(Presentation::Warning);
        QCOMPARE(control.borderColor(), appearance.colors()->warningText());
        QCOMPARE(control.disabledBorderColor(), appearance.colors()->warningText());
        QVERIFY(rgbContrast(control.borderColor(), appearance.colors()->content(), appearance.colors()->canvas()) >= 5);
        QVERIFY(rgbContrast(control.borderColor(), appearance.colors()->warningSurface(), appearance.colors()->canvas()) >= 5);
        control.setBorderColor(QColor("purple"));
        control.setValidation(Presentation::Invalid);
        QCOMPARE(control.borderColor(), QColor("purple"));
        QCOMPARE(control.disabledBorderColor(), QColor("purple"));
        control.resetOverrides();
        QCOMPARE(control.tone(), Presentation::Destructive);
        QCOMPARE(control.validation(), Presentation::Invalid);
        QCOMPARE(control.borderColor(), appearance.colors()->errorText());
        QCOMPARE(control.disabledBorderColor(), appearance.colors()->errorText());
    }
    control.setTone(Presentation::Auto);
    QCOMPARE(control.backgroundColor(), appearance.colors()->control());
    control.setTone(static_cast<Presentation::Tone>(99));
    QCOMPARE(control.tone(), Presentation::Auto);
    QVERIFY(hasDiagnostic(control.diagnostics(), QStringLiteral("invalid-value"), QStringLiteral("tone")));
}

void AppearanceTests::solidRecipes_data()
{
    QTest::addColumn<QString>("scheme");
    QTest::addColumn<QString>("contrast");
    QTest::addColumn<QString>("prefix");
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        for (const auto &contrast : {QStringLiteral("normal"), QStringLiteral("high")}) {
            for (const auto &prefix : {QStringLiteral("primary"), QStringLiteral("caution"), QStringLiteral("destructive")}) {
                const QString tag = scheme + QLatin1Char('-') + contrast + QLatin1Char('-') + prefix;
                QTest::newRow(qPrintable(tag)) << scheme << contrast << prefix;
            }
        }
    }
}

void AppearanceTests::solidRecipes()
{
    QFETCH(QString, scheme);
    QFETCH(QString, contrast);
    QFETCH(QString, prefix);
    Appearance appearance;
    appearance.setInput({{QStringLiteral("scheme"), scheme}, {QStringLiteral("contrast"), contrast}});
    const auto *colors = appearance.colors();
    const auto *motion = appearance.motion();
    const bool high = contrast == QLatin1String("high");
    const qreal target = high ? 7 : 5;
    QCOMPARE(motion->hoverBlend(), 0.08);
    QCOMPARE(motion->pressedBlend(), 0.16);
    const bool primary = prefix == QLatin1String("primary");
    const bool caution = prefix == QLatin1String("caution");
    const QString semanticRole = primary ? QStringLiteral("accent") : caution ? QStringLiteral("warning") : QStringLiteral("error");
    const QString onRole = primary ? QStringLiteral("onAccent") : caution ? QStringLiteral("onWarning") : QStringLiteral("onError");
    const auto color = [&](const QString &name) { return colors->values().value(name).value<QColor>(); };
    const QColor semantic = color(semanticRole);
    const QColor surface = color(prefix + QStringLiteral("Surface"));
    const QColor hover = color(prefix + QStringLiteral("Hover"));
    const QColor pressed = color(prefix + QStringLiteral("Pressed"));
    const QColor text = color(prefix + QStringLiteral("Text"));
    const QColor outline = color(prefix + QStringLiteral("Outline"));
    QCOMPARE(outline, text);
    const QColor backdrop = opaqueOver(colors->content(), opaqueOver(colors->canvas(), QColor(Qt::white)));
    if (primary) {
        QCOMPARE(semantic, QColor("#5C9CF5"));
    } else {
        const auto profile = AirmailProfiles::profile(scheme, QStringLiteral("Test Sans")).value(QStringLiteral("colors")).toMap();
        const mcu::Hct anchor(profile.value(semanticRole).value<QColor>().rgb()), actual(semantic.rgb());
        QVERIFY(actual.get_chroma() >= 8);
        QVERIFY(mcu::DiffDegrees(actual.get_hue(), anchor.get_hue()) <= 22);
    }
    verifyPaletteSeparation(colors->values(), QStringLiteral("solid recipe"));
    QCOMPARE(surface, semantic);
    const QColor baseText = TokenSchema::contrastingText(semantic, {surface}, backdrop, 7);
    QCOMPARE(hover, rgbFeedback(surface, baseText, motion->hoverBlend()));
    QCOMPARE(pressed, rgbFeedback(surface, baseText, motion->pressedBlend()));
    const QColor previousResting = TokenSchema::contrastingText(semantic, {surface}, backdrop, target);
    QCOMPARE(hover, rgbFeedback(surface, previousResting, motion->hoverBlend()));
    QCOMPARE(pressed, rgbFeedback(surface, previousResting, motion->pressedBlend()));
    QVERIFY(rgbContrast(baseText, pressed, backdrop) > rgbContrast(baseText, hover, backdrop));
    QVERIFY(rgbContrast(baseText, hover, backdrop) > rgbContrast(baseText, surface, backdrop));
    QCOMPARE(color(semanticRole + QStringLiteral("Hover")), hover);
    QCOMPARE(color(semanticRole + QStringLiteral("Pressed")), pressed);
    QCOMPARE(text, TokenSchema::contrastingText(semantic, {surface, hover, pressed}, backdrop, 7));
    QCOMPARE(text.spec(), QColor::Rgb);
    QCOMPARE(text.alpha(), 255);
    QCOMPARE(color(onRole).spec(), QColor::Rgb);
    QCOMPARE(color(onRole).alpha(), 255);
    QCOMPARE(color(onRole), text);
    QVERIFY(surface != hover && hover != pressed);
    for (const auto *state : {"Surface", "Hover", "Pressed"}) {
        const QString backgroundRole = prefix + QLatin1String(state);
        const QColor background = color(backgroundRole);
        QVERIFY(background != colors->content());
        verifyReadableContrast(text, background, backdrop, target, prefix + QStringLiteral("Text/") + backgroundRole);
        verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.") + prefix + QStringLiteral("Text/") + backgroundRole,
                                 text, background, backdrop, target);
    }
    for (const auto &suffix : {QString(), QStringLiteral("Hover"), QStringLiteral("Pressed")}) {
        const QString backgroundRole = semanticRole + suffix;
        verifyReadableContrast(color(onRole), color(backgroundRole), backdrop, target, onRole + QLatin1Char('/') + backgroundRole);
        verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.") + onRole + QLatin1Char('/') + backgroundRole,
                                 color(onRole), color(backgroundRole), backdrop, target);
    }
    QVERIFY(rgbContrast(outline, surface, backdrop) >= 3);
    verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.") + prefix + QStringLiteral("Outline/") + prefix + QStringLiteral("Surface"),
                             outline, surface, backdrop, 3);
    QVERIFY(!hasDiagnostic(appearance.diagnostics(), QStringLiteral("low-contrast"),
                           QStringLiteral("tokens.colors.") + prefix + QStringLiteral("Outline/content")));
    verifyDerivedContrast(text, {surface, hover, pressed}, backdrop, 7, prefix + QStringLiteral(" label target"));
    if (high)
        QVERIFY(text != QColor(Qt::black) && text != QColor(Qt::white));
    // Normal-mode fills are not moved to reach 7; their strongest label may
    // therefore be an endpoint instead of the old 5:1 hue-related proposal.
    if (text != QColor(Qt::black) && text != QColor(Qt::white))
        QVERIFY(primary ? text.blue() > text.red() : text.red() > text.blue());
    QVERIFY(appearance.diagnostics().isEmpty());

    ControlAppearance control, neutral;
    control.setSource(&appearance);
    neutral.setSource(&appearance);
    control.setTone(primary ? Presentation::Primary : caution ? Presentation::Caution : Presentation::Destructive);
    QCOMPARE(control.backgroundColor(), surface);
    QCOMPARE(control.hoverBackgroundColor(), stateFeedback(surface, text, motion->hoverBlend(), backdrop, target));
    QCOMPARE(control.pressedBackgroundColor(), stateFeedback(surface, text, motion->pressedBlend(), backdrop, target));
    QCOMPARE(control.selectedBackgroundColor(), stateFeedback(pressed, text, motion->pressedBlend(), backdrop, target,
        {surface, control.hoverBackgroundColor()}, true));
    QCOMPARE(control.inactiveBackgroundColor(), surface);
    QCOMPARE(control.foregroundColor(), text);
    QCOMPARE(control.selectedForegroundColor(), text);
    QCOMPARE(control.inactiveForegroundColor(), text);
    QCOMPARE(control.borderColor(), outline);
    QCOMPARE(control.disabledBorderColor(), colors->outline());
    QCOMPARE(control.fillColor(), colors->accent());
    QCOMPARE(control.disabledBackgroundColor(), colors->disabledSurface());
    QCOMPARE(control.disabledForegroundColor(), colors->disabledText());
    QCOMPARE(control.focusColor(), colors->focus());
    for (const auto *state : {"backgroundColor", "hoverBackgroundColor", "pressedBackgroundColor"}) {
        verifyReadableContrast(control.foregroundColor(), control.property(state).value<QColor>(), backdrop, target,
                               QStringLiteral("semantic ") + QLatin1String(state));
        verifyContrastDiagnostic(control.diagnostics(), QStringLiteral("foregroundColor/") + QLatin1String(state),
                                 text, control.property(state).value<QColor>(), backdrop, target);
    }
    verifyReadableContrast(control.selectedForegroundColor(), control.selectedBackgroundColor(), backdrop, target);
    verifyReadableContrast(control.inactiveForegroundColor(), control.inactiveBackgroundColor(), backdrop, target);
    verifyContrastDiagnostic(control.diagnostics(), QStringLiteral("selectedForegroundColor/selectedBackgroundColor"), text, control.selectedBackgroundColor(), backdrop, target);
    verifyContrastDiagnostic(control.diagnostics(), QStringLiteral("inactiveForegroundColor/inactiveBackgroundColor"), text, surface, backdrop, target);
    QVERIFY(control.diagnostics().isEmpty());
    QCOMPARE(control.resolvedFont(), neutral.resolvedFont());
    for (const auto *property : {"controlRadius", "minimumHeight", "horizontalPadding", "verticalPadding", "spacing"})
        QCOMPARE(control.property(property), neutral.property(property));
    QCOMPARE(neutral.backgroundColor(), colors->control());
    QCOMPARE(neutral.hoverBackgroundColor(), stateFeedback(colors->control(), colors->text(), motion->hoverBlend(), backdrop, target));
    QCOMPARE(neutral.pressedBackgroundColor(), stateFeedback(colors->control(), colors->text(), motion->pressedBlend(), backdrop, target));
    QCOMPARE(neutral.foregroundColor(), colors->text());
    QCOMPARE(neutral.selectedBackgroundColor(), stateFeedback(colors->selection(), colors->text(), motion->pressedBlend(), backdrop, target,
        {neutral.backgroundColor(), neutral.hoverBackgroundColor()}, true));
    QCOMPARE(neutral.inactiveBackgroundColor(), colors->inactiveSelection());
    QCOMPARE(neutral.borderColor(), colors->outline());
    const auto rawColors = AirmailProfiles::profile(scheme, appearance.typography()->family()).value(QStringLiteral("colors")).toMap();
    for (const auto *role : {"control", "controlHover", "controlPressed"})
        QCOMPARE(colors->values().value(QString::fromLatin1(role)), rawColors.value(QString::fromLatin1(role)));
    for (const auto *state : {"backgroundColor", "hoverBackgroundColor", "pressedBackgroundColor"})
        verifyReadableContrast(neutral.foregroundColor(), neutral.property(state).value<QColor>(), backdrop, target,
                               QStringLiteral("neutral ") + QLatin1String(state));

    control.setFamily(QStringLiteral("label"));
    QCOMPARE(control.backgroundColor(), colors->transparent());
    QCOMPARE(control.foregroundColor(), color(semanticRole + QStringLiteral("Text")));
}

void AppearanceTests::semanticBorderOverrides()
{
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        for (const auto tone : {Presentation::Primary, Presentation::Caution, Presentation::Destructive}) {
            Appearance appearance;
            ControlAppearance control;
            control.setSource(&appearance);
            control.setTone(tone);
            const QString prefix = tone == Presentation::Primary ? QStringLiteral("primary")
                : tone == Presentation::Caution ? QStringLiteral("caution") : QStringLiteral("destructive");
            const auto setColors = [&](const QVariantMap &colors) {
                appearance.setInput({{QStringLiteral("scheme"), scheme},
                    {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), colors}}}});
            };
            setColors({{prefix + QStringLiteral("Text"), QColor("purple")}});
            QCOMPARE(appearance.colors()->values().value(prefix + QStringLiteral("Outline")).value<QColor>(), QColor("purple"));
            QCOMPARE(control.borderColor(), control.foregroundColor());
            control.setForegroundColor(QColor("green"));
            QCOMPARE(control.borderColor(), QColor("green"));
            control.setBorderColor(control.borderColor()); // Presence, even when equal to text.
            control.setForegroundColor(QColor("blue"));
            QCOMPARE(control.borderColor(), QColor("green"));
            control.resetBorderColor();
            QCOMPARE(control.borderColor(), QColor("blue"));

            setColors({{prefix + QStringLiteral("Text"), QColor("purple")},
                       {prefix + QStringLiteral("Outline"), QColor("purple")}});
            QCOMPARE(control.borderColor(), QColor("purple")); // Explicit global outline beats implicit coupling.
            control.setBorderColor(QColor("red"));
            QCOMPARE(control.borderColor(), QColor("red"));
            control.resetBorderColor();
            QCOMPARE(control.borderColor(), QColor("purple"));
            setColors({});
            QCOMPARE(control.borderColor(), QColor("blue"));

            control.setValidation(Presentation::Invalid);
            QCOMPARE(control.borderColor(), appearance.colors()->errorText());
            control.setBorderColor(QColor("orange"));
            QCOMPARE(control.borderColor(), QColor("orange"));
            control.resetBorderColor();
            QCOMPARE(control.borderColor(), appearance.colors()->errorText());
            control.setValidation(Presentation::None);
            QCOMPARE(control.borderColor(), QColor("blue"));
            control.resetOverrides();
            QCOMPARE(control.borderColor(), control.foregroundColor());
            control.setBackgroundColor(QColor("purple"));
            QCOMPARE(control.borderColor(), control.foregroundColor());
            control.resetOverrides();
            QCOMPARE(control.borderColor(), appearance.colors()->values().value(prefix + QStringLiteral("Text")).value<QColor>());
            QCOMPARE(control.disabledBorderColor(), appearance.colors()->outline());
            QCOMPARE(control.focusColor(), appearance.colors()->focus());
            control.setTone(Presentation::Neutral);
            QCOMPARE(control.borderColor(), appearance.colors()->outline());
        }
    }
}

void AppearanceTests::solidOverrides()
{
    const QVariantMap explicitColors{
        {QStringLiteral("primarySurface"), QColor("#eadff5")}, {QStringLiteral("primaryHover"), QColor("#dfceed")},
        {QStringLiteral("primaryPressed"), QColor("#d5bde6")}, {QStringLiteral("primaryText"), QColor("#311448")},
        {QStringLiteral("primaryOutline"), QColor("#684080")},
        {QStringLiteral("destructiveSurface"), QColor("#f6e4e5")}, {QStringLiteral("destructiveHover"), QColor("#efd4d7")},
        {QStringLiteral("destructivePressed"), QColor("#e9c5ca")}, {QStringLiteral("destructiveText"), QColor("#4a1018")},
        {QStringLiteral("destructiveOutline"), QColor("#8c3543")},
        {QStringLiteral("cautionSurface"), QColor("#f5e4c0")}, {QStringLiteral("cautionHover"), QColor("#edd8b0")},
        {QStringLiteral("cautionPressed"), QColor("#e5cca0")}, {QStringLiteral("cautionText"), QColor("#483110")},
        {QStringLiteral("cautionOutline"), QColor("#805c30")},
        {QStringLiteral("accent"), QColor("#8657b8")}, {QStringLiteral("onAccent"), QColor(Qt::white)},
        {QStringLiteral("error"), QColor("#a32b3c")}, {QStringLiteral("onError"), QColor(Qt::white)},
        {QStringLiteral("warning"), QColor("#b88120")}, {QStringLiteral("onWarning"), QColor(Qt::white)}};
    const QVariantMap motion{{QStringLiteral("hoverBlend"), 0.04}, {QStringLiteral("pressedBlend"), 0.12}};
    for (const auto tone : {Presentation::Primary, Presentation::Caution, Presentation::Destructive}) {
        Appearance appearance;
        ControlAppearance control;
        control.setSource(&appearance);
        control.setTone(tone);
        const QString prefix = tone == Presentation::Primary ? QStringLiteral("primary")
            : tone == Presentation::Caution ? QStringLiteral("caution") : QStringLiteral("destructive");
        const QColor original = control.backgroundColor();
        control.setBackgroundColor(original); // Equal to the default is still an explicit override.
        control.setForegroundColor(QColor(Qt::white));
        control.setHoverBackgroundColor(QColor("#602080"));
        control.setPressedBackgroundColor(QColor("#501070"));
        control.setSelectedBackgroundColor(QColor("#401060"));
        control.setSelectedForegroundColor(QColor("#fff0ff"));
        control.setInactiveBackgroundColor(QColor("#301050"));
        control.setInactiveForegroundColor(QColor("#ffe0ff"));
        control.setBorderColor(QColor("purple"));
        for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
            appearance.setInput({{QStringLiteral("scheme"), scheme}, {QStringLiteral("contrast"), QStringLiteral("high")},
                                 {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), explicitColors},
                                                                       {QStringLiteral("motion"), motion}}}});
            for (auto field = explicitColors.cbegin(); field != explicitColors.cend(); ++field)
                QCOMPARE(appearance.colors()->values().value(field.key()), field.value());
            QCOMPARE(appearance.motion()->hoverBlend(), 0.04);
            QCOMPARE(appearance.motion()->pressedBlend(), 0.12);
            QCOMPARE(control.backgroundColor(), original);
            QCOMPARE(control.foregroundColor(), QColor(Qt::white));
            QCOMPARE(control.hoverBackgroundColor(), QColor("#602080"));
            QCOMPARE(control.pressedBackgroundColor(), QColor("#501070"));
            QCOMPARE(control.selectedBackgroundColor(), QColor("#401060"));
            QCOMPARE(control.selectedForegroundColor(), QColor("#fff0ff"));
            QCOMPARE(control.inactiveBackgroundColor(), QColor("#301050"));
            QCOMPARE(control.inactiveForegroundColor(), QColor("#ffe0ff"));
            QCOMPARE(control.borderColor(), QColor("purple"));
            QCOMPARE(control.disabledBackgroundColor(), appearance.colors()->disabledSurface());
        }
        control.resetOverrides();
        QCOMPARE(control.backgroundColor(), explicitColors.value(prefix + QStringLiteral("Surface")).value<QColor>());
        QCOMPARE(control.hoverBackgroundColor(), explicitColors.value(prefix + QStringLiteral("Hover")).value<QColor>());
        QCOMPARE(control.pressedBackgroundColor(), explicitColors.value(prefix + QStringLiteral("Pressed")).value<QColor>());
        QCOMPARE(control.foregroundColor(), explicitColors.value(prefix + QStringLiteral("Text")).value<QColor>());
        QCOMPARE(control.borderColor(), explicitColors.value(prefix + QStringLiteral("Outline")).value<QColor>());
        QCOMPARE(control.selectedBackgroundColor(), control.pressedBackgroundColor());
        QCOMPARE(control.inactiveBackgroundColor(), control.backgroundColor());
        QCOMPARE(control.selectedForegroundColor(), control.foregroundColor());
        QCOMPARE(control.inactiveForegroundColor(), control.foregroundColor());
        QCOMPARE(control.tone(), tone);

        control.setBorderColor(QColor("purple"));
        QCOMPARE(control.disabledBorderColor(), QColor("purple"));
        control.setDisabledBorderColor(QColor("gray"));
        control.setBorderColor(QColor("green"));
        QCOMPARE(control.disabledBorderColor(), QColor("gray"));
        control.resetDisabledBorderColor();
        QCOMPARE(control.disabledBorderColor(), QColor("green"));
        control.resetOverrides();
        QCOMPARE(control.disabledBorderColor(), appearance.colors()->outline());

        appearance.setInput({{QStringLiteral("scheme"), QStringLiteral("dark")},
                             {QStringLiteral("contrast"), QStringLiteral("high")},
                             {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("motion"), motion}}}});
        const QColor semantic = tone == Presentation::Primary ? appearance.colors()->accent()
            : tone == Presentation::Caution ? appearance.colors()->warning() : appearance.colors()->error();
        QCOMPARE(control.backgroundColor(), semantic);
        QCOMPARE(control.hoverBackgroundColor(), stateFeedback(semantic, control.foregroundColor(), 0.04, appearance.colors()->content(), 7));
        QCOMPARE(control.pressedBackgroundColor(), stateFeedback(semantic, control.foregroundColor(), 0.12, appearance.colors()->content(), 7));
        for (const auto &background : {control.backgroundColor(), control.hoverBackgroundColor(), control.pressedBackgroundColor()})
            verifyReadableContrast(control.foregroundColor(), background, appearance.colors()->content(), 7);
        QCOMPARE(control.borderColor(), appearance.colors()->values().value(prefix + QStringLiteral("Outline")).value<QColor>());
        control.setBackgroundColor(QColor("purple"));
        QCOMPARE(control.hoverBackgroundColor(), stateFeedback(QColor("purple"), control.foregroundColor(), 0.04, appearance.colors()->content(), 7));
        QCOMPARE(control.pressedBackgroundColor(), stateFeedback(QColor("purple"), control.foregroundColor(), 0.12, appearance.colors()->content(), 7));
        appearance.setInput({{QStringLiteral("scheme"), QStringLiteral("light")}});
        QCOMPARE(control.backgroundColor(), QColor("purple"));
        control.resetOverrides();
        QCOMPARE(control.backgroundColor(), appearance.colors()->values().value(prefix + QStringLiteral("Surface")).value<QColor>());
        QVERIFY(control.diagnostics().isEmpty());
    }

    Appearance appearance;
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        for (const auto &contrast : {QStringLiteral("normal"), QStringLiteral("high")}) {
            for (const QColor accent : {QColor("#8657b8"), QColor("#ffed00"), QColor("#20845f"),
                                       QColor("#808080"), QColor("#805C9CF5")}) {
                appearance.setInput({{QStringLiteral("scheme"), scheme}, {QStringLiteral("contrast"), contrast},
                                     {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"),
                                         QVariantMap{{QStringLiteral("accent"), accent}}}}}});
                const auto *colors = appearance.colors();
                QCOMPARE(colors->accent(), accent);
                QCOMPARE(colors->primarySurface(), accent);
                QCOMPARE(colors->primaryText().alpha(), 255);
                const qreal target = contrast == QLatin1String("high") ? 7 : 5;
                const QColor backdrop = opaqueOver(colors->content(), opaqueOver(colors->canvas(), QColor(Qt::white)));
                const QColor baseText = TokenSchema::contrastingText(accent, {accent}, backdrop, 7);
                QCOMPARE(colors->primaryHover(), rgbFeedback(accent, baseText, appearance.motion()->hoverBlend()));
                QCOMPARE(colors->primaryPressed(), rgbFeedback(accent, baseText, appearance.motion()->pressedBlend()));
                const QColor previousResting = TokenSchema::contrastingText(accent, {accent}, backdrop, target);
                QCOMPARE(colors->primaryHover(), rgbFeedback(accent, previousResting, appearance.motion()->hoverBlend()));
                QCOMPARE(colors->primaryPressed(), rgbFeedback(accent, previousResting, appearance.motion()->pressedBlend()));
                const QString context = QStringLiteral("primaryText scheme=%1 contrast=%2 seed=%3 content=%4 canvas=%5")
                    .arg(scheme, contrast, accent.name(QColor::HexArgb), colors->content().name(QColor::HexArgb), colors->canvas().name(QColor::HexArgb));
                verifyDerivedContrast(colors->primaryText(), {colors->primarySurface(), colors->primaryHover(), colors->primaryPressed()},
                                      backdrop, 7, context);
                for (const auto *state : {"Surface", "Hover", "Pressed"}) {
                    const QString role = QStringLiteral("primary") + QLatin1String(state);
                    const QColor background = colors->values().value(role).value<QColor>();
                    verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.primaryText/") + role,
                                             colors->primaryText(), background, backdrop, target);
                }
                if (accent.red() == accent.green() && accent.green() == accent.blue()) {
                    QCOMPARE(colors->primaryText().red(), colors->primaryText().green());
                    QCOMPARE(colors->primaryText().green(), colors->primaryText().blue());
                }
                if (scheme == QLatin1String("light") && accent == QColor("#ffed00"))
                    QVERIFY(colors->primaryText() != accent);
            }
        }
    }
    appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("accent"), QColor("#ffed00")},
        {QStringLiteral("primaryText"), QColor(Qt::white)}, {QStringLiteral("destructiveText"), QColor(Qt::white)},
        {QStringLiteral("cautionText"), QColor(Qt::white)}}));
    QCOMPARE(appearance.colors()->primaryText(), QColor(Qt::white));
    QCOMPARE(appearance.colors()->destructiveText(), QColor(Qt::white));
    QCOMPARE(appearance.colors()->cautionText(), QColor(Qt::white));
    const QColor backdrop = opaqueOver(appearance.colors()->content(), opaqueOver(appearance.colors()->canvas(), QColor(Qt::white)));
    for (const auto *prefix : {"primary", "caution", "destructive"}) {
        for (const auto *state : {"Surface", "Hover", "Pressed"}) {
            const QString path = QStringLiteral("tokens.colors.") + QLatin1String(prefix) + QStringLiteral("Text/")
                + QLatin1String(prefix) + QLatin1String(state);
            // A diagnostic is required exactly when the pair actually fails: a
            // new generated fill may legitimately read with the explicit white.
            verifyContrastDiagnostic(appearance.diagnostics(), path, QColor(Qt::white),
                                     appearance.colors()->values().value(QString::fromLatin1(prefix) + QLatin1String(state)).value<QColor>(),
                                     backdrop, 5);
        }
    }
}

void AppearanceTests::solidBackdropConsistency_data()
{
    QTest::addColumn<QString>("scheme");
    QTest::addColumn<QString>("contrast");
    QTest::addColumn<QVariantMap>("backdropColors");
    for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
        for (const auto &contrast : {QStringLiteral("normal"), QStringLiteral("high")}) {
            const QString tag = scheme + QLatin1Char('-') + contrast;
            QTest::newRow(qPrintable(tag + QStringLiteral("-default"))) << scheme << contrast << QVariantMap{};
            QTest::newRow(qPrintable(tag + QStringLiteral("-opaque"))) << scheme << contrast << QVariantMap{
                {QStringLiteral("content"), QColor("#1a2838")}, {QStringLiteral("canvas"), QColor("#f6efe2")}};
            QTest::newRow(qPrintable(tag + QStringLiteral("-translucent-content"))) << scheme << contrast << QVariantMap{
                {QStringLiteral("content"), QColor("#c0f8f2e8")}, {QStringLiteral("canvas"), QColor("#182838")}};
            QTest::newRow(qPrintable(tag + QStringLiteral("-translucent-content-and-canvas"))) << scheme << contrast << QVariantMap{
                {QStringLiteral("content"), QColor("#80406080")}, {QStringLiteral("canvas"), QColor("#60302010")}};
        }
    }
}

void AppearanceTests::solidBackdropConsistency()
{
    QFETCH(QString, scheme);
    QFETCH(QString, contrast);
    QFETCH(QVariantMap, backdropColors);
    const qreal target = contrast == QLatin1String("high") ? 7 : 5;
    const QStringList states{QStringLiteral("Surface"), QStringLiteral("Hover"), QStringLiteral("Pressed")};
    const QList<const char *> localStates{"backgroundColor", "hoverBackgroundColor", "pressedBackgroundColor"};
    for (const auto tone : {Presentation::Primary, Presentation::Caution, Presentation::Destructive}) {
        const bool primary = tone == Presentation::Primary;
        const bool caution = tone == Presentation::Caution;
        const QString prefix = primary ? QStringLiteral("primary") : caution ? QStringLiteral("caution") : QStringLiteral("destructive");
        const QString semanticRole = primary ? QStringLiteral("accent") : caution ? QStringLiteral("warning") : QStringLiteral("error");
        const QString onRole = primary ? QStringLiteral("onAccent") : caution ? QStringLiteral("onWarning") : QStringLiteral("onError");
        const QColor seed(primary ? "#805C9CF5" : caution ? "#80F5A742" : "#80E06C75");
        QVariantMap suppliedColors = backdropColors;
        suppliedColors.insert(semanticRole, seed);
        QVariantMap input{{QStringLiteral("scheme"), scheme}, {QStringLiteral("contrast"), contrast},
            {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), suppliedColors}}}};
        Appearance appearance;
        appearance.setInput(input);
        ControlAppearance control;
        control.setSource(&appearance);
        control.setTone(tone);
        const auto *colors = appearance.colors();
        const QColor backdrop = opaqueOver(colors->content(), opaqueOver(colors->canvas(), QColor(Qt::white)));
        const QString context = QStringLiteral("%1 scheme=%2 contrast=%3 seed=%4 content=%5 canvas=%6")
            .arg(prefix, scheme, contrast, seed.name(QColor::HexArgb), colors->content().name(QColor::HexArgb), colors->canvas().name(QColor::HexArgb));
        const QList<QColor> surfaces{colors->values().value(prefix + QStringLiteral("Surface")).value<QColor>(),
            colors->values().value(prefix + QStringLiteral("Hover")).value<QColor>(),
            colors->values().value(prefix + QStringLiteral("Pressed")).value<QColor>()};
        QCOMPARE(surfaces.first(), seed);
        const QColor derived = colors->values().value(prefix + QStringLiteral("Text")).value<QColor>();
        QCOMPARE(control.foregroundColor(), derived);
        QCOMPARE(derived, TokenSchema::contrastingText(seed, surfaces, TokenSchema::composite(colors->content(), colors->canvas()), 7));
        // Force the local derivation path, not merely consumption of the shared text role.
        control.setBackgroundColor(seed);
        QCOMPARE(control.foregroundColor(), derived);
        verifyDerivedContrast(derived, surfaces, backdrop, 7, context);
        QCOMPARE(hasDiagnostic(appearance.diagnostics(), QStringLiteral("translucent-canvas"), QStringLiteral("tokens.colors.canvas")),
                 colors->canvas().alphaF() < 1);

        // An explicit foreground identical to the rendered normal fill must be diagnosed.
        // Its reported ratio also detects a wrong backdrop even when both paths warn.
        const QColor unreadable = opaqueOver(seed, backdrop);
        for (const bool explicitPair : {false, true}) {
            if (explicitPair) {
                suppliedColors.insert(prefix + QStringLiteral("Text"), unreadable);
                suppliedColors.insert(onRole, unreadable);
                input.insert(QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), suppliedColors}});
                appearance.setInput(input);
                control.setForegroundColor(unreadable);
            }
            const auto values = colors->values();
            for (auto field = suppliedColors.cbegin(); field != suppliedColors.cend(); ++field)
                QCOMPARE(values.value(field.key()), field.value());
            const QColor text = values.value(prefix + QStringLiteral("Text")).value<QColor>();
            QCOMPARE(text, explicitPair ? unreadable : derived);
            QCOMPARE(control.foregroundColor(), text);
            QCOMPARE(values.value(onRole).value<QColor>(), text);
            const QList<QColor> expectedSurfaces = explicitPair ? QList<QColor>{seed,
                rgbFeedback(seed, unreadable, appearance.motion()->hoverBlend()),
                rgbFeedback(seed, unreadable, appearance.motion()->pressedBlend())} : surfaces;
            for (qsizetype i = 0; i < states.size(); ++i) {
                const QString globalRole = prefix + states[i];
                const QString semanticState = semanticRole + (i == 0 ? QString() : states[i]);
                const QColor background = values.value(globalRole).value<QColor>();
                QCOMPARE(background, expectedSurfaces[i]);
                QCOMPARE(values.value(semanticState).value<QColor>(), background);
                const QColor localBackground = control.property(localStates[i]).value<QColor>();
                if (i == 0)
                    QCOMPARE(localBackground, seed);
                else
                    QCOMPARE(localBackground.rgba(), stateFeedback(seed, control.foregroundColor(),
                        i == 1 ? appearance.motion()->hoverBlend() : appearance.motion()->pressedBlend(), backdrop, target).rgba());
                const QString globalPath = QStringLiteral("tokens.colors.") + prefix + QStringLiteral("Text/") + globalRole;
                const QString localPath = QStringLiteral("foregroundColor/") + QLatin1String(localStates[i]);
                verifyContrastDiagnostic(appearance.diagnostics(), globalPath, text, background, backdrop, target);
                verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.") + onRole + QLatin1Char('/') + semanticState,
                                         text, background, backdrop, target);
                verifyContrastDiagnostic(control.diagnostics(), localPath, control.foregroundColor(), localBackground, backdrop, target);
                if (explicitPair)
                    QVERIFY2(rgbContrast(text, background, backdrop) < target, qPrintable(context + QLatin1Char(' ') + globalRole));
            }
        }
    }
}

void AppearanceTests::applicationStateOverrides()
{
    for (const auto &prefix : {QStringLiteral("primary"), QStringLiteral("caution"), QStringLiteral("destructive")}) {
        Appearance appearance;
        const QString seedRole = prefix == QLatin1String("primary") ? QStringLiteral("accent")
            : prefix == QLatin1String("caution") ? QStringLiteral("warning") : QStringLiteral("error");
        const auto defaults = appearance.colors()->values();
        QVariantMap pinned{{seedRole, QColor("#20845f")}};
        for (const auto *state : {"Surface", "Hover", "Pressed", "Text", "Outline"}) {
            const QString role = prefix + QLatin1String(state);
            pinned.insert(role, defaults.value(role));
        }
        for (const auto &scheme : {QStringLiteral("light"), QStringLiteral("dark")}) {
            appearance.setInput({{QStringLiteral("scheme"), scheme}, {QStringLiteral("contrast"), QStringLiteral("high")},
                                 {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), pinned}}}});
            for (auto field = pinned.cbegin(); field != pinned.cend(); ++field)
                QCOMPARE(appearance.colors()->values().value(field.key()), field.value());
        }
        appearance.setInput(tokenInput(QStringLiteral("colors"), {{seedRole, QColor("#20845f")}}));
        QCOMPARE(appearance.colors()->values().value(prefix + QStringLiteral("Surface")).value<QColor>(), QColor("#20845f"));
        QVERIFY(appearance.colors()->values().value(prefix + QStringLiteral("Text")) != defaults.value(prefix + QStringLiteral("Text")));

        for (const auto *state : {"Hover", "Pressed"}) {
            for (const auto &rolePrefix : {prefix, seedRole}) {
                // Only one state is supplied: the normal surface must stay live.
                const QString overrideRole = rolePrefix + QLatin1String(state);
                appearance.setInput(tokenInput(QStringLiteral("colors"), {{overrideRole, QColor(Qt::black)}}));
                const auto values = appearance.colors()->values();
                QCOMPARE(values.value(overrideRole).value<QColor>(), QColor(Qt::black));
                QCOMPARE(values.value(prefix + QLatin1String(state)).value<QColor>(), QColor(Qt::black));
                const QString missingRole = prefix + (QLatin1String(state) == QLatin1String("Hover")
                    ? QStringLiteral("Pressed") : QStringLiteral("Hover"));
                if (prefix == QLatin1String("primary")) {
                    QCOMPARE(values.value(prefix + QStringLiteral("Surface")), defaults.value(prefix + QStringLiteral("Surface")));
                    QCOMPARE(values.value(missingRole), defaults.value(missingRole));
                } else {
                    const QColor base = values.value(seedRole).value<QColor>();
                    const QColor resting = TokenSchema::contrastingText(base, {base}, appearance.colors()->content(), 7);
                    QCOMPARE(values.value(prefix + QStringLiteral("Surface")).value<QColor>(), base);
                    QCOMPARE(values.value(missingRole).value<QColor>(), rgbFeedback(base, resting,
                        missingRole.endsWith(QLatin1String("Hover")) ? appearance.motion()->hoverBlend() : appearance.motion()->pressedBlend()));
                }
                const QColor text = values.value(prefix + QStringLiteral("Text")).value<QColor>();
                const QList<QColor> backgrounds{values.value(prefix + QStringLiteral("Surface")).value<QColor>(),
                    values.value(prefix + QStringLiteral("Hover")).value<QColor>(), values.value(prefix + QStringLiteral("Pressed")).value<QColor>()};
                const QColor backdrop = opaqueOver(appearance.colors()->content(), opaqueOver(appearance.colors()->canvas(), QColor(Qt::white)));
                QCOMPARE(text, TokenSchema::contrastingText(values.value(seedRole).value<QColor>(), backgrounds, backdrop, 7));
                verifyDerivedContrast(text, backgrounds, backdrop, 7, QStringLiteral("state-only reselection"));
                for (const auto *checkedState : {"Surface", "Hover", "Pressed"}) {
                    const QString role = prefix + QLatin1String(checkedState);
                    verifyContrastDiagnostic(appearance.diagnostics(), QStringLiteral("tokens.colors.") + prefix + QStringLiteral("Text/") + role,
                                              text, values.value(role).value<QColor>(), backdrop, 5);
                }
                appearance.setInput({});
                QCOMPARE(appearance.colors()->values(), defaults);
            }
        }
        const QVariantMap backgrounds{{prefix + QStringLiteral("Surface"), QColor(Qt::black)},
            {prefix + QStringLiteral("Hover"), QColor(Qt::white)}, {prefix + QStringLiteral("Pressed"), QColor(Qt::black)}};
        for (const auto &contrast : {QStringLiteral("normal"), QStringLiteral("high")}) {
            appearance.setInput({{QStringLiteral("contrast"), contrast},
                                 {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), backgrounds}}}});
            const auto values = appearance.colors()->values();
            const QColor text = values.value(prefix + QStringLiteral("Text")).value<QColor>();
            const QColor backdrop = opaqueOver(appearance.colors()->content(), opaqueOver(appearance.colors()->canvas(), QColor(Qt::white)));
            for (auto state = backgrounds.cbegin(); state != backgrounds.cend(); ++state) {
                QCOMPARE(values.value(state.key()), state.value());
                // Black/white maximin stays near sqrt(21), below both resolver targets.
                QVERIFY(rgbContrast(text, state.value().value<QColor>()) >= 4.5);
                const QString path = QStringLiteral("tokens.colors.") + prefix + QStringLiteral("Text/") + state.key();
                verifyContrastDiagnostic(appearance.diagnostics(), path, text, state.value().value<QColor>(),
                                         backdrop, contrast == QLatin1String("high") ? 7 : 5);
                QVERIFY(hasDiagnostic(appearance.diagnostics(), QStringLiteral("low-contrast"), path));
            }
        }
        appearance.setInput({});
        for (const auto *state : {"Surface", "Hover", "Pressed", "Text", "Outline"}) {
            const QString role = prefix + QLatin1String(state);
            QCOMPARE(appearance.colors()->values().value(role), defaults.value(role));
        }
    }
}

void AppearanceTests::localStateOverrides()
{
    for (const auto tone : {Presentation::Neutral, Presentation::Primary, Presentation::Caution, Presentation::Destructive}) {
        Appearance appearance;
        ControlAppearance control, sibling;
        control.setSource(&appearance);
        sibling.setSource(&appearance);
        control.setTone(tone);
        sibling.setTone(tone);
        const QColor originalText = control.foregroundColor();
        control.setForegroundColor(originalText);
        appearance.setInput({{QStringLiteral("scheme"), QStringLiteral("dark")},
                             {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), QVariantMap{
                                 {QStringLiteral("accent"), QColor("#20845f")}, {QStringLiteral("warning"), QColor("#8657b8")},
                                 {QStringLiteral("error"), QColor("#5C9CF5")}}}}}});
        QCOMPARE(control.foregroundColor(), originalText);
        control.resetForegroundColor();
        QCOMPARE(control.foregroundColor(), sibling.foregroundColor());

        // With every unmodified state white, a black hover OR press needs a middle tone.
        QVariantMap colors{{QStringLiteral("control"), QColor(Qt::white)}, {QStringLiteral("text"), QColor(Qt::black)}};
        for (const auto *prefix : {"primary", "caution", "destructive"}) {
            for (const auto *state : {"Surface", "Hover", "Pressed"})
                colors.insert(QString::fromLatin1(prefix) + QLatin1String(state), QColor(Qt::white));
        }
        const QVariantMap tokens{{QStringLiteral("colors"), colors},
            {QStringLiteral("motion"), QVariantMap{{QStringLiteral("hoverBlend"), 0.0}, {QStringLiteral("pressedBlend"), 0.0}}}};
        for (const auto &contrast : {QStringLiteral("normal"), QStringLiteral("high")}) {
            appearance.setInput({{QStringLiteral("contrast"), contrast}, {QStringLiteral("tokens"), tokens}});
            const qreal target = contrast == QLatin1String("high") ? 7 : 5;
            for (const auto *state : {"hoverBackgroundColor", "pressedBackgroundColor"}) {
                const QColor sharedText = control.foregroundColor();
                QVERIFY(control.setProperty(state, QColor(Qt::black)));
                QCOMPARE(control.backgroundColor(), QColor(Qt::white));
                QCOMPARE(control.property(state).value<QColor>(), QColor(Qt::black));
                QVERIFY(control.foregroundColor() != sharedText);
                for (const auto *background : {"backgroundColor", "hoverBackgroundColor", "pressedBackgroundColor"}) {
                    const QColor surface = control.property(background).value<QColor>();
                    // Best effort for incompatible black/white states, not target compliance.
                    QVERIFY(rgbContrast(control.foregroundColor(), surface) >= 4.5);
                    const QString path = QStringLiteral("foregroundColor/") + QLatin1String(background);
                    verifyContrastDiagnostic(control.diagnostics(), path, control.foregroundColor(), surface, QColor(Qt::white), target);
                    QVERIFY(hasDiagnostic(control.diagnostics(), QStringLiteral("low-contrast"), path));
                }
                QCOMPARE(control.selectedForegroundColor(), sibling.selectedForegroundColor());
                QCOMPARE(control.inactiveForegroundColor(), sibling.inactiveForegroundColor());
                control.setForegroundColor(QColor(Qt::white));
                QCOMPARE(control.foregroundColor(), QColor(Qt::white));
                QVERIFY(hasDiagnostic(control.diagnostics(), QStringLiteral("low-contrast"), QStringLiteral("foregroundColor/backgroundColor")));
                control.resetForegroundColor();
                QVERIFY(rgbContrast(control.foregroundColor(), QColor(Qt::black)) >= 4.5);
                QVERIFY(rgbContrast(control.foregroundColor(), QColor(Qt::white)) >= 4.5);
                const auto property = control.metaObject()->property(control.metaObject()->indexOfProperty(state));
                QVERIFY(property.reset(&control));
                QCOMPARE(control.foregroundColor(), sibling.foregroundColor());
                QCOMPARE(control.property(state), sibling.property(state));
            }
        }

        appearance.setInput({});
        control.setBackgroundColor(QColor("#101020"));
        control.setHoverBackgroundColor(QColor("#202030"));
        control.setPressedBackgroundColor(QColor("#303040"));
        control.setForegroundColor(QColor("#101020"));
        for (const auto *state : {"backgroundColor", "hoverBackgroundColor", "pressedBackgroundColor"})
            QVERIFY(hasDiagnostic(control.diagnostics(), QStringLiteral("low-contrast"), QStringLiteral("foregroundColor/") + QLatin1String(state)));
        control.resetForegroundColor();
        for (const auto &background : {control.backgroundColor(), control.hoverBackgroundColor(), control.pressedBackgroundColor()})
            QVERIFY(rgbContrast(control.foregroundColor(), background) >= 5);
        const QColor ordinaryText = control.foregroundColor();
        control.setSelectedBackgroundColor(QColor(Qt::white));
        control.setInactiveBackgroundColor(QColor(Qt::black));
        QVERIFY(rgbContrast(control.selectedForegroundColor(), control.selectedBackgroundColor()) >= 5);
        QVERIFY(rgbContrast(control.inactiveForegroundColor(), control.inactiveBackgroundColor()) >= 5);
        QCOMPARE(control.foregroundColor(), ordinaryText);
        control.setSelectedForegroundColor(QColor(Qt::white));
        control.setInactiveForegroundColor(QColor(Qt::black));
        QVERIFY(hasDiagnostic(control.diagnostics(), QStringLiteral("low-contrast"), QStringLiteral("selectedForegroundColor/selectedBackgroundColor")));
        QVERIFY(hasDiagnostic(control.diagnostics(), QStringLiteral("low-contrast"), QStringLiteral("inactiveForegroundColor/inactiveBackgroundColor")));
        control.resetSelectedForegroundColor();
        control.resetInactiveForegroundColor();
        QVERIFY(rgbContrast(control.selectedForegroundColor(), QColor(Qt::white)) >= 5);
        QVERIFY(rgbContrast(control.inactiveForegroundColor(), QColor(Qt::black)) >= 5);
        control.resetOverrides();
        QCOMPARE(control.foregroundColor(), sibling.foregroundColor());
        QCOMPARE(control.selectedForegroundColor(), sibling.selectedForegroundColor());
        QCOMPARE(control.inactiveForegroundColor(), sibling.inactiveForegroundColor());
        QCOMPARE(control.tone(), tone);

        appearance.setInput(tokenInput(QStringLiteral("colors"), {{QStringLiteral("canvas"), QColor(Qt::black)},
            {QStringLiteral("content"), QColor("#40000000")}}));
        control.setBackgroundColor(QColor("#405C9CF5"));
        control.setHoverBackgroundColor(QColor("#605C9CF5"));
        control.setPressedBackgroundColor(QColor("#805C9CF5"));
        const QColor canvas = opaqueOver(appearance.colors()->content(), opaqueOver(appearance.colors()->canvas(), QColor(Qt::white)));
        QCOMPARE(control.backgroundColor(), QColor("#405C9CF5"));
        QCOMPARE(control.hoverBackgroundColor(), QColor("#605C9CF5"));
        QCOMPARE(control.pressedBackgroundColor(), QColor("#805C9CF5"));
        QCOMPARE(control.foregroundColor().alpha(), 255);
        for (const auto *state : {"backgroundColor", "hoverBackgroundColor", "pressedBackgroundColor"}) {
            const QColor background = control.property(state).value<QColor>();
            QVERIFY(rgbContrast(control.foregroundColor(), background, canvas) >= 5);
            verifyContrastDiagnostic(control.diagnostics(), QStringLiteral("foregroundColor/") + QLatin1String(state),
                                     control.foregroundColor(), background, canvas, 5);
        }
        QVERIFY(rgbContrast(control.foregroundColor(), control.backgroundColor(), QColor(Qt::white)) < 5);
    }
}

void AppearanceTests::sourceLifetimeAndReplacement()
{
    ControlAppearance control;
    auto first = std::make_unique<Appearance>();
    Appearance second;
    first->setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}});
    control.setSource(first.get());
    control.setControlRadius(9);
    QCOMPARE(control.backgroundColor(), first->colors()->control());
    control.setSource(&second);
    QCOMPARE(control.backgroundColor(), second.colors()->control());
    QSignalSpy changed(&control, &ControlAppearance::resolvedChanged);
    first->setInput({});
    QCOMPARE(changed.count(), 0);
    control.setSource(first.get());
    first->setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}});
    changed.clear();
    first.reset();
    QCOMPARE(changed.count(), 1);
    QVERIFY(!control.source());
    QCOMPARE(control.backgroundColor(), QColor("#F0F2F4"));
    QCOMPARE(control.controlRadius(), 9.0);
    control.resetControlRadius();
    QCOMPARE(control.controlRadius(), 4.0);
}

void AppearanceTests::localAtomicNotification()
{
    Appearance appearance;
    ControlAppearance control;
    control.setSource(&appearance);
    QSignalSpy changes(&control, &ControlAppearance::resolvedChanged);
    const auto observation = connect(&control, &ControlAppearance::resolvedChanged, this, [&] {
        QCOMPARE(control.backgroundColor(), appearance.colors()->control());
        QCOMPARE(control.foregroundColor(), appearance.colors()->text());
        QCOMPARE(control.controlRadius(), 10.0);
        QCOMPARE(control.fontPixelSize(), 26.0);
        QCOMPARE(control.resolvedFont().pixelSize(), 26);
        QVERIFY(control.minimumHeight() >= QFontMetricsF(control.resolvedFont()).height() + 2 * control.verticalPadding());
    });
    appearance.setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}, {QStringLiteral("fontScale"), 2},
                         {QStringLiteral("tokens"), QVariantMap{{QStringLiteral("metrics"), QVariantMap{{QStringLiteral("controlRadius"), 10}}}}}});
    QCOMPARE(changes.count(), 1);
    disconnect(observation);
    control.setBackgroundColor(QColor("purple"));
    control.setControlRadius(9);
    changes.clear();
    control.resetOverrides();
    QCOMPARE(changes.count(), 1);
    QCOMPARE(control.backgroundColor(), appearance.colors()->control());
    QCOMPARE(control.controlRadius(), 10.0);
}

int main(int argc, char *argv[])
{
    // Shared test-only trace activation: inert unless IA_TEST_TRACE_MODE is set.
    const bool traced = InterActive::TestTrace::install(argv[0]);
    QGuiApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);
    AppearanceTests test;
    const int result = QTest::qExec(&test, argc, argv);
    if (traced && !InterActive::TestTrace::finalize()) {
        std::fprintf(stderr, "Test trace session failed.\n");
        return 1;
    }
    return result;
}
#include "tst_appearance.moc"
