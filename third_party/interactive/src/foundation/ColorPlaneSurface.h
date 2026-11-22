// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "interactive_foundation_export.h"
#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>
namespace InterActive {
class INTERACTIVE_FOUNDATION_EXPORT ColorPlaneSurface : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal hue READ hue WRITE setHue NOTIFY hueChanged FINAL)
public:
    explicit ColorPlaneSurface(QQuickItem *parent = nullptr);
    qreal hue() const { return m_hue; }
    void setHue(qreal hue);
    void paint(QPainter *painter) override;
    quint64 rebuildCount() const { return m_rebuilds; }
    qint64 lastRebuildNanoseconds() const { return m_lastRebuildNs; }
    QSize cachedPixelSize() const { return m_image.size(); }
    qreal cachedDpr() const { return m_dpr; }
protected:
    void geometryChange(const QRectF &geometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &data) override;
signals:
    void hueChanged();
private:
    void refresh();
    qreal m_hue = 0;
    qreal m_cachedHue = -1;
    qreal m_dpr = 0;
    quint64 m_rebuilds = 0;
    qint64 m_lastRebuildNs = 0;
    QImage m_image;
};
} // namespace InterActive
