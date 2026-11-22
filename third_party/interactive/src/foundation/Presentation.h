// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "interactive_foundation_export.h"
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace InterActive {

class INTERACTIVE_FOUNDATION_EXPORT Presentation : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Presentation provides enums")
public:
    enum Tone { Auto, Neutral, Primary, Destructive, Caution };
    Q_ENUM(Tone)
    enum Validation { None, Warning, Invalid };
    Q_ENUM(Validation)
    enum IndicatorPosition { Leading, Trailing };
    Q_ENUM(IndicatorPosition)
    enum State { Rest, Hover, Pressed, Selected, SelectedHover, SelectedPressed,
                 Inactive, InactiveHover, Disabled };
    Q_ENUM(State)
};

} // namespace InterActive
