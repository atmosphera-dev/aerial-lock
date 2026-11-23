// Missing desktop capability for private SNI fixtures, never a product portal.
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include <QTextStream>

using Settings = QMap<QString, QVariantMap>;
Q_DECLARE_METATYPE(Settings)

class SettingsPeer : public QDBusVirtualObject {
public:
    QString introspect(const QString &) const override
    {
        return QStringLiteral("<interface name=\"org.freedesktop.portal.Settings\">"
            "<method name=\"ReadAll\"><arg type=\"as\" direction=\"in\"/>"
            "<arg type=\"a{sa{sv}}\" direction=\"out\"/></method></interface>");
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override
    {
        if (message.interface() != "org.freedesktop.portal.Settings" || message.member() != "ReadAll")
            return false;
        // No portal preference: retain actual GTK_THEME light/dark rendering.
        return bus.send(message.createReply({QVariant::fromValue(Settings{})}));
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qDBusRegisterMetaType<Settings>();
    auto bus = QDBusConnection::sessionBus();
    SettingsPeer peer;
    if (!bus.registerVirtualObject("/org/freedesktop/portal/desktop", &peer)
        || !bus.registerService("org.freedesktop.portal.Desktop")) return 1;
    QTextStream(stdout) << "SETTINGS_PEER_READY\n" << Qt::flush;
    return app.exec();
}
