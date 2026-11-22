// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "interactive_foundation_export.h"
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QRect>
#include <QScreen>
#include <QtQml/qqmlregistration.h>

namespace InterActive {

// Public Qt data only: no parent lookup, layout policy or appearance state.
class INTERACTIVE_FOUNDATION_EXPORT ScreenGeometry : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged FINAL)
    Q_PROPERTY(QRect availableGeometry READ availableGeometry NOTIFY availableGeometryChanged FINAL)
public:
    explicit ScreenGeometry(QObject *parent = nullptr) : QObject(parent) {}
    QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);
    QRect availableGeometry() const { return m_availableGeometry; }
signals:
    void windowChanged();
    void availableGeometryChanged();
private:
    void observeScreen(QScreen *screen);
    void updateGeometry();
    QPointer<QQuickWindow> m_window;
    QPointer<QScreen> m_screen;
    QRect m_availableGeometry;
    QMetaObject::Connection m_windowScreenConnection;
    QMetaObject::Connection m_windowDestroyedConnection;
    QMetaObject::Connection m_geometryConnection;
    QMetaObject::Connection m_screenDestroyedConnection;
};

} // namespace InterActive
