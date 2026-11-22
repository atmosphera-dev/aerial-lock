// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "Appearance.h"
#include <QPointer>
#include <optional>

namespace InterActive {

// A decorative line has a sparse color override, not text/font/state machinery.
class INTERACTIVE_FOUNDATION_EXPORT SeparatorAppearance : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(InterActive::Appearance *source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    Q_PROPERTY(QColor foregroundColor READ foregroundColor WRITE setForegroundColor RESET resetForegroundColor NOTIFY foregroundColorChanged FINAL)
    Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY diagnosticsChanged FINAL)
public:
    explicit SeparatorAppearance(QObject *parent = nullptr);
    Appearance *source() const { return m_source; }
    void setSource(Appearance *source);
    QColor foregroundColor() const;
    void setForegroundColor(const QColor &color);
    void resetForegroundColor();
    QVariantList diagnostics() const { return m_diagnostics; }
    Q_INVOKABLE void resetOverrides() { resetForegroundColor(); }
signals:
    void sourceChanged();
    void foregroundColorChanged();
    void diagnosticsChanged();
private:
    void publish();
    QPointer<Appearance> m_source;
    QMetaObject::Connection m_changed, m_destroyed;
    std::optional<QColor> m_override;
    QColor m_notified;
    QVariantList m_diagnostics, m_notifiedDiagnostics;
    bool m_publishing = false;
};

} // namespace InterActive
