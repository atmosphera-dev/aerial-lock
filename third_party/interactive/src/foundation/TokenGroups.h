// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "TokenDefinitions.h"
#include "interactive_foundation_export.h"
#include <QColor>
#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace InterActive {

class Appearance;
struct PaletteState;
struct PropertyNotifications;

class INTERACTIVE_FOUNDATION_EXPORT TokenGroup : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
public:
    explicit TokenGroup(QObject *parent = nullptr);
    ~TokenGroup() override;
    QVariantMap values() const { return m_values; }
signals:
    void changed();
protected:
    QVariantMap m_values;
private:
    friend class Appearance;
    void install(const QVariantMap &values);
    void initializeNotifications();
    void publish();
    std::unique_ptr<PropertyNotifications> m_notifications;
};

#define IA_COLOR_PROPERTY(name, light, dark) \
    Q_PROPERTY(QColor name READ name NOTIFY name##Changed FINAL)
#define IA_COLOR_GETTER(name, light, dark) \
    QColor name() const;
#define IA_REAL_PROPERTY(name, value, min, max) \
    Q_PROPERTY(qreal name READ name NOTIFY name##Changed FINAL)
#define IA_REAL_GETTER(name, defaultValue, min, max) \
    qreal name() const { return m_values.value(QStringLiteral(#name)).toDouble(); }
#define IA_INT_PROPERTY(name, value, min, max) \
    Q_PROPERTY(int name READ name NOTIFY name##Changed FINAL)
#define IA_INT_GETTER(name, defaultValue, min, max) \
    int name() const { return m_values.value(QStringLiteral(#name)).toInt(); }
#define IA_URL_PROPERTY(name, file) Q_PROPERTY(QUrl name READ name NOTIFY name##Changed FINAL)
#define IA_URL_GETTER(name, file) \
    QUrl name() const { return m_values.value(QStringLiteral(#name)).toUrl(); }

class INTERACTIVE_FOUNDATION_EXPORT ColorTokens : public TokenGroup
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ColorTokens)
    QML_UNCREATABLE("Token groups are owned by Appearance")
    IA_COLOR_TOKENS(IA_COLOR_PROPERTY)
public:
    using TokenGroup::TokenGroup;
    IA_COLOR_TOKENS(IA_COLOR_GETTER)
signals:
#define IA_TOKEN_SIGNAL(name, ...) void name##Changed();
    IA_COLOR_TOKENS(IA_TOKEN_SIGNAL)
private:
    friend class Appearance;
    std::shared_ptr<const PaletteState> m_palette;
};

class INTERACTIVE_FOUNDATION_EXPORT MetricTokens : public TokenGroup
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MetricTokens)
    QML_UNCREATABLE("Token groups are owned by Appearance")
    IA_METRIC_TOKENS(IA_REAL_PROPERTY)
public:
    using TokenGroup::TokenGroup;
    IA_METRIC_TOKENS(IA_REAL_GETTER)
signals:
    IA_METRIC_TOKENS(IA_TOKEN_SIGNAL)
};

class INTERACTIVE_FOUNDATION_EXPORT TypographyTokens : public TokenGroup
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TypographyTokens)
    QML_UNCREATABLE("Token groups are owned by Appearance")
    Q_PROPERTY(QString family READ family NOTIFY familyChanged FINAL)
    IA_TYPE_SIZE_TOKENS(IA_REAL_PROPERTY)
    IA_TYPE_WEIGHT_TOKENS(IA_INT_PROPERTY)
public:
    using TokenGroup::TokenGroup;
    QString family() const { return m_values.value(QStringLiteral("family")).toString(); }
    IA_TYPE_SIZE_TOKENS(IA_REAL_GETTER)
    IA_TYPE_WEIGHT_TOKENS(IA_INT_GETTER)
signals:
    void familyChanged();
    IA_TYPE_SIZE_TOKENS(IA_TOKEN_SIGNAL)
    IA_TYPE_WEIGHT_TOKENS(IA_TOKEN_SIGNAL)
};

class INTERACTIVE_FOUNDATION_EXPORT MotionTokens : public TokenGroup
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MotionTokens)
    QML_UNCREATABLE("Token groups are owned by Appearance")
    IA_MOTION_INT_TOKENS(IA_INT_PROPERTY)
    IA_MOTION_REAL_TOKENS(IA_REAL_PROPERTY)
public:
    using TokenGroup::TokenGroup;
    IA_MOTION_INT_TOKENS(IA_INT_GETTER)
    IA_MOTION_REAL_TOKENS(IA_REAL_GETTER)
signals:
    IA_MOTION_INT_TOKENS(IA_TOKEN_SIGNAL)
    IA_MOTION_REAL_TOKENS(IA_TOKEN_SIGNAL)
};

class INTERACTIVE_FOUNDATION_EXPORT IconTokens : public TokenGroup
{
    Q_OBJECT
    QML_NAMED_ELEMENT(IconTokens)
    QML_UNCREATABLE("Token groups are owned by Appearance")
    Q_PROPERTY(qreal strokeWidth READ strokeWidth NOTIFY strokeWidthChanged FINAL)
    IA_ICON_URL_TOKENS(IA_URL_PROPERTY)
public:
    using TokenGroup::TokenGroup;
    qreal strokeWidth() const { return m_values.value(QStringLiteral("strokeWidth")).toDouble(); }
    IA_ICON_URL_TOKENS(IA_URL_GETTER)
signals:
    void strokeWidthChanged();
    IA_ICON_URL_TOKENS(IA_TOKEN_SIGNAL)
};

#undef IA_COLOR_PROPERTY
#undef IA_COLOR_GETTER
#undef IA_REAL_PROPERTY
#undef IA_REAL_GETTER
#undef IA_INT_PROPERTY
#undef IA_INT_GETTER
#undef IA_URL_PROPERTY
#undef IA_URL_GETTER
#undef IA_TOKEN_SIGNAL

} // namespace InterActive
