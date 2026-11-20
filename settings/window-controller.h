#pragma once
#include <QDBusContext>
#include <QObject>
#include <QVariantMap>
#include <memory>
class QQmlApplicationEngine;
namespace Aerial::Settings {
class SettingsController;
class WindowController : public QObject {
    Q_OBJECT
public:
    explicit WindowController(SettingsController &settings, QObject *parent = nullptr);
    ~WindowController() override;
    void open(const QVariantMap &platform = {}, int page = -1);
    bool hasWindow() const;
    QString panel() const;
    QString intervalText() const;
    int page() const;
signals:
    void closed();
private:
    SettingsController &settings;
    std::unique_ptr<QQmlApplicationEngine> engine;
    bool closing = false;
    quint64 generation = 0;
};
class ApplicationActivation : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")
    Q_PROPERTY(QString Profile READ profile CONSTANT)
    Q_PROPERTY(QString Panel READ panel)
    Q_PROPERTY(QString IntervalText READ intervalText)
    Q_PROPERTY(int Page READ page)
public:
    explicit ApplicationActivation(WindowController &window, QString profile, QObject *parent = nullptr) : QObject(parent), window(window), selectedProfile(std::move(profile)) {}
    QString profile() const { return selectedProfile; }
    QString panel() const { return window.panel(); }
    QString intervalText() const { return window.intervalText(); }
    int page() const { return window.page(); }
public slots:
    void Activate(const QVariantMap &platform_data);
    void Open(const QStringList &uris, const QVariantMap &platform_data);
    void ActivateAction(const QString &action_name, const QVariantList &parameter, const QVariantMap &platform_data);
signals:
    void trayRequested();
private:
    WindowController &window;
    QString selectedProfile;
};
}
