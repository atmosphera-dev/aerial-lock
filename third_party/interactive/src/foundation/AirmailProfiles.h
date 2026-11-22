// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "interactive_foundation_export.h"
#include <QString>
#include <QVariantMap>

namespace InterActive {

class INTERACTIVE_FOUNDATION_EXPORT AirmailProfiles
{
public:
    static QVariantMap profile(const QString &scheme, const QString &fontFamily);
};

} // namespace InterActive
