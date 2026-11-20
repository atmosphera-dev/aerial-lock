#include "common/client/service-connection.h"
#include <QDBusVirtualObject>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QXmlStreamWriter>
#include <QUrl>
#include <QtTest>
#include <unistd.h>
#include <sys/prctl.h>
#include <signal.h>

using namespace Aerial;
using namespace Aerial::Client;
namespace {
const QString jobPath = QString(Path) + "/jobs/jfixture";
const QString controlInterface = "org.aeriallock.TestPeer";
constexpr qulonglong large = 9007199254740993ULL;
QString source, serviceBinary, testPython;

// Separate-process peer: delayed replies really cross a bus, never Qt's
// synchronous same-process dispatch shortcut. No production test interface.
class Peer : public QDBusVirtualObject {
public:
    Peer(QDBusConnection bus, QString profile) : bus(std::move(bus)), profile(std::move(profile)) {}
    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &) override
    {
        const auto method = message.member();
        if (message.interface() == controlInterface) {
            if (method == "Configure") {
                const auto changes = qdbus_cast<QVariantMap>(message.arguments().first());
                for (auto it = changes.begin(); it != changes.end(); ++it) flags[it.key()] = it.value();
            } else if (method == "Release") {
                const auto saved = held; held.clear();
                for (const auto &reply : saved) bus.send(reply);
            } else if (method == "Relinquish") bus.unregisterService(Service);
            else if (method == "Notify") {
                auto signal = QDBusMessage::createSignal(Path, Properties, "PropertiesChanged");
                signal << QString(Service) << QVariantMap{} << QStringList{"State"}; bus.send(signal); bus.send(signal);
            } else if (method == "NotifyJob") {
                const auto changed = unpack(flags.value("jobChanged")).toMap();
                for (const auto &key : {"State", "SourceId", "Details", "ReceivedBytes"})
                    if (changed.contains(key)) flags[QString("job") + key] = changed.value(key);
                auto signal = QDBusMessage::createSignal(jobPath, Properties, "PropertiesChanged");
                signal << QString(JobInterface) << changed << QStringList{}; bus.send(signal);
            } else if (method == "NotifyService") {
                auto signal = QDBusMessage::createSignal(Path, Properties, "PropertiesChanged");
                signal << QString(Service) << unpack(flags.value("serviceChanged")).toMap() << QStringList{}; bus.send(signal);
            } else if (method == "Stats") { bus.send(message.createReply(QVariantList{counts})); return true; }
            bus.send(message.createReply()); return true;
        }
        counts[method] = counts.value(method).toInt() + 1;
        QDBusMessage reply;
        if (method == "GetAll") {
            if (message.path() == Path) {
                if (flags.value("invalidateBeforeFailedFacts").toBool()) {
                    flags["invalidateBeforeFailedFacts"] = false;
                    auto signal = QDBusMessage::createSignal(Path, Properties, "PropertiesChanged");
                    signal << QString(Service) << QVariantMap{} << QStringList{"State"}; bus.send(signal);
                    bus.send(message.createErrorReply(QDBusError::Failed, "one failed Facts read after invalidation")); return true;
                }
                QVariantMap values{{"Profile", flags.value("wrongProfile").toBool() ? "wrong" : profile},
                    {"ConfigRevision", revision}, {"State", flags.value("state", "idle")},
                    {"MaxBytes", QVariant::fromValue(large)}, {"UsageKnown", true}};
                values["PartialBytes"] = flags.value("partialBytes", 0);
                if (flags.value("factsProgressDuringRead").toBool()) {
                    flags["partialBytes"] = flags.value("partialBytes", 0).toInt() + 1;
                    auto signal = QDBusMessage::createSignal(Path, Properties, "PropertiesChanged");
                    signal << QString(Service) << QVariantMap{{"PartialBytes", flags.value("partialBytes")}} << QStringList{}; bus.send(signal);
                }
                if (flags.value("crossProgressDuringRead").toBool()) {
                    auto signal = QDBusMessage::createSignal(jobPath, Properties, "PropertiesChanged");
                    signal << QString(JobInterface) << QVariantMap{{"ReceivedBytes", 100}} << QStringList{}; bus.send(signal);
                }
                reply = message.createReply(QVariantList{values});
            } else {
                if (flags.value("expireJob").toBool()) {
                    flags["noJobs"] = true;
                    reply = message.createErrorReply(QDBusError::UnknownObject, "expired");
                } else {
                    SourceList nested{{{"code", "Success"}, {"bytes", QVariant::fromValue(large)}}};
                    QVariantMap values{{"State", flags.value("jobState", "succeeded")}, {"SourceId", flags.value("jobSourceId", "fixture")}, {"Cancellable", false},
                        {"Details", QVariantMap{{"nested", QVariantMap{{"bytes", QVariant::fromValue(large)}}},
                            {"sourceResults", QVariant::fromValue(nested)}}}};
                    if (flags.contains("jobDetails")) values["Details"] = flags.value("jobDetails");
                    values["ReceivedBytes"] = flags.value("jobReceivedBytes", 0);
                    if (flags.value("progressDuringRead").toBool()) {
                        flags["jobReceivedBytes"] = flags.value("jobReceivedBytes", 0).toInt() + 1;
                        auto signal = QDBusMessage::createSignal(jobPath, Properties, "PropertiesChanged");
                        signal << QString(JobInterface) << QVariantMap{{"ReceivedBytes", flags.value("jobReceivedBytes")}} << QStringList{};
                        bus.send(signal); // Before the reply, on every round, without a timing assumption.
                    }
                    if (flags.value("crossProgressDuringRead").toBool()) {
                        auto signal = QDBusMessage::createSignal(Path, Properties, "PropertiesChanged");
                        signal << QString(Service) << QVariantMap{{"PartialBytes", 100}} << QStringList{}; bus.send(signal);
                    }
                    reply = message.createReply(QVariantList{values});
                    if (flags.value("holdJobProperties").toBool()) { held.append(reply); return true; }
                }
            }
        } else if (method == "Get") {
            reply = message.createReply(QVariantList{QVariant::fromValue(QDBusVariant(flags.value("wrongProfile").toBool() ? "wrong" : profile))});
            if (flags.value("holdProfile").toBool()) { held.append(reply); return true; }
        } else if (method == "GetSettings") {
            const auto settings = flags.value("settings", QString("{\"marker\":") + revision.mid(1) + "}").toString();
            reply = message.createReply(QVariantList{settings, settings, revision});
            if (flags.value("bootstrapRace").toBool()) {
                flags["bootstrapRace"] = false; held.append(reply); revision = "r2";
                auto signal = QDBusMessage::createSignal(Path, Service, "SettingsChanged");
                signal << revision << QStringList{"marker"}; bus.send(signal); return true;
            }
            if (flags.value("holdRead").toBool()) { held.append(reply); return true; }
        } else if (method == "ListSources") {
            if (flags.value("invalidateBeforeFailedSources").toBool()) {
                flags["invalidateBeforeFailedSources"] = false;
                auto signal = QDBusMessage::createSignal(Path, Service, "SettingsChanged");
                signal << revision << QStringList{"marker"}; bus.send(signal);
                reply = message.createErrorReply(QDBusError::Failed, "one failed read after invalidation");
            } else if (flags.value("failSources").toBool()) reply = message.createErrorReply(QDBusError::Failed, "not readable");
            else reply = message.createReply(QVariantList{QVariant::fromValue(SourceList{{{"id", "fixture"}, {"enabled", true},
                {"activeDownloads", flags.value("jobState") == "downloading" && flags.value("jobSourceId", "fixture") == "fixture" ? 1 : 0}}})});
            if (flags.value("holdSources").toBool()) { held.append(reply); return true; }
        } else if (method == "ListJobs") {
            auto signal = QDBusMessage::createSignal(jobPath, JobInterface, "Finished");
            signal << QString("succeeded") << QString();
            if (!sentFinished) { sentFinished = true; bus.send(signal); }
            const QList<QDBusObjectPath> jobs = flags.value("noJobs").toBool() ? QList<QDBusObjectPath>{} : QList<QDBusObjectPath>{QDBusObjectPath(jobPath)};
            reply = message.createReply(QVariantList{QVariant::fromValue(jobs)});
        } else if (method == "UpdateSettings") {
            if (flags.value("conflict").toBool()) reply = message.createErrorReply("org.aeriallock.Aerial1.Error.Conflict", "do not echo https://token");
            else {
                if (flags.contains("settings")) {
                    const auto current = QJsonDocument::fromJson(flags.value("settings").toString().toUtf8()).object();
                    const auto patch = QJsonDocument::fromJson(message.arguments().value(1).toString().toUtf8()).object();
                    flags["settings"] = QString::fromUtf8(QJsonDocument(mergeObjects(current, patch)).toJson(QJsonDocument::Compact));
                    revision = "r" + QString::number(revision.mid(1).toInt() + 1);
                }
                reply = message.createReply(QVariantList{revision});
            }
            if (flags.value("holdWrite").toBool()) { held.append(reply); return true; }
        } else if (method == "Acquire" || method == "Prune" || method == "Refresh") {
            reply = message.createReply(QVariantList{QVariant::fromValue(QDBusObjectPath(jobPath))});
        } else reply = message.createReply();
        bus.send(reply); return true;
    }
    QDBusConnection bus;
    QString profile, revision = "r1";
    QVariantMap flags, counts;
    QList<QDBusMessage> held;
    bool sentFinished = false;
};

