#include "fixture.h"
#include "async-call.h"
#include "tray-controller.h"
#include "window-controller.h"
#include "common/client/client-support.h"
#include <QApplication>
#include <QDBusConnectionInterface>
#include <QDBusVirtualObject>
#include <QDBusPendingCallWatcher>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTextStream>
#include <QSaveFile>
#include <QTextDocument>
#include <optional>
#include <utility>
using namespace Aerial;
using namespace Aerial::Settings;
using namespace Aerial::Settings::Test;
// An independent watcher peer: it uses only standard interface names and raw
// messages, not the production transport or menu types.
class WatcherPeer : public QDBusVirtualObject {
    Q_OBJECT
public:
    QDBusConnection bus = QDBusConnection::sessionBus();
    QStringList items; int registrations = 0, notifications = 0, hostDelay = 0, propertyReplies = 0; bool host = true, reject = false, holdReply = false;
    QList<QDBusMessage> heldReplies;
    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &m, const QDBusConnection &) override
    {
        const auto a = m.arguments(); const auto method = m.member(); auto reply = [&](const QVariantList &values = {}) { bus.send(m.createReply(values)); };
        if (m.interface() == "org.freedesktop.Notifications") { ++notifications; reply({uint(1)}); return true; }
        if (m.interface() == "org.freedesktop.DBus.Properties") {
            if (m.path() != "/StatusNotifierWatcher" || method != "Get" || a.size() != 2
                || a[0].toString() != "org.kde.StatusNotifierWatcher" || a[1].toString() != "IsStatusNotifierHostRegistered") {
                bus.send(m.createErrorReply(QDBusError::InvalidArgs, "Unknown host-status request")); return true;
            }
            if (holdReply) heldReplies << m;
            else QTimer::singleShot(hostDelay, this, [this, m] { bus.send(m.createReply({QVariant::fromValue(QDBusVariant(host))})); ++propertyReplies; }); return true;
        }
        if (m.interface() == "org.kde.StatusNotifierWatcher" && method == "RegisterStatusNotifierItem") {
            items << (a[0].toString().startsWith('/') ? m.service() + a[0].toString() : a[0].toString()); ++registrations;
            if (reject) bus.send(m.createErrorReply(QDBusError::Failed, "registration rejected")); else reply(); return true;
        }
        if (m.interface() == "org.aeriallock.TrayPeer") {
            if (method == "Info") reply({items, registrations, notifications, propertyReplies});
            else if (method == "Delay") { hostDelay = a[0].toInt(); reply(); }
            else if (method == "Hold") { holdReply = true; reply(); }
            else if (method == "Release") {
                holdReply = false; const auto held = std::exchange(heldReplies, {});
                for (const auto &request : held) { bus.send(request.createReply({QVariant::fromValue(QDBusVariant(host))})); ++propertyReplies; }
                reply();
            }
            else if (method == "Reject") { reject = a[0].toBool(); reply(); }
            else if (method == "Host") {
                host = a[0].toBool(); auto s = QDBusMessage::createSignal("/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher", host ? "StatusNotifierHostRegistered" : "StatusNotifierHostUnregistered"); bus.send(s); reply();
            } else if (method == "Replace") { bus.unregisterService("org.kde.StatusNotifierWatcher"); bus.registerService("org.kde.StatusNotifierWatcher"); reply(); }
            else if (method == "Quit") { reply(); QTimer::singleShot(0, qApp, &QCoreApplication::quit); }
            return true;
        }
        return false;
    }
};
class PeerProcess {
public:
    QProcess process;
    PeerProcess() { process.start(QCoreApplication::applicationFilePath(), {"--peer"}); }
    bool ready() { return QTest::qWaitFor([&] { return process.readAllStandardOutput().contains("READY"); }, 5000); }
    QDBusMessage call(const QString &method, const QVariantList &arguments = {})
    {
        auto m = QDBusMessage::createMethodCall("org.aeriallock.TrayPeer", "/TrayPeer", "org.aeriallock.TrayPeer", method); m.setArguments(arguments); return QDBusConnection::sessionBus().call(m);
    }
    int registrations() { return call("Info").arguments()[1].toInt(); }
    int replies() { return call("Info").arguments()[3].toInt(); }
    QString item() { return qdbus_cast<QStringList>(call("Info").arguments()[0]).last().section('/', 0, 0); }
    ~PeerProcess() { if (process.state() != QProcess::NotRunning) { call("Quit"); process.waitForFinished(5000); } }
};
class TrayTest : public QObject {
    Q_OBJECT
    QDBusMessage request(const QString &owner, const QString &path, const QString &interface, const QString &method, const QVariantList &arguments = {}, int *callbackCount = nullptr, std::optional<QDBusPendingCall> *observed = nullptr)
    {
        auto m = QDBusMessage::createMethodCall(owner, path, interface, method); m.setArguments(arguments);
        struct Result { QDBusMessage message; bool done = false; };
        auto state = std::make_shared<Result>(); QObject requestLifetime;
        const auto call = QDBusConnection::sessionBus().asyncCall(m);
        if (observed) observed->emplace(call);
        Aerial::Settings::watch(&requestLifetime, call, [state, callbackCount](const QDBusMessage &reply) {
            if (callbackCount) ++*callbackCount;
            state->message = reply; state->done = true;
        });
        if (!QTest::qWaitFor([&] { return state->done; }, 5000)) return {};
        return state->message;
    }
private slots:
    void requestTimeoutCancelsItsCallback()
    {
        PeerProcess peer; QVERIFY(peer.ready()); peer.call("Hold");
        int callbacks = 0; std::optional<QDBusPendingCall> pending;
        const auto reply = request("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", "org.freedesktop.DBus.Properties", "Get", {"org.kde.StatusNotifierWatcher", "IsStatusNotifierHostRegistered"}, &callbacks, &pending);
        QCOMPARE(reply.type(), QDBusMessage::InvalidMessage);
        QVERIFY(pending.has_value()); QDBusPendingCallWatcher completion(*pending); QVERIFY(!completion.isFinished());
        peer.call("Release"); QTRY_VERIFY(completion.isFinished()); QCOMPARE(completion.reply().type(), QDBusMessage::ReplyMessage); QCOMPARE(callbacks, 0);
        QCOMPARE(request("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", "org.freedesktop.DBus.Properties", "Get", {"org.kde.StatusNotifierWatcher", "IsStatusNotifierHostRegistered"}, &callbacks).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(callbacks, 1);
    }
    void contextMenuIsNotPrimaryActivation()
    {
        PeerProcess peer; QVERIFY(peer.ready()); QMenu menu; menu.addAction("Fetch now");
        TrayItem item(menu, "org.kde.StatusNotifierWatcher"); QSignalSpy opened(&item, &TrayItem::openRequested);
        item.ContextMenu(50, 50); QCOMPARE(opened.size(), 0);
        const auto platform = QGuiApplication::platformName();
        if (platform.startsWith("wayland") || platform == "offscreen" || platform == "minimal") QVERIFY(!menu.isVisible());
        else {
            QVERIFY(menu.isVisible()); QVERIFY(QTest::qWaitForWindowExposed(menu.windowHandle()));
            auto *action = menu.actions().first(); QSignalSpy triggered(action, &QAction::triggered);
            QTest::mouseClick(&menu, Qt::LeftButton, {}, menu.actionGeometry(action).center());
            QCOMPARE(triggered.size(), 1);
        }
        menu.close();
    }
    void slowHostIsNotSupersededByTimer()
    {
        PeerProcess peer; QVERIFY(peer.ready()); peer.call("Delay", {2500});
        Fixture f; QVERIFY(f.start());
        TrayController tray(*f.controller, QDBusConnection::sessionBus(), nullptr, [] { return true; });
        tray.show(); QTRY_COMPARE_WITH_TIMEOUT(peer.registrations(), 1, 6000);
        QVERIFY(f.controller->tray().value("visible").toBool());
    }
    void registrationFailureCanRecover()
    {
        PeerProcess peer; QVERIFY(peer.ready()); peer.call("Reject", {true}); Fixture f; QVERIFY(f.start());
        TrayController tray(*f.controller, QDBusConnection::sessionBus(), nullptr, [] { return true; }); tray.show();
        QTRY_VERIFY(f.controller->tray().value("message").toString().contains("Couldn't show"));
        QVERIFY(!f.controller->tray().value("visible").toBool());
        peer.call("Reject", {false}); peer.call("Host", {true});
        QTRY_VERIFY(f.controller->tray().value("visible").toBool());
        QTRY_VERIFY(!f.controller->tray().value("message").toString().contains("Couldn't show"));
    }
    void activatingExistingWindowPreservesItsState()
    {
        Fixture f; QVERIFY(f.start()); WindowController window(*f.controller); window.open();
        QQuickWindow *quick = nullptr;
        for (auto *candidate : QGuiApplication::topLevelWindows()) if ((quick = qobject_cast<QQuickWindow *>(candidate))) break;
        QVERIFY(quick);
        for (const auto state : {Qt::WindowMaximized, Qt::WindowFullScreen}) {
            quick->setWindowStates(state); window.open(); QCOMPARE(quick->windowStates(), Qt::WindowStates(state));
        }
        quick->setWindowStates(Qt::WindowMinimized); window.open(); QVERIFY(!(quick->windowStates() & Qt::WindowMinimized));
        quick->close(); QTRY_VERIFY(!window.hasWindow());
    }
    void statusClassification()
    {
        for (const auto &reason : {"Offline", "Metered", "CacheFull", "TransferFailed", "NoRemoteSources", "PlaybackProtected"}) {
            QVariantMap p{{"DeferReason", reason}, {"State", "error"}}; QVERIFY(!TrayController::needsAttention(p));
        }
        QVERIFY(TrayController::needsAttention({{"DeferReason", "StorageError"}}));
        QVERIFY(TrayController::needsAttention({{"DeferReason", "InvalidConfig"}, {"SettingsDiagnostics", QVariantList{QVariantMap{{"severity", "warning"}, {"code", "InvalidField"}}}}}));
        QVERIFY(TrayController::needsAttention({{"SettingsDiagnostics", QVariantList{QVariantMap{{"severity", "error"}}}}}));
        QVERIFY(!TrayController::needsAttention({{"SettingsDiagnostics", QVariantList{QVariantMap{{"severity", "warning"}}}}}));
        QCOMPARE(TrayController::badge({}, false, false), QString("ready"));
        QCOMPARE(TrayController::badge({}, true, true), QString("downloading"));
        QCOMPARE(TrayController::badge({}, false, true), QString("paused"));
        QCOMPARE(TrayController::badge({{"DeferReason", "StorageError"}}, false, false), QString("problem"));
        Client::ServiceConnection::Snapshot snapshot;
        snapshot.effective = QJsonObject{{"acquisition", QJsonObject{{"automatic", QJsonObject{{"enabled", false}}}}}};
        snapshot.jobs = {QVariantMap{{"State", "downloading"}, {"Cancellable", true}, {"Details", QVariantMap{{"assetTitle", "Manual clip"}}}}};
        QCOMPARE(StatusFormatter::summary(snapshot), QString("Downloading Manual clip"));
    }
    void tooltipTreatsTitlesAsPlainText()
    {
        QMenu menu; PeerProcess peer; QVERIFY(peer.ready());
        // Observe the actual exported property, then decode it as a rich-text
        // tray host does. Markup in manifest titles must remain literal text.
        TrayItem item(menu, "org.kde.StatusNotifierWatcher");
        const QString text = "Downloading <b>Grand & Canyon</b>\n4 videos ready";
        item.update("Active", {}, {}, text);
        QTRY_COMPARE(peer.registrations(), 1);
        const auto reply = request(peer.item(), "/StatusNotifierItem", "org.freedesktop.DBus.Properties", "GetAll", {"org.kde.StatusNotifierItem"});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        const auto tooltip = qdbus_cast<TrayTooltip>(qdbus_cast<QVariantMap>(reply.arguments().first()).value("ToolTip"));
        QVERIFY(!tooltip.text.contains("<b>"));
        QTextDocument rendered; rendered.setHtml(tooltip.text);
        QCOMPARE(rendered.toPlainText(), text);
    }
    void protocolAndLifetime()
    {
        PeerProcess peer; QVERIFY(peer.ready()); Fixture f; QVERIFY(f.start());
        // Offscreen has no desktop platform integration. Inject only the Qt
        // availability observation; SNI host state still comes from the peer.
        TrayController tray(*f.controller, QDBusConnection::sessionBus(), nullptr, [] { return true; }); WindowController window(*f.controller);
        connect(&tray, &TrayController::openRequested, &window, [&](int page, const QVariantMap &p) { window.open(p, page); });
        QVERIFY(!window.hasWindow()); tray.show(); QTRY_COMPARE(peer.registrations(), 1);
        QVERIFY(!window.hasWindow()); QVERIFY(f.controller->tray().value("available").toBool());
        const auto owner = peer.item();
        auto result = request(owner, "/StatusNotifierItem", "org.freedesktop.DBus.Properties", "GetAll", {"org.kde.StatusNotifierItem"});
        QCOMPARE(result.type(), QDBusMessage::ReplyMessage);
        const auto p = Client::unpack(result.arguments().first()).toMap();
        QCOMPARE(p.value("Status").toString(), QString("Active")); QCOMPARE(p.value("Title").toString(), QString("Aerial"));
        QCOMPARE(p.value("Category").toString(), QString("ApplicationStatus")); QVERIFY(!p.value("ItemIsMenu").toBool());
        const auto tooltip = qdbus_cast<TrayTooltip>(qdbus_cast<QVariantMap>(result.arguments().first()).value("ToolTip"));
        QCOMPARE(tooltip.title, QString("Aerial")); QVERIFY(tooltip.text.contains("Paused")); QVERIFY(tooltip.text.contains("videos ready"));
        const auto icons = qdbus_cast<TrayImages>(qdbus_cast<QVariantMap>(result.arguments().first()).value("IconPixmap"));
        QCOMPARE(icons.size(), 7); for (const auto &icon : icons) { QCOMPARE(icon.width, icon.height); QCOMPARE(icon.bytes.size(), icon.width * icon.height * 4); }
        // Independent gdbus introspection validates the wire signatures rather
        // than relying solely on round-tripping our own marshallers.
        QProcess introspect; introspect.start("gdbus", {"introspect", "--session", "--dest", owner, "--object-path", "/StatusNotifierItem"});
        QVERIFY2(introspect.waitForStarted(5000), qPrintable(introspect.errorString()));
        QTRY_COMPARE(introspect.state(), QProcess::NotRunning); const auto xml = introspect.readAllStandardOutput();
        QCOMPARE(introspect.exitStatus(), QProcess::NormalExit); QCOMPARE(introspect.exitCode(), 0);
        QVERIFY(xml.contains("a(iiay) IconPixmap")); QVERIFY(xml.contains("(sa(iiay)ss) ToolTip")); QVERIFY(xml.contains("readonly o Menu"));
        result = request(owner, "/MenuBar", "com.canonical.dbusmenu", "GetLayout", {0, -1, QStringList{}});
        QCOMPARE(result.type(), QDBusMessage::ReplyMessage); QCOMPARE(result.signature(), QString("u(ia{sv}av)"));
        const auto layout = qdbus_cast<MenuLayout>(result.arguments()[1]); QCOMPARE(layout.children.size(), 6);
        auto item = [&](int index) { return qdbus_cast<MenuLayout>(layout.children[index].variant()); };
        QCOMPARE(item(0).properties.value("label").toString(), QString("Open Aerial"));
        QCOMPARE(item(1).properties.value("type").toString(), QString("separator"));
        QCOMPARE(item(2).properties.value("label").toString(), QString("Fetch videos now"));
        QCOMPARE(item(3).properties.value("label").toString(), QString("Resume automatic downloads"));
        QCOMPARE(item(5).properties.value("label").toString(), QString("Hide tray icon"));
        for (int index : {0, 2, 3, 5}) QVERIFY(item(index).properties.value("enabled").toBool());
        result = request(owner, "/MenuBar", "com.canonical.dbusmenu", "GetGroupProperties", {QVariant::fromValue(MenuIds{item(0).id, item(2).id}), QStringList{"label"}});
        QCOMPARE(result.signature(), QString("a(ia{sv})")); QCOMPARE(qdbus_cast<MenuPropertiesList>(result.arguments()[0]).size(), 2);
        result = request(owner, "/MenuBar", "com.canonical.dbusmenu", "GetProperty", {item(2).id, "label"}); QCOMPARE(result.signature(), QString("v"));
        QCOMPARE(request(owner, "/MenuBar", "com.canonical.dbusmenu", "AboutToShow", {0}).signature(), QString("b"));
        QCOMPARE(request(owner, "/MenuBar", "com.canonical.dbusmenu", "Event", {0, "opened", QVariant::fromValue(QDBusVariant(0)), uint(0)}).type(), QDBusMessage::ReplyMessage);
        result = request(owner, "/MenuBar", "com.canonical.dbusmenu", "AboutToShowGroup", {QVariant::fromValue(MenuIds{0, 999})}); QCOMPARE(result.signature(), QString("aiai"));
        QCOMPARE(qdbus_cast<MenuIds>(result.arguments()[1]), MenuIds{999});
        result = request(owner, "/MenuBar", "com.canonical.dbusmenu", "EventGroup", {QVariant::fromValue(MenuEvents{{999, "clicked", QDBusVariant(0), 0}})});
        QCOMPARE(result.signature(), QString("ai")); QCOMPARE(qdbus_cast<MenuIds>(result.arguments()[0]), MenuIds{999});
        auto event = [&](int index) { return request(owner, "/MenuBar", "com.canonical.dbusmenu", "Event", {item(index).id, "clicked", QVariant::fromValue(QDBusVariant(0)), uint(0)}); };
        QCOMPARE(event(3).type(), QDBusMessage::ReplyMessage); QTRY_VERIFY(f.control("automatic").value("on").toBool());
        event(3); QTRY_VERIFY(!f.control("automatic").value("on").toBool());
        event(2); QTRY_VERIFY(!f.service->snapshot().jobs.isEmpty()); // Real service Acquire, even with no remote sources.
        QCOMPARE(event(0).type(), QDBusMessage::ReplyMessage); QTRY_VERIFY(window.hasWindow());
        QPointer<QQuickWindow> first;
        for (auto *w : QGuiApplication::topLevelWindows()) if (auto *q = qobject_cast<QQuickWindow *>(w)) first = q;
        QVERIFY(first); QCOMPARE(window.page(), int(SettingsController::OverviewPage)); QPointer<QQmlEngine> engine = qmlEngine(first); QVERIFY(engine);
        first->close(); QTRY_VERIFY(!window.hasWindow()); QVERIFY(first.isNull()); QVERIFY(engine.isNull());
        event(0); QTRY_VERIFY(window.hasWindow());
        QPointer<QQuickWindow> closingWindow;
        for (auto *w : QGuiApplication::topLevelWindows()) if (auto *q = qobject_cast<QQuickWindow *>(w)) closingWindow = q;
        QVERIFY(closingWindow); QPointer<QQmlEngine> closingEngine = qmlEngine(closingWindow); QVERIFY(closingEngine);
        closingWindow->close(); window.open(); QVERIFY(closingWindow.isNull());
        QPointer<QQuickWindow> replacement;
        for (auto *w : QGuiApplication::topLevelWindows()) if (auto *q = qobject_cast<QQuickWindow *>(w)) replacement = q;
        QVERIFY(replacement); QPointer<QQmlEngine> replacementEngine = qmlEngine(replacement); QVERIFY(replacementEngine);
        QTRY_VERIFY(closingEngine.isNull()); QVERIFY(window.hasWindow()); QVERIFY(replacement); QVERIFY(replacementEngine);
        QPointer<QQuickWindow> primaryOld;
        for (auto *w : QGuiApplication::topLevelWindows()) if (auto *q = qobject_cast<QQuickWindow *>(w)) primaryOld = q;
        QVERIFY(primaryOld); primaryOld->close(); QTRY_VERIFY(!window.hasWindow()); QVERIFY(primaryOld.isNull());
        QCOMPARE(request(owner, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "Activate", {0, 0}).type(), QDBusMessage::ReplyMessage);
        QTRY_VERIFY(window.hasWindow());
        for (const auto &method : {"SecondaryActivate", "ContextMenu"}) QCOMPARE(request(owner, "/StatusNotifierItem", "org.kde.StatusNotifierItem", method, {0, 0}).type(), QDBusMessage::ReplyMessage);
        const auto before = peer.registrations(); const auto rotation = f.control("cache.rotationDays").value("value").toInt() == 1 ? 7 : 1;
        f.cli({"config", "set", "cache.rotationDays", QString::number(rotation)});
        QTRY_COMPARE(f.service->snapshot().effective.value("cache").toObject().value("rotationDays").toInt(), rotation); QCOMPARE(peer.registrations(), before);
        peer.call("Host", {false}); QTRY_VERIFY(!f.controller->tray().value("available").toBool()); QVERIFY(tray.enabled());
        peer.call("Host", {true}); QTRY_COMPARE(peer.registrations(), before + 1);
        peer.call("Replace"); QTRY_COMPARE(peer.registrations(), before + 2);
        QCOMPARE(peer.call("Info").arguments()[2].toInt(), 0);
        const auto newOwner = peer.item();
        auto status = [&] {
            const auto reply = request(newOwner, "/StatusNotifierItem", "org.freedesktop.DBus.Properties", "Get", {"org.kde.StatusNotifierItem", "Status"});
            return reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() == 1 ? qdbus_cast<QDBusVariant>(reply.arguments().first()).variant().toString() : QString();
        };
        QSaveFile broken(f.paths.config); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("{\n broken\n"); QVERIFY(broken.commit());
        QTRY_COMPARE(status(), QString("NeedsAttention"));
        auto label = [&](int index) {
            const auto reply = request(newOwner, "/MenuBar", "com.canonical.dbusmenu", "GetProperty", {item(index).id, "label"});
            return reply.type() == QDBusMessage::ReplyMessage ? qdbus_cast<QDBusVariant>(reply.arguments().first()).variant().toString() : QString();
        };
        QTRY_COMPARE(label(2), QString("Check service status"));
        QCOMPARE(label(3), QString("Review settings"));
        QSaveFile repair(f.paths.config); QVERIFY(repair.open(QIODevice::WriteOnly)); repair.write("{\"sources\":[],\"acquisition\":{\"automatic\":{\"enabled\":false}}}"); QVERIFY(repair.commit());
        QTRY_COMPARE(status(), QString("Active"));
        QCOMPARE(request(newOwner, "/MenuBar", "com.canonical.dbusmenu", "Event", {item(5).id, "clicked", QVariant::fromValue(QDBusVariant(0)), uint(0)}).type(), QDBusMessage::ReplyMessage);
        QTRY_VERIFY(!tray.enabled()); QVERIFY(window.hasWindow()); QVERIFY(f.daemon.state() == QProcess::Running);
        QTRY_VERIFY(!tray.keepsAlive()); tray.show(); QVERIFY(!tray.enabled()); // Late startup forward after Hide settled.
        tray.setEnabled(true); QVERIFY(tray.enabled());
        for (auto *w : QGuiApplication::topLevelWindows()) if (qobject_cast<QQuickWindow *>(w)) w->close(); QTRY_VERIFY(!window.hasWindow());
    }
};
class TrayDiscoveryTest : public QObject {
    Q_OBJECT
private slots:
    void transientDenialRecoversWithoutOwnerChange()
    {
        const auto path = qEnvironmentVariable("AERIAL_TEST_DISCOVERY_CONFIG"); QVERIFY(!path.isEmpty());
        PeerProcess peer; QVERIFY(peer.ready());
        const auto bus = QDBusConnection::sessionBus(); const auto paths = ConfigPaths::environment(QString(AERIAL_SETTINGS_TEST_SOURCE));
        Client::ServiceConnection service(bus, paths, paths.bundle); SettingsController settings(service);
        TrayController tray(settings, bus, nullptr, [] { return true; });
        QSignalSpy observed(&tray, &TrayController::changed); tray.show();
        QTRY_VERIFY(observed.size() > 1); QVERIFY(!settings.tray().value("available").toBool());
        QCOMPARE(peer.registrations(), 0);
        QFile config(path); QVERIFY(config.open(QIODevice::ReadOnly)); auto text = config.readAll(); config.close();
        const QByteArray denial = "<deny send_destination=\"org.freedesktop.DBus\" send_interface=\"org.freedesktop.DBus\" send_member=\"GetNameOwner\"/>";
        QCOMPARE(text.count(denial), 1); text.replace(denial, "");
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate)); QCOMPARE(config.write(text), text.size()); config.close();
        const auto reload = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ReloadConfig");
        QVERIFY(QDBusConnection::sessionBus().call(reload).type() != QDBusMessage::ErrorMessage);
        QTRY_VERIFY_WITH_TIMEOUT(settings.tray().value("available").toBool(), 5000);
        QTRY_COMPARE(peer.registrations(), 1); QVERIFY(settings.tray().value("visible").toBool());
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false); registerTypes(); registerTrayTypes();
    Q_INIT_RESOURCE(ui);
    if (app.arguments().contains("--peer")) {
        WatcherPeer peer; const auto flags = QDBusConnection::SubPath;
        if (!peer.bus.registerVirtualObject("/StatusNotifierWatcher", &peer, flags) || !peer.bus.registerVirtualObject("/TrayPeer", &peer, flags)
            || !peer.bus.registerVirtualObject("/org/freedesktop/Notifications", &peer, flags)
            || !peer.bus.registerService("org.kde.StatusNotifierWatcher") || !peer.bus.registerService("org.aeriallock.TrayPeer") || !peer.bus.registerService("org.freedesktop.Notifications")) return 1;
        QTextStream(stdout) << "READY\n" << Qt::flush; return app.exec();
    }
    if (app.arguments().contains("--discovery")) { TrayDiscoveryTest test; return QTest::qExec(&test, 1, argv); }
    TrayTest test; return QTest::qExec(&test, argc, argv);
}
#include "tray.moc"
