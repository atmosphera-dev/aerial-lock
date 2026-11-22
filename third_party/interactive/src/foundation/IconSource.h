// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "interactive_foundation_export.h"
#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace InterActive {

class INTERACTIVE_FOUNDATION_EXPORT IconSource : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit IconSource(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE QUrl resolve(QObject *origin, const QUrl &source) const
    {
        if (source.isEmpty() || !source.isRelative())
            return source;
        // This public API exposes the outer context used by Qt's icon owner.
        if (const auto *context = QQmlEngine::contextForObject(origin))
            return context->resolvedUrl(source);
        return source;
    }
};

} // namespace InterActive
