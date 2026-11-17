#pragma once
#include "../common/config-document.h"
#include <QObject>
#include <QVariantMap>

// Read-only one-shot local facade. Creating or reading it never writes settings
// or contacts another process. ConfigStore owns presentation/i18n above it.
class LocalConfig : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap data READ data NOTIFY changed)
    Q_PROPERTY(QVariantMap rawUser READ rawUser NOTIFY changed)
    Q_PROPERTY(QStringList diagnostics READ diagnostics NOTIFY changed)
    Q_PROPERTY(QString profile READ profile NOTIFY changed)
    Q_PROPERTY(QString revision READ revision NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
public:
    explicit LocalConfig(QObject *parent = nullptr) : QObject(parent) {}
    QVariantMap data() const { return document.effective.toVariantMap(); }
    QVariantMap rawUser() const { return document.user.toVariantMap(); }
    QStringList diagnostics() const { return document.diagnostics; }
    QString profile() const { return document.profile; }
    QString revision() const { return document.revision; }
    bool ready() const { return document.bundleValid; }
    Q_INVOKABLE bool load(const QString &bundle, const QString &userPath);
signals:
    void changed();
private:
    Aerial::ConfigDocument document;
};
