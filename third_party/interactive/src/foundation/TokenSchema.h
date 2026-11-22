// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "interactive_foundation_export.h"
#include <QColor>
#include <QList>
#include <QSet>
#include <QString>
#include <QVariantMap>

namespace InterActive {

struct INTERACTIVE_FOUNDATION_EXPORT ResolvedAppearance
{
    bool accepted = true;
    QString scheme = QStringLiteral("light");
    QString density = QStringLiteral("compact");
    QString contrast = QStringLiteral("normal");
    qreal fontScale = 1;
    bool reducedMotion = false;
    QVariantMap tokens;
    QSet<QString> explicitTokens;
    QVariantList diagnostics;
};

// Compatibility facade for the stateless API used by applications and tests.
// Foundation internals use the private validation, builder and color-math units.
class INTERACTIVE_FOUNDATION_EXPORT TokenSchema
{
public:
    static ResolvedAppearance resolve(const QVariantMap &input, const QString &fontFamily);
    static QVariantMap definitions();
    static bool validate(const QString &group, const QString &name, const QVariant &value,
                         QVariant *normalized, QVariantList *diagnostics,
                         const QString &path = {}, bool resolvedValue = false);
    static QVariantMap diagnostic(const QString &path, const QString &code,
                                  const QString &message, const QString &severity = QStringLiteral("warning"));
    static QColor blend(const QColor &base, const QColor &toward, qreal amount);
    static QColor feedbackSurface(const QColor &background, const QColor &foreground, qreal amount);
    static QColor composite(const QColor &foreground, const QColor &background);
    static qreal contrastRatio(const QColor &foreground, const QColor &background);
    static QColor contrastingText(const QColor &seed, const QList<QColor> &backgrounds,
                                  const QColor &canvas, qreal target = 4.5);
    static void checkContrast(QVariantList *diagnostics, const QString &path,
                              const QColor &foreground, const QColor &background,
                              qreal target = 4.5);
};

} // namespace InterActive