struct Process {
    QProcess child;
    ~Process() {
        if (child.state() != QProcess::NotRunning) {
            child.terminate();
            if (!child.waitForFinished(5000)) { child.kill(); child.waitForFinished(5000); }
        }
    }
    void peer(const QString &profile) {
        child.start(QCoreApplication::applicationFilePath(), {"--peer", profile, QString::number(getpid())});
        QVERIFY(child.waitForStarted(5000));
        QVERIFY(child.waitForReadyRead(5000));
        QCOMPARE(child.readAllStandardOutput().trimmed(), QByteArray("READY"));
    }
};
QVariantMap control(Process &peer, const QString &method, const QVariantMap &flags = {})
{
    // The peer's unique owner is advertised by Stats; use PID-specific control
    // service so relinquished owners remain independently addressable.
    auto call = QDBusMessage::createMethodCall("org.aeriallock.TestPeer.p" + QString::number(peer.child.processId()), "/control", controlInterface, method);
    if (method == "Configure") call << flags;
    const auto reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage) qFatal("peer control: %s", qPrintable(reply.errorName()));
    return reply.arguments().isEmpty() ? QVariantMap{} : unpack(reply.arguments().first()).toMap();
}
ConfigPaths fixturePaths(const QString &root)
{
    return {source, root + "/config/config.json", root + "/cache", root + "/state", root + "/runtime"};
}
}

