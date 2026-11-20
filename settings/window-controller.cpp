#include "window-controller.h"
#include "settings-controller.h"
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
namespace Aerial::Settings {
WindowController::WindowController(SettingsController &settings, QObject *parent) : QObject(parent), settings(settings) {}
WindowController::~WindowController() = default;
bool WindowController::hasWindow() const { return engine && !engine->rootObjects().isEmpty(); }
QString WindowController::panel() const { return settings.panel(); }
QString WindowController::intervalText() const { return hasWindow() ? engine->rootObjects().first()->property("intervalText").toString() : QString(); }
void WindowController::open(const QVariantMap &platform)
{
    if (!engine) {
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->setInitialProperties({{"controller", QVariant::fromValue(&settings)}});
        engine->load(QUrl("qrc:/settings/qml/Main.qml"));
        if (engine->rootObjects().isEmpty()) { engine.reset(); emit closed(); return; }
        auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        connect(window, &QQuickWindow::closing, this, [this] {
            QTimer::singleShot(0, this, [this] { engine.reset(); emit closed(); });
        });
    }
    auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
    const auto token = platform.value("activation-token").toString();
    if (!token.isEmpty()) qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
    window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
    window->setVisible(true); window->requestActivate();
}
void ApplicationActivation::Activate(const QVariantMap &platform)
{
    window.open(platform);
    if (!window.hasWindow() && calledFromDBus()) sendErrorReply("org.freedesktop.DBus.Error.Failed", "Could not open the settings window");
}
void ApplicationActivation::Open(const QStringList &uris, const QVariantMap &platform)
{
    if (!uris.isEmpty()) { if (calledFromDBus()) sendErrorReply("org.freedesktop.DBus.Error.InvalidArgs", "Aerial does not open documents"); return; }
    Activate(platform);
}
void ApplicationActivation::ActivateAction(const QString &action, const QVariantList &parameters, const QVariantMap &)
{
    if (action != "tray" || !parameters.isEmpty()) {
        if (calledFromDBus()) sendErrorReply("org.freedesktop.DBus.Error.InvalidArgs", "Unknown action or parameters");
        return;
    }
    emit trayRequested();
}
}
