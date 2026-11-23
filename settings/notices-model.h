#pragma once
#include <QObject>
#include <QVariantMap>
#include "common/client/service-connection.h"

namespace Aerial::Settings {
class SourcesController;
class SystemdController;
class StatusModel;
class NoticesModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QVariantMap idle READ idle NOTIFY changed)
public:
    NoticesModel(Client::ServiceConnection &service, SourcesController &sources, SystemdController &systemd, StatusModel &status, QObject *parent = nullptr);
    QVariantList entries() const { return current; }
    QVariantMap idle() const;
    void setDownloadErrors(const QString &download, const QString &automatic);
    static QString connectionDiagnostic(const Client::ServiceConnection &service);
signals:
    void changed();
private:
    void sample();
    Client::ServiceConnection &service;
    SourcesController &sources;
    SystemdController &systemd;
    StatusModel &status;
    QVariantList current;
    QString downloadError, automaticError;
};
}
