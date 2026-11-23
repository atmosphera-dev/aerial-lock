#pragma once
#include "common/client/service-connection.h"
#include <optional>

namespace Aerial::Settings {
// One coherent projection for the window, cache facts and tray. No job transport.
class StatusModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap facts READ facts NOTIFY changed)
public:
    explicit StatusModel(Client::ServiceConnection &service, QObject *parent = nullptr);
    QVariantMap facts() const { return current; }
    void setAutomaticIntent(bool on, bool pending);
    static QVariantMap project(const Client::ServiceConnection::Snapshot &snapshot, bool available = true);
    static QVariantMap transfer(const Client::ServiceConnection::Snapshot &snapshot);
    static bool needsAttention(const QVariantMap &properties);
    static QString badge(const QVariantMap &properties, bool downloading, bool paused);
signals:
    void changed();
private:
    void sample();
    Client::ServiceConnection &service;
    QVariantMap current;
    std::optional<bool> automaticIntent;
};
}
