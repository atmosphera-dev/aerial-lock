#include "fixture.h"
#include <QSaveFile>
#include <QDBusReply>
#include <limits>
using namespace Aerial;
using namespace Aerial::Settings;
using namespace Aerial::Settings::Test;
class ModelTest : public QObject {
    Q_OBJECT
private slots:
    void statusFacts()
    {
        Client::ServiceConnection::Snapshot snapshot;
        snapshot.effective = {{"acquisition", QJsonObject{{"automatic", QJsonObject{{"enabled", true}}}}}};
        snapshot.properties = {{"DeferReason", "Metered"}};
        QCOMPARE(StatusFormatter::summary(snapshot), QString("Waiting: metered connection"));
        snapshot.properties["DeferReason"] = "StorageError";
        QCOMPARE(StatusFormatter::summary(snapshot), QString("Cache needs attention"));
        snapshot.jobs = {QVariantMap{{"State", "downloading"}, {"Cancellable", true}, {"TotalKnown", true},
            {"TotalBytes", QVariant::fromValue(9007199254740993ULL)}, {"ReceivedBytes", QVariant::fromValue(4503599627370497ULL)},
            {"Details", QVariantMap{{"assetTitle", "Grand Canyon"}}}}};
        snapshot.properties.remove("DeferReason");
        QCOMPARE(StatusFormatter::summary(snapshot), QString("Downloading Grand Canyon, 50%"));
        auto job = snapshot.jobs.first().toMap(); job["TotalKnown"] = false; snapshot.jobs = {job};
        QCOMPARE(StatusFormatter::summary(snapshot), QString("Downloading Grand Canyon"));
    }
    void completionCarriesItsSnapshot()
    {
        // A reply is reported only with state sampled after it: an intent sent
        // from the completion handler must not carry the replaced revision.
        Fixture fixture; QVERIFY(fixture.start());
        auto *service = fixture.service.get(); QStringList observed;
        connect(service, &Client::ServiceConnection::mutationFinished, this, [&](quint64, const QVariant &result, const QString &error) {
            observed << error << result.toString() << service->snapshot().revision;
            if (observed.size() == 3) service->updateSettings(service->snapshot().revision, {{"backgroundColor", "#000080"}});
        });
        service->updateSettings(service->snapshot().revision, {{"backgroundColor", "#102030"}});
        QTRY_COMPARE(observed.size(), 6);
        QVERIFY(observed[0].isEmpty()); QCOMPARE(observed[2], observed[1]);
        QVERIFY(observed[3].isEmpty()); QCOMPARE(observed[5], observed[4]);
    }
    void sharedProjectionAndSemanticActions()
    {
        Fixture fixture; QVERIFY(fixture.start()); auto &settings = *fixture.controller;
        QCOMPARE(settings.statusModel()->facts(), settings.cache()->facts());
        QCOMPARE(settings.status(), settings.statusModel()->facts().value("summary").toString());
        QCOMPARE(settings.statusModel()->facts().value("secondaryActionId").toString(), QString("resume-downloads"));
        settings.dispatchAction("pause-downloads"); // A stale opposite action cannot toggle state.
        QVERIFY(!fixture.control("automatic").value("pending").toBool());
        settings.dispatchAction("resume-downloads");
        QTRY_COMPARE(settings.statusModel()->facts().value("secondaryActionId").toString(), QString("pause-downloads"));
        QTRY_VERIFY(!fixture.control("automatic").value("pending").toBool());
        QVERIFY(QJsonDocument::fromJson(fixture.cli({"config", "show", "--json"})).object().value("acquisition").toObject().value("automatic").toObject().value("enabled").toBool());
        QSignalSpy navigation(&settings, &SettingsController::pageRequested);
        settings.dispatchAction("open-sources"); QCOMPARE(navigation.takeFirst().first().toString(), QString("sources"));
        settings.sources()->begin(); settings.sources()->field("name", "Unsaved source");
        // The private test bus has no systemd peer. Wait for discovery rather
        // than sampling its initial state against a later service outage.
        QTRY_VERIFY(!settings.systemd()->error().isEmpty());
        const auto idle = settings.notices()->idle(); QVERIFY(!idle.value("message").toString().isEmpty());
        fixture.daemon.terminate(); QVERIFY(fixture.daemon.waitForFinished(5000));
        QTRY_COMPARE(settings.panel(), QString("unavailable"));
        QVERIFY(settings.statusModel()->facts().value("primaryActionId").toString().isEmpty());
        QVERIFY(settings.sources()->sheetOpen()); QCOMPARE(settings.sources()->draft().value("name").toString(), QString("Unsaved source"));
        bool serviceNotice = false;
        for (const auto &entry : settings.notices()->entries()) serviceNotice |= entry.toMap().value("id") == "service";
        QVERIFY(serviceNotice); QCOMPARE(settings.notices()->idle(), idle);
    }
    void projectionUnknownAndExactStorage()
    {
        Client::ServiceConnection::Snapshot snapshot;
        snapshot.effective = {{"acquisition", QJsonObject{{"automatic", QJsonObject{{"enabled", true}}}}}};
        snapshot.properties = {{"ReadyClips", QVariant::fromValue(9007199254740993ULL)}, {"NextCheck", 1},
            {"ManagedBytes", QVariant::fromValue(std::numeric_limits<qulonglong>::max())}, {"ReservedBytes", QVariant::fromValue(10ULL)}, {"UsageKnown", true}};
        snapshot.jobs = {QVariantMap{{"State", "downloading"}, {"Cancellable", true}, {"TotalKnown", false}, {"TotalBytes", 100}, {"ReceivedBytes", 50}}};
        const auto facts = StatusModel::project(snapshot);
        QCOMPARE(facts.value("readyCount").toULongLong(), 9007199254740993ULL);
        QVERIFY(!facts.value("progressKnown").toBool()); QCOMPARE(facts.value("progress").toDouble(), 0.);
        QVERIFY(!facts.value("detail").toString().contains("left"));
        QCOMPARE(facts.value("segments"), CacheController::storageFacts(snapshot.properties).value("segments"));
        QCOMPARE(facts.value("nextCheckText").toString(), QDateTime::fromSecsSinceEpoch(1).toLocalTime().toString("ddd h:mm AP"));
    }
    void downloadsRoundTrips()
    {
        Fixture fixture; QVERIFY(fixture.start());
        QCOMPARE(fixture.controller->panel(), QString("settings"));
        QCOMPARE(fixture.control("automatic").value("value").toBool(), false);
        const auto keys = fixture.controller->controls().keys();
        for (const auto &key : keys) {
            if (!key.startsWith("acquisition.automatic.") || key == "acquisition.automatic.allowedResolutions") continue;
            for (bool value : {false, true}) {
                fixture.controller->edit(key, value);
                QCOMPARE(fixture.control(key).value("value").toBool(), value);
                QTRY_VERIFY(!fixture.control(key).value("pending").toBool());
                QCOMPARE(fixture.control(key).value("error").toString(), QString());
                const auto saved = QJsonDocument::fromJson(fixture.cli({"config", "show", "--json"})).object();
                QJsonValue leaf(saved); for (const auto &part : key.split('.')) leaf = leaf.toObject().value(part);
                QVERIFY(leaf.isBool()); QCOMPARE(leaf.toBool(), value);
            }
        }
        for (const auto seconds : {3600, 21600, 86400}) {
            fixture.controller->edit("acquisition.checkIntervalSeconds", seconds);
            QTRY_VERIFY(!fixture.control("acquisition.checkIntervalSeconds").value("pending").toBool());
            QCOMPARE(fixture.service->snapshot().user["acquisition"].toObject()["checkIntervalSeconds"].toInt(), seconds);
        }
        for (bool enabled : {true, false}) {
            fixture.controller->edit("acquisition.automatic.allowedResolutions", enabled);
            QTRY_VERIFY(!fixture.control("acquisition.automatic.allowedResolutions").value("pending").toBool());
            QCOMPARE(fixture.control("acquisition.automatic.allowedResolutions").value("value").toList(), enabled ? QVariantList({1080,2160}) : QVariantList({1080}));
        }
        fixture.controller->edit("automatic", true);
        QTRY_VERIFY(!fixture.control("automatic").value("pending").toBool());
        QCOMPARE(fixture.service->snapshot().effective["acquisition"].toObject()["automatic"].toObject()["enabled"].toBool(), true);
    }
    void customAndExternal()
    {
        Fixture fixture; QVERIFY(fixture.start());
        for (const auto &encoded : {"[]", "[2160]", "[2160,1080]"}) {
            fixture.cli({"config", "set", "acquisition.automatic.allowedResolutions", encoded});
            const auto expected = QJsonDocument::fromJson(encoded).array().toVariantList();
            QTRY_COMPARE(fixture.control("acquisition.automatic.allowedResolutions").value("value").toList(), expected);
            QVERIFY(fixture.control("acquisition.automatic.allowedResolutions").value("custom").toBool());
            QFile file(fixture.paths.config); QVERIFY(file.open(QIODevice::ReadOnly)); const auto before = file.readAll(); file.close();
            QSignalSpy samples(fixture.service.get(), &Client::ServiceConnection::changed); fixture.controller->retry();
            QTRY_VERIFY(!samples.isEmpty() && fixture.service->ready());
            QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before);
        }
        fixture.cli({"config", "set", "acquisition.checkIntervalSeconds", "12345"});
        QTRY_COMPARE(fixture.control("acquisition.checkIntervalSeconds").value("value").toInt(), 12345);
        QVERIFY(!fixture.control("acquisition.checkIntervalSeconds").value("inherited").toBool());
        QVERIFY(fixture.control("acquisition.checkIntervalSeconds").value("choices").toList().first().toMap().value("label").toString().contains("12345"));
        const QString huge = "9007199254740993";
        fixture.cli({"config", "set", "acquisition.checkIntervalSeconds", huge});
        QTRY_COMPARE(fixture.control("acquisition.checkIntervalSeconds").value("value").toLongLong(), 9007199254740993LL);
        const auto custom = fixture.control("acquisition.checkIntervalSeconds").value("choices").toList().first().toMap();
        QCOMPARE(custom.value("token").toString(), huge);
        fixture.controller->choose("acquisition.checkIntervalSeconds", custom.value("token").toString());
        QTRY_VERIFY(!fixture.control("acquisition.checkIntervalSeconds").value("pending").toBool());
        QCOMPARE(fixture.service->snapshot().user["acquisition"].toObject()["checkIntervalSeconds"].toInteger(), 9007199254740993LL);
    }
    void latestIntentOwnsPresentation()
    {
        Fixture fixture; QVERIFY(fixture.start());
        fixture.controller->edit("acquisition.checkIntervalSeconds", 21600);
        fixture.controller->edit("acquisition.checkIntervalSeconds", 86400);
        QCOMPARE(fixture.control("acquisition.checkIntervalSeconds").value("value").toInt(), 86400);
        QVERIFY(fixture.control("acquisition.checkIntervalSeconds").value("pending").toBool());
        fixture.controller->edit("acquisition.automatic.allowMetered", true);
        QTRY_VERIFY(!fixture.control("acquisition.checkIntervalSeconds").value("pending").toBool());
        QTRY_VERIFY(!fixture.control("acquisition.automatic.allowMetered").value("pending").toBool());
        QCOMPARE(fixture.control("acquisition.checkIntervalSeconds").value("value").toInt(), 86400);
        QCOMPARE(fixture.control("acquisition.checkIntervalSeconds").value("error").toString(), QString());
        QCOMPARE(fixture.control("acquisition.automatic.allowMetered").value("error").toString(), QString());
        const auto saved = QJsonDocument::fromJson(fixture.cli({"config", "show", "--json"})).object().value("acquisition").toObject();
        QCOMPARE(saved.value("checkIntervalSeconds").toInt(), 86400);
        QVERIFY(saved.value("automatic").toObject().value("allowMetered").toBool());
        // A retired reply cannot overwrite the completed, observed newer edit.
        fixture.service->mutationFinished(1, {}, "org.aeriallock.Aerial1.Error.Conflict");
        QCOMPARE(fixture.control("acquisition.checkIntervalSeconds").value("value").toInt(), 86400);
        QCOMPARE(fixture.control("acquisition.checkIntervalSeconds").value("error").toString(), QString());
        QVERIFY(!fixture.control("acquisition.checkIntervalSeconds").value("pending").toBool());
    }
    void ownerLossRecovers()
    {
        Fixture fixture; QVERIFY(fixture.start());
        QFile file(fixture.paths.config); QVERIFY(file.open(QIODevice::ReadOnly)); const auto before = file.readAll(); file.close();
        fixture.daemon.terminate(); QVERIFY(fixture.daemon.waitForFinished(5000));
        QTRY_COMPARE(fixture.controller->panel(), QString("unavailable"));
        fixture.controller->edit("automatic", true);
        fixture.daemon.start(QString(AERIAL_SETTINGS_TEST_SOURCE) + "/companion/aerial", {}); QVERIFY(fixture.daemon.waitForStarted());
        QTRY_COMPARE(fixture.controller->panel(), QString("settings"));
        QVERIFY(!fixture.control("automatic").value("on").toBool());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before);
    }
    void invalidExistingFieldIsAnObservedDefault()
    {
        Fixture fixture; QVERIFY(fixture.start());
        const QByteArray invalid = "{\"sources\":[],\"acquisition\":{\"checkIntervalSeconds\":-1,\"automatic\":{\"enabled\":false}}}";
        QSaveFile file(fixture.paths.config); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(invalid); QVERIFY(file.commit());
        QTRY_VERIFY(fixture.control("acquisition.checkIntervalSeconds").value("error").toString().contains("service default"));
        bool found = false;
        for (const auto &entry : fixture.controller->notices()->entries()) {
            const auto notice = entry.toMap();
            if (notice.value("id").toString().startsWith("config:")) { found = true; QCOMPARE(notice.value("actionId").toString(), QString("open-settings-folder")); QVERIFY(notice.value("message").toString().contains("acquisition.checkIntervalSeconds")); }
        }
        QVERIFY(found);
        QCOMPARE(fixture.controller->panel(), QString("settings"));
        QVERIFY(fixture.control("acquisition.checkIntervalSeconds").value("inherited").toBool());
        QCOMPARE(fixture.control("acquisition.checkIntervalSeconds").value("value").toInt(), 3600);
        QFile check(fixture.paths.config); QVERIFY(check.open(QIODevice::ReadOnly)); QCOMPARE(check.readAll(), invalid);
    }
    void automaticDiagnosticUsesTheFullSettingPath()
    {
        Fixture f; QVERIFY(f.start()); QSaveFile file(f.paths.config); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"sources\":[],\"acquisition\":{\"automatic\":{\"enabled\":\"wrong\"}}}"); QVERIFY(file.commit());
        QTRY_VERIFY(f.control("automatic").value("error").toString().contains("acquisition.automatic.enabled"));
    }
    void acceptedCleanupReadFailureIsActionableAndRecoverable_data()
    {
        QTest::addColumn<bool>("omitted"); QTest::newRow("present") << false; QTest::newRow("omitted") << true;
    }
    void acceptedCleanupReadFailureIsActionableAndRecoverable()
    {
        QFETCH(bool, omitted);
        const auto bus = QDBusConnection::sessionBus(); const auto paths = ConfigPaths::environment(QString(AERIAL_SETTINGS_TEST_SOURCE));
        QProcess peer; peer.start(QCoreApplication::applicationDirPath() + "/settings-client-probe", {"--peer", readConfig(paths).profile, QString::number(QCoreApplication::applicationPid())});
        struct Retire { QProcess &peer; ~Retire() { peer.terminate(); peer.waitForFinished(5000); } } retire{peer};
        QVERIFY(peer.waitForStarted()); QVERIFY(peer.waitForReadyRead()); QCOMPARE(peer.readAllStandardOutput().trimmed(), QByteArray("READY"));
        const auto name = "org.aeriallock.TestPeer.p" + QString::number(peer.processId());
        auto call = [&](const QString &method, const QVariantMap &flags = QVariantMap()) {
            auto message = QDBusMessage::createMethodCall(name, "/control", "org.aeriallock.TestPeer", method);
            if (method == "Configure") message << flags;
            return bus.call(message);
        };
        Client::ServiceConnection service(bus, paths, paths.bundle); service.start(); QTRY_VERIFY(service.ready()); CacheController cache(service);
        call("Configure", {{"failJob", true}}); cache.prepareCleanup();
        QTRY_COMPARE(cache.cleanup().value("state").toString(), QString("error"));
        QVERIFY(cache.cleanup().value("error").toString().contains("accepted"));
        QVERIFY(!service.ready());
        call("Configure", {{"failJob", false}, {"noJobs", omitted}, {"jobDetails", QVariantMap{{"prune", QVariantMap{{"removedFiles", 0}, {"removedBytes", 0}}}}}});
        service.refresh(); QTRY_VERIFY(service.ready());
        if (omitted) {
            QTRY_VERIFY(cache.cleanup().value("error").toString().contains("no longer available"));
            QDBusReply<QVariantMap> before(call("Stats")); QCOMPARE(before.value().value("Prune").toInt(), 1);
            call("Configure", {{"noJobs", false}}); service.refresh(); QTRY_VERIFY(service.ready());
            cache.prepareCleanup(); QTRY_COMPARE(cache.cleanup().value("state").toString(), QString("confirm"));
        } else { QTRY_COMPARE(cache.cleanup().value("state").toString(), QString("confirm")); }
        QVERIFY(cache.cleanup().value("error").toString().isEmpty());
        QDBusReply<QVariantMap> stats(call("Stats")); QVERIFY(stats.isValid()); QCOMPARE(stats.value().value("Prune").toInt(), omitted ? 2 : 1);
    }
    void queuedEditsSurviveOnlyTheirOwnersReadFailure_data()
    {
        QTest::addColumn<bool>("ownerLeaves"); QTest::newRow("same-owner") << false; QTest::newRow("owner-leaves") << true;
    }
    void queuedEditsSurviveOnlyTheirOwnersReadFailure()
    {
        QFETCH(bool, ownerLeaves);
        const auto bus = QDBusConnection::sessionBus(); QTemporaryDir root;
        const auto paths = ConfigPaths::environment(QString(AERIAL_SETTINGS_TEST_SOURCE));
        QProcess peer; peer.start(QCoreApplication::applicationDirPath() + "/settings-client-probe", {"--peer", readConfig(paths).profile, QString::number(QCoreApplication::applicationPid())});
        struct Retire { QProcess &peer; ~Retire() { peer.terminate(); peer.waitForFinished(5000); } } retire{peer};
        QVERIFY(peer.waitForStarted()); QVERIFY(peer.waitForReadyRead()); QCOMPARE(peer.readAllStandardOutput().trimmed(), QByteArray("READY"));
        const auto name = "org.aeriallock.TestPeer.p" + QString::number(peer.processId());
        auto control = [&](const QString &method, const QVariantMap &flags = QVariantMap()) {
            auto call = QDBusMessage::createMethodCall(name, "/control", "org.aeriallock.TestPeer", method);
            if (method == "Configure") call << flags;
            return bus.call(call);
        };
        control("Configure", {{"settings", "{\"acquisition\":{\"checkIntervalSeconds\":3600,\"automatic\":{\"enabled\":false,\"allowMetered\":false}}}"}, {"failSources", true}});
        Client::ServiceConnection service(bus, paths, paths.bundle); service.start();
        control("Configure", {{"failSources", false}}); service.refresh(); QTRY_VERIFY(service.ready());
        SettingsController settings(service);
        control("Configure", {{"failSources", true}});
        settings.edit("acquisition.checkIntervalSeconds", 21600); settings.edit("acquisition.automatic.allowMetered", true);
        QTRY_VERIFY(!service.ready()); QVERIFY(settings.controls().value("acquisition.automatic.allowMetered").toMap().value("pending").toBool());
        if (ownerLeaves) {
            control("Relinquish"); QTRY_VERIFY(service.uniqueOwner().isEmpty());
            QTRY_VERIFY(!settings.controls().value("acquisition.automatic.allowMetered").toMap().value("pending").toBool());
            QVERIFY(!settings.controls().value("acquisition.automatic.allowMetered").toMap().value("error").toString().isEmpty()); return;
        }
        control("Configure", {{"failSources", false}}); settings.retry();
        QTRY_VERIFY(!settings.controls().value("acquisition.automatic.allowMetered").toMap().value("pending").toBool());
        const auto acquisition = service.snapshot().effective.value("acquisition").toObject();
        QVERIFY(acquisition.value("automatic").toObject().value("allowMetered").toBool());
        QCOMPARE(acquisition.value("checkIntervalSeconds").toInt(), 21600);
    }
    void conflictValidationAndBrokenFile()
    {
        Fixture fixture; QVERIFY(fixture.start());
        // Do not pump this client's events while the external client commits:
        // our edit must use the revision the control actually displayed.
        const auto old = fixture.service->snapshot().revision;
        fixture.cli({"config", "set", "acquisition.checkIntervalSeconds", "21600"});
        QCOMPARE(fixture.service->snapshot().revision, old);
        fixture.controller->edit("acquisition.checkIntervalSeconds", 86400);
        QTRY_VERIFY(fixture.control("acquisition.checkIntervalSeconds").value("error").toString().contains("Changed elsewhere"));
        QTRY_COMPARE(fixture.control("acquisition.checkIntervalSeconds").value("value").toInt(), 21600);
        fixture.controller->edit("acquisition.checkIntervalSeconds", -1);
        QTRY_VERIFY(fixture.control("acquisition.checkIntervalSeconds").value("error").toString().contains("rejected"));
        QTRY_COMPARE(fixture.control("acquisition.checkIntervalSeconds").value("value").toInt(), 21600);
        QSaveFile file(fixture.paths.config); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{\n\"broken\":\n}"); QVERIFY(file.commit());
        QTRY_COMPARE(fixture.controller->panel(), QString("diagnostic"));
        QVERIFY(fixture.controller->diagnostic().contains("line"));
        fixture.controller->edit("automatic", true);
        QFile check(fixture.paths.config); QVERIFY(check.open(QIODevice::ReadOnly)); QCOMPARE(check.readAll(), QByteArray("{\n\"broken\":\n}")); check.close();
        QSaveFile repaired(fixture.paths.config); QVERIFY(repaired.open(QIODevice::WriteOnly)); repaired.write("{\"sources\":[],\"acquisition\":{\"automatic\":{\"enabled\":false}}}"); QVERIFY(repaired.commit());
        QTRY_COMPARE(fixture.controller->panel(), QString("settings"));
    }
};
QTEST_GUILESS_MAIN(ModelTest)
#include "model.moc"
