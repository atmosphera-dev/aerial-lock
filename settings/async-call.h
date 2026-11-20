#pragma once
#include <QDBusPendingCallWatcher>
#include <QDBusConnection>
#include <QDBusPendingReply>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <memory>

namespace Aerial::Settings {
inline void watch(QObject *context, const QDBusPendingCall &call, std::function<void(const QDBusMessage &)> complete)
{
    auto *watcher = new QDBusPendingCallWatcher(call, context);
    const auto handled = std::make_shared<bool>(false);
    const auto done = [watcher, handled, complete = std::move(complete)] {
        if (*handled) return;
        *handled = true;
        const auto reply = watcher->reply(); watcher->deleteLater(); complete(reply);
    };
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context, done);
    if (watcher->isFinished()) QTimer::singleShot(0, context, done);
}
inline void discoverOwner(QObject *context, QDBusConnection bus, QString name, std::function<bool()> current,
                          std::function<void(const QDBusMessage &)> complete)
{
    if (!current()) return;
    auto request = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner");
    request << name;
    watch(context, bus.asyncCall(request, 5000), [context, bus, name, current, complete](const QDBusMessage &message) {
        if (!current()) return;
        const QDBusPendingReply<QString> owner(message);
        const bool retry = owner.isError() && owner.error().name() != "org.freedesktop.DBus.Error.NameHasNoOwner"
            && owner.error().name() != "org.freedesktop.DBus.Error.ServiceUnknown";
        const QPointer<QObject> alive(context);
        complete(message);
        if (alive && retry && current()) QTimer::singleShot(1000, context, [context, bus, name, current, complete] {
            if (current()) discoverOwner(context, bus, name, current, complete);
        });
    });
}
}
