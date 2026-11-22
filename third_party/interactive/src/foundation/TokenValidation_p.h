// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "TokenSchema.h"

namespace InterActive {

// Schema and input/diagnostic boundary, independent of palette construction.
class TokenValidation
{
public:
    static QVariantMap definitions();
    static bool validate(const QString &group, const QString &name, const QVariant &value,
                         QVariant *normalized, QVariantList *diagnostics,
                         const QString &path = {}, bool resolvedValue = false);
    static QVariantMap diagnostic(const QString &path, const QString &code,
                                  const QString &message, const QString &severity = QStringLiteral("warning"));
    static ResolvedAppearance parseEnvelope(const QVariantMap &input);
    static void applyTokens(ResolvedAppearance &result, const QVariantMap &suppliedGroups);
    static void checkContrast(QVariantList *diagnostics, const QString &path,
                              const QColor &foreground, const QColor &background, qreal target = 4.5);
};

} // namespace InterActive
