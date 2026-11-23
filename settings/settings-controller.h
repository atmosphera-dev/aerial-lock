#pragma once
#include "common/client/service-connection.h"
#include "cache-controller.h"
#include "portal-client.h"
#include "sources-controller.h"
#include "systemd-controller.h"
#include "theme-catalog.h"
#include "notices-model.h"
#include <QObject>
#include <QVariantMap>

namespace Aerial::Settings {
class SettingsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap controls READ controls NOTIFY changed)
    Q_PROPERTY(QString panel READ panel NOTIFY changed)
    Q_PROPERTY(QString diagnostic READ diagnostic NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(Aerial::Settings::StatusModel *statusModel READ statusModel CONSTANT)
    Q_PROPERTY(Aerial::Settings::NoticesModel *notices READ notices CONSTANT)
    Q_PROPERTY(QString connectionObservation READ connectionObservation NOTIFY changed)
    Q_PROPERTY(QString powerObservation READ powerObservation NOTIFY changed)
    Q_PROPERTY(Aerial::Settings::SourcesController *sources READ sources CONSTANT)
    Q_PROPERTY(Aerial::Settings::CacheController *cache READ cache CONSTANT)
    Q_PROPERTY(Aerial::Settings::ThemeCatalog *themes READ themes CONSTANT)
    Q_PROPERTY(Aerial::Settings::PortalClient *portal READ portal CONSTANT)
    Q_PROPERTY(Aerial::Settings::SystemdController *systemd READ systemd CONSTANT)
    Q_PROPERTY(QVariantMap about READ about NOTIFY changed)
    Q_PROPERTY(QVariantMap tray READ tray NOTIFY trayChanged)
public:
    explicit SettingsController(Client::ServiceConnection &service, QObject *parent = nullptr);
    QVariantMap controls() const;
    QString panel() const;
    QString diagnostic() const;
    QString status() const;
    StatusModel *statusModel() { return cacheController.statusModel(); }
    NoticesModel *notices() { return &noticesModel; }
    QString connectionObservation() const;
    QString powerObservation() const;
    SourcesController *sources() { return &sourceController; }
    CacheController *cache() { return &cacheController; }
    ThemeCatalog *themes() { return &themeCatalog; }
    PortalClient *portal() { return &portalClient; }
    SystemdController *systemd() { return &systemdController; }
    QVariantMap about() const;
    QVariantMap tray() const { return trayState; }
    QVariantMap serviceProperties() const { return service.snapshot().properties; }
    void setTrayState(const QVariantMap &state) { if (trayState != state) { trayState = state; emit trayChanged(); } }
    Q_INVOKABLE void showTray(bool on) { emit trayChangeRequested(on); }
    Q_INVOKABLE void setColor(const QString &color);
    Q_INVOKABLE QString canonicalColor(const QString &color) const;
    Q_INVOKABLE void openSettingsFolder();
    Q_INVOKABLE void openCacheFolder();
    Q_INVOKABLE void edit(const QString &key, const QVariant &value);
    Q_INVOKABLE void choose(const QString &key, const QString &token);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void dispatchAction(const QString &id, const QString &sourceId = {});
    Q_INVOKABLE void confirmIdleEnable();
signals:
    void pageRequested(const QString &page);
    void idleConfirmationRequested();
    void changed();
    void trayChanged();
    void trayChangeRequested(bool on);
private:
    struct Intent { QString key; QVariant value; bool finished = false; QString revision; };
    void sampled();
    void dispatchEdit();
    void observeIdleWrites();
    Client::ServiceConnection &service;
    PortalClient portalClient;
    SourcesController sourceController;
    CacheController cacheController;
    ThemeCatalog themeCatalog;
    SystemdController systemdController;
    NoticesModel noticesModel;
    QMap<quint64, Intent> intents;
    QMap<QString, quint64> latest;
    QMap<QString, QString> errors;
    QMap<QString, QVariant> queued;
    QStringList editOrder;
    QString editOwner;
    QVariantMap trayState{{"available", false}, {"on", false}, {"persistent", false}, {"message", "No desktop tray was found."}};
};
class StatusFormatter {
public:
    static QString summary(const Client::ServiceConnection::Snapshot &snapshot);
    static QString ready(qulonglong count);
};
void registerTypes();
}
