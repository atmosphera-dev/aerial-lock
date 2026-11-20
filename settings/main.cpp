#include "settings-controller.h"
#include "window-controller.h"
#include "native/shell-dir.h"
#include <QApplication>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QTextStream>
#include <QTimer>
#ifndef AERIAL_COMPANION_DATADIR
#define AERIAL_COMPANION_DATADIR "/usr/share/aerial-lock"
#endif
namespace {
void watch(QApplication &app, const QDBusPendingCall &call, std::function<void(const QDBusMessage &)> complete)
{
    auto *watcher = new QDBusPendingCallWatcher(call, &app);
    const auto handled = std::make_shared<bool>(false);
    const auto done = [watcher, handled, complete = std::move(complete)] {
        if (*handled) return;
        *handled = true;
        const auto reply = watcher->reply(); watcher->deleteLater(); complete(reply);
    };
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, &app, done);
    if (watcher->isFinished()) QTimer::singleShot(0, &app, done);
}
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Aerial");
    app.setDesktopFileName("org.aeriallock.AerialSettings");
    app.setQuitOnLastWindowClosed(false);
    const auto args = app.arguments().mid(1);
    if (args == QStringList{"--help"}) { QTextStream(stdout) << "usage: aerial-settings [--tray]\n"; return 0; }
    if (!args.isEmpty() && args != QStringList{"--tray"}) return 2;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return 1;
    const auto paths = Aerial::ConfigPaths::environment(shellDir(AERIAL_COMPANION_DATADIR));
    const auto profile = Aerial::readConfig(paths).profile;
    constexpr auto name = "org.aeriallock.AerialSettings";
    const auto claimed = bus.interface()->registerService(name, QDBusConnectionInterface::DontQueueService, QDBusConnectionInterface::DontAllowReplacement);
    if (!claimed.isValid()) return 1;
    QVariantMap platform;
    for (const auto &pair : {qMakePair("XDG_ACTIVATION_TOKEN", "activation-token"), qMakePair("DESKTOP_STARTUP_ID", "desktop-startup-id")})
        if (!qEnvironmentVariable(pair.first).isEmpty()) platform[pair.second] = qEnvironmentVariable(pair.first);
    if (claimed.value() != QDBusConnectionInterface::ServiceRegistered) {
        auto ownerCall = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner");
        ownerCall << QString(name);
        watch(app, bus.asyncCall(ownerCall, 5000), [&, bus, platform, profile, args](const QDBusMessage &message) {
            QDBusPendingReply<QString> ownerReply(message);
            if (ownerReply.isError()) { app.exit(1); return; }
            const auto owner = ownerReply.value();
            auto profileCall = QDBusMessage::createMethodCall(owner, "/org/aeriallock/AerialSettings", "org.freedesktop.DBus.Properties", "Get");
            profileCall << QString("org.freedesktop.Application") << QString("Profile");
            watch(app, bus.asyncCall(profileCall, 5000), [&, bus, platform, profile, args, owner](const QDBusMessage &message) {
                QDBusPendingReply<QDBusVariant> checked(message);
                if (checked.isError() || checked.value().variant().toString() != profile) { app.exit(1); return; }
                auto call = QDBusMessage::createMethodCall(owner, "/org/aeriallock/AerialSettings", "org.freedesktop.Application", args.isEmpty() ? "Activate" : "ActivateAction");
                call.setAutoStartService(false);
                if (!args.isEmpty()) call << QString("tray") << QVariantList{};
                call << platform;
                watch(app, bus.asyncCall(call, 5000), [&](const QDBusMessage &reply) { app.exit(reply.type() == QDBusMessage::ErrorMessage ? 1 : 0); });
            });
        });
        return app.exec();
    }
    Aerial::Settings::registerTypes();
    Aerial::Client::ServiceConnection service(bus, paths, AERIAL_COMPANION_DATADIR);
    Aerial::Settings::SettingsController settings(service);
    Aerial::Settings::WindowController window(settings);
    Aerial::Settings::ApplicationActivation activation(window, profile);
    if (!bus.registerObject("/org/aeriallock/AerialSettings", &activation, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties)) return 1;
    QObject::connect(&window, &Aerial::Settings::WindowController::closed, &app, &QApplication::quit);
    service.start();
    if (args.isEmpty()) { window.open(platform); if (!window.hasWindow()) return 1; }
    return app.exec();
}
