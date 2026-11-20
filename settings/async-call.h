#pragma once
#include <QDBusPendingCallWatcher>
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
}
