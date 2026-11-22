// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ColorPlaneSurface.h"
#include <QColor>
#include <QElapsedTimer>
#include <QPainter>
#include <QQuickWindow>
#include <QtMath>
namespace InterActive {
ColorPlaneSurface::ColorPlaneSurface(QQuickItem *parent) : QQuickPaintedItem(parent)
{
}
void ColorPlaneSurface::setHue(qreal hue)
{
    if (!qIsFinite(hue)) return;
    hue = qBound(qreal(0), hue, qreal(1));
    if (m_hue == hue) return;
    m_hue = hue;
    refresh();
    emit hueChanged();
}
void ColorPlaneSurface::geometryChange(const QRectF &geometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(geometry, oldGeometry);
    if (geometry.size() != oldGeometry.size()) refresh();
}
void ColorPlaneSurface::itemChange(ItemChange change, const ItemChangeData &data)
{
    QQuickPaintedItem::itemChange(change, data);
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) refresh();
}
void ColorPlaneSurface::refresh()
{
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1;
    const QSize pixels(width() > 0 ? qCeil(width() * dpr) : 0, height() > 0 ? qCeil(height() * dpr) : 0);
    if (m_image.size() == pixels && m_dpr == dpr && m_cachedHue == m_hue) return;
    QImage image;
    if (!pixels.isEmpty()) {
        QElapsedTimer elapsed;
        elapsed.start();
        image = QImage(pixels, QImage::Format_RGB32);
        image.setDevicePixelRatio(dpr);
        for (int y = 0; y < pixels.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            const qreal saturation = pixels.height() > 1 ? 1 - qreal(y) / (pixels.height() - 1) : 0;
            for (int x = 0; x < pixels.width(); ++x) {
                const qreal lightness = pixels.width() > 1 ? qreal(x) / (pixels.width() - 1) : 0;
                line[x] = QColor::fromHslF(m_hue == 1 ? 0 : m_hue, saturation, lightness).rgb();
            }
        }
        ++m_rebuilds;
        m_lastRebuildNs = elapsed.nsecsElapsed();
    }
    m_image = image; m_cachedHue = m_hue; m_dpr = dpr;
    update();
}
void ColorPlaneSurface::paint(QPainter *painter)
{
    if (!m_image.isNull()) painter->drawImage(boundingRect(), m_image);
}
} // namespace InterActive
