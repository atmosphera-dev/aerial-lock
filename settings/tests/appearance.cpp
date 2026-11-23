#include "appearance-bridge.h"
#include "interactive-runtime.h"
#include "common/client/client-support.h"
#include <Appearance.h>
#include <QApplication>
#include <QDBusArgument>
#include <QDBusVirtualObject>
#include <QPalette>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTextStream>
#include <QtTest>

using namespace Aerial::Settings;
constexpr auto Portal = "org.freedesktop.portal.Desktop";
constexpr auto Path = "/org/freedesktop/portal/desktop";
constexpr auto Interface = "org.freedesktop.portal.Settings";
constexpr auto Group = "org.freedesktop.appearance";
class AppearancePeer : public QDBusVirtualObject {
public:
    QDBusConnection bus = QDBusConnection::sessionBus();
    QVariant scheme = uint(1);
    QVariantList rgb{.8, .2, .4};
    bool hold = false;
    QList<std::function<void()>> waiting;
    QString introspect(const QString &) const override { return {}; }
    QVariant value(const QString &key) const {
        if (key == "color-scheme") return scheme;
        QDBusArgument arg; arg.beginStructure(); for (const auto &channel : rgb) arg << channel.toDouble(); arg.endStructure();
        return QVariant::fromValue(arg);
    }
    void signal(const QString &key) {
        auto message = QDBusMessage::createSignal(Path, Interface, "SettingChanged");
        message << QString(Group) << key << QVariant::fromValue(QDBusVariant(value(key))); bus.send(message);
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &) override {
        const auto method = message.member();
        if (message.interface() == Interface && method == "Read") {
            const auto result = value(message.arguments().at(1).toString());
            const auto reply = [this, message, result] { bus.send(message.createReply({QVariant::fromValue(QDBusVariant(result))})); };
            if (hold) waiting << reply; else reply();
            return true;
        }
        if (message.interface() != "org.aeriallock.AppearanceTest") return false;
        const auto args = message.arguments();
        if (method == "Set") {
            const auto config = Aerial::Client::unpack(args.first()).toMap();
            if (config.contains("scheme")) scheme = config.value("scheme");
            if (config.contains("rgb")) rgb = config.value("rgb").toList();
            if (config.contains("hold")) hold = config.value("hold").toBool();
            if (config.value("signal").toBool()) { signal("color-scheme"); signal("accent-color"); }
        } else if (method == "Release") { const auto pending = waiting; waiting.clear(); for (const auto &reply : pending) reply(); }
        else if (method == "Drop") bus.unregisterService(Portal);
        else if (method == "Acquire") bus.registerService(Portal);
        else if (method == "Pending") { bus.send(message.createReply(QVariantList{int(waiting.size())})); return true; }
        bus.send(message.createReply()); return true;
    }
};
struct PeerProcess {
    QProcess process;
    QString name;
    bool start(bool acquire = true) {
        process.start(QCoreApplication::applicationFilePath(), {"--peer", acquire ? "acquire" : "wait"});
        if (!process.waitForStarted() || !process.waitForReadyRead()) return false;
        name = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        return name.startsWith("org.aeriallock.AppearancePeer");
    }
    QDBusMessage call(const QString &method, const QVariantMap &config = {}) {
        auto message = QDBusMessage::createMethodCall(name, "/control", "org.aeriallock.AppearanceTest", method);
        if (method == "Set") message << config;
        return QDBusConnection::sessionBus().call(message);
    }
    ~PeerProcess() { process.terminate(); process.waitForFinished(5000); }
};
class AppearanceTest : public QObject {
    Q_OBJECT
    QtMessageHandler previousHandler = nullptr;
private slots:
    void initTestCase() { previousHandler = qInstallMessageHandler(nullptr); }
    void cleanupTestCase() { qInstallMessageHandler(previousHandler); }
    void liveInputsAndRenderedControls()
    {
        PeerProcess peer; QVERIFY(peer.start());
        QQmlEngine engine; configureInterActive(engine); AppearanceBridge bridge(engine);
        auto *appearance = engine.singletonInstance<InterActive::Appearance *>("InterActive.Foundation", "Appearance");
        QTRY_COMPARE(appearance->scheme(), QString("dark"));
        QTRY_COMPARE(appearance->colors()->accent(), QColor::fromRgbF(.8, .2, .4));
        // The desktop accent is authoritative; IA may retain it with an honest
        // contrast diagnostic. No invalid application input is acceptable.
        for (const auto &entry : appearance->diagnostics())
            QCOMPARE(entry.toMap().value("code").toString(), QString("low-contrast"));
        QCOMPARE(appearance->density(), QString("compact"));
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import InterActive
import InterActive.Foundation
Window {
    width: 400; height: 180; visible: true
    color: Appearance.colors.content
    property color resolvedContent: Appearance.colors.content
    IAButton { objectName: "action"; x: 24; y: 24; text: "Keep value"; checkable: true; checked: true }
    IAComboBox { objectName: "choice"; x: 24; y: 60; width: 300; model: 20; currentIndex: 15 }
    IAProgressBar { objectName: "progress"; x: 24; y: 100; width: 300; value: 0.5 }
}
)", QUrl("inline:appearance-proof.qml"));
        QTRY_VERIFY(component.status() != QQmlComponent::Loading);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create()); QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.data()); QVERIFY(window); QVERIFY(QTest::qWaitForWindowExposed(window));
        const auto frame = window->grabWindow(); QVERIFY(!frame.isNull());
        QCOMPARE(frame.pixelColor(390 * frame.width() / window->width(), 170 * frame.height() / window->height()), appearance->colors()->content());
        auto *progress = root->findChild<QQuickItem *>("progress"); QVERIFY(progress);
        const auto sample = progress->mapToScene(QPointF(progress->width() / 4, progress->height() / 2));
        QCOMPARE(frame.pixelColor(qRound(sample.x() * frame.width() / window->width()), qRound(sample.y() * frame.height() / window->height())).rgba(), appearance->colors()->accent().rgba());
        auto *action = root->findChild<QQuickItem *>("action"); QVERIFY(action); action->forceActiveFocus();
        const auto originalPalette = QApplication::palette();
        peer.call("Set", {{"scheme", uint(2)}, {"rgb", QVariantList{.1, .7, .3}}, {"signal", true}});
        QTRY_COMPARE(appearance->scheme(), QString("light"));
        QTRY_COMPARE(appearance->colors()->accent(), QColor::fromRgbF(.1, .7, .3));
        QTRY_COMPARE(root->property("resolvedContent").value<QColor>(), appearance->colors()->content());
        QTest::qWait(50); const auto light = window->grabWindow(); QVERIFY(!light.isNull());
        QCOMPARE(light.pixelColor(390 * light.width() / window->width(), 170 * light.height() / window->height()), appearance->colors()->content());
        QCOMPARE(light.pixelColor(qRound(sample.x() * light.width() / window->width()), qRound(sample.y() * light.height() / window->height())).rgba(), appearance->colors()->accent().rgba());
        QVERIFY(light != frame); QVERIFY(action->hasActiveFocus()); QVERIFY(action->property("checked").toBool());
        QCOMPARE(action->property("text").toString(), QString("Keep value")); QCOMPARE(QApplication::palette(), originalPalette);
        auto *choice = root->findChild<QQuickItem *>("choice"); QVERIFY(choice);
        auto *popup = choice->property("popup").value<QObject *>(); QVERIFY(popup);
        QVERIFY(QMetaObject::invokeMethod(popup, "open")); QTRY_VERIFY(popup->property("opened").toBool());
        auto *list = popup->property("contentItem").value<QObject *>(); QVERIFY(list);
        const auto scroll = list->property("contentY");
        QPointer<QQuickItem> focused = window->activeFocusItem(); QVERIFY(focused);
        // A palette change updates only missing portal keys, never valid ones.
        QPalette fallback = originalPalette; fallback.setColor(QPalette::Window, Qt::black); fallback.setColor(QPalette::Highlight, Qt::cyan);
        QApplication::setPalette(fallback);
        QCOMPARE(appearance->scheme(), QString("light")); QCOMPARE(appearance->colors()->accent(), QColor::fromRgbF(.1, .7, .3));
        peer.call("Set", {{"scheme", uint(0)}, {"signal", true}});
        QTRY_COMPARE(appearance->scheme(), QString("dark")); QCOMPARE(appearance->colors()->accent(), QColor::fromRgbF(.1, .7, .3));
        peer.call("Set", {{"scheme", uint(2)}, {"rgb", QVariantList{2., .2, .3}}, {"signal", true}});
        QTRY_COMPARE(appearance->scheme(), QString("light")); QTRY_COMPARE(appearance->colors()->accent(), QColor(Qt::cyan));
        peer.call("Set", {{"scheme", "1"}, {"signal", true}});
        QTRY_COMPARE(appearance->scheme(), QString("dark"));
        QVERIFY(popup->property("opened").toBool()); QCOMPARE(choice->property("currentIndex").toInt(), 15);
        QCOMPARE(list->property("contentY"), scroll); QCOMPARE(window->activeFocusItem(), focused.data());
        QVERIFY(QMetaObject::invokeMethod(popup, "close")); QTRY_VERIFY(!popup->property("visible").toBool());
        QApplication::setPalette(originalPalette);
        window->close();
    }
    void signalAndOwnerFenceLateReads()
    {
        PeerProcess first; QVERIFY(first.start()); first.call("Set", {{"hold", true}});
        QQmlEngine engine; configureInterActive(engine); AppearanceBridge bridge(engine);
        auto *appearance = engine.singletonInstance<InterActive::Appearance *>("InterActive.Foundation", "Appearance");
        QTRY_COMPARE(first.call("Pending").arguments().first().toInt(), 2);
        first.call("Set", {{"scheme", uint(2)}, {"rgb", QVariantList{.2, .3, .4}}, {"signal", true}});
        QTRY_COMPARE(appearance->scheme(), QString("light"));
        QTRY_COMPARE(appearance->colors()->accent(), QColor::fromRgbF(.2, .3, .4));
        first.call("Release"); QTest::qWait(50);
        QCOMPARE(appearance->scheme(), QString("light")); QCOMPARE(appearance->colors()->accent(), QColor::fromRgbF(.2, .3, .4));
        // Start another engine with held reads, then replace the portal owner.
        QQmlEngine secondEngine; configureInterActive(secondEngine); AppearanceBridge second(secondEngine);
        auto *other = secondEngine.singletonInstance<InterActive::Appearance *>("InterActive.Foundation", "Appearance");
        QTRY_COMPARE(first.call("Pending").arguments().first().toInt(), 2);
        PeerProcess replacement; QVERIFY(replacement.start(false));
        replacement.call("Set", {{"scheme", uint(1)}, {"rgb", QVariantList{.6, .5, .1}}});
        first.call("Drop"); replacement.call("Acquire");
        QTRY_COMPARE(other->scheme(), QString("dark")); QTRY_COMPARE(other->colors()->accent(), QColor::fromRgbF(.6, .5, .1));
        first.call("Release"); first.call("Set", {{"scheme", uint(2)}, {"signal", true}}); QTest::qWait(50);
        QCOMPARE(other->scheme(), QString("dark")); QCOMPARE(other->colors()->accent(), QColor::fromRgbF(.6, .5, .1));
        QCOMPARE(appearance->colors()->accent(), other->colors()->accent());
    }
};
int main(int argc, char **argv)
{
    if (argc > 1 && QByteArray(argv[1]) == "--peer") {
        QCoreApplication app(argc, argv); AppearancePeer peer;
        const auto name = "org.aeriallock.AppearancePeer.p" + QString::number(QCoreApplication::applicationPid());
        if (!peer.bus.registerService(name) || !peer.bus.registerVirtualObject("/control", &peer)
            || !peer.bus.registerVirtualObject(Path, &peer) || (QByteArray(argv[2]) == "acquire" && !peer.bus.registerService(Portal))) return 1;
        QTextStream(stdout) << name << '\n' << Qt::flush; return app.exec();
    }
    QApplication app(argc, argv); AppearanceTest test; return QTest::qExec(&test, argc, argv);
}
#include "appearance.moc"
