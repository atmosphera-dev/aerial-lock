#pragma once
#include "config-repository.h"
#include <QDBusConnection>
#include <QDBusContext>

namespace Aerial {
// The generated adaptor owns wire dispatch. Business rules remain in the
// repository/managers, shared by automatic actions and API requests.
class CompanionService : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_PROPERTY(uint ApiVersion READ apiVersion CONSTANT)
    Q_PROPERTY(QString Profile READ profile CONSTANT)
    Q_PROPERTY(QString ConfigRevision READ revision)
public:
    CompanionService(ConfigRepository &config, QDBusConnection bus, QObject *parent = nullptr);
    bool start();
    uint apiVersion() const { return 1; }
    QString profile() const { return config.document().profile; }
    QString revision() const { return config.document().revision; }
public slots:
    QString GetSettings(QString &effectiveJson, QString &revision);
    QString UpdateSettings(const QString &expectedRevision, const QString &mergePatchJson);
    void SetPaused(bool paused);
signals:
    void SettingsChanged(const QString &revision, const QStringList &paths);
private:
    void error(const ConfigTransaction &result);
    ConfigRepository &config;
    QDBusConnection bus;
};
}
