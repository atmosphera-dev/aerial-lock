// SPDX-License-Identifier: LGPL-3.0-or-later
#include "TokenSchema.h"
#include "ColorMath_p.h"
#include "PaletteBuilder_p.h"
#include "TokenValidation_p.h"

namespace InterActive {

ResolvedAppearance TokenSchema::resolve(const QVariantMap &input, const QString &fontFamily)
{
    auto result = PaletteBuilder::prepare(input, fontFamily);
    if (!result.accepted)
        return result;
    result = PaletteBuilder::calculatePalette(std::move(result));
    PaletteBuilder::resolveNoncolors(&result);
    return result;
}

QVariantMap TokenSchema::definitions()
{
    return TokenValidation::definitions();
}

bool TokenSchema::validate(const QString &group, const QString &name, const QVariant &value,
                           QVariant *normalized, QVariantList *diagnostics, const QString &path, bool resolvedValue)
{
    return TokenValidation::validate(group, name, value, normalized, diagnostics, path, resolvedValue);
}

QVariantMap TokenSchema::diagnostic(const QString &path, const QString &code,
                                  const QString &message, const QString &severity)
{
    return TokenValidation::diagnostic(path, code, message, severity);
}

QColor TokenSchema::blend(const QColor &base, const QColor &toward, qreal amount)
{
    return ColorMath::blend(base, toward, amount);
}

QColor TokenSchema::feedbackSurface(const QColor &background, const QColor &foreground, qreal amount)
{
    return ColorMath::feedbackSurface(background, foreground, amount);
}

QColor TokenSchema::composite(const QColor &foreground, const QColor &background)
{
    return ColorMath::composite(foreground, background);
}

qreal TokenSchema::contrastRatio(const QColor &foreground, const QColor &background)
{
    return ColorMath::contrastRatio(foreground, background);
}

QColor TokenSchema::contrastingText(const QColor &seed, const QList<QColor> &backgrounds,
                                   const QColor &canvas, qreal target)
{
    return ColorMath::contrastingText(seed, backgrounds, canvas, target);
}

void TokenSchema::checkContrast(QVariantList *diagnostics, const QString &path,
                                const QColor &foreground, const QColor &background, qreal target)
{
    TokenValidation::checkContrast(diagnostics, path, foreground, background, target);
}

} // namespace InterActive
