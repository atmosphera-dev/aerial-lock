// SPDX-License-Identifier: LGPL-3.0-or-later
#include "FontEnvironment_p.h"
#include <QEvent>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QThread>

namespace InterActive {

FontEnvironment *FontEnvironment::shared()
{
    // Only process-wide font/screen observation is shared, never appearance input.
    // Application ownership also makes recreation safe in embedding/test hosts.
    static QPointer<FontEnvironment> environment;
    if (!environment)
        environment = new FontEnvironment(QCoreApplication::instance());
    return environment;
}

FontEnvironment::FontEnvironment(QObject *parent) : QObject(parent)
{
    auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!application)
        return;
    Q_ASSERT(thread() == application->thread());
    application->installEventFilter(this);
    connect(application, &QGuiApplication::fontDatabaseChanged, this, &FontEnvironment::invalidate);
    connect(application, &QGuiApplication::primaryScreenChanged, this, &FontEnvironment::screenEnvironmentChanged);
    connect(application, &QGuiApplication::screenAdded, this, &FontEnvironment::observeScreens);
    connect(application, &QGuiApplication::screenRemoved, this, &FontEnvironment::observeScreens);
    observeScreens();
}

QVariantList FontEnvironment::screenEnvironment() const
{
    const auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    QVariantList values{QVariant::fromValue(QGuiApplication::primaryScreen()),
        application ? application->devicePixelRatio() : qreal(1)};
    for (auto *screen : QGuiApplication::screens()) {
        values.append(QVariant::fromValue(screen));
        values.append(screen->logicalDotsPerInchX());
        values.append(screen->logicalDotsPerInchY());
        values.append(screen->devicePixelRatio());
    }
    return values;
}

void FontEnvironment::observeScreens()
{
    for (const auto &connection : m_screenConnections)
        disconnect(connection);
    m_screenConnections.clear();
    for (auto *screen : QGuiApplication::screens()) {
        m_screenConnections.append(connect(screen, &QScreen::logicalDotsPerInchChanged,
            this, &FontEnvironment::screenEnvironmentChanged));
        m_screenConnections.append(connect(screen, &QScreen::physicalDotsPerInchChanged,
            this, &FontEnvironment::screenEnvironmentChanged));
        m_screenConnections.append(connect(screen, &QScreen::geometryChanged,
            this, &FontEnvironment::screenEnvironmentChanged));
    }
    screenEnvironmentChanged();
}

void FontEnvironment::screenEnvironmentChanged()
{
    const auto values = screenEnvironment();
    if (m_screens == values)
        return;
    const bool initialized = !m_screens.isEmpty();
    m_screens = values;
    if (initialized)
        invalidate();
}

void FontEnvironment::invalidate()
{
    Q_ASSERT(QThread::currentThread() == thread());
    ++m_generation;
    emit invalidated();
}

bool FontEnvironment::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ApplicationFontChange && watched == QCoreApplication::instance())
        invalidate();
    else if (event->type() == QEvent::DevicePixelRatioChange)
        screenEnvironmentChanged();
    return false;
}

} // namespace InterActive
