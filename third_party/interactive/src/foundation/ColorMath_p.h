// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QList>

namespace InterActive {

// Pure color helpers; no schema, profile or appearance ownership.
class ColorMath
{
public:
    static QColor blend(const QColor &base, const QColor &toward, qreal amount);
    static QColor feedbackSurface(const QColor &background, const QColor &foreground, qreal amount);
    static QColor composite(const QColor &foreground, const QColor &background);
    static qreal contrastRatio(const QColor &foreground, const QColor &background);
    static QColor contrastingText(const QColor &seed, const QList<QColor> &backgrounds,
                                  const QColor &canvas, qreal target = 4.5);
};

} // namespace InterActive
