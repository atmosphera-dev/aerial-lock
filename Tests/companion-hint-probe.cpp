#include "../native/companion-client.h"
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QtTest>
#include <dbus/dbus.h>
#include <memory>

class HintPeer : public QDBusVirtualObject {
public:
    QList<QDBusMessage> calls;
    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &) override
    {
        calls.append(message);
        // Deliberately never reply. A stalled optional service is harmless.
        return true;
    }
};

class HintTests : public QObject {
    Q_OBJECT
private slots:
    void activationProfile()
    {
        const QString home = "/home/fixture", runtime = "/run/user/1000", bundle = "/usr/share/aerial-lock";
        const Aerial::ConfigPaths ordinary{bundle, home + "/.config/aerial-lock/config.json",
            home + "/.cache/aerial-lock/managed-v1", home + "/.local/state/aerial-lock", runtime + "/aerial-lock"};
        QVERIFY(Aerial::isDefaultProfile(ordinary, bundle, home, runtime));
        for (auto field : {&Aerial::ConfigPaths::bundle, &Aerial::ConfigPaths::config, &Aerial::ConfigPaths::cache,
                            &Aerial::ConfigPaths::state, &Aerial::ConfigPaths::runtime}) {
            auto other = ordinary; other.*field += "-dev";
            QVERIFY(!Aerial::isDefaultProfile(other, bundle, home, runtime));
        }
    }
    void oneWayMessages()
    {
        auto server = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"), "hint-peer");
        HintPeer peer;
        QVERIFY(server.registerVirtualObject("/org/aeriallock/Companion1", &peer));
        QVERIFY(server.registerService("org.aeriallock.Companion1"));
        CompanionContentClient client(QDBusConnection::sessionBus());
        QElapsedTimer timer; timer.start();
        client.ensureContent("/fixture/dev-bundle", "fixture-profile", 1440, {"mountain", "sea"});
        client.ensureContent("/fixture/dev-bundle", "fixture-profile", 2160, {});
        QVERIFY(timer.elapsed() < 100);
        QTRY_COMPARE(peer.calls.size(), 1);
        const auto ensure = peer.calls.first();
        QCOMPARE(ensure.member(), "EnsureContent");
        QVERIFY(!ensure.isReplyRequired());
        const auto hints = qdbus_cast<QVariantMap>(ensure.arguments().first());
        QCOMPARE(hints.value("profile").toString(), "fixture-profile");
        QCOMPARE(hints.value("height").metaType().id(), int(QMetaType::Int));
        QCOMPARE(hints.value("height").toInt(), 1440);
        QCOMPARE(qdbus_cast<QStringList>(hints.value("topics")), (QStringList{"mountain", "sea"}));
        client.reportPlayback("fixture-profile", QString(64, 'a'));
        QTRY_COMPARE(peer.calls.size(), 2);
        const auto report = peer.calls.last();
        QCOMPARE(report.member(), "ReportPlayback");
        QVERIFY(!report.isReplyRequired());
        QCOMPARE(report.arguments(), (QList<QVariant>{QString("fixture-profile"), QString(64, 'a')}));
        server.unregisterService("org.aeriallock.Companion1");
        server.unregisterObject("/org/aeriallock/Companion1");
        CompanionContentClient absent(QDBusConnection::sessionBus());
        timer.restart(); absent.ensureContent("/fixture", "fixture", 0, {});
        QVERIFY(timer.elapsed() < 100);
        CompanionContentClient disconnected(QDBusConnection("no-such-connection"));
        timer.restart(); disconnected.ensureContent("/fixture", "fixture", 0, {});
        disconnected.reportPlayback("fixture", QString(64, 'b'));
        QVERIFY(timer.elapsed() < 100);
    }
    void wireFlags()
    {
        // Qt's fromDBusMessage does not decode auto_start. Inspect that flag at
        // the wire boundary instead of mistaking its default getter for data.
        DBusError error; dbus_error_init(&error);
        auto server = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
        QVERIFY(server != nullptr);
        dbus_connection_set_exit_on_disconnect(server, false);
        QCOMPARE(dbus_bus_request_name(server, "org.aeriallock.Companion1", DBUS_NAME_FLAG_DO_NOT_QUEUE, &error), DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
        CompanionContentClient client(QDBusConnection::sessionBus());
        client.ensureContent("/fixture/dev-bundle", "profile", 0, {});
        client.reportPlayback("profile", QString(64, 'a'));
        QStringList observed;
        QElapsedTimer deadline; deadline.start();
        while (observed.size() < 2 && deadline.elapsed() < 2000) {
            QTest::qWait(5);
            dbus_connection_read_write(server, 0);
            while (auto raw = dbus_connection_pop_message(server)) {
                const std::unique_ptr<DBusMessage, decltype(&dbus_message_unref)> message(raw, dbus_message_unref);
                if (dbus_message_get_type(raw) != DBUS_MESSAGE_TYPE_METHOD_CALL) continue;
                QVERIFY(dbus_message_get_no_reply(raw));
                QVERIFY(!dbus_message_get_auto_start(raw));
                observed.append(QString::fromUtf8(dbus_message_get_member(raw)));
            }
        }
        dbus_connection_close(server); dbus_connection_unref(server); dbus_error_free(&error);
        QCOMPARE(observed, (QStringList{"EnsureContent", "ReportPlayback"}));
    }
};

QTEST_GUILESS_MAIN(HintTests)
#include "companion-hint-probe.moc"
