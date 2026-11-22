// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ScreenGeometry.h"

namespace InterActive {

void ScreenGeometry::setWindow(QQuickWindow *window)
{
    if (m_window == window)
        return;
    disconnect(m_windowScreenConnection);
    disconnect(m_windowDestroyedConnection);
    m_window = window;
    if (window) {
        m_windowScreenConnection = connect(window, &QWindow::screenChanged,
            this, &ScreenGeometry::observeScreen);
        m_windowDestroyedConnection = connect(window, &QObject::destroyed, this, [this] {
            m_window = nullptr;
            observeScreen(nullptr);
            emit windowChanged();
        });
    }
    observeScreen(window ? window->screen() : nullptr);
    emit windowChanged();
}

void ScreenGeometry::observeScreen(QScreen *screen)
{
    disconnect(m_geometryConnection);
    disconnect(m_screenDestroyedConnection);
    m_screen = screen;
    if (screen) {
        m_geometryConnection = connect(screen, &QScreen::availableGeometryChanged,
            this, &ScreenGeometry::updateGeometry);
        m_screenDestroyedConnection = connect(screen, &QObject::destroyed, this, [this] {
            m_screen = nullptr;
            updateGeometry();
        });
    }
    updateGeometry();
}

void ScreenGeometry::updateGeometry()
{
    const QRect geometry = m_screen ? m_screen->availableGeometry() : QRect();
    if (m_availableGeometry == geometry)
        return;
    m_availableGeometry = geometry;
    emit availableGeometryChanged();
}

} // namespace InterActive
