// Controllable login1 peer. Only the caller-created private session bus is used.
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QDBusArgument>
#include <QDBusVariant>
#include <QDBusError>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

class Peer : public QDBusVirtualObject {
public:
    QDBusConnection bus = QDBusConnection::sessionBus();
    QJsonObject config;
    QJsonArray events;
    QList<QDBusMessage> held;
    QString introspect(const QString &) const override { return {}; }
    void signal(const QString &name, bool value) {
        auto message = QDBusMessage::createSignal("/org/freedesktop/login1", "org.freedesktop.login1.Manager", name);
        message << value;
        bus.send(message);
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override {
        const auto method = message.member();
        if (message.path() == "/fixture") {
            const auto input = QJsonDocument::fromJson(message.arguments().value(0).toByteArray()).object();
            if (method == "Configure") { config = input; events = {}; }
            else if (method == "Signal") signal(input["name"].toString(), input["value"].toBool());
            else if (method == "Release") {
                const auto pending = held;
                held.clear();
                for (const auto &old : pending) reply(old);
            } else if (method == "Drop") bus.unregisterService("org.freedesktop.login1");
            else if (method == "Claim") {
                bus = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"), "replacement");
                bus.registerVirtualObject("/org/freedesktop/login1", this, QDBusConnection::SubPath);
                bus.registerService("org.freedesktop.login1");
            } else if (method == "Quit") QCoreApplication::quit();
            else if (method != "Events") return false;
            connection.send(message.createReply(QVariantList{QString::fromUtf8(QJsonDocument(events).toJson(QJsonDocument::Compact))}));
            return true;
        }
        events.append(QJsonObject{{"method", method}, {"signature", message.signature()},
                                 {"sender", message.service()},
                                 {"interactive", message.isInteractiveAuthorizationAllowed()},
                                 {"flags", double(message.arguments().value(0).toULongLong())}});
        if (config["hold"].toString() == method) held.append(message);
        else reply(message);
        return true;
    }
    void reply(const QDBusMessage &message) {
        const auto method = message.member();
        const auto faults = config["faults"].toObject();
        if (faults.contains(method)) {
            bus.send(message.createErrorReply(faults[method].toString(), "fixture failure"));
        } else if (method.startsWith("Can")) {
            const auto value = config["caps"].toObject().value(method);
            if (value.isBool()) bus.send(message.createReply(QVariantList{value.toBool()}));
            else bus.send(message.createReply(QVariantList{value.isUndefined() ? QStringLiteral("yes") : value.toString()}));
        } else if (method == "ListInhibitors") {
            if (config["malformedInhibitors"].toBool()) { bus.send(message.createReply(QVariantList{QStringLiteral("bad")})); return; }
            sendInhibitors(message);
        } else if (method == "GetAll") {
            QVariantMap properties{{"PreparingForSleep", config["sleeping"].toBool()},
                                   {"PreparingForShutdown", config["shuttingDown"].toBool()}};
            bus.send(message.createReply(QVariantList{properties}));
        } else if (method.endsWith("WithFlags")) {
            const bool sleep = method == "SuspendWithFlags" || method == "HibernateWithFlags";
            if (config["signalBeforeReply"].toBool()) {
                signal(sleep ? "PrepareForSleep" : "PrepareForShutdown", true);
                signal(sleep ? "PrepareForSleep" : "PrepareForShutdown", false);
            }
            bus.send(message.createReply());
        } else bus.send(message.createErrorReply(QDBusError::UnknownMethod, "unknown fixture method"));
    }
    void sendInhibitors(const QDBusMessage &message);
};

struct Inhibitor { QString what, who, why, mode; uint uid = 1000, pid = 1; };
using Inhibitors = QList<Inhibitor>;
Q_DECLARE_METATYPE(Inhibitor)
Q_DECLARE_METATYPE(Inhibitors)
QDBusArgument &operator<<(QDBusArgument &argument, const Inhibitor &value) {
    argument.beginStructure();
    argument << value.what << value.who << value.why << value.mode << value.uid << value.pid;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, Inhibitor &value) {
    argument.beginStructure();
    argument >> value.what >> value.who >> value.why >> value.mode >> value.uid >> value.pid;
    argument.endStructure();
    return argument;
}
void Peer::sendInhibitors(const QDBusMessage &message) {
    Inhibitors values;
    for (const auto &entry : config["inhibitors"].toArray()) {
        const auto row = entry.toObject();
        values.append({row["what"].toString(), row.value("who").toString("fixture"), row.value("why").toString("fixture"), row["mode"].toString(),
                       uint(row.value("uid").toInt(1000)), 1});
    }
    bus.send(message.createReply(QVariantList{QVariant::fromValue(values)}));
}

#include <QDBusMetaType>
#include <QDBusError>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    qDBusRegisterMetaType<Inhibitor>();
    qDBusRegisterMetaType<Inhibitors>();
    Peer peer;
    if (!peer.bus.registerVirtualObject("/org/freedesktop/login1", &peer, QDBusConnection::SubPath)
        || !peer.bus.registerVirtualObject("/fixture", &peer)
        || !peer.bus.registerService("org.aeriallock.PowerFixture")
        || !peer.bus.registerService("org.freedesktop.login1")) return 1;
    puts("READY"); fflush(stdout);
    return app.exec();
}
