// SPDX-License-Identifier: LGPL-3.0-or-later
#include "AirmailProfiles.h"
#include "TokenDefinitions.h"
#include <QColor>
#include <QUrl>

namespace InterActive {

QVariantMap AirmailProfiles::profile(const QString &scheme, const QString &fontFamily)
{
    const bool dark = scheme == QLatin1String("dark");
    QVariantMap colors, metrics, typography, motion, icons;
#define IA_SEED_COLOR(name, light, darkValue) \
    colors.insert(QStringLiteral(#name), QColor(QString::fromLatin1(dark ? darkValue : light)));
    IA_COLOR_TOKENS(IA_SEED_COLOR)
#undef IA_SEED_COLOR
#define IA_SEED_METRIC(name, value, min, max) metrics.insert(QStringLiteral(#name), qreal(value));
    IA_METRIC_TOKENS(IA_SEED_METRIC)
#undef IA_SEED_METRIC
    typography.insert(QStringLiteral("family"), fontFamily);
#define IA_SEED_SIZE(name, value, min, max) typography.insert(QStringLiteral(#name), qreal(value));
    IA_TYPE_SIZE_TOKENS(IA_SEED_SIZE)
#undef IA_SEED_SIZE
#define IA_SEED_WEIGHT(name, value, min, max) typography.insert(QStringLiteral(#name), int(value));
    IA_TYPE_WEIGHT_TOKENS(IA_SEED_WEIGHT)
#undef IA_SEED_WEIGHT
#define IA_SEED_MOTION_INT(name, value, min, max) motion.insert(QStringLiteral(#name), int(value));
    IA_MOTION_INT_TOKENS(IA_SEED_MOTION_INT)
#undef IA_SEED_MOTION_INT
#define IA_SEED_MOTION_REAL(name, value, min, max) motion.insert(QStringLiteral(#name), qreal(value));
    IA_MOTION_REAL_TOKENS(IA_SEED_MOTION_REAL)
#undef IA_SEED_MOTION_REAL
    motion.insert(QStringLiteral("semanticBlend"), dark ? 0.12 : 0.08);
    icons.insert(QStringLiteral("strokeWidth"), 1.5);
#define IA_SEED_URL(name, file) \
    icons.insert(QStringLiteral(#name), QUrl(QStringLiteral("qrc:/qt/qml/InterActive/Foundation/icons/" file)));
    IA_ICON_URL_TOKENS(IA_SEED_URL)
#undef IA_SEED_URL
    return {{QStringLiteral("colors"), colors}, {QStringLiteral("metrics"), metrics},
            {QStringLiteral("typography"), typography}, {QStringLiteral("motion"), motion},
            {QStringLiteral("icons"), icons}};
}

} // namespace InterActive
