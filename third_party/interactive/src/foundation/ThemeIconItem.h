// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "interactive_foundation_export.h"
#include <QColor>
#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace InterActive {

class INTERACTIVE_FOUNDATION_EXPORT ThemeIconItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged FINAL)
public:
    explicit ThemeIconItem(QQuickItem *parent = nullptr);
    QString name() const { return m_name; }
    void setName(const QString &name);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    bool available() const { return m_available; }
    void paint(QPainter *painter) override;
signals:
    void nameChanged();
    void colorChanged();
    void availableChanged();
protected:
    void geometryChange(const QRectF &geometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &data) override;
private:
    void refresh();
    QString m_name;
    QColor m_color = Qt::transparent;
    bool m_available = false;
    QImage m_image;
};

} // namespace InterActive
