#include "fixture.h"
#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QSaveFile>
using namespace Aerial::Settings;
class QuickHarness : public SettingsController {
    Q_OBJECT
public:
    Aerial::Settings::Test::Fixture &fixture;
    QByteArray validConfig;
    QuickHarness(Aerial::Settings::Test::Fixture &fixture) : SettingsController(*fixture.service), fixture(fixture) {}
    Q_INVOKABLE QVariant saved(const QString &key)
    {
        QJsonValue value(QJsonDocument::fromJson(fixture.cli({"config", "show", "--json"})).object());
        for (const auto &part : key.split('.')) value = value.toObject().value(part);
        return value.toVariant();
    }
    Q_INVOKABLE void externalEdit(const QString &key, const QString &json) { fixture.cli({"config", "set", key, json}); }
    Q_INVOKABLE void configureTray(bool available, bool on, bool persistent = false) { setTrayState({{"available", available}, {"on", on}, {"persistent", persistent}, {"message", "Desktop tray fixture"}}); }
    Q_INVOKABLE void stopService() { fixture.daemon.terminate(); if (!fixture.daemon.waitForFinished(5000)) qFatal("fixture did not stop"); }
    Q_INVOKABLE void startService() { fixture.daemon.start(QString(AERIAL_SETTINGS_TEST_SOURCE) + "/companion/aerial", {}); if (!fixture.daemon.waitForStarted()) qFatal("fixture did not start"); }
    Q_INVOKABLE void breakConfig()
    {
        QFile file(fixture.paths.config); if (!file.open(QIODevice::ReadOnly)) qFatal("fixture config read failed"); validConfig = file.readAll(); file.close();
        QSaveFile broken(fixture.paths.config); if (!broken.open(QIODevice::WriteOnly)) qFatal("fixture config open failed"); broken.write("{\n\"broken\":\n}"); if (!broken.commit()) qFatal("fixture publish failed");
    }
    Q_INVOKABLE bool brokenBytesUnchanged()
    {
        QFile file(fixture.paths.config); return file.open(QIODevice::ReadOnly) && file.readAll() == "{\n\"broken\":\n}";
    }
    Q_INVOKABLE void repairConfig()
    {
        QSaveFile repaired(fixture.paths.config); if (!repaired.open(QIODevice::WriteOnly)) qFatal("fixture config open failed"); repaired.write(validConfig); if (!repaired.commit()) qFatal("fixture publish failed");
    }
};
class Setup : public QObject {
    Q_OBJECT
public:
    Aerial::Settings::Test::Fixture fixture;
public slots:
    void applicationAvailable()
    {
        registerTypes();
        if (!fixture.start()) qFatal("settings QML service fixture failed");
        qmlRegisterSingletonType<QuickHarness>("AerialLock.SettingsTest", 1, 0, "Harness", [](QQmlEngine *engine, QJSEngine *) -> QObject * {
            return engine->property("aerialHarness").value<QObject *>();
        });
    }
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        auto *harness = new QuickHarness(fixture);
        harness->setParent(engine);
        QQmlEngine::setObjectOwnership(harness, QQmlEngine::CppOwnership);
        engine->setProperty("aerialHarness", QVariant::fromValue<QObject *>(harness));
    }
};
QUICK_TEST_MAIN_WITH_SETUP(settings, Setup)
#include "quick.moc"
