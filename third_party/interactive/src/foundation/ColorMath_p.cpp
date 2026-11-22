// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ColorMath_p.h"
#include "AppearanceTrace_p.h"
#include "cpp/cam/hct.h"
#include "cpp/contrast/contrast.h"
#include "cpp/utils/utils.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace InterActive {
namespace {

qreal luminance(const QColor &color)
{
    const auto linear = [](qreal c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

namespace mcu = material_color_utilities;

} // namespace

QColor ColorMath::blend(const QColor &base, const QColor &toward, qreal amount)
{
    amount = std::clamp(amount, qreal(0), qreal(1));
    const auto mix = [amount](qreal a, qreal b) { return a + (b - a) * amount; };
    return QColor::fromRgbF(mix(base.redF(), toward.redF()), mix(base.greenF(), toward.greenF()),
                           mix(base.blueF(), toward.blueF()), mix(base.alphaF(), toward.alphaF()));
}

QColor ColorMath::feedbackSurface(const QColor &background, const QColor &foreground, qreal amount)
{
    const QColor black(Qt::black), white(Qt::white);
    const QColor toward = contrastRatio(foreground, white) >= contrastRatio(foreground, black) ? white : black;
    return blend(background, toward, amount);
}

QColor ColorMath::composite(const QColor &foreground, const QColor &background)
{
    const qreal alpha = foreground.alphaF() + background.alphaF() * (1 - foreground.alphaF());
    if (alpha == 0)
        return QColor(Qt::transparent);
    const auto over = [&](qreal f, qreal b) {
        return (f * foreground.alphaF() + b * background.alphaF() * (1 - foreground.alphaF())) / alpha;
    };
    return QColor::fromRgbF(over(foreground.redF(), background.redF()),
                           over(foreground.greenF(), background.greenF()),
                           over(foreground.blueF(), background.blueF()), alpha);
}

qreal ColorMath::contrastRatio(const QColor &foreground, const QColor &background)
{
    // Callers flatten their actual surface first. White is the documented
    // final reference only when an application makes even its canvas translucent.
    const QColor surface = composite(background, QColor(Qt::white));
    const qreal a = luminance(composite(foreground, surface));
    const qreal b = luminance(surface);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

QColor ColorMath::contrastingText(const QColor &seed, const QList<QColor> &backgrounds,
                                   const QColor &canvas, qreal target)
{
    const TraceAggregate aggregated(TraceScope::AggContrastingText, 0);
    if (!seed.isValid() || backgrounds.isEmpty() || !std::isfinite(target) || target < 1 || target > 21)
        return {};
    const QColor backdrop = composite(canvas, QColor(Qt::white));
    const QColor opaqueSeed = QColor::fromRgb(composite(seed, backdrop).rgb());
    const mcu::Hct hct(opaqueSeed.rgb());
    // CAM16 assigns small chroma to even RGB gray. It is not intended hue.
    const qreal chroma = seed.red() == seed.green() && seed.green() == seed.blue() ? 0 : hct.get_chroma();
    // Prepare each actual background once and retain full-precision and RGB8
    // luminances; candidate measurement reuses them within this call.
    QList<qreal> surfaceLuminances;
    QList<qreal> luminances;
    for (const QColor &background : backgrounds) {
        if (!background.isValid())
            return {};
        const QColor surface = composite(background, backdrop);
        const qreal surfaceLuminance = luminance(surface);
        surfaceLuminances.append(surfaceLuminance);
        surfaceLuminances.append(luminance(QColor::fromRgb(surface.rgb())));
        luminances.append(surfaceLuminance * 100);
    }
    const auto minimumContrast = [&](const QColor &foreground) {
        qreal minimum = 21;
        // Every generated foreground and the flattened seed is opaque. Surface
        // luminance is invariant across the candidate tones in this invocation.
        const qreal foregroundLuminance = luminance(foreground);
        for (const qreal backgroundLuminance : surfaceLuminances) {
            minimum = std::min(minimum, (std::max(foregroundLuminance, backgroundLuminance) + 0.05)
                / (std::min(foregroundLuminance, backgroundLuminance) + 0.05));
        }
        return minimum;
    };
    const auto fastReturn = [](const QColor &result) {
        const TraceAggregate fast(TraceScope::AggForegroundFast, 0);
        return result;
    };
    if (minimumContrast(opaqueSeed) >= target
        && (chroma != 0 || (opaqueSeed.red() == opaqueSeed.green() && opaqueSeed.green() == opaqueSeed.blue())))
        return fastReturn(opaqueSeed);

    // Cheap direct proposals before constructing interval-probe arrays. MCU's
    // algebraic exterior solutions use extrema across the full-precision and
    // RGB8 background representations; literal endpoints cover the cases where
    // MCU's safety margin returns -1 although the endpoint meets the target.
    qreal minimumTone = std::numeric_limits<qreal>::infinity();
    qreal maximumTone = -std::numeric_limits<qreal>::infinity();
    for (const qreal surfaceLuminance : surfaceLuminances) {
        const qreal tone = mcu::LstarFromY(surfaceLuminance * 100);
        minimumTone = std::min(minimumTone, tone);
        maximumTone = std::max(maximumTone, tone);
    }
    QList<QColor> fastProposals;
    if (const qreal tone = mcu::Darker(minimumTone, target); tone >= 0)
        fastProposals.append(QColor::fromRgb(mcu::Hct(hct.get_hue(), chroma, tone).ToInt()));
    if (const qreal tone = mcu::Lighter(maximumTone, target); tone >= 0)
        fastProposals.append(QColor::fromRgb(mcu::Hct(hct.get_hue(), chroma, tone).ToInt()));
    fastProposals.append(QColor(Qt::black));
    fastProposals.append(QColor(Qt::white));
    QColor bestFast;
    qreal bestFastDistance = std::numeric_limits<qreal>::infinity();
    bool fastReadable = false;
    for (const QColor &candidate : fastProposals) {
        // Judge the actual quantized color; a requested tone is not the output.
        if (minimumContrast(candidate) < target)
            continue;
        const qreal distance = std::abs(mcu::LstarFromArgb(candidate.rgb()) - hct.get_tone());
        if (!fastReadable || distance < bestFastDistance
            || (distance == bestFastDistance && candidate.rgb() < bestFast.rgb())) {
            bestFast = candidate;
            bestFastDistance = distance;
            fastReadable = true;
        }
    }
    if (fastReadable)
        return fastReturn(bestFast);

    // General fallback for mixed dark/light backgrounds, narrow feasible bands,
    // gamut/quantization edges and impossible targets. Its interval intersection,
    // interior probes and balanced-gap candidates are preserved unchanged.
    const TraceAggregate fallbackScope(TraceScope::AggForegroundFallback, 0);
    QList<qreal> tones{0, 100, hct.get_tone()};
    QList<QPair<qreal, qreal>> intervals{{0, 100}};
    for (const qreal y : luminances) {
        const qreal tone = mcu::LstarFromY(y);
        for (const qreal candidate : {mcu::Darker(tone, target), mcu::Lighter(tone, target)}) {
            if (candidate >= 0)
                tones.append(candidate);
        }
        // Each surface permits a darker OR lighter luminance interval. Keep
        // the intersection, including middle-tone solutions between surfaces.
        const qreal dark = (y + 5) / target - 5;
        const qreal light = target * (y + 5) - 5;
        QList<QPair<qreal, qreal>> remaining;
        for (const auto &[low, high] : intervals) {
            if (low <= std::min(high, dark))
                remaining.append({low, std::min(high, dark)});
            if (std::max(low, light) <= high)
                remaining.append({std::max(low, light), high});
        }
        intervals = std::move(remaining);
    }
    for (const auto &[low, high] : intervals) {
        const qreal lowTone = mcu::LstarFromY(low), highTone = mcu::LstarFromY(high);
        tones.append(std::clamp(hct.get_tone(), lowTone, highTone));
        // Narrow intervals may be smaller than the upstream 0.4-tone safety
        // margin. Probe their interior, then judge the quantized RGB result.
        for (int step = 0; step <= 16; ++step)
            tones.append(lowTone + (highTone - lowTone) * step / 16);
    }
    std::sort(luminances.begin(), luminances.end());
    for (qsizetype i = 1; i < luminances.size(); ++i) {
        // If the requested ratio is impossible, the maximin candidate in each
        // gap balances contrast with its two nearest bounding surfaces.
        tones.append(mcu::LstarFromY(std::sqrt((luminances[i - 1] + 5) * (luminances[i] + 5)) - 5));
    }
    QColor best = chroma == 0 ? QColor::fromRgb(mcu::Hct(hct.get_hue(), 0, hct.get_tone()).ToInt()) : opaqueSeed;
    qreal bestRatio = minimumContrast(best);
    qreal bestDistance = std::numeric_limits<qreal>::infinity();
    bool readable = false;
    for (const qreal tone : tones) {
        const QColor candidate = QColor::fromRgb(mcu::Hct(hct.get_hue(), chroma, tone).ToInt());
        const qreal ratio = minimumContrast(candidate);
        const qreal distance = std::abs(mcu::LstarFromArgb(candidate.rgb()) - hct.get_tone());
        if (ratio >= target ? (!readable || distance < bestDistance) : (!readable && ratio > bestRatio)) {
            best = candidate;
            bestRatio = ratio;
            bestDistance = distance;
            readable = ratio >= target;
        }
    }
    return best;
}

} // namespace InterActive
