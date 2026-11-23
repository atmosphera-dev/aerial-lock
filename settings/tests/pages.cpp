#include "fixture.h"
#include <QImage>
#include <QSaveFile>
#include <limits>
#include <sys/stat.h>
using namespace Aerial;
using namespace Aerial::Settings;
using namespace Aerial::Settings::Test;
namespace {
QJsonArray saved(Fixture &f) {
    QJsonParseError error; const auto document = QJsonDocument::fromJson(f.cli({"config", "show", "--json"}), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() || !document.object().value("sources").isArray()) qFatal("config show must return an explicit sources array");
    return document.object().value("sources").toArray();
}
QByteArray bytes(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) qFatal("config preservation read failed");
    const auto result = file.readAll(); if (file.error() != QFileDevice::NoError) qFatal("config preservation read failed"); return result;
}
}
class PagesTest : public QObject {
    Q_OBJECT
private slots:
    void lateControllersReadTheCurrentSnapshot()
    {
        Fixture f; QVERIFY(f.start());
        f.cli({"config", "set", "sources", "[{\"id\":\"local\",\"name\":\"Local\",\"type\":\"folder\",\"source\":\"/home/tester/Videos\"}]"});
        QTRY_COMPARE(f.service->snapshot().sources.size(), 1);
        SettingsController late(*f.service);
        QCOMPARE(late.themes()->rows().size(), 5);
        QCOMPARE(late.sources()->rows().size(), 1);
        QCOMPARE(late.sources()->rows().first().toMap().value("id").toString(), QString("local"));
    }
    void storageAccounting()
    {
        QVariantMap p{{"UsageKnown", true}, {"ManagedBytes", QVariant::fromValue(100ULL)},
            {"PartialBytes", QVariant::fromValue(40ULL)}, {"ReservedBytes", QVariant::fromValue(20ULL)},
            {"MaxBytes", QVariant::fromValue(200ULL)}, {"ReadyClips", 0}};
        const auto facts = CacheController::storageFacts(p);
        QCOMPARE(facts.value("usage").toString(), QString("100 bytes of 200 bytes used"));
        QCOMPARE(facts.value("free").toString(), QString("80 bytes free"));
        QCOMPARE(facts.value("usageKnown").toBool(), true);
        QCOMPARE(facts.value("storageFraction").toDouble(), 0.6);
        QVERIFY(facts.value("storageDescription").toString().contains("40 bytes in the current download"));
        QVERIFY(facts.value("storageDescription").toString().contains("20 bytes reserved"));
        const auto segments = facts.value("segments").toList();
        QCOMPARE(segments.size(), 3); QCOMPARE(facts.value("legend").toList().size(), 4);
        QCOMPARE(segments[0].toMap().value("kind").toString(), QString("stored"));
        QCOMPARE(segments[0].toMap().value("fraction").toDouble(), 0.3);
        QCOMPARE(segments[1].toMap().value("fraction").toDouble(), 0.2);
        QCOMPARE(segments[2].toMap().value("fraction").toDouble(), 0.1);
        QCOMPARE(facts.value("legend").toList()[0].toMap().value("label").toString(), QString("Stored data 60 bytes"));
        p["PartialBytes"] = QVariant::fromValue(0ULL);
        QCOMPARE(CacheController::storageFacts(p).value("legend").toList().size(), 4);
        QCOMPARE(CacheController::storageFacts(p).value("legend").toList()[0].toMap().value("label").toString(), QString("Stored data 100 bytes"));
        p["PartialBytes"] = QVariant::fromValue(40ULL); p["MaxBytes"] = QVariant::fromValue(80ULL);
        QCOMPARE(CacheController::storageFacts(p).value("free").toString(), QString("40 bytes over limit"));
        QCOMPARE(CacheController::storageFacts(p).value("storageFraction").toDouble(), 1.);
        QVERIFY(CacheController::storageFacts(p).value("storageDescription").toString().contains("40 bytes over limit"));
        p["MaxBytes"] = QVariant::fromValue(0ULL);
        QCOMPARE(CacheController::storageFacts(p).value("free").toString(), QString("120 bytes over limit"));
        p["ManagedBytes"] = QVariant::fromValue(9007199254740993ULL);
        p["PartialBytes"] = QVariant::fromValue(4503599627370496ULL);
        p["ReservedBytes"] = QVariant::fromValue(1ULL); p["MaxBytes"] = QVariant::fromValue(9007199254740996ULL);
        QCOMPARE(CacheController::storageFacts(p).value("free").toString(), QString("2 bytes free"));
        p["ManagedBytes"] = QVariant::fromValue(std::numeric_limits<qulonglong>::max());
        p["ReservedBytes"] = QVariant::fromValue(2ULL); p["MaxBytes"] = p["ManagedBytes"];
        QCOMPARE(CacheController::storageFacts(p).value("free").toString(), QString("2 bytes over limit"));
        p["ReservedBytes"] = p["ManagedBytes"]; p["MaxBytes"] = QVariant::fromValue(0ULL);
        QVERIFY(CacheController::storageFacts(p).value("free").toString().endsWith(" GB over limit"));
        p["ManagedBytes"] = QVariant::fromValue(100ULL); p["PartialBytes"] = QVariant::fromValue(200ULL);
        p["ReservedBytes"] = QVariant::fromValue(0ULL); p["MaxBytes"] = QVariant::fromValue(200ULL);
        QCOMPARE(CacheController::storageFacts(p).value("segments").toList().size(), 3);
        QCOMPARE(CacheController::storageFacts(p).value("segments").toList()[0].toMap().value("fraction").toDouble(), 0.0);
        p["UsageKnown"] = false;
        const auto unknown = CacheController::storageFacts(p);
        QCOMPARE(unknown.value("usageKnown").toBool(), false);
        QCOMPARE(unknown.value("storageFraction").toDouble(), 0.);
        QCOMPARE(unknown.value("usage").toString(), QString("Cache usage unavailable"));
        QVERIFY(unknown.value("free").toString().isEmpty()); QVERIFY(unknown.value("segments").toList().isEmpty());
        QVERIFY(unknown.value("legend").toList().isEmpty());
        p["UsageKnown"] = true; p["ManagedBytes"] = p["PartialBytes"] = p["ReservedBytes"] = p["MaxBytes"] = QVariant::fromValue(0ULL);
        const auto empty = CacheController::storageFacts(p);
        QCOMPARE(empty.value("free").toString(), QString("0 bytes free"));
        QCOMPARE(empty.value("storageFraction").toDouble(), 0.);
        for (const auto &segment : empty.value("segments").toList()) QCOMPARE(segment.toMap().value("fraction").toDouble(), 0.0);
        Fixture f; QVERIFY(f.start());
        QTRY_VERIFY(f.controller->serviceProperties().value("UsageKnown").toBool());
        const auto actual = f.controller->serviceProperties();
        QCOMPARE(f.controller->cache()->facts().value("usage").toString(),
                 CacheController::bytes(actual.value("ManagedBytes").toULongLong()) + " of " + CacheController::bytes(actual.value("MaxBytes").toULongLong()) + " used");
        QCOMPARE(f.controller->cache()->facts().value("legend").toList().size(), 4);
        QVERIFY(f.controller->cache()->facts().value("legend").toList()[0].toMap().value("label").toString().startsWith("Stored data "));
    }
    void settingsRoundTrips()
    {
        Fixture f; QVERIFY(f.start());
        for (const auto &key : {"cache.maxBytes", "cache.rotationDays", "idle.timeoutSeconds"}) {
            auto choices = f.control(key).value("choices").toList();
            for (const auto &entry : choices) {
                const auto choice = entry.toMap();
                f.controller->choose(key, choice.value("token").toString());
                QTRY_VERIFY(!f.control(key).value("pending").toBool());
                QCOMPARE(f.control(key).value("error").toString(), QString());
                QCOMPARE(f.control(key).value("value"), choice.value("value"));
            }
            const QString custom = QString(key) == "cache.maxBytes" ? "9007199254740993" : "12345";
            f.cli({"config", "set", key, custom});
            QTRY_COMPARE(f.control(key).value("value").toLongLong(), custom.toLongLong());
            QCOMPARE(f.control(key).value("choices").toList().first().toMap().value("token").toString(), custom);
        }
        for (const auto &key : {"powerActions.enabled", "idle.respectInhibitors", "idle.lockOnSuspend", "idle.logindLockUnlock"}) {
            for (bool on : {false, true}) {
                f.controller->edit(key, on); QTRY_VERIFY(!f.control(key).value("pending").toBool());
                QCOMPARE(f.control(key).value("value").toBool(), on); QCOMPARE(f.control(key).value("error").toString(), QString());
            }
        }
        f.cli({"config", "set", "backgroundColor", "\"navy\""});
        QTRY_COMPARE(f.control("backgroundColor").value("value").toString(), QString("navy"));
        QSignalSpy samples(f.service.get(), &Client::ServiceConnection::changed); f.controller->retry();
        QTRY_VERIFY(!samples.isEmpty() && f.service->ready());
        QCOMPARE(f.control("backgroundColor").value("value").toString(), QString("navy"));
        QCOMPARE(f.controller->canonicalColor("navy"), QString("#000080"));
        f.controller->setColor("navy"); QTRY_VERIFY(!f.control("backgroundColor").value("pending").toBool());
        QCOMPARE(f.control("backgroundColor").value("value").toString(), QString("#000080"));
        f.controller->setColor("not-a-color"); QVERIFY(f.control("backgroundColor").value("error").toString().contains("valid color"));
        QCOMPARE(f.controller->about().value("config").toString(), f.paths.config);
        const auto published = f.service->snapshot().properties;
        QVERIFY(!published.value("Version").toString().isEmpty()); QVERIFY(published.value("ApiVersion").toUInt() > 0);
        QCOMPARE(f.controller->about().value("version").toString(), "aerial " + published.value("Version").toString() + ", interface version " + published.value("ApiVersion").toString());
    }
    void sourceDraftTransactions()
    {
        Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources();
        s->begin(); s->field("name", "Draft name"); s->field("source", "ftp://example.org/private"); s->save();
        QVERIFY(s->sheetOpen()); QVERIFY(s->error().contains("HTTP")); QCOMPARE(saved(f).size(), 0);
        s->field("source", "https://example.org/list.json?token=never-log"); s->field("maxResolution", 1080); s->save();
        QTRY_VERIFY(!s->sheetOpen()); QTRY_COMPARE(s->rows().size(), 1);
        auto original = saved(f).first().toObject(); const auto id = original.value("id").toString(); QVERIFY(!id.isEmpty());
        original["minResolution"] = 1080; original["future"] = QJsonObject{{"keep", 17}};
        QJsonObject second{{"id", "local"}, {"type", "folder"}, {"name", "My videos"}, {"source", "/home/tester/Videos/one # %.mp4"}};
        f.cli({"config", "set", "sources", QString::fromUtf8(QJsonDocument(QJsonArray{original, second}).toJson(QJsonDocument::Compact))});
        QTRY_COMPARE(s->rows().size(), 2);
        s->begin(id); s->field("name", "Renamed"); s->field("maxResolution", 0); s->save();
        QTRY_VERIFY(!s->sheetOpen());
        const auto edited = saved(f).first().toObject();
        QCOMPARE(edited.value("id"), original.value("id")); QCOMPARE(edited.value("minResolution").toInt(), 1080);
        QCOMPARE(edited.value("future"), original.value("future")); QVERIFY(!edited.contains("maxResolution")); QVERIFY(!edited.contains("safeName"));
        s->toggle(id, false); QTRY_VERIFY(!s->rows().first().toMap().value("enabled").toBool());
        QCOMPARE(saved(f).first().toObject().value("future"), original.value("future"));
        s->beginReorder(id); s->dropBefore(""); QTRY_COMPARE(s->rows().last().toMap().value("id").toString(), id);
        QCOMPARE(saved(f).first().toObject().value("id").toString(), QString("local"));
        s->remove("local"); QTRY_COMPARE(s->rows().size(), 1);
        const auto before = bytes(f.paths.config); s->begin(id); s->field("name", "Not saved"); s->dismiss();
        QCOMPARE(bytes(f.paths.config), before);
    }
    void existingLocalPathsSurviveRename_data()
    {
        QTest::addColumn<QString>("type"); QTest::addColumn<QString>("path");
        for (const auto *type : {"folder", "file"})
            for (const auto *path : {"~/Videos/clip.mp4", "$HOME/Videos/clip.mp4"})
                QTest::newRow(qPrintable(QString(type) + '-' + path)) << QString(type) << QString(path);
    }
    void existingLocalPathsSurviveRename()
    {
        QFETCH(QString, type); QFETCH(QString, path);
        Fixture f; QVERIFY(f.start()); auto *sources = f.controller->sources();
        const QJsonArray original{QJsonObject{{"id", "local"}, {"type", type}, {"name", "Original"}, {"source", path}}};
        f.cli({"config", "set", "sources", QString::fromUtf8(QJsonDocument(original).toJson(QJsonDocument::Compact))});
        QTRY_COMPARE(sources->rows().size(), 1);
        sources->begin("local"); sources->field("name", "Renamed"); sources->save();
        QTRY_VERIFY(!sources->sheetOpen());
        const auto result = saved(f).first().toObject();
        QCOMPARE(result.value("name").toString(), QString("Renamed"));
        QCOMPARE(result.value("source").toString(), path); QCOMPARE(result.value("type").toString(), type);
        sources->begin("local"); sources->field("source", "~/Videos/changed.mp4"); sources->save();
        QVERIFY(sources->sheetOpen()); QVERIFY(sources->error().contains("Choose a local"));
        QCOMPARE(saved(f).first().toObject(), result); sources->dismiss();
        sources->begin("local"); sources->field("type", type == "file" ? "folder" : "file"); sources->save();
        QVERIFY(sources->sheetOpen()); QVERIFY(sources->error().contains("Choose a local"));
        QCOMPARE(saved(f).first().toObject(), result); sources->dismiss();
    }
    void sourceConflictAndOwnerPreserveDraft()
    {
        Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources();
        s->begin(); s->field("name", "Unsubmitted"); s->field("source", "https://example.org/a.json");
        f.cli({"config", "set", "cache.rotationDays", "1"});
        s->save(); QTRY_VERIFY(s->error().contains("Changed elsewhere"));
        QVERIFY(s->sheetOpen()); QCOMPARE(s->draft().value("name").toString(), QString("Unsubmitted")); QCOMPARE(saved(f).size(), 0);
        f.daemon.terminate(); QVERIFY(f.daemon.waitForFinished());
        QTRY_COMPARE(f.controller->panel(), QString("unavailable"));
        QCOMPARE(s->draft().value("name").toString(), QString("Unsubmitted"));
        f.daemon.start(QString(AERIAL_SETTINGS_TEST_SOURCE) + "/companion/aerial", {}); QVERIFY(f.daemon.waitForStarted());
        QTRY_COMPARE(f.controller->panel(), QString("settings")); s->field("name", "Still mine"); s->save();
        QVERIFY(s->sheetOpen()); QVERIFY(s->error().contains("service changed")); QCOMPARE(saved(f).size(), 0);
        s->dismiss();
    }
    void sourceRetryWithoutEditing()
    {
        Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources();
        const auto folder = f.root.path() + "/videos"; QVERIFY(QDir().mkpath(folder));
        s->begin(); s->field("name", "Kept draft"); s->field("type", "folder"); s->field("source", folder);
        // A failed chooser leaves the existing valid draft ready for submission.
        s->choosePath();
        f.controller->portal()->finished(1, {}, false, "Timeout");
        QVERIFY(s->error().contains("Timeout")); QVERIFY(s->sheetOpen());
        s->save(); QTRY_VERIFY(!s->sheetOpen());
        QCOMPARE(saved(f).first().toObject().value("name").toString(), QString("Kept draft"));
    }
    void disabledSourceDoesNotOfferImpossibleRetry()
    {
        Fixture f; QVERIFY(f.start()); auto *sources = f.controller->sources();
        f.cli({"config", "set", "sources", "[{\"id\":\"failed\",\"name\":\"Failed\",\"type\":\"community\",\"source\":\"http://127.0.0.1:1/list.json\"}]"});
        QTRY_COMPARE(sources->rows().size(), 1); f.service->refreshSources({"failed"});
        QTRY_VERIFY(sources->rows().first().toMap().value("canRetry").toBool());
        const auto sourceNotice = [&] {
            for (const auto &entry : f.controller->notices()->entries()) if (entry.toMap().value("id") == "source:failed") return entry.toMap();
            return QVariantMap{};
        };
        QTRY_COMPARE(sourceNotice().value("actionId").toString(), QString("retry-source"));
        QCOMPARE(sourceNotice().value("sourceId").toString(), QString("failed"));
        QCOMPARE(sourceNotice().value("message"), sources->rows().first().toMap().value("description"));
        sources->toggle("failed", false); QTRY_VERIFY(!sources->rows().first().toMap().value("enabled").toBool());
        QVERIFY(!sources->rows().first().toMap().value("canRetry").toBool());
        QTRY_VERIFY(sourceNotice().isEmpty());
        QVERIFY(sources->rows().first().toMap().value("description").toString().startsWith("Community pack"));
    }
    void missingSourceCannotRetargetAnOpenDraft()
    {
        Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources();
        f.cli({"config", "set", "sources", "[{\"id\":\"a\",\"name\":\"A\",\"type\":\"community\",\"source\":\"https://example.org/a.json\"}]"});
        QTRY_COMPARE(s->rows().size(), 1); s->begin("a"); s->field("name", "Edited A");
        const auto draft = s->draft(); s->begin("missing"); QCOMPARE(s->draft(), draft);
        s->save(); QTRY_VERIFY(!s->sheetOpen()); const auto result = saved(f);
        QCOMPARE(result.size(), 1); QCOMPARE(result.first().toObject().value("id").toString(), QString("a"));
    }
    void retryFailureCannotPolluteAnOpenDraft()
    {
        Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources();
        s->retry("a"); s->begin(); s->field("name", "Draft B");
        f.service->mutationFinished(1, {}, "org.aeriallock.Test.RetryFailed");
        QCOMPARE(s->draft().value("error").toString(), QString());
        QCOMPARE(s->draft().value("name").toString(), QString("Draft B"));
        QVERIFY(s->error().contains("RetryFailed"));
    }
    void sourceDismissalIsIdempotent()
    {
        Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources(); s->begin();
        QSignalSpy changes(s, &SourcesController::changed); s->dismiss(); QCOMPARE(changes.size(), 1);
        s->dismiss(); QCOMPARE(changes.size(), 1);
    }
    void newLocalSourcesMustMatchTheirPathType()
    {
        Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources();
        const auto path = f.root.path() + "/clip.mp4"; QFile video(path); QVERIFY(video.open(QIODevice::WriteOnly)); video.write("fixture"); video.close();
        for (const auto &entry : {qMakePair(QString("folder"), path), qMakePair(QString("file"), f.root.path())}) {
            s->begin(); s->field("name", "Mismatched"); s->field("type", entry.first); s->field("source", entry.second); s->save();
            QVERIFY(s->sheetOpen()); QVERIFY(!s->draft().value("error").toString().isEmpty()); QCOMPARE(saved(f).size(), 0); s->dismiss();
        }
        const QJsonArray original{QJsonObject{{"id", "local"}, {"name", "Folder"}, {"type", "folder"}, {"source", f.root.path()}}};
        f.cli({"config", "set", "sources", QString::fromUtf8(QJsonDocument(original).toJson(QJsonDocument::Compact))});
        QTRY_COMPARE(s->rows().size(), 1); s->begin("local"); s->field("type", "file"); s->save();
        QVERIFY(s->sheetOpen()); QVERIFY(!s->draft().value("error").toString().isEmpty()); QCOMPARE(saved(f), original);
    }
    void sourceWritesNeverReplaceALossyProjection_data()
    {
        QTest::addColumn<QString>("action"); for (const auto *action : {"toggle", "remove", "append", "reorder"}) QTest::newRow(action) << QString(action);
    }
    void sourceWritesNeverReplaceALossyProjection()
    {
        QFETCH(QString, action); Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources();
        const QJsonArray raw{QJsonObject{{"id", "a"}, {"type", "folder"}, {"name", "A"}, {"source", f.root.path()}},
            QJsonObject{{"id", "bad"}, {"type", "community"}, {"name", "Bad"}, {"source", "ftp://example.org/list.json"}},
            QJsonObject{{"id", "b"}, {"type", "folder"}, {"name", "B"}, {"source", f.root.path()}}};
        QSaveFile config(f.paths.config); QVERIFY(config.open(QIODevice::WriteOnly)); config.write(QJsonDocument(QJsonObject{{"sources", raw}}).toJson()); QVERIFY(config.commit());
        QTRY_COMPARE(f.service->snapshot().user.value("sources").toArray().size(), 3); QTRY_COMPARE(s->rows().size(), 2);
        const auto before = bytes(f.paths.config);
        if (action == "toggle") s->toggle("a", false);
        else if (action == "remove") s->remove("a");
        else if (action == "reorder") { s->beginReorder("a"); s->dropBefore(""); }
        else { s->begin(); s->field("name", "New"); s->field("source", "https://example.org/new.json"); s->save(); }
        QTRY_VERIFY(!s->busy()); QCOMPARE(bytes(f.paths.config), before); QVERIFY(s->error().contains("omitted"));
    }
    void sourceManifestValidationMatchesTheResolver()
    {
        Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources();
        s->begin(); s->field("name", "Fragment"); s->field("source", "https://example.org/list.json#videos"); s->save();
        QVERIFY(s->sheetOpen()); QVERIFY(!s->draft().value("error").toString().isEmpty()); QCOMPARE(saved(f).size(), 0);
    }
    void explicitHttpQuerySourcesRemainSupported()
    {
        Fixture f; QVERIFY(f.start());
        const auto reply = f.cli({"config", "set", "sources", "[{\"id\":\"public\",\"name\":\"Public\",\"type\":\"community\",\"source\":\"http://127.0.0.1/list.json?edition=public\"}]"});
        QVERIFY(!reply.isEmpty()); QCOMPARE(saved(f).first().toObject().value("source").toString(), QString("http://127.0.0.1/list.json?edition=public"));
    }
    void sourceSaveCannotBeDismissedWhilePending_data()
    {
        QTest::addColumn<bool>("conflict"); QTest::newRow("success") << false; QTest::newRow("conflict") << true;
    }
    void sourceSaveCannotBeDismissedWhilePending()
    {
        QFETCH(bool, conflict); Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources();
        s->begin(); s->field("name", "Kept until completion"); s->field("source", "https://example.org/list.json");
        if (conflict) f.cli({"config", "set", "cache.rotationDays", "1"});
        s->save(); QVERIFY(s->busy()); s->dismiss(); QVERIFY(s->sheetOpen());
        QCOMPARE(s->draft().value("name").toString(), QString("Kept until completion")); QTRY_VERIFY(!s->busy());
        if (conflict) { QVERIFY(s->sheetOpen()); QVERIFY(s->draft().value("error").toString().contains("Changed elsewhere")); s->dismiss(); QVERIFY(!s->sheetOpen()); }
        else QTRY_VERIFY(!s->sheetOpen());
    }
    void applePresetDoesNotReuseAnOccupiedIdentity()
    {
        Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources();
        f.cli({"config", "set", "sources", "[{\"id\":\"apple-tv\",\"name\":\"Converted\",\"type\":\"community\",\"source\":\"https://example.org/list.json\"}]"});
        QTRY_COMPARE(s->rows().size(), 1); QVERIFY(s->appleAvailable()); s->begin(); const auto generated = s->draft().value("id");
        s->field("type", "defaultApple"); QCOMPARE(s->draft().value("id"), generated); s->save(); QTRY_VERIFY(!s->sheetOpen()); QCOMPARE(saved(f).size(), 2);
    }
    void appleReselectionRetainsDraftEdits()
    {
        Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources(); s->begin(); s->field("type", "defaultApple");
        s->field("name", "Edited Apple"); const auto draft = s->draft(); s->field("type", "defaultApple"); QCOMPARE(s->draft(), draft);
    }
    void spacedRemoteAddressUsesTheServiceParser()
    {
        Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources();
        f.cli({"config", "set", "sources", "[{\"id\":\"space\",\"name\":\"Space\",\"type\":\"community\",\"source\":\"https://example.org/my pack.json\"}]"});
        QTRY_COMPARE(s->rows().size(), 1); s->begin("space"); s->field("name", "Renamed"); s->save(); QTRY_VERIFY(!s->sheetOpen());
        QCOMPARE(saved(f).first().toObject().value("name").toString(), QString("Renamed"));
    }
    void defaultSourceTransaction()
    {
        Fixture f; QVERIFY(f.start()); auto *s = f.controller->sources(); QVERIFY(s->appleAvailable());
        const auto descriptor = f.service->snapshot().properties.value("DefaultSources").toList().first().toMap();
        s->begin(); s->field("type", "defaultApple");
        QCOMPARE(s->draft().value("source"), descriptor.value("source")); s->save();
        QTRY_VERIFY(!s->sheetOpen()); QTRY_VERIFY(!s->appleAvailable());
        const auto source = saved(f).first().toObject(); QCOMPARE(source.value("source").toString(), descriptor.value("source").toString());
        QVERIFY(!source.contains("safeName"));
        QSaveFile inherited(f.paths.config); QVERIFY(inherited.open(QIODevice::WriteOnly));
        inherited.write("{\"acquisition\":{\"automatic\":{\"enabled\":false}}}"); QVERIFY(inherited.commit());
        QTRY_VERIFY(!f.service->snapshot().user.contains("sources"));
        const auto before = bytes(f.paths.config); s->begin(source.value("id").toString()); s->dismiss();
        QCOMPARE(bytes(f.paths.config), before);
        s->toggle(s->rows().first().toMap().value("id").toString(), false);
        QTRY_VERIFY(f.service->snapshot().user.contains("sources"));
        QVERIFY(!saved(f).first().toObject().value("enabled").toBool());
    }
    void cleanupAwaitsResults()
    {
        Fixture f; QVERIFY(f.start()); auto *cache = f.controller->cache();
        cache->prepareCleanup(); QCOMPARE(cache->cleanup().value("state").toString(), QString("pending"));
        QTRY_COMPARE(cache->cleanup().value("state").toString(), QString("confirm"));
        QVERIFY(cache->cleanup().value("message").toString().contains("0 files"));
        const auto count = f.service->snapshot().jobs.size(); cache->dismissCleanup();
        QTest::qWait(100); QCOMPARE(f.service->snapshot().jobs.size(), count);
        cache->prepareCleanup(); QTRY_COMPARE(cache->cleanup().value("state").toString(), QString("confirm"));
        cache->confirmCleanup(); QTRY_COMPARE(cache->cleanup().value("state").toString(), QString("result"));
        QVERIFY(cache->cleanup().value("message").toString().startsWith("Removed "));
        QVERIFY(cache->activity().size() >= 3);
    }
    void cleanupDismissalDoesNotLoseAcceptedWork()
    {
        Fixture f; QVERIFY(f.start()); auto *cache = f.controller->cache();
        cache->prepareCleanup(); QCOMPARE(cache->cleanup().value("state").toString(), QString("pending"));
        cache->dismissCleanup(); QVERIFY(cache->cleanup().value("state").toString().isEmpty()
            || (cache->cleanup().contains("visible") && !cache->cleanup().value("visible").toBool()));
        QTRY_COMPARE(cache->cleanup().value("state").toString(), QString("confirm"));
        QVERIFY(!cache->cleanup().value("visible").toBool()); QVERIFY(cache->cleanup().value("message").toString().contains("0 files"));
        cache->prepareCleanup(); QVERIFY(cache->cleanup().value("visible").toBool());
        QTRY_COMPARE(cache->cleanup().value("state").toString(), QString("confirm"));
    }
    void themeDiscoveryTracksARecreatedConfigDirectory()
    {
        Fixture f; QVERIFY(f.start());
        const auto parent = QFileInfo(f.paths.config).absolutePath(); QVERIFY(QDir().rename(parent, parent + "-old"));
        ThemeCatalog catalog(*f.service); const auto count = catalog.rows().size();
        QVERIFY(!QFileInfo::exists(parent)); QVERIFY(QDir().mkpath(parent + "/themes"));
        QVERIFY(QFile::copy(parent + "-old/config.json", f.paths.config));
        QFile theme(parent + "/themes/Fresh.qml"); QVERIFY(theme.open(QIODevice::WriteOnly)); theme.write("not evaluated"); theme.close();
        QTRY_COMPARE(catalog.rows().size(), count + 1); QCOMPARE(catalog.rows().last().toMap().value("name").toString(), QString("Fresh.qml"));
    }
    void themeOverridesAreReadOnly()
    {
        Fixture f; QVERIFY(f.start()); auto *catalog = f.controller->themes();
        QTRY_COMPARE(catalog->rows().size(), 5);
        QCOMPARE(catalog->rows().first().toMap().value("name").toString(), QString("Macintosh.qml"));
        QVERIFY(!catalog->rows().first().toMap().value("preview").toString().isEmpty());
        const auto dir = QFileInfo(f.paths.config).absolutePath() + "/themes"; QVERIFY(QDir().mkpath(dir));
        QFile malicious(dir + "/Macintosh.qml"); QVERIFY(malicious.open(QIODevice::WriteOnly)); malicious.write("not QML - never evaluate me"); malicious.close();
        QTRY_VERIFY(catalog->rows().first().toMap().value("user").toBool());
        QCOMPARE(catalog->rows().first().toMap().value("preview").toString(), QString());
        QVERIFY(QDir().mkpath(dir + "/previews"));
        QImage image(16, 10, QImage::Format_RGB32); image.fill(Qt::blue); QVERIFY(image.save(dir + "/previews/Macintosh.png"));
        QTRY_VERIFY(!catalog->rows().first().toMap().value("preview").toString().isEmpty());
        QVERIFY(catalog->rows().first().toMap().value("preview").toString().startsWith("data:image/png;base64,"));
        const auto before = catalog->rows().first().toMap().value("preview").toString();
        image.fill(Qt::red); QVERIFY(image.save(dir + "/previews/Macintosh.png"));
        // A retained URL still decodes the validated blue snapshot after the
        // mutable pathname changes, even before the filesystem watcher runs.
        QCOMPARE(QImage::fromData(QByteArray::fromBase64(before.section(',', 1).toLatin1())).pixelColor(0, 0), QColor(Qt::blue));
        QTRY_VERIFY(catalog->rows().first().toMap().value("preview").toString() != before);
        QFile broken(dir + "/previews/Macintosh.png"); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("broken"); broken.close();
        QTRY_COMPARE(catalog->rows().first().toMap().value("preview").toString(), QString());
        f.controller->edit("theme", "Enfield.qml"); QTRY_VERIFY(!f.control("theme").value("pending").toBool());
        QCOMPARE(f.control("theme").value("value").toString(), QString("Enfield.qml"));
    }
    void previewsHaveOneCatalogBudget()
    {
        Fixture f; QVERIFY(f.start()); auto *catalog = f.controller->themes();
        const auto dir = QFileInfo(f.paths.config).absolutePath() + "/themes"; QVERIFY(QDir().mkpath(dir + "/previews"));
        QFile theme(dir + "/Macintosh.qml"); QVERIFY(theme.open(QIODevice::WriteOnly)); theme.write("not evaluated"); theme.close();
        QImage image(2048, 2048, QImage::Format_RGB32); image.fill(Qt::blue); QVERIFY(image.save(dir + "/previews/Macintosh.png"));
        QTRY_VERIFY(catalog->rows().first().toMap().value("user").toBool());
        QCOMPARE(catalog->rows().first().toMap().value("preview").toString(), QString());
    }
    void encodedPreviewsHaveOneCatalogBudget()
    {
        Fixture f; QVERIFY(f.start()); auto *catalog = f.controller->themes();
        const auto dir = QFileInfo(f.paths.config).absolutePath() + "/themes"; QVERIFY(QDir().mkpath(dir + "/previews"));
        QImage image(1024, 512, QImage::Format_RGB32); quint32 random = 1;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
            random = random * 1664525 + 1013904223; image.setPixel(x, y, 0xff000000 | (random >> 8));
        }
        for (int i = 0; i < 8; ++i) {
            const auto name = QString("Large%1").arg(i); QFile theme(dir + '/' + name + ".qml"); QVERIFY(theme.open(QIODevice::WriteOnly)); theme.write("not evaluated"); theme.close();
            QVERIFY(image.save(dir + "/previews/" + name + ".png"));
            if (i == 0) {
                QTRY_COMPARE(catalog->rows().size(), 6);
                QTRY_VERIFY(!catalog->rows().last().toMap().value("preview").toString().isEmpty());
            }
        }
        QTRY_COMPARE(catalog->rows().size(), 13);
        qint64 retained = 0; for (const auto &row : catalog->rows()) retained += row.toMap().value("preview").toString().size() * 2;
        QVERIFY(retained > 0); QVERIFY2(retained <= 8 * 1024 * 1024, qPrintable(QString::number(retained)));
    }
    void nonRegularPreviewDoesNotBlock()
    {
        Fixture f; QVERIFY(f.start()); const auto dir = QFileInfo(f.paths.config).absolutePath() + "/themes";
        QVERIFY(QDir().mkpath(dir + "/previews")); QFile theme(dir + "/Macintosh.qml"); QVERIFY(theme.open(QIODevice::WriteOnly)); theme.close();
        QVERIFY(::mkfifo(qPrintable(dir + "/previews/Macintosh.png"), 0600) == 0);
        QTRY_VERIFY(f.controller->themes()->rows().first().toMap().value("user").toBool());
        QCOMPARE(f.controller->themes()->rows().first().toMap().value("preview").toString(), QString());
    }
};
QTEST_GUILESS_MAIN(PagesTest)
#include "pages.moc"
