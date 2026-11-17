// Test-only QML plugin: the real service's C++ constructor selects a private
// session bus. No product QML/config/environment switch can select a transport.
#include "../native/power-actions.h"
#include <QQmlExtensionPlugin>
#include <qqml.h>

class FixtureActions : public PowerActions {
    Q_OBJECT
public:
    explicit FixtureActions(QObject *parent = nullptr)
        : PowerActions(QDBusConnection::sessionBus(), parent) {}
    Q_INVOKABLE QString control(const QString &method, const QString &input) {
        auto message = QDBusMessage::createMethodCall("org.aeriallock.PowerFixture", "/fixture",
            "org.aeriallock.PowerFixture", method);
        message << input;
        const auto reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 3000);
        if (reply.type() != QDBusMessage::ReplyMessage) qFatal("private power fixture control failed");
        return reply.arguments().value(0).toString();
    }
};

class PowerFixturePlugin : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QQmlExtensionInterface/1.0")
public:
    void registerTypes(const char *uri) override { qmlRegisterType<FixtureActions>(uri, 1, 0, "FixtureActions"); }
};
#include "power-actions-controller.moc"
