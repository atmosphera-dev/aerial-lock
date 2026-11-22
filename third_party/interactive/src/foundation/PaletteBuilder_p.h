// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "TokenSchema.h"

namespace InterActive {

// Profile preparation and finished palette/non-color output construction.
class PaletteBuilder
{
public:
    static ResolvedAppearance prepare(const QVariantMap &input, const QString &fontFamily);
    static ResolvedAppearance calculatePalette(ResolvedAppearance prepared);
    static void resolveNoncolors(ResolvedAppearance *prepared,
                                 const ResolvedAppearance *previousInput = nullptr,
                                 const ResolvedAppearance *previousOutput = nullptr);
};

} // namespace InterActive
