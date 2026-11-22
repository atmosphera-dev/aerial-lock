// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "TokenGroups.h"
#include "TokenSchema.h"
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace InterActive {

class INTERACTIVE_FOUNDATION_EXPORT Appearance : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap input READ input WRITE setInput NOTIFY inputChanged FINAL)
    Q_PROPERTY(InterActive::ColorTokens *colors READ colors CONSTANT FINAL)
    Q_PROPERTY(InterActive::MetricTokens *metrics READ metrics CONSTANT FINAL)
    Q_PROPERTY(InterActive::TypographyTokens *typography READ typography CONSTANT FINAL)
    Q_PROPERTY(InterActive::MotionTokens *motion READ motion CONSTANT FINAL)
    Q_PROPERTY(InterActive::IconTokens *icons READ icons CONSTANT FINAL)
    Q_PROPERTY(QString scheme READ scheme NOTIFY schemeChanged FINAL)
    Q_PROPERTY(QString density READ density NOTIFY densityChanged FINAL)
    Q_PROPERTY(QString contrast READ contrast NOTIFY contrastChanged FINAL)
    Q_PROPERTY(qreal fontScale READ fontScale NOTIFY fontScaleChanged FINAL)
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY reducedMotionChanged FINAL)
    Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY diagnosticsChanged FINAL)
public:
    explicit Appearance(QObject *parent = nullptr);
    ~Appearance() override;
    static Appearance *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    QVariantMap input() const { return m_input; }
    void setInput(const QVariantMap &input);
    ColorTokens *colors() const { return m_colors; }
    MetricTokens *metrics() const { return m_metrics; }
    TypographyTokens *typography() const { return m_typography; }
    MotionTokens *motion() const { return m_motion; }
    IconTokens *icons() const { return m_icons; }
    QString scheme() const { return m_resolved.scheme; }
    QString density() const { return m_resolved.density; }
    QString contrast() const { return m_resolved.contrast; }
    qreal fontScale() const { return m_resolved.fontScale; }
    bool reducedMotion() const { return m_resolved.reducedMotion; }
    QVariantList diagnostics() const { return m_diagnostics; }
    const ResolvedAppearance &snapshot() const { return m_resolved; }
    static QString initialFontFamily();
signals:
    void inputChanged();
    void resolvedChanged();
    void diagnosticsChanged();
    void inputRejected(const QVariantList &diagnostics);
    void schemeChanged();
    void densityChanged();
    void contrastChanged();
    void fontScaleChanged();
    void reducedMotionChanged();
private:
    QVariantMap m_input;
    const QString m_fontFamily;
    ResolvedAppearance m_prepared;
    ResolvedAppearance m_resolved;
    QVariantList m_diagnostics;
    ColorTokens *m_colors;
    MetricTokens *m_metrics;
    TypographyTokens *m_typography;
    MotionTokens *m_motion;
    IconTokens *m_icons;
};

} // namespace InterActive
