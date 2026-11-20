#include "portal-client.h"
#include "systemd-controller.h"
#include "tray-controller.h"
#include "window-controller.h"
#include "fixture.h"
#include "common/client/client-support.h"
#include <QCoreApplication>
#include <QApplication>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusUnixFileDescriptor>
#include <QDBusVirtualObject>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtTest>
#include <fcntl.h>
using namespace Aerial::Settings;
bool noSources(const QByteArray &bytes)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    return error.error == QJsonParseError::NoError && document.isObject() && document.object().value("sources").isArray()
        && document.object().value("sources").toArray().isEmpty();
}
struct Change { QString type, path, source; };
Q_DECLARE_METATYPE(Change)
using Changes = QList<Change>;
Q_DECLARE_METATYPE(Changes)
QDBusArgument &operator<<(QDBusArgument &arg, const Change &change) { arg.beginStructure(); arg << change.type << change.path << change.source; arg.endStructure(); return arg; }
const QDBusArgument &operator>>(const QDBusArgument &arg, Change &change) { arg.beginStructure(); arg >> change.type >> change.path >> change.source; arg.endStructure(); return arg; }
class Peer : public QDBusVirtualObject {
    Q_OBJECT
public:
    QDBusConnection bus = QDBusConnection::sessionBus();
    QStringList calls;
    QJsonObject config;
    QMap<QString, QVariantMap> units;
    QList<std::function<void()>> waiting;
    QList<std::function<void()>> replies;
    int serial = 0;
    bool subscribed = false;
    Peer()
    {
        for (const auto &unit : {"aerial.service", "aerial-idle.service", "aerial-tray.service"})
            units[unit] = {{"fileState", "disabled"}, {"ActiveState", "inactive"}, {"InvocationID", QByteArray::fromHex("01")}, {"LoadState", "loaded"}};
    }
    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &m, const QDBusConnection &) override
    {
        const auto a = m.arguments(); const auto method = m.member();
        auto reply = [&](const QVariantList &args = {}) { bus.send(m.createReply(args)); };
        if (m.interface() == "org.aeriallock.TestPeer") {
            if (method == "Configure") {
                config = QJsonDocument::fromJson(a[0].toString().toUtf8()).object();
                if (config.contains("idleFile")) units["aerial-idle.service"]["fileState"] = config["idleFile"].toString();
                 if (config.contains("idleActive")) units["aerial-idle.service"]["ActiveState"] = config["idleActive"].toString();
                if (config.contains("idleInvocation")) units["aerial-idle.service"]["InvocationID"] = config["idleInvocation"].toString().toUtf8();
                if (config.contains("trayFile")) units["aerial-tray.service"]["fileState"] = config["trayFile"].toString();
                reply();
                if (!config["hold"].toBool()) { const auto jobs = waiting; waiting.clear(); for (const auto &done : jobs) done(); }
            } else if (method == "Calls") reply({calls});
            else if (method == "ExternalUnit") {
                const auto unit = a[0].toString(); auto path = unit; path.replace("-", "_2d"); path.replace(".", "_2e");
                auto signal = QDBusMessage::createSignal("/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", config["idleUnloaded"].toBool() ? "UnitRemoved" : "UnitNew");
                signal << unit << QDBusObjectPath("/org/freedesktop/systemd1/unit/" + path); bus.send(signal); reply();
            }
            else if (method == "Quit") { reply(); QTimer::singleShot(0, qApp, &QCoreApplication::quit); }
            else if (method == "Late") { const auto pending = waiting; waiting.clear(); for (const auto &done : pending) done(); reply(); }
            else if (method == "OneLate") { if (!waiting.isEmpty()) waiting.takeFirst()(); reply(); }
            else if (method == "Replies") { const auto pending = replies; replies.clear(); for (const auto &done : pending) done(); reply(); }
            return true;
        }
        if (m.interface() == "org.freedesktop.portal.Request" && method == "Close") { calls << "Close" << m.path(); reply(); return true; }
        if (m.interface().startsWith("org.freedesktop.portal.")) {
            calls << m.interface().section('.', -1) + "." + method;
            const auto options = Aerial::Client::unpack(a.last()).toMap();
            if (method == "OpenFile" && m.interface().endsWith("FileChooser")) calls << (options.value("directory").toBool() ? "directory" : "file");
            if (method == "OpenFile" && m.interface().endsWith("OpenURI")) {
                const auto fd = qvariant_cast<QDBusUnixFileDescriptor>(a[1]);
                calls << ((fcntl(fd.fileDescriptor(), F_GETFL) & O_ACCMODE) == O_RDONLY ? "read-only" : "writable");
                calls << QFileInfo("/proc/self/fd/" + QString::number(fd.fileDescriptor())).symLinkTarget();
            }
            if (method == "OpenURI") calls << a[1].toString();
            QString sender = m.service().mid(1); sender.replace('.', '_');
            auto path = "/org/freedesktop/portal/desktop/request/" + sender + '/' + options.value("handle_token").toString();
            if (config["mode"] == "alternate" || config["holdPortalReply"].toBool()) path += "_old";
            const auto respond = [this, path] {
                auto signal = QDBusMessage::createSignal(path, "org.freedesktop.portal.Request", "Response");
                const uint code = config["mode"] == "cancel" ? 1 : config["mode"] == "deny" ? 2 : 0;
                signal << code << QVariantMap{{"uris", QStringList{config.value("uri").toString("file:///home/tester/Videos/one%20%23%20%25%20%C3%A9.mp4")}}}; bus.send(signal);
            };
            if (config["holdPortalReply"].toBool()) {
                replies << [this, m, path] { bus.send(m.createReply({QVariant::fromValue(QDBusObjectPath(path))})); };
                calls << "ActualHandle:" + path;
            } else if (config["mode"] == "early") { respond(); reply({QVariant::fromValue(QDBusObjectPath(path))}); }
            else { reply({QVariant::fromValue(QDBusObjectPath(path))}); if (config["mode"] == "late" || config["mode"] == "alternate") waiting << respond; else QTimer::singleShot(20, this, respond); }
            return true;
        }
        if (m.interface() == "org.freedesktop.DBus.Properties") {
            calls << "GetAll";
            auto unit = m.path().section('/', -1); unit.replace("_2e", "."); unit.replace("_2d", "-"); auto properties = units.value(unit);
            if (unit == "aerial-idle.service" && config["failProperties"].toBool()) {
                bus.send(m.createErrorReply(QDBusError::Timeout, "transient property read")); return true;
            }
            QDBusArgument future; future.beginStructure(); future << QString("opaque future property"); future.endStructure();
            properties["FutureStructuredProperty"] = QVariant::fromValue(future);
            properties["FutureStructureArray"] = QVariant::fromValue(Changes{{"opaque", "array", "element"}});
            properties.remove("fileState");
            if (config["holdProperties"].toBool()) replies << [this, m, properties] { bus.send(m.createReply(QVariantList{properties})); };
            else reply({properties}); return true;
        }
        if (m.interface() != "org.freedesktop.systemd1.Manager") return false;
        calls << method;
        if (method == "Subscribe") {
            if (config["alreadySubscribed"].toBool()) { subscribed = true; bus.send(m.createErrorReply("org.freedesktop.systemd1.AlreadySubscribed", "Subscribed before a local deadline")); }
            else if (config["failSubscribe"].toBool()) { config["failSubscribe"] = false; bus.send(m.createErrorReply(QDBusError::Timeout, "transient subscription failure")); }
            else { subscribed = true; reply(); }
        }
        else if (!subscribed) bus.send(m.createErrorReply("org.aeriallock.Test.Unsubscribed", "Subscribe before snapshot"));
        else if (method == "GetUnitFileState" && a[0].toString() == "aerial-idle.service" && config["missingFile"].toBool()) {
            bus.send(m.createErrorReply("org.freedesktop.DBus.Error.FileNotFound", "No idle unit file"));
        }
        else if (method == "GetUnitFileState" && a[0].toString() == "aerial-idle.service" && (config["failFileRead"].toBool() || config["failFileReadAlways"].toBool())) {
            config["failFileRead"] = false; bus.send(m.createErrorReply(QDBusError::Timeout, "transient file-state read"));
        }
        else if (method == "GetUnitFileState") reply({units.value(a[0].toString()).value("fileState")});
        else if (method == "GetUnit" && config["idleUnloaded"].toBool() && a[0].toString() == "aerial-idle.service") bus.send(m.createErrorReply("org.freedesktop.systemd1.NoSuchUnit", "Idle not loaded"));
        else if (method == "GetUnit" && config["trayLookupError"].toBool() && a[0].toString() == "aerial-tray.service") bus.send(m.createErrorReply(QDBusError::AccessDenied, "lookup denied"));
        else if (method == "GetUnit" && config["trayCollected"].toBool() && a[0].toString() == "aerial-tray.service") bus.send(m.createErrorReply("org.freedesktop.systemd1.NoSuchUnit", "Unit aerial-tray.service not loaded."));
        else if (method == "GetUnit") { auto unit = a[0].toString(); unit.replace("-", "_2d"); unit.replace(".", "_2e"); reply({QVariant::fromValue(QDBusObjectPath("/org/freedesktop/systemd1/unit/" + unit))}); }
        else if (method == "EnableUnitFiles" || method == "DisableUnitFiles") {
            const auto list = qdbus_cast<QStringList>(a[0]);
            if ((method == "EnableUnitFiles" && a[1].toBool()) || (a.size() == 3 && a[2].toBool())) { bus.send(m.createErrorReply("org.aeriallock.Test.InvalidFlags", "No enable-runtime or force")); return true; }
            for (const auto &unit : list) {
                calls << unit;
                const auto file = units[unit].value("fileState").toString();
                if (method == "EnableUnitFiles") units[unit]["fileState"] = "enabled";
                else {
                    calls << (a[1].toBool() ? "disable-runtime" : "disable-persistent");
                    if (a[1].toBool() || file != "enabled-runtime") units[unit]["fileState"] = config["bothScopes"].toBool() && !a[1].toBool() ? "enabled-runtime" : "disabled";
                }
            }
            Changes changes{{"symlink", "owned-fixture", "owned-fixture"}};
            reply(method == "EnableUnitFiles" ? QVariantList{true, QVariant::fromValue(changes)} : QVariantList{QVariant::fromValue(changes)});
            auto signal = QDBusMessage::createSignal("/org/freedesktop/systemd1", m.interface(), "UnitFilesChanged"); bus.send(signal);
        } else if (method == "Reload") reply();
        else if (method == "StartUnit" || method == "RestartUnit") {
            if (a[1].toString() != "replace") { bus.send(m.createErrorReply("org.aeriallock.Test.InvalidMode", "replace required")); return true; }
            const auto unit = a[0].toString(); const uint number = ++serial;
            const auto job = QDBusObjectPath("/org/freedesktop/systemd1/job/" + QString::number(number));
            const auto done = [this, unit, job, number] {
                units[unit]["ActiveState"] = config["failJob"].toBool() ? "failed" : config["inactiveAfterJob"].toBool() || (unit == "aerial-tray.service" && config["trayForward"].toBool()) ? "inactive" : "active";
                if (!config["noNewInvocation"].toBool()) units[unit]["InvocationID"] = QByteArray::number(number + 1);
                units[unit]["Result"] = "success"; units[unit]["ExecMainStatus"] = 0;
                if (config.contains("fileAfterJob")) units[unit]["fileState"] = config["fileAfterJob"].toString();
                auto signal = QDBusMessage::createSignal("/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "JobRemoved");
                signal << number << QVariant::fromValue(job) << unit << (config["failJob"].toBool() ? QString("failed") : QString("done")); bus.send(signal);
            };
            if (config["hold"].toBool()) waiting << done; else done(); // Deliberately before the method reply.
            if (config["holdReply"].toBool()) replies << [this, m, job] { bus.send(m.createReply({QVariant::fromValue(job)})); };
            else reply({QVariant::fromValue(job)});
        } else bus.send(m.createErrorReply("org.freedesktop.DBus.Error.UnknownMethod", "Not allowed"));
        return true;
    }
};
struct PeerProcess {
    QProcess process;
    PeerProcess() { process.start(QCoreApplication::applicationFilePath(), {"--peer"}); }
    bool ready() { return QTest::qWaitFor([&] { return process.readAllStandardOutput().contains("READY"); }, 5000); }
    QDBusMessage call(const QString &method, const QVariantList &arguments = {})
    {
        auto m = QDBusMessage::createMethodCall("org.aeriallock.TestPeer", "/org/freedesktop/TestPeer", "org.aeriallock.TestPeer", method); m.setArguments(arguments);
        return QDBusConnection::sessionBus().call(m);
    }
    void configure(const QJsonObject &config) { call("Configure", {QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact))}); }
    QStringList calls() { return qdbus_cast<QStringList>(call("Calls").arguments().first()); }
    ~PeerProcess() { if (process.state() != QProcess::NotRunning) { call("Quit"); if (!process.waitForFinished(5000)) { process.terminate(); process.waitForFinished(); } } }
};
class DesktopTest : public QObject {
    Q_OBJECT
private slots:
    void portalFeedbackSurvivesPanelChanges()
    {
        PeerProcess peer; QVERIFY(peer.ready());
        Test::Fixture fixture; QVERIFY(fixture.start());
        WindowController window(*fixture.controller); window.open();
        QQuickWindow *quick = nullptr;
        for (auto *candidate : QGuiApplication::topLevelWindows()) if ((quick = qobject_cast<QQuickWindow *>(candidate))) break;
        QVERIFY(quick); QVERIFY(QTest::qWaitForWindowExposed(quick));
        QFile config(fixture.paths.config); QVERIFY(config.open(QIODevice::ReadOnly));
        const auto original = config.readAll(); config.close();
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate)); config.write("{broken"); config.close();
        QTRY_COMPARE(fixture.controller->panel(), QString("diagnostic"));
        peer.configure({{"mode", "deny"}});
        fixture.controller->openSettingsFolder();
        QTRY_VERIFY(!fixture.controller->portal()->error().isEmpty());
        const auto feedbackItems = quick->findChildren<QQuickItem *>("portalError");
        QCOMPARE(feedbackItems.size(), 1); auto *feedback = feedbackItems.first();
        QTRY_VERIFY(feedback->isVisible());
        QCOMPARE(feedback->property("text").toString(), fixture.controller->portal()->error());
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate)); config.write(original); config.close();
        QTRY_COMPARE(fixture.controller->panel(), QString("settings"));
        for (int page = 0; page < 5; ++page) {
            quick->setProperty("page", page); QTRY_VERIFY(feedback->isVisible());
            QCOMPARE(feedback->property("text").toString(), fixture.controller->portal()->error());
        }
        peer.configure({}); QSignalSpy finished(fixture.controller->portal(), &PortalClient::finished);
        fixture.controller->openSettingsFolder(); QTRY_COMPARE(finished.size(), 1);
        QVERIFY(fixture.controller->portal()->error().isEmpty());
        QTRY_VERIFY(!feedback->isVisible());
        quick->close(); QTRY_VERIFY(!window.hasWindow());
    }
    void portals()
    {
        for (const auto &bytes : {QByteArray(), QByteArray("broken"), QByteArray("{}"), QByteArray("[]"), QByteArray("{\"sources\":null}"), QByteArray("{\"sources\":[{}]}")}) QVERIFY(!noSources(bytes));
        QVERIFY(noSources("{\"sources\":[]}"));
        PeerProcess peer; QVERIFY(peer.ready()); PortalClient portal(QDBusConnection::sessionBus()); QSignalSpy finished(&portal, &PortalClient::finished);
        for (const auto &mode : {"early", "alternate", "cancel", "deny"}) {
            peer.configure({{"mode", mode}}); QSignalSpy requestState(&portal, &PortalClient::changed); const auto id = portal.choose(true);
            if (QString(mode) == "alternate") { QTRY_COMPARE(requestState.size(), 2); peer.call("Late"); }
            QTRY_COMPARE(finished.size(), 1); const auto result = finished.takeFirst(); QCOMPARE(result[0].toULongLong(), id);
            if (QString(mode) == "cancel") { QVERIFY(result[2].toBool()); QVERIFY(result[3].toString().isEmpty()); }
            else if (QString(mode) == "deny") { QVERIFY(!result[2].toBool()); QVERIFY(result[1].toString().isEmpty()); QCOMPARE(result[3].toString(), QString("The chooser closed without a selection. Your fields are kept.")); }
            else { QCOMPARE(result[1].toString(), QString::fromUtf8("/home/tester/Videos/one # % \xc3\xa9.mp4")); QVERIFY(result[3].toString().isEmpty()); }
        }
        peer.configure({{"mode", "late"}}); const auto cancelled = portal.choose(false);
        QTRY_VERIFY(peer.calls().contains("file")); portal.cancel(cancelled); QTRY_COMPARE(finished.size(), 1); finished.clear();
        peer.call("Late"); QTest::qWait(50); QCOMPARE(finished.size(), 0);
        for (const auto &uri : {"https://example.org/a.mp4", "file://remote/share/a.mp4", "file:///a.mp4#fragment"}) {
            peer.configure({{"uri", uri}}); portal.choose(false); QTRY_COMPARE(finished.size(), 1); QVERIFY(!finished.takeFirst()[3].toString().isEmpty());
        }
        QTemporaryDir directory; portal.openFolder(directory.path()); QTRY_COMPARE(finished.size(), 1); QVERIFY(finished.takeFirst()[3].toString().isEmpty());
        QVERIFY(peer.calls().contains("read-only")); QVERIFY(peer.calls().contains(directory.path()));
        portal.openLink("https://example.org/documentation"); QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!finished.first()[2].toBool()); QVERIFY(finished.first()[3].toString().isEmpty()); finished.clear();
        QVERIFY(peer.calls().contains("https://example.org/documentation"));
        peer.configure({{"mode", "late"}}); const auto held = peer.calls().count("FileChooser.OpenFile"); portal.choose(false);
        QTRY_COMPARE(peer.calls().count("FileChooser.OpenFile"), held + 1);
        peer.call("Quit"); QVERIFY(peer.process.waitForFinished()); QTRY_COMPARE(finished.size(), 1); QVERIFY(!finished.takeFirst()[3].toString().isEmpty());
    }
    void missingPortal()
    {
        PortalClient portal(QDBusConnection::sessionBus()); QSignalSpy finished(&portal, &PortalClient::finished);
        portal.choose(false); QTRY_COMPARE(finished.size(), 1); QVERIFY(!finished.first()[3].toString().isEmpty());
    }
    void connectionRetryWithoutSystemd()
    {
        Test::Fixture fixture;
        const auto root = fixture.root.path(), source = QString(AERIAL_SETTINGS_TEST_SOURCE);
        fixture.paths = {source, root + "/config.json", root + "/cache", root + "/state", root + "/runtime"};
        fixture.daemon.start(QFileInfo(QCoreApplication::applicationFilePath()).absolutePath() + "/settings-client-probe",
            {"--peer", Aerial::readConfig(fixture.paths).profile, QString::number(QCoreApplication::applicationPid())});
        QVERIFY(fixture.daemon.waitForStarted()); QVERIFY(fixture.daemon.waitForReadyRead());
        QCOMPARE(fixture.daemon.readAllStandardOutput().trimmed(), QByteArray("READY"));
        const auto configure = [&](bool wrong) {
            auto call = QDBusMessage::createMethodCall(Aerial::Client::Service, Aerial::Client::Path, "org.aeriallock.TestPeer", "Configure");
            call << QVariantMap{{"wrongProfile", wrong}, {"noSources", true}};
            return QDBusConnection::sessionBus().call(call).type() != QDBusMessage::ErrorMessage;
        };
        QVERIFY(configure(true));
        fixture.service = std::make_unique<Aerial::Client::ServiceConnection>(QDBusConnection::sessionBus(), fixture.paths, source);
        fixture.controller = std::make_unique<SettingsController>(*fixture.service); fixture.service->start();
        QTRY_VERIFY(fixture.service->errorName().endsWith("ProfileMismatch"));
        QTRY_VERIFY(!fixture.controller->systemd()->error().isEmpty());
        QVERIFY(!fixture.controller->systemd()->available());
        const auto owner = fixture.service->uniqueOwner(); QVERIFY(!owner.isEmpty());
        WindowController window(*fixture.controller); window.open();
        QQuickWindow *quick = nullptr;
        for (auto *candidate : QGuiApplication::topLevelWindows()) if ((quick = qobject_cast<QQuickWindow *>(candidate))) break;
        QVERIFY(quick); QVERIFY(QTest::qWaitForWindowExposed(quick));
        auto *retry = quick->findChild<QQuickItem *>("connectionRetry"); QVERIFY(retry); QVERIFY(retry->isVisible());
        auto *start = quick->findChild<QQuickItem *>("serviceStart"); QVERIFY(start); QVERIFY(!start->isVisible());
        QVERIFY(configure(false)); QVERIFY(!fixture.service->ready());
        QTest::mouseClick(quick, Qt::LeftButton, Qt::NoModifier, retry->mapToScene({retry->width() / 2, retry->height() / 2}).toPoint());
        QTRY_VERIFY(fixture.service->ready()); QCOMPARE(fixture.service->uniqueOwner(), owner);
        QCOMPARE(fixture.controller->panel(), QString("settings"));
        quick->close(); QTRY_VERIFY(!window.hasWindow());
    }
    void cancelledPortalClosesLateAlternateHandleOnce()
    {
        PeerProcess peer; QVERIFY(peer.ready()); peer.configure({{"holdPortalReply", true}});
        PortalClient portal(QDBusConnection::sessionBus()); QSignalSpy done(&portal, &PortalClient::finished);
        const auto id = portal.choose(false); QTRY_VERIFY(peer.calls().contains("FileChooser.OpenFile"));
        const auto actual = peer.calls().filter("ActualHandle:").first().mid(QString("ActualHandle:").size());
        portal.cancel(id); QTRY_COMPARE(done.size(), 1); QVERIFY(done.first()[2].toBool());
        peer.call("Replies"); QTRY_VERIFY(peer.calls().contains(actual));
        QTest::qWait(40); QCOMPARE(done.size(), 1);
    }
    void disablingDoesNotRequireRuntimeProperties()
    {
        PeerProcess peer; QVERIFY(peer.ready()); SystemdController manager(QDBusConnection::sessionBus());
        QTRY_VERIFY(!manager.units().value("aerial-idle.service").toMap().value("invocation").toString().isEmpty());
        peer.configure({{"holdProperties", true}}); QSignalSpy done(&manager, &SystemdController::operationFinished);
        manager.operate("aerial-idle.service", "disable"); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 2000);
        QVERIFY(done.first()[2].toBool());
    }
    void disablingCannotBeConfirmedByFailedFileRead()
    {
        PeerProcess peer; QVERIFY(peer.ready()); SystemdController manager(QDBusConnection::sessionBus()); QTRY_VERIFY(manager.available());
        peer.configure({{"failFileReadAlways", true}}); QSignalSpy done(&manager, &SystemdController::operationFinished);
        manager.operate("aerial-idle.service", "disable"); QTRY_COMPARE(done.size(), 1); QVERIFY(!done.first()[2].toBool());
    }
    void enableStartRequiresPersistentEnablement_data()
    {
        QTest::addColumn<QString>("unit"); QTest::addColumn<bool>("forward"); QTest::addColumn<bool>("collected");
        QTest::newRow("idle") << QString("aerial-idle.service") << false << false;
        QTest::newRow("tray-active") << QString("aerial-tray.service") << false << false;
        QTest::newRow("tray-forward") << QString("aerial-tray.service") << true << false;
        QTest::newRow("tray-collected") << QString("aerial-tray.service") << true << true;
    }
    void enableStartRequiresPersistentEnablement()
    {
        QFETCH(QString, unit); QFETCH(bool, forward); QFETCH(bool, collected);
        PeerProcess peer; QVERIFY(peer.ready()); SystemdController manager(QDBusConnection::sessionBus()); QTRY_VERIFY(manager.available());
        peer.configure({{"fileAfterJob", "enabled-runtime"}, {"trayForward", forward}, {"trayCollected", collected}});
        QSignalSpy done(&manager, &SystemdController::operationFinished); manager.operate(unit, "enableStart");
        QTRY_COMPARE(done.size(), 1); QVERIFY(!done.first()[2].toBool());
        QVERIFY(manager.error().contains("persistently enabled"));
    }
    void systemd()
    {
        PeerProcess peer; QVERIFY(peer.ready()); SystemdController manager(QDBusConnection::sessionBus());
        QSignalSpy operations(&manager, &SystemdController::operationFinished);
        QTRY_VERIFY(manager.available()); QTRY_COMPARE(manager.units().size(), 3);
        QTRY_COMPARE(manager.idle().value("action").toString(), QString("enableStart"));
        QCOMPARE(peer.calls().first(), QString("Subscribe"));
        manager.operate("unrelated.service", "start"); QTest::qWait(30); QVERIFY(!peer.calls().contains("StartUnit"));
        manager.operate("aerial-idle.service", "enableStart"); QTRY_COMPARE(operations.size(), 1); QVERIFY(operations.takeFirst()[2].toBool());
        QVERIFY(!manager.idle().value("fresh").toBool());
        QCOMPARE(manager.idle().value("action").toString(), QString("restart"));
        const auto before = peer.calls().count("RestartUnit"); QTest::qWait(50); QCOMPARE(peer.calls().count("RestartUnit"), before);
        peer.configure({{"hold", true}}); manager.operate("aerial-idle.service", "restart");
        QTRY_COMPARE(peer.calls().count("RestartUnit"), before + 1);
        peer.configure({});
        QTRY_COMPARE(operations.size(), 1); QVERIFY(operations.takeFirst()[2].toBool()); QVERIFY(!manager.idle().value("fresh").toBool());
        manager.operate("aerial-idle.service", "restart"); QTRY_COMPARE(operations.size(), 1); QVERIFY(operations.takeFirst()[2].toBool()); QVERIFY(!manager.idle().value("fresh").toBool());
        peer.configure({{"idleFile", "masked"}}); manager.refresh(); QTRY_VERIFY(manager.idle().value("message").toString().contains("masked"));
        const auto calls = peer.calls().count("StartUnit"); manager.operate("aerial-idle.service", "start");
        QTRY_COMPARE(operations.size(), 1); QVERIFY(!operations.takeFirst()[2].toBool()); QCOMPARE(peer.calls().count("StartUnit"), calls);
        peer.configure({{"idleFile", "enabled"}, {"failJob", true}}); manager.refresh();
        QTRY_COMPARE(manager.units().value("aerial-idle.service").toMap().value("fileState").toString(), QString("enabled"));
        manager.operate("aerial-idle.service", "restart"); QTRY_COMPARE(operations.size(), 1); QVERIFY(!operations.takeFirst()[2].toBool());
        QVERIFY(!manager.error().isEmpty());
        peer.configure({}); manager.operate("aerial-tray.service", "enableStart"); QTRY_COMPARE(operations.size(), 1); QVERIFY(operations.takeFirst()[2].toBool());
        peer.configure({{"trayForward", true}}); manager.operate("aerial-tray.service", "start"); QTRY_COMPARE(operations.size(), 1); QVERIFY(operations.takeFirst()[2].toBool());
        peer.configure({{"trayForward", true}, {"trayCollected", true}}); manager.operate("aerial-tray.service", "start"); QTRY_COMPARE(operations.size(), 1); QVERIFY(operations.takeFirst()[2].toBool());
        peer.configure({{"trayForward", true}, {"trayLookupError", true}}); manager.operate("aerial-tray.service", "start"); QTRY_COMPARE(operations.size(), 1); QVERIFY(!operations.takeFirst()[2].toBool());
        QVERIFY(manager.error().contains("AccessDenied"));
        peer.configure({{"failJob", true}, {"trayCollected", true}}); manager.operate("aerial-tray.service", "start"); QTRY_COMPARE(operations.size(), 1); QVERIFY(!operations.takeFirst()[2].toBool());
        manager.operate("aerial-tray.service", "disable"); QTRY_COMPARE(operations.size(), 1); QVERIFY(operations.takeFirst()[2].toBool());
        QVERIFY(!peer.calls().contains("StopUnit"));
        for (const auto &file : {"linked", "linked-runtime", "enabled-runtime"}) {
            peer.configure({{"idleFile", file}, {"idleActive", "inactive"}, {"idleUnloaded", true}}); manager.refresh();
            QTRY_COMPARE(manager.units().value("aerial-idle.service").toMap().value("fileState").toString(), QString(file));
            QTRY_COMPARE(manager.idle().value("action").toString(), QString("enableStart"));
        }
        peer.configure({{"idleFile", "enabled"}, {"idleActive", "active"}}); peer.call("ExternalUnit", {"aerial-idle.service"});
        QTRY_COMPARE(manager.idle().value("observation").toString(), QString("aerial-idle is running."));
        peer.configure({{"idleFile", "enabled"}, {"idleUnloaded", true}}); peer.call("ExternalUnit", {"aerial-idle.service"});
        QTRY_COMPARE(manager.idle().value("observation").toString(), QString("aerial-idle is not running."));
        peer.call("Quit"); QVERIFY(peer.process.waitForFinished()); QTRY_VERIFY(!manager.available());
        PeerProcess replacement; QVERIFY(replacement.ready()); QTRY_VERIFY(manager.available());
        QTRY_COMPARE(replacement.calls().first(), QString("Subscribe"));
    }
    void enablingRunningIdleDoesNotConfirmSettings()
    {
        PeerProcess peer; QVERIFY(peer.ready());
        peer.configure({{"idleFile", "disabled"}, {"idleActive", "active"}, {"noNewInvocation", true}});
        SystemdController manager(QDBusConnection::sessionBus());
        QSignalSpy operations(&manager, &SystemdController::operationFinished);
        QTRY_VERIFY(manager.available());
        QTRY_COMPARE(manager.units().value("aerial-idle.service").toMap().value("active").toString(), QString("active"));
        QCOMPARE(manager.idle().value("action").toString(), QString("enableStart"));
        manager.operate("aerial-idle.service", "enableStart");
        QTRY_COMPARE(operations.size(), 1); QVERIFY(operations.takeFirst()[2].toBool());
        QVERIFY(!manager.idle().value("fresh").toBool());
        QCOMPARE(manager.idle().value("action").toString(), QString("restart"));
        peer.configure({}); manager.operate("aerial-idle.service", "restart");
        QTRY_COMPARE(operations.size(), 1); QVERIFY(operations.takeFirst()[2].toBool());
        QVERIFY(!manager.idle().value("fresh").toBool());
    }
    void delayedIdleSampleDoesNotConfirmSettings()
    {
        PeerProcess peer; QVERIFY(peer.ready()); SystemdController manager(QDBusConnection::sessionBus());
        QTRY_VERIFY(manager.available()); QTRY_VERIFY(!manager.units().value("aerial-idle.service").toMap().value("invocation").toString().isEmpty());
        // An external start read old config, but its new invocation is not sampled yet.
        peer.configure({{"idleActive", "active"}, {"idleInvocation", "external"}, {"noNewInvocation", true}});
        QSignalSpy done(&manager, &SystemdController::operationFinished);
        manager.operate("aerial-idle.service", "enableStart"); QTRY_COMPARE(done.size(), 1); QVERIFY(done.first()[2].toBool());
        QVERIFY(!manager.idle().value("fresh").toBool()); QCOMPARE(manager.idle().value("action").toString(), QString("restart"));
    }
    void subscriptionRetriesWithoutOwnerChange()
    {
        PeerProcess peer; QVERIFY(peer.ready()); peer.configure({{"failSubscribe", true}});
        SystemdController manager(QDBusConnection::sessionBus()); QTRY_VERIFY_WITH_TIMEOUT(manager.available(), 5000);
        QCOMPARE(peer.calls().count("Subscribe"), 2);
    }
    void discoveryRetriesWithoutOwnerChange()
    {
        const auto path = qEnvironmentVariable("AERIAL_TEST_DISCOVERY_CONFIG"); QVERIFY(!path.isEmpty());
        PeerProcess peer; QVERIFY(peer.ready()); SystemdController manager(QDBusConnection::sessionBus(), nullptr, 1000);
        QTRY_VERIFY(!manager.error().isEmpty()); QVERIFY(!manager.available());
        QFile config(path); QVERIFY(config.open(QIODevice::ReadOnly)); auto text = config.readAll(); config.close();
        const QByteArray denial = "<deny send_destination=\"org.freedesktop.DBus\" send_interface=\"org.freedesktop.DBus\" send_member=\"GetNameOwner\"/>";
        QCOMPARE(text.count(denial), 1); text.replace(denial, "");
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate)); QCOMPARE(config.write(text), text.size()); config.close();
        const auto reload = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ReloadConfig");
        QVERIFY(QDBusConnection::sessionBus().call(reload).type() != QDBusMessage::ErrorMessage);
        QTRY_VERIFY_WITH_TIMEOUT(manager.available(), 5000);
        QTRY_COMPARE(manager.units().value("aerial-idle.service").toMap().value("fileState").toString(), QString("disabled"));
        QCOMPARE(peer.calls().count("Subscribe"), 1);
        QSignalSpy done(&manager, &SystemdController::operationFinished);
        manager.operate("aerial-idle.service", "start");
        QTRY_COMPARE(done.size(), 1); QVERIFY(done.first()[2].toBool());
        peer.configure({{"idleFile", "enabled"}}); peer.call("ExternalUnit", {QString("aerial-idle.service")});
        QTRY_COMPARE(manager.units().value("aerial-idle.service").toMap().value("fileState").toString(), QString("enabled"));
    }
    void alreadySubscribedIsReady()
    {
        PeerProcess peer; QVERIFY(peer.ready()); peer.configure({{"alreadySubscribed", true}});
        SystemdController manager(QDBusConnection::sessionBus()); QTRY_VERIFY_WITH_TIMEOUT(manager.available(), 2000);
        QCOMPARE(peer.calls().count("Subscribe"), 1);
    }
    void idleRestartCannotConfirmAnUnknownUnitProfile()
    {
        PeerProcess peer; QVERIFY(peer.ready()); peer.configure({{"idleFile", "enabled"}}); SystemdController manager(QDBusConnection::sessionBus());
        QTRY_VERIFY(manager.available()); QSignalSpy done(&manager, &SystemdController::operationFinished);
        manager.operate("aerial-idle.service", "restart"); QTRY_COMPARE(done.size(), 1); QVERIFY(done.first()[2].toBool());
        QVERIFY(!manager.idle().value("fresh").toBool()); QVERIFY(manager.idle().value("message").toString().contains("profile"));
    }
    void failedFileStateReadRecoversWithoutAnOwnerChange()
    {
        PeerProcess peer; QVERIFY(peer.ready()); peer.configure({{"failFileRead", true}, {"idleUnloaded", true}});
        SystemdController manager(QDBusConnection::sessionBus()); QTRY_VERIFY(manager.available());
        QTRY_COMPARE_WITH_TIMEOUT(manager.units().value("aerial-idle.service").toMap().value("fileState").toString(), QString("disabled"), 5000);
        QCOMPARE(manager.idle().value("action").toString(), QString("enableStart"));
    }
    void missingIdleFileIsUnavailable()
    {
        PeerProcess peer; QVERIFY(peer.ready()); peer.configure({{"missingFile", true}, {"idleUnloaded", true}});
        SystemdController manager(QDBusConnection::sessionBus()); QTRY_VERIFY(manager.available());
        QTRY_COMPARE(manager.units().value("aerial-idle.service").toMap().value("fileState").toString(), QString("not-found"));
        QVERIFY(manager.idle().value("action").toString().isEmpty()); QVERIFY(manager.idle().value("message").toString().contains("install", Qt::CaseInsensitive));
    }
    void failedPropertiesCannotClaimCachedActivity()
    {
        PeerProcess peer; QVERIFY(peer.ready()); peer.configure({{"idleFile", "enabled"}, {"idleActive", "active"}});
        SystemdController manager(QDBusConnection::sessionBus()); QTRY_COMPARE(manager.idle().value("action").toString(), QString("restart"));
        peer.configure({{"failProperties", true}, {"idleActive", "inactive"}}); manager.refresh();
        QTRY_COMPARE(manager.units().value("aerial-idle.service").toMap().value("active").toString(), QString("unknown"));
        QVERIFY(manager.idle().value("observation").toString().contains("unknown")); QVERIFY(manager.idle().value("action").toString().isEmpty());
        peer.configure({}); QTRY_COMPARE_WITH_TIMEOUT(manager.idle().value("action").toString(), QString("start"), 5000);
    }
    void sourceSaveWaitsForTheChooser()
    {
        PeerProcess peer; QVERIFY(peer.ready()); peer.configure({{"mode", "late"}});
        Aerial::Settings::Test::Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources();
        s->begin(); s->field("type", "folder"); s->field("name", "Folder"); s->field("source", f.root.path()); s->choosePath();
        QTRY_VERIFY(peer.calls().contains("directory")); s->save(); QVERIFY(!s->busy()); QVERIFY(s->sheetOpen());
        QVERIFY(noSources(f.cli({"config", "show", "--json"}))); s->dismiss(); QVERIFY(!s->sheetOpen());
    }
    void idleLifecycleCannotOvertakeSettingsWrites_data()
    {
        QTest::addColumn<int>("timeout"); QTest::newRow("saved") << 60; QTest::newRow("rejected") << -1;
    }
    void idleLifecycleCannotOvertakeSettingsWrites()
    {
        QFETCH(int, timeout); PeerProcess peer; QVERIFY(peer.ready()); peer.configure({{"idleFile", "enabled"}, {"idleActive", "active"}});
        Aerial::Settings::Test::Fixture f; QVERIFY(f.start()); auto *manager = f.controller->systemd();
        QTRY_COMPARE(manager->idle().value("action").toString(), QString("restart")); const auto before = peer.calls().count("RestartUnit");
        f.controller->edit("idle.timeoutSeconds", timeout); QVERIFY(f.control("idle.timeoutSeconds").value("pending").toBool());
        QVERIFY(manager->idle().value("action").toString().isEmpty()); manager->operate("aerial-idle.service", "restart");
        QTRY_VERIFY(!f.control("idle.timeoutSeconds").value("pending").toBool());
        QTRY_COMPARE(manager->idle().value("action").toString(), QString("restart")); QCOMPARE(peer.calls().count("RestartUnit"), before);
        if (timeout < 0) QVERIFY(!f.control("idle.timeoutSeconds").value("error").toString().isEmpty());
    }
    void jobDeadlineFencesLateSignalsAndReplies_data()
    {
        QTest::addColumn<bool>("holdReply"); QTest::newRow("queued-job") << false; QTest::newRow("late-method-reply") << true;
    }
    void jobDeadlineFencesLateSignalsAndReplies()
    {
        QFETCH(bool, holdReply);
        PeerProcess peer; QVERIFY(peer.ready()); peer.configure({{"hold", true}, {"holdReply", holdReply}});
        SystemdController manager(QDBusConnection::sessionBus(), nullptr, 700); QTRY_VERIFY(manager.available());
        QSignalSpy done(&manager, &SystemdController::operationFinished);
        manager.operate("aerial-idle.service", "start"); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 2500);
        QVERIFY(!done.first()[2].toBool()); QVERIFY(!manager.idle().value("pending").toBool()); QVERIFY(manager.error().contains("not confirmed"));
        peer.configure({{"hold", true}}); manager.operate("aerial-idle.service", "start");
        QTRY_COMPARE(peer.calls().count("StartUnit"), 2); peer.call("Replies"); peer.call("OneLate");
        QTest::qWait(40); QCOMPARE(done.size(), 1); QVERIFY(manager.idle().value("pending").toBool());
        peer.call("Late"); QTRY_COMPARE(done.size(), 2); QVERIFY(done.last()[2].toBool());
        // The first operation's already-armed timer cannot complete this or a later operation.
        QTest::qWait(750); QCOMPARE(done.size(), 2);
    }
    void disableBothStartupScopes()
    {
        PeerProcess peer; QVERIFY(peer.ready()); peer.configure({{"trayFile", "enabled"}, {"bothScopes", true}});
        SystemdController manager(QDBusConnection::sessionBus()); QTRY_VERIFY(manager.available());
        QSignalSpy done(&manager, &SystemdController::operationFinished); manager.operate("aerial-tray.service", "disable");
        QTRY_COMPARE(done.size(), 1); QVERIFY(done.first()[2].toBool());
        QCOMPARE(manager.units().value("aerial-tray.service").toMap().value("fileState").toString(), QString("disabled"));
        QVERIFY(peer.calls().contains("disable-runtime")); QVERIFY(peer.calls().contains("disable-persistent"));
    }
    void trayStartupPersistenceIsNotSessionEnablement()
    {
        PeerProcess peer; QVERIFY(peer.ready()); const auto bus = QDBusConnection::sessionBus();
        const auto paths = Aerial::ConfigPaths::environment(QString(AERIAL_SETTINGS_TEST_SOURCE));
        Aerial::Client::ServiceConnection service(bus, paths, paths.bundle); SettingsController settings(service);
        TrayController tray(settings, bus, nullptr, [] { return false; }); tray.show();
        for (const auto &file : {"enabled", "enabled-runtime", "disabled"}) {
            peer.configure({{"trayFile", file}}); settings.systemd()->refresh();
            QTRY_COMPARE(settings.systemd()->units().value("aerial-tray.service").toMap().value("fileState").toString(), QString(file));
            QTRY_COMPARE(settings.tray().value("persistent").toBool(), QString(file) == "enabled");
            QVERIFY(settings.tray().value("on").toBool());
        }
    }
    void failedOperationKeepsDisplayReason()
    {
        PeerProcess peer; QVERIFY(peer.ready()); peer.configure({{"inactiveAfterJob", true}});
        SystemdController manager(QDBusConnection::sessionBus()); QTRY_VERIFY(manager.available());
        QSignalSpy done(&manager, &SystemdController::operationFinished); manager.operate("aerial-idle.service", "start");
        QTRY_COMPARE(done.size(), 1); QVERIFY(!done.first()[2].toBool());
        QVERIFY(manager.error().contains("The job finished but the unit is not active."));
    }
    void missingManager()
    {
        SystemdController manager(QDBusConnection::sessionBus()); QTRY_VERIFY(!manager.error().isEmpty());
        QVERIFY(!manager.available()); QVERIFY(manager.idle().value("action").toString().isEmpty());
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.arguments().size() == 5 && app.arguments()[1].startsWith("--native-")) {
        const auto mode = app.arguments()[1], value = app.arguments()[2], expected = app.arguments()[3], output = app.arguments()[4];
        QJsonObject receipt; bool complete = false, accepted = false;
        std::unique_ptr<Aerial::Settings::Test::Fixture> fixture;
        PortalClient standalone(QDBusConnection::sessionBus());
        PortalClient *portal = &standalone;
        if (mode == "--native-choose") {
            const auto address = qEnvironmentVariable("AERIAL_TEST_SERVICE_BUS"); if (address.isEmpty()) return 2;
            fixture = std::make_unique<Aerial::Settings::Test::Fixture>();
            fixture->environment.insert("DBUS_SESSION_BUS_ADDRESS", address);
            const auto bus = QDBusConnection::connectToBus(address, "native-source-fixture");
            if (!fixture->start(bus)) {
                const auto error = fixture->service ? fixture->service->errorName() : QString("daemon did not start");
                fprintf(stderr, "Native source fixture failed: %s; daemon=%s\n", qPrintable(error), fixture->daemon.readAllStandardError().constData());
                return 1;
            }
            auto *sources = fixture->controller->sources();
            portal = fixture->controller->portal();
            sources->begin(); sources->field("type", value); sources->field("name", "Portal selection");
        }
        QObject::connect(portal, &PortalClient::finished, &app, [&](quint64, const QString &path, bool cancelled, const QString &error) {
            receipt = {{"path", path}, {"cancelled", cancelled}, {"error", error}};
            accepted = expected == "closed" ? !cancelled && path.isEmpty() && !error.isEmpty()
                : error.isEmpty() && (expected == "cancel" ? cancelled : !cancelled && (mode != "--native-choose" || path == expected));
            complete = true;
        });
        if (mode == "--native-choose") fixture->controller->sources()->choosePath();
        else if (mode == "--native-folder") portal->openFolder(value);
        else if (mode == "--native-link") portal->openLink(value);
        else return 2;
        if (!QTest::qWaitFor([&] { return complete; }, 60000)) return 1;
        if (!accepted) { fprintf(stderr, "Unexpected portal outcome: %s\n", QJsonDocument(receipt).toJson(QJsonDocument::Compact).constData()); return 1; }
        if (fixture) {
            auto *sources = fixture->controller->sources();
            if (expected == "cancel" || expected == "closed") {
                if (!sources->draft().value("source").toString().isEmpty()) return 1;
                if (!noSources(fixture->cli({"config", "show", "--json"}))) return 1;
                receipt["noMutation"] = true;
            } else {
                if (sources->draft().value("source").toString() != expected) return 1;
                sources->save(); if (!QTest::qWaitFor([&] { return !sources->sheetOpen(); }, 5000)) return 1;
                const auto saved = QJsonDocument::fromJson(fixture->cli({"config", "show", "--json"})).object().value("sources").toArray();
                if (saved.size() != 1 || saved.first().toObject().value("source").toString() != expected) return 1;
                receipt["savedViaServiceAndCli"] = true;
            }
        }
        QFile file(output); if (!file.open(QIODevice::WriteOnly)) return 1; file.write(QJsonDocument(receipt).toJson());
        return 0;
    }
    if (app.arguments().contains("--peer")) {
        qDBusRegisterMetaType<Change>(); qDBusRegisterMetaType<Changes>(); Peer peer;
        if (!peer.bus.registerVirtualObject("/org/freedesktop", &peer, QDBusConnection::SubPath)) return 1;
        for (const auto &name : {"org.freedesktop.portal.Desktop", "org.freedesktop.systemd1", "org.aeriallock.TestPeer"}) if (!peer.bus.registerService(name)) return 1;
        fprintf(stdout, "READY\n"); fflush(stdout); return app.exec();
    }
    registerTypes(); DesktopTest test; return QTest::qExec(&test, argc, argv);
}
#include "desktop.moc"
