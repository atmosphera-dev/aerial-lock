#pragma once
#include "common/client/service-connection.h"
#include <QFileSystemWatcher>

namespace Aerial::Settings {
class ThemeCatalog : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
public:
    explicit ThemeCatalog(Client::ServiceConnection &service, QObject *parent = nullptr);
    QVariantList rows() const { return themes; }
signals:
    void changed();
private:
    void sample();
    Client::ServiceConnection &service;
    QFileSystemWatcher watcher;
    QVariantList themes;
    QString bundle, user;
};
}