class ClientTest : public QObject {
    Q_OBJECT
private slots:
    void peerDiesWithItsParent()
    {
        QProcess parent;
        const auto path = qgetenv("PATH"); qputenv("PATH", QByteArray());
        parent.start(testPython, {"-c", "import os,subprocess,sys,time; p=subprocess.Popen([sys.argv[1], '--peer', 'fixture', str(os.getpid())],stdout=subprocess.PIPE); assert p.stdout.readline().strip()==b'READY'; print(p.pid,flush=True); time.sleep(120)", QCoreApplication::applicationFilePath()});
        const bool started = parent.waitForStarted(); qputenv("PATH", path);
        QVERIFY(started); QVERIFY(parent.waitForReadyRead());
        bool valid = false; const auto pid = parent.readAllStandardOutput().trimmed().toLongLong(&valid); QVERIFY(valid && pid > 1);
        parent.kill(); QVERIFY(parent.waitForFinished());
        QTRY_VERIFY_WITH_TIMEOUT([&] { QFile stat("/proc/" + QString::number(pid) + "/stat"); if (!stat.open(QIODevice::ReadOnly)) return true;
            return stat.readAll().split(')').last().trimmed().startsWith('Z'); }(), 5000);
    }
    void noOwnerAndDisconnected()
    {
        QTemporaryDir root;
        ServiceConnection client(QDBusConnection::sessionBus(), fixturePaths(root.path()), source);
        client.start(); QTRY_VERIFY(!client.errorName().isEmpty());
        QVERIFY(client.errorName().endsWith("NameHasNoOwner")); QVERIFY(!client.ready());
        QVERIFY(!QFile::exists(qEnvironmentVariable("AERIAL_ACTIVATION_MARKER")));
        ServiceConnection absent(QDBusConnection("missing-settings-bus"), fixturePaths(root.path()), source);
        absent.start(); QCOMPARE(absent.errorName(), QString("org.freedesktop.DBus.Error.Disconnected"));
        QSignalSpy mutations(&absent, &ServiceConnection::mutationFinished);
        absent.setPaused(true); QTRY_COMPARE(mutations.size(), 1);
        QVERIFY(mutations.first()[2].toString().endsWith("Unavailable"));
    }
    void bootstrapInvalidationAndNestedValues()
    {
        QTemporaryDir root; const auto paths = fixturePaths(root.path());
        Process peer; peer.peer(readConfig(paths).profile); control(peer, "Configure", {{"bootstrapRace", true}});
        ServiceConnection client(QDBusConnection::sessionBus(), paths, source);
        QList<QString> published;
        connect(&client, &ServiceConnection::changed, this, [&] { if (client.ready()) published.append(client.snapshot().revision); });
        int ticks = 0; QTimer timer; timer.setInterval(5); connect(&timer, &QTimer::timeout, this, [&] { ++ticks; }); timer.start();
        client.start();
        QTRY_VERIFY(control(peer, "Stats").value("GetSettings").toInt() >= 1);
        QTest::qWait(50); QVERIFY(!client.ready()); QVERIFY(ticks > 2);
        control(peer, "Release"); QTRY_VERIFY(client.ready());
        QCOMPARE(client.snapshot().revision, QString("r2")); QVERIFY(!published.contains("r1"));
        QCOMPARE(client.snapshot().user.value("marker").toInt(), 2);
        QCOMPARE(client.snapshot().properties.value("MaxBytes").metaType().id(), int(QMetaType::ULongLong));
        QCOMPARE(client.snapshot().properties.value("MaxBytes").toULongLong(), large);
        QCOMPARE(client.snapshot().sources.size(), 1); QCOMPARE(client.snapshot().jobs.size(), 1);
        const auto details = client.snapshot().jobs.first().toMap().value("Details").toMap();
        QCOMPARE(details.value("nested").toMap().value("bytes").toULongLong(), large);
        QCOMPARE(details.value("sourceResults").toList().first().toMap().value("bytes").toULongLong(), large);
        const auto reads = control(peer, "Stats").value("GetAll").toInt();
        control(peer, "Configure", {{"state", "paused"}}); control(peer, "Notify");
        QTRY_COMPARE(client.snapshot().properties.value("State").toString(), QString("paused"));
        QVERIFY(control(peer, "Stats").value("GetAll").toInt() > reads);
        control(peer, "Configure", {{"expireJob", true}}); client.refresh();
        QTRY_VERIFY(client.snapshot().jobs.isEmpty()); QVERIFY(client.ready());
        // A failed section must not turn into a coherent-ready snapshot when
        // a different in-flight section subsequently settles.
        control(peer, "Configure", {{"failSources", true}}); client.refresh();
        QTRY_VERIFY(!client.ready()); QTest::qWait(50); QVERIFY(!client.ready());
        control(peer, "Configure", {{"failSources", false}}); client.refresh(); QTRY_VERIFY(client.ready());
    }
    void profileAndIntentIsolation()
    {
        QTemporaryDir root; const auto paths = fixturePaths(root.path());
        Process peer; peer.peer(readConfig(paths).profile); control(peer, "Configure", {{"wrongProfile", true}});
        ServiceConnection client(QDBusConnection::sessionBus(), paths, source); client.start();
        QTRY_VERIFY(client.errorName().endsWith("ProfileMismatch"));
        QCOMPARE(control(peer, "Stats").value("GetSettings").toInt(), 0);
        control(peer, "Configure", {{"wrongProfile", false}}); client.refresh(); QTRY_VERIFY(client.ready());
        QSignalSpy mutations(&client, &ServiceConnection::mutationFinished);
        control(peer, "Configure", {{"wrongProfile", true}});
        client.setPaused(true); QTRY_COMPARE(mutations.size(), 1);
        QVERIFY(mutations.last()[2].toString().endsWith("ProfileMismatch"));
        QCOMPARE(control(peer, "Stats").value("SetPaused").toInt(), 0);
        control(peer, "Configure", {{"wrongProfile", false}, {"conflict", true}});
        client.updateSettings(client.snapshot().revision, {{"language", "en"}});
        QTRY_COMPARE(mutations.size(), 2);
        QVERIFY(mutations.last()[2].toString().endsWith("Conflict"));
        QTest::qWait(50); QCOMPARE(control(peer, "Stats").value("UpdateSettings").toInt(), 1);
        QVERIFY(!mutations.last()[2].toString().contains("token"));
    }
    void progressOnlyInvalidationCannotStarveMutations_data()
    {
        QTest::addColumn<bool>("jobs"); QTest::addColumn<bool>("facts"); QTest::addColumn<bool>("cross");
        QTest::newRow("jobs") << true << false << false; QTest::newRow("facts") << false << true << false;
        QTest::newRow("both") << true << true << false; QTest::newRow("cross-section") << false << false << true;
    }
    void progressOnlyInvalidationCannotStarveMutations()
    {
        QFETCH(bool, jobs); QFETCH(bool, facts); QFETCH(bool, cross);
        QTemporaryDir root; const auto paths = fixturePaths(root.path()); Process peer; peer.peer(readConfig(paths).profile);
        ServiceConnection client(QDBusConnection::sessionBus(), paths, source); client.start(); QTRY_VERIFY(client.ready());
        control(peer, "Configure", {{"progressDuringRead", jobs}, {"factsProgressDuringRead", facts}, {"crossProgressDuringRead", cross}}); QSignalSpy mutations(&client, &ServiceConnection::mutationFinished);
        client.setPaused(true); QTRY_COMPARE_WITH_TIMEOUT(mutations.size(), 1, 2000);
        QVERIFY(mutations.first()[2].toString().isEmpty()); QVERIFY(client.ready());
        control(peer, "Configure", {{"progressDuringRead", false}, {"factsProgressDuringRead", false}, {"crossProgressDuringRead", false}});
        if (jobs) QTRY_VERIFY(client.snapshot().jobs.first().toMap().value("ReceivedBytes").toInt() > 0);
        if (facts) QTRY_VERIFY(client.snapshot().properties.value("PartialBytes").toInt() > 0);
        QCOMPARE(control(peer, "Stats").value("SetPaused").toInt(), 1);
    }
    void queuedInvalidationSurvivesAReadFailure()
    {
        QTemporaryDir root; const auto paths = fixturePaths(root.path()); Process peer; peer.peer(readConfig(paths).profile);
        ServiceConnection client(QDBusConnection::sessionBus(), paths, source); client.start(); QTRY_VERIFY(client.ready());
        control(peer, "Configure", {{"invalidateBeforeFailedSources", true}}); QSignalSpy mutations(&client, &ServiceConnection::mutationFinished);
        client.setPaused(true); QTRY_COMPARE(mutations.size(), 1);
        QTRY_VERIFY(client.ready()); QCOMPARE(control(peer, "Stats").value("SetPaused").toInt(), 1);
        control(peer, "Configure", {{"invalidateBeforeFailedFacts", true}});
        bool sawFailure = false;
        connect(&client, &ServiceConnection::changed, &client, [&] { sawFailure |= !client.ready() && client.errorName().endsWith("Failed"); });
        control(peer, "Notify"); QTRY_VERIFY(sawFailure); QTRY_VERIFY(client.ready());
        QCOMPARE(control(peer, "Stats").value("SetPaused").toInt(), 1);
    }
    void sourceInvalidationFollowsJobSemantics()
    {
        QTemporaryDir root; const auto paths = fixturePaths(root.path());
        Process peer; peer.peer(readConfig(paths).profile);
        ServiceConnection client(QDBusConnection::sessionBus(), paths, source); client.start(); QTRY_VERIFY(client.ready());
        for (const auto &key : {"State", "SourceId", "Details"}) {
            const auto before = control(peer, "Stats").value("ListSources").toInt();
            const QVariant value = QString(key) == "Details" ? QVariant(QVariantMap{{"sourceResults", QVariantMap{{"fixture", "changed"}}}})
                : QVariant(QString(key) == "State" ? "downloading" : "changed");
            control(peer, "Configure", {{"jobChanged", QVariantMap{{key, value}}}}); control(peer, "NotifyJob");
            QTRY_VERIFY(control(peer, "Stats").value("ListSources").toInt() > before);
            if (QString(key) == "State") QTRY_COMPARE(client.snapshot().sources.first().toMap().value("activeDownloads").toInt(), 1);
        }
        QSignalSpy coherent(&client, &ServiceConnection::changed); client.refresh();
        QTRY_VERIFY(!coherent.isEmpty() && client.ready());
        const auto before = control(peer, "Stats").value("ListSources").toInt();
        auto progress = client.snapshot().jobs.first().toMap(); progress.remove("path"); progress["ReceivedBytes"] = 1024;
        control(peer, "Configure", {{"jobChanged", progress}}); control(peer, "NotifyJob");
        QTRY_COMPARE(client.snapshot().jobs.first().toMap().value("ReceivedBytes").toInt(), 1024);
        QCOMPARE(control(peer, "Stats").value("ListSources").toInt(), before);
    }
    void returnTransitionInvalidatesSourcesBeforeSamplingCatchesUp_data()
    {
        QTest::addColumn<QString>("key"); QTest::addColumn<QVariant>("different");
        QTest::newRow("State") << QString("State") << QVariant("downloading");
        QTest::newRow("SourceId") << QString("SourceId") << QVariant("other-source");
        QTest::newRow("Details") << QString("Details") << QVariant(QVariantMap{{"sourceResults", QVariantMap{{"fixture", "different"}}}});
    }
    void returnTransitionInvalidatesSourcesBeforeSamplingCatchesUp()
    {
        QFETCH(QString, key); QFETCH(QVariant, different);
        QTemporaryDir root; const auto paths = fixturePaths(root.path()); Process peer; peer.peer(readConfig(paths).profile);
        ServiceConnection client(QDBusConnection::sessionBus(), paths, source); client.start(); QTRY_VERIFY(client.ready());
        const auto before = control(peer, "Stats").value("ListSources").toInt();
        const auto original = client.snapshot().jobs.first().toMap().value(key);
        control(peer, "Configure", {{"holdSources", true}, {"holdJobProperties", true}, {"jobChanged", QVariantMap{{key, different}}}});
        control(peer, "NotifyJob"); QTRY_COMPARE(control(peer, "Stats").value("ListSources").toInt(), before + 1);
        control(peer, "Configure", {{"jobChanged", QVariantMap{{key, original}}}}); control(peer, "NotifyJob");
        control(peer, "Configure", {{"holdSources", false}, {"holdJobProperties", false}}); control(peer, "Release");
        QTRY_VERIFY(control(peer, "Stats").value("ListSources").toInt() >= before + 2);
        QTRY_COMPARE(client.snapshot().sources.first().toMap().value("activeDownloads").toInt(), 0);
    }
    void replacementDiscardsLateReadAndWrite()
    {
        QTemporaryDir root; const auto paths = fixturePaths(root.path()); const auto profile = readConfig(paths).profile;
        Process old; old.peer(profile); control(old, "Configure", {{"holdRead", true}});
        ServiceConnection client(QDBusConnection::sessionBus(), paths, source); client.start();
        QTRY_VERIFY(control(old, "Stats").value("GetSettings").toInt() >= 1);
        const auto firstOwner = client.uniqueOwner(); control(old, "Relinquish");
        Process replacement; replacement.peer(profile);
        QTRY_VERIFY(client.ready()); QVERIFY(client.uniqueOwner() != firstOwner);
        QSignalSpy changes(&client, &ServiceConnection::changed);
        control(old, "Release"); QTest::qWait(50); QVERIFY(client.ready());
        QCOMPARE(changes.size(), 0); // Even an identical late snapshot is discarded.
        control(old, "Notify"); QTest::qWait(50); QCOMPARE(changes.size(), 0);
        const auto secondOwner = client.uniqueOwner();
        control(replacement, "Configure", {{"holdWrite", true}});
        QSignalSpy mutations(&client, &ServiceConnection::mutationFinished);
        const auto intent = client.updateSettings(client.snapshot().revision, {{"theme", "Enfield.qml"}});
        QTRY_COMPARE(control(replacement, "Stats").value("UpdateSettings").toInt(), 1);
        control(replacement, "Relinquish"); Process third; third.peer(profile);
        QTRY_VERIFY(client.ready() && client.uniqueOwner() != secondOwner);
        QTRY_COMPARE(mutations.size(), 1); QCOMPARE(mutations[0][0].toULongLong(), intent);
        QVERIFY(mutations[0][2].toString().endsWith("OwnerChanged"));
        control(replacement, "Release"); QTest::qWait(50);
        QCOMPARE(mutations.size(), 1); QCOMPARE(control(third, "Stats").value("UpdateSettings").toInt(), 0);
        // Replacement by a wrong profile revokes readiness, even after a
        // previously matching service supplied a durable job snapshot.
        control(third, "Relinquish"); Process wrong; wrong.peer("wrong");
        QTRY_VERIFY(client.errorName().endsWith("ProfileMismatch")); QVERIFY(!client.ready());
    }
    void storageFailureInvalidatesTransferConsumers()
    {
        QTemporaryDir root; const auto paths = fixturePaths(root.path()); Process peer; peer.peer(readConfig(paths).profile);
        control(peer, "Configure", {{"jobState", "downloading"}});
        ServiceConnection client(QDBusConnection::sessionBus(), paths, source); client.start(); QTRY_VERIFY(client.ready());
        QTRY_COMPARE(client.snapshot().sources.first().toMap().value("activeDownloads").toInt(), 1);
        control(peer, "Configure", {{"jobState", "recovering"}, {"serviceChanged", QVariantMap{{"State", "error"}, {"DeferReason", "StorageError"}}}});
        control(peer, "NotifyService"); QTRY_COMPARE(client.snapshot().sources.first().toMap().value("activeDownloads").toInt(), 0);
        QTRY_COMPARE(client.snapshot().jobs.first().toMap().value("State").toString(), QString("recovering"));
    }
    void acceptedReplySurvivesOwnerDeparture()
    {
        QTemporaryDir root; const auto paths = fixturePaths(root.path()); Process peer; peer.peer(readConfig(paths).profile);
        ServiceConnection client(QDBusConnection::sessionBus(), paths, source); client.start(); QTRY_VERIFY(client.ready());
        const auto reads = control(peer, "Stats").value("GetSettings").toInt(); control(peer, "Configure", {{"holdRead", true}});
        QSignalSpy mutations(&client, &ServiceConnection::mutationFinished); client.prune(false);
        QTRY_VERIFY(control(peer, "Stats").value("GetSettings").toInt() > reads); QCOMPARE(mutations.size(), 0);
        control(peer, "Relinquish"); QTRY_COMPARE(mutations.size(), 1); QVERIFY(!client.ready());
        QVERIFY(mutations.first()[2].toString().endsWith("AcceptedResultUnavailable"));
        QCOMPARE(qvariant_cast<QDBusObjectPath>(mutations.first()[1]).path(), jobPath);
        QCOMPARE(control(peer, "Stats").value("Prune").toInt(), 1);
    }
    void busLossAndProfileCheckRace()
    {
        QTemporaryDir root; const auto paths = fixturePaths(root.path()); const auto profile = readConfig(paths).profile;
        Process daemon;
        QFile config(root.path() + "/bus.conf"); QVERIFY(config.open(QIODevice::WriteOnly));
        QXmlStreamWriter xml(&config); xml.writeStartElement("busconfig");
        xml.writeTextElement("type", "session"); xml.writeTextElement("auth", "EXTERNAL");
        xml.writeTextElement("listen", "unix:path=" + QString::fromLatin1(QUrl::toPercentEncoding(root.path() + "/bus", "/._-", "~")));
        xml.writeStartElement("policy"); xml.writeAttribute("context", "default");
        for (const auto *key : {"user", "send_destination", "receive_sender", "own"}) { xml.writeEmptyElement("allow"); xml.writeAttribute(key, "*"); }
        xml.writeEndElement(); xml.writeEndElement(); config.close();
        daemon.child.start("dbus-daemon", {"--config-file=" + config.fileName(), "--nofork", "--nopidfile", "--print-address=1"});
        QVERIFY(daemon.child.waitForStarted()); QVERIFY(daemon.child.waitForReadyRead());
        const auto address = QString::fromUtf8(daemon.child.readAllStandardOutput()).trimmed();
        auto bus = QDBusConnection::connectToBus(address, "settings-loss"); QVERIFY(bus.isConnected());
        Process peer; auto env = QProcessEnvironment::systemEnvironment(); env.insert("DBUS_SESSION_BUS_ADDRESS", address);
        peer.child.setProcessEnvironment(env); peer.peer(profile);
        ServiceConnection client(bus, paths, source); client.start(); QTRY_VERIFY(client.ready());
        daemon.child.terminate(); QVERIFY(daemon.child.waitForFinished(5000));
        QTRY_VERIFY(!client.ready()); QCOMPARE(client.errorName(), QString("org.freedesktop.DBus.Error.Disconnected"));
        QDBusConnection::disconnectFromBus("settings-loss");

        Process first; first.peer(profile);
        ServiceConnection current(QDBusConnection::sessionBus(), paths, source); current.start(); QTRY_VERIFY(current.ready());
        control(first, "Configure", {{"holdProfile", true}});
        QSignalSpy mutations(&current, &ServiceConnection::mutationFinished);
        current.setPaused(true); QTRY_COMPARE(control(first, "Stats").value("Get").toInt(), 1);
        control(first, "Relinquish"); Process second; second.peer(profile); QTRY_VERIFY(current.ready());
        QTRY_COMPARE(mutations.size(), 1); QVERIFY(mutations[0][2].toString().endsWith("OwnerChanged"));
        control(first, "Release"); QTest::qWait(50);
        QCOMPARE(control(first, "Stats").value("SetPaused").toInt(), 0);
        QCOMPARE(control(second, "Stats").value("SetPaused").toInt(), 0);
    }
    void timeoutDoesNotReplay()
    {
        QTemporaryDir root; const auto paths = fixturePaths(root.path());
        Process peer; peer.peer(readConfig(paths).profile);
        ServiceConnection client(QDBusConnection::sessionBus(), paths, source); client.start(); QTRY_VERIFY(client.ready());
        control(peer, "Configure", {{"holdWrite", true}});
        QSignalSpy mutations(&client, &ServiceConnection::mutationFinished);
        client.updateSettings(client.snapshot().revision, {{"theme", "Enfield.qml"}});
        QTRY_COMPARE_WITH_TIMEOUT(mutations.size(), 1, 8000);
        QVERIFY(!mutations[0][2].toString().isEmpty());
        QCOMPARE(control(peer, "Stats").value("UpdateSettings").toInt(), 1);
        control(peer, "Release"); QTest::qWait(50);
        QCOMPARE(mutations.size(), 1); QCOMPARE(control(peer, "Stats").value("UpdateSettings").toInt(), 1);
    }
    void realServiceAndDurableReconnect()
    {
        QTemporaryDir root;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("AERIAL_LOCK_SHELL", source);
        for (const auto &key : {"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME", "XDG_RUNTIME_DIR"}) env.insert(key, root.path() + "/" + key);
        const ConfigPaths paths{source, env.value("XDG_CONFIG_HOME") + "/aerial-lock/config.json",
            env.value("XDG_CACHE_HOME") + "/aerial-lock/managed-v1", env.value("XDG_STATE_HOME") + "/aerial-lock", env.value("XDG_RUNTIME_DIR") + "/aerial-lock"};
        QVERIFY(QDir().mkpath(QFileInfo(paths.config).absolutePath()));
        QFile config(paths.config); QVERIFY(config.open(QIODevice::WriteOnly)); config.write("{\"sources\":[],\"acquisition\":{\"automatic\":{\"enabled\":false}}}"); config.close();
        Process service; service.child.setProcessEnvironment(env); service.child.start(serviceBinary, {}); QVERIFY(service.child.waitForStarted());
        ServiceConnection client(QDBusConnection::sessionBus(), paths, source); client.start(); QTRY_VERIFY(client.ready());
        QSignalSpy mutations(&client, &ServiceConnection::mutationFinished);
        const auto before = client.snapshot().revision;
        client.updateSettings(before, settingPatch("idle.timeoutSeconds", 42));
        QTRY_COMPARE(mutations.size(), 1); QCOMPARE(mutations.last()[2].toString(), QString());
        QTRY_VERIFY(client.snapshot().revision != before);
        QCOMPARE(client.snapshot().user.value("idle").toObject().value("timeoutSeconds").toInt(), 42);
        client.updateSettings(before, {{"language", "en"}}); QTRY_COMPARE(mutations.size(), 2);
        QVERIFY(mutations.last()[2].toString().endsWith("Conflict"));
        client.refreshSources(); QTRY_COMPARE(mutations.size(), 3);
        QTRY_VERIFY(!client.snapshot().jobs.isEmpty());
        QTRY_VERIFY(!client.snapshot().jobs.first().toMap().value("Cancellable").toBool());
        const auto job = client.snapshot().jobs.first().toMap().value("path");
        service.child.terminate(); QVERIFY(service.child.waitForFinished(5000)); QTRY_VERIFY(!client.ready());
        service.child.start(serviceBinary, {}); QVERIFY(service.child.waitForStarted()); QTRY_VERIFY(client.ready());
        QCOMPARE(client.snapshot().jobs.first().toMap().value("path"), job);
        QCOMPARE(client.snapshot().jobs.first().toMap().value("ErrorCode").toString(), QString("NoRemoteSources"));
        // External/CLI-style updates reach the already subscribed consumer.
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate)); config.write("{\"sources\":[],\"theme\":\"Clockwork.qml\"}"); config.close();
        QTRY_COMPARE(client.snapshot().effective.value("theme").toString(), QString("Clockwork.qml"));
    }
    void defaultActivationOnly()
    {
        // The private bus has only a marker activator, never a production Exec.
        QTemporaryDir root; const auto home = qgetenv("HOME"); qputenv("HOME", root.path().toUtf8());
        const ConfigPaths ordinary{source, root.path() + "/.config/aerial-lock/config.json", root.path() + "/.cache/aerial-lock/managed-v1",
            root.path() + "/.local/state/aerial-lock", "/run/user/" + QString::number(getuid()) + "/aerial-lock"};
        QVERIFY(mayActivate(ordinary, source));
        ServiceConnection client(QDBusConnection::sessionBus(), ordinary, source); client.start();
        QTRY_VERIFY(QFile::exists(qEnvironmentVariable("AERIAL_ACTIVATION_MARKER")));
        QTRY_VERIFY(!client.errorName().isEmpty()); QVERIFY(!client.ready()); qputenv("HOME", home);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv); registerBusTypes();
    if (app.arguments().value(1) == "--peer") {
        // The wrapper can SIGKILL the probe at its deadline. Bind this helper
        // to the explicit spawning process, including the pre-prctl death race.
        const auto parent = app.arguments().value(3).toLongLong();
        if (parent <= 1 || prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || getppid() != parent) return 1;
        auto bus = QDBusConnection::sessionBus(); Peer peer(bus, app.arguments().value(2));
        if (!bus.registerVirtualObject("/", &peer, QDBusConnection::SubPath)
            || !bus.registerService(Service)
            || !bus.registerService("org.aeriallock.TestPeer.p" + QString::number(QCoreApplication::applicationPid()))) return 1;
        puts("READY"); fflush(stdout); return app.exec();
    }
    if (argc != 3) return 2;
    testPython = qEnvironmentVariable("AERIAL_TEST_PYTHON");
    if (!QFileInfo(testPython).isAbsolute() || !QFileInfo(testPython).isExecutable()) return 2;
    serviceBinary = QString::fromLocal8Bit(argv[1]); source = QDir::cleanPath(QDir::current().absoluteFilePath(QString::fromLocal8Bit(argv[2])));
    ClientTest tests; return QTest::qExec(&tests, 1, argv);
}
#include "settings-client-probe.moc"
