#include "fixture.h"
#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
using namespace Aerial::Settings;
class Setup : public QObject {
    Q_OBJECT
public:
    Aerial::Settings::Test::Fixture fixture;
public slots:
    void applicationAvailable()
    {
        registerTypes();
        if (!fixture.start()) qFatal("settings QML service fixture failed");
        qmlRegisterSingletonType<SettingsController>("AerialLock.SettingsTest", 1, 0, "Harness", [](QQmlEngine *engine, QJSEngine *) -> QObject * {
            return engine->property("aerialHarness").value<QObject *>();
        });
    }
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        auto *harness = new SettingsController(*fixture.service);
        harness->setParent(engine);
        QQmlEngine::setObjectOwnership(harness, QQmlEngine::CppOwnership);
        engine->setProperty("aerialHarness", QVariant::fromValue<QObject *>(harness));
    }
};
QUICK_TEST_MAIN_WITH_SETUP(settings, Setup)
#include "quick.moc"
