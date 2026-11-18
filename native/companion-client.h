#pragma once
#include "../common/config-document.h"
#include <QDBusConnection>
#include <QObject>
#include <QStringList>
#include <optional>

// Optional, one-way content notifications. No proxy, introspection, reply or
// pending call belongs on the lock/auth/readiness path.
class CompanionContentClient : public QObject {
    Q_OBJECT
public:
    explicit CompanionContentClient(QObject *parent = nullptr) : QObject(parent) {}
    explicit CompanionContentClient(QDBusConnection bus, QObject *parent = nullptr)
        : QObject(parent), bus(std::move(bus)) {}
    Q_INVOKABLE void ensureContent(const QString &bundle, const QString &profile, int height, const QStringList &topics);
    Q_INVOKABLE void reportPlayback(const QString &profile, const QString &assetKey);
private:
    std::optional<QDBusConnection> bus;
    bool ensured = false;
};
