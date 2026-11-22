// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ThemeIconItem.h"
#include "AppearanceTrace_p.h"
#include <QIcon>
#include <QPainter>
#include <QQuickWindow>
#include <QtMath>

namespace InterActive {

ThemeIconItem::ThemeIconItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void ThemeIconItem::setName(const QString &name)
{
    if (m_name == name)
        return;
    m_name = name;
    refresh();
    emit nameChanged();
}

void ThemeIconItem::setColor(const QColor &color)
{
    if (m_color == color)
        return;
    m_color = color;
    refresh();
    emit colorChanged();
}

void ThemeIconItem::geometryChange(const QRectF &geometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(geometry, oldGeometry);
    if (geometry.size() != oldGeometry.size())
        refresh();
}

void ThemeIconItem::itemChange(ItemChange change, const ItemChangeData &data)
{
    QQuickPaintedItem::itemChange(change, data);
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged)
        refresh();
}

void ThemeIconItem::refresh()
{
    TraceSpan span(TraceScope::IconRefresh, this);
    // Icon engines may load files and create pixmaps: keep them on the GUI thread.
    const QIcon icon = m_name.isEmpty() ? QIcon() : QIcon::fromTheme(m_name);
    QImage image;
    if (!icon.isNull() && width() > 0 && height() > 0) {
        const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1;
        const QSize size(qCeil(width()), qCeil(height()));
        image = icon.pixmap(size, dpr, QIcon::Normal, QIcon::Off).toImage();
        if (!image.isNull() && m_color.alpha() != 0) {
            // Opaque theme pixmaps may use an RGB format that cannot retain tint alpha.
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QPainter painter(&image);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(QRect(QPoint(), image.size()), m_color);
        }
    }
    m_image = image;
    // A non-null QIcon can still fail to rasterize; leave source fallback available.
    const bool available = !m_image.isNull();
    if (m_available != available) {
        m_available = available;
        emit availableChanged();
    }
    update();
}

void ThemeIconItem::paint(QPainter *painter)
{
    // The render thread only consumes the image prepared before synchronization.
    if (m_image.isNull())
        return;
    QSizeF size = m_image.deviceIndependentSize();
    size.scale(boundingRect().size(), Qt::KeepAspectRatio);
    const QRectF target((width() - size.width()) / 2, (height() - size.height()) / 2,
                        size.width(), size.height());
    painter->drawImage(target, m_image);
}

} // namespace InterActive
