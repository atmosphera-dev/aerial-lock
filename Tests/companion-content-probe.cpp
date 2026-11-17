#include "../common/content-snapshot.h"
#include "../companion/cache-store.h"
#include "../companion/source-resolver.h"
#include "../companion/acquisition-planner.h"
#include "../native/local-content.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>
#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

using namespace Aerial;

static Representation representation(const QString &name)
{
    Representation rep;
    rep.key = contentDigest(name.toUtf8()); rep.sourceId = "fixture"; rep.assetId = name;
    rep.url = QUrl("http://localhost/" + name); rep.height = 1080;
    rep.metadata = {{"title", "Mountains"}};
    return rep;
}

static bool publish(CacheStore &cache, const Representation &rep, qint64 now)
{
    const int fd = cache.openPartial(rep.key, true);
    if (fd < 0) return false;
    const bool ok = cache.reserve(4096) && cache.append(fd, QByteArray(4096, 'x')) && fsync(fd) == 0;
    close(fd);
    return ok && cache.complete(rep, {{"initialFrameDecoded", true}}, now);
}

class ContentTests : public QObject {
    Q_OBJECT
private slots:
    void resolver()
    {
        const QJsonObject source{{"id", "fixture"}, {"type", "community"}, {"source", "http://localhost/pack"}};
        const QUrl base("http://localhost/pack/manifest.json");
        QCOMPARE(SourceResolver::manifestUrl(source), base);
        const auto wrapper = SourceResolver::parse(source, base, "{\"manifestUrl\":\"../entries.json\"}");
        QVERIFY(wrapper.valid()); QCOMPARE(wrapper.next, QUrl("http://localhost/entries.json"));
        const QJsonArray assets{
            QJsonObject{{"id", "../large"}, {"url-1080-H264", "large.mp4"}, {"url-4K-SDR", "large4k.mp4"},
                {"sizes", QJsonObject{{"url-1080-H264", 500}, {"url-4K-SDR", 900}}}},
            QJsonObject{{"id", "unknown"}, {"url-1080-H264", "unknown.mp4"}},
            QJsonObject{{"id", "small"}, {"url-1080-H264", "small.mp4"}, {"sizes", QJsonObject{{"url-1080-H264", 100}}},
                {"title", "Mountain"}, {"pointsOfInterest", QJsonObject{{"0", "Summit"}}}}};
        const auto bytes = QJsonDocument(QJsonObject{{"assets", assets}}).toJson();
        const auto manifest = SourceResolver::parse(source, base, bytes);
        QVERIFY(manifest.valid()); QCOMPARE(manifest.representations.size(), 4);
        const auto automatic = SourceResolver::select(manifest.representations, source, 2160, {1080}, false);
        QCOMPARE(automatic.size(), 3); QCOMPARE(automatic[0].assetId, "small");
        QCOMPARE(automatic[1].assetId, "../large"); QCOMPARE(automatic[2].bytes, -1);
        for (const auto &rep : automatic) { QCOMPARE(rep.height, 1080); QVERIFY(contentKey(rep.key)); }
        QVERIFY(SourceResolver::select(manifest.representations, source, 0, {}, false).isEmpty());
        const auto manual = SourceResolver::select(manifest.representations, source, 2160, {}, true);
        QCOMPARE(manual[1].height, 2160);
        auto band = source; band["maxResolution"] = 1080;
        for (const auto &rep : SourceResolver::select(manifest.representations, band, 2160, {}, true)) QCOMPARE(rep.height, 1080);
        const auto meta = automatic[0].metadata;
        QCOMPARE(meta.value("pointsOfInterest").toObject().value("0").toString(), "Summit");
        QVERIFY(!SourceResolver::parse(source, QUrl("https://localhost/m.json"), "{\"manifestUrl\":\"http://localhost/down\"}").valid());
        QVERIFY(!SourceResolver::remoteUrl(QUrl("file:///etc/passwd")));
        for (const auto &reference : {"", "  "}) {
            const auto invalid = QJsonDocument(QJsonObject{{"assets", QJsonArray{QJsonObject{{"id", "empty"}, {"url-1080-H264", reference}}}}}).toJson();
            QVERIFY(!SourceResolver::parse(source, base, invalid).valid());
            const auto invalidWrapper = QJsonDocument(QJsonObject{{"manifestUrl", reference}}).toJson();
            QVERIFY(!SourceResolver::parse(source, base, invalidWrapper).valid());
            QVERIFY(SourceResolver::resolveRemote(base, QUrl(reference)).isEmpty());
        }
        QVERIFY(SourceResolver::resolveRemote(QUrl("https://localhost/manifest"), QUrl("http://localhost/clip")).isEmpty());
        QCOMPARE(SourceResolver::resolveRemote(base, QUrl("clip.mp4")), QUrl("http://localhost/pack/clip.mp4"));
    }

    void selectedTarMember()
    {
        const QJsonObject source{{"id", "fixture"}};
        const QByteArray json("{\"assets\":[{\"id\":\"one\",\"url-1080-H264\":\"one.mp4\"}]}");
        for (const auto &name : {"entries.json", "./entries.json", "../entries.json", "nested/entries.json"}) {
            QByteArray tar(16384, 0); size_t size = 0;
            archive *writer = archive_write_new();
            archive_write_set_format_ustar(writer);
            QCOMPARE(archive_write_open_memory(writer, tar.data(), tar.size(), &size), ARCHIVE_OK);
            auto *entry = archive_entry_new();
            archive_entry_set_pathname(entry, name); archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0600); archive_entry_set_size(entry, json.size());
            QCOMPARE(archive_write_header(writer, entry), ARCHIVE_OK);
            QCOMPARE(archive_write_data(writer, json.data(), json.size()), json.size());
            archive_entry_free(entry); archive_write_close(writer); archive_write_free(writer); tar.resize(size);
            const auto manifest = SourceResolver::parse(source, QUrl("http://localhost/a.tar"), tar);
            QCOMPARE(manifest.valid(), QByteArray(name) == "entries.json" || QByteArray(name) == "./entries.json");
        }
    }

    void cacheAccountingAndOwnership()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        CacheStore cache(root.path() + "/cache"); QVERIFY(cache.valid()); cache.configure(16384, 0);
        CacheStore duplicate(root.path() + "/cache"); QVERIFY(!duplicate.valid());
        for (const auto cap : {quint64(0), quint64(1)}) {
            CacheStore tiny(root.path() + "/tiny-" + QString::number(cap), cap);
            QVERIFY(!tiny.valid()); QCOMPARE(tiny.usage().bytes, quint64(0));
        }
        QVERIFY(publish(cache, representation("first"), 1));
        const auto usage = cache.usage(); QVERIFY(usage.safe); QVERIFY(usage.bytes >= 4096); QCOMPARE(usage.reserved, quint64(0));
        const auto capacity = quint64(16384) - usage.bytes;
        QVERIFY(cache.reserve(capacity)); QVERIFY(!cache.canWrite(1));
        QVERIFY(cache.canWrite(1, true)); cache.releaseReservation();
        QVERIFY(!cache.reserve(capacity + 1));
        cache.configure(0, 0); QVERIFY(!cache.canWrite(1)); QVERIFY(!cache.reserve(1));
        cache.configure(1000000, 0); QVERIFY(cache.writeMetadata("manifests/one.json", {{"data", "metadata counts"}}));
        QVERIFY(cache.usage().bytes > usage.bytes);
        const auto personal = root.path() + "/personal";
        QDir().mkdir(personal); QFile file(personal + "/keep"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("mine"); file.close();
        CacheStore unowned(personal); QVERIFY(!unowned.valid()); QVERIFY(QFileInfo::exists(personal + "/keep"));
        QVERIFY(QFile::link(personal, root.path() + "/cache/escape"));
        QVERIFY(!cache.usage().safe); QVERIFY(!cache.canWrite(1));
        QVERIFY(!cache.files().replace("escape/keep", "changed"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), "mine");
    }

    void snapshotPinningAndRestart()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        const auto directory = root.path() + "/cache";
        auto cache = std::make_unique<CacheStore>(directory); QVERIFY(cache->valid()); cache->configure(1000000, 0);
        QVERIFY(publish(*cache, representation("first"), 1));
        auto snapshot = std::make_unique<ContentSnapshot>(directory); QVERIFY(snapshot->valid());
        QCOMPARE(snapshot->entries().size(), 1); const auto frozen = snapshot->generation();
        QVERIFY(publish(*cache, representation("second"), 2));
        QCOMPARE(snapshot->entries().size(), 1); QCOMPARE(snapshot->generation(), frozen);
        auto respawn = std::make_unique<ContentSnapshot>(directory, frozen);
        QVERIFY(respawn->valid()); QCOMPARE(respawn->entries().size(), 1);
        QCOMPARE(respawn->generation(), frozen);
        ContentSnapshot frozenEmpty(directory, QString());
        QVERIFY(!frozenEmpty.valid()); QVERIFY(frozenEmpty.entries().isEmpty());
        QVERIFY(cache->prune(false).value("protected").toBool());
        cache.reset(); cache = std::make_unique<CacheStore>(directory); QVERIFY(cache->valid()); cache->configure(1000000, 0);
        QVERIFY(cache->prune(false).value("protected").toBool());
        const QJsonArray sources{QJsonObject{{"id", "fixture"}, {"type", "community"}, {"name", "Renamed"}}};
        QCOMPARE(snapshot->forSources(sources).at(0).toObject().value("sourceName").toString(), "Renamed");
        QVERIFY(snapshot->forSources({}).isEmpty());
        snapshot.reset();
        // The respawned client also pins the original generation until exit.
        QVERIFY(cache->prune(false).value("protected").toBool());
        respawn.reset();
        QCOMPARE(cache->prune(true).value("removedBytes").toInteger(), 4096);
        QCOMPARE(cache->entries().size(), 2);
        QCOMPARE(cache->prune(false).value("removedBytes").toInteger(), 4096);
        QCOMPARE(cache->entries().size(), 1);
        ContentSnapshot next(directory); QVERIFY(next.valid()); QCOMPARE(next.entries().size(), 1);
        QCOMPARE(next.entries().at(0).toObject().value("assetId").toString(), "second");
        QVERIFY(ContentSnapshot::reportPlayback(root.path() + "/state", representation("second").key, 123));
        QCOMPARE(ContentSnapshot::lastPlayback(root.path() + "/state").value("playedAt").toInteger(), 123);
    }

    void nonblockingAndCorruption()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        CacheStore cache(root.path() + "/cache"); cache.configure(1000000, 0); QVERIFY(cache.valid());
        QVERIFY(publish(cache, representation("first"), 1));
        int lock = cache.files().open("playback.lock", O_RDWR); QVERIFY(lock >= 0); QCOMPARE(flock(lock, LOCK_EX | LOCK_NB), 0);
        ContentSnapshot contended(cache.path()); QVERIFY(!contended.valid()); QVERIFY(contended.entries().isEmpty()); close(lock);
        QVERIFY(cache.files().replace("current.json", "bad"));
        ContentSnapshot corrupt(cache.path()); QVERIFY(!corrupt.valid()); QVERIFY(corrupt.entries().isEmpty());
        QVERIFY(!cache.files().replace("../escape", "bad"));
        QVERIFY(!cache.files().replace("/absolute", "bad"));
    }

    void loweredCapReclamation()
    {
        QTemporaryDir root; CacheStore cache(root.path() + "/cache"); cache.configure(1000000, 0);
        QVERIFY(publish(cache, representation("first"), 1));
        QVERIFY(publish(cache, representation("second"), 2));
        auto reader = std::make_unique<ContentSnapshot>(cache.path()); QVERIFY(reader->valid());
        const auto original = cache.usage().bytes;
        cache.configure(0, 0);
        QVERIFY(cache.prune(false).value("protected").toBool()); QCOMPARE(cache.usage().bytes, original);
        QVERIFY(!cache.canWrite(1));
        reader.reset();
        QCOMPARE(cache.prune(false).value("removedBytes").toInteger(), 4096);
        QCOMPARE(cache.entries().size(), 1); QVERIFY(cache.usage().bytes < original);
        QVERIFY(!cache.canWrite(1));
        ContentSnapshot fallback(cache.path()); QVERIFY(fallback.valid());
        QCOMPARE(fallback.entries()[0].toObject().value("assetId").toString(), "second");
    }

    void nativeEpisodeGeneration()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        CacheStore cache(root.path() + "/cache"); cache.configure(1000000, 0); QVERIFY(cache.valid());
        QVERIFY(publish(cache, representation("first"), 1));
        ContentSnapshot supervisor(cache.path()); QVERIFY(supervisor.valid());
        QVERIFY(publish(cache, representation("second"), 2));
        const QVariantList sources{QVariantMap{{"id", "fixture"}, {"type", "community"}, {"name", "Fixture"}}};
        qputenv("AERIAL_LOCK_CONTENT_ROOT", cache.path().toUtf8());
        qputenv("AERIAL_LOCK_CONTENT_GENERATION", supervisor.generation().toUtf8());
        LocalContent child;
        child.freeze(cache.path(), root.path() + "/state", sources);
        QCOMPARE(child.generation(), supervisor.generation());
        QCOMPARE(child.forSource("fixture").size(), 1);
        LocalContent respawn;
        respawn.freeze(cache.path(), root.path() + "/state", sources);
        QCOMPARE(respawn.forSource("fixture").size(), 1);
        qputenv("AERIAL_LOCK_CONTENT_GENERATION", "");
        LocalContent emptyEpisode;
        emptyEpisode.freeze(cache.path(), root.path() + "/state", sources);
        QVERIFY(emptyEpisode.forSource("fixture").isEmpty());
        qputenv("AERIAL_LOCK_CONTENT_GENERATION", supervisor.generation().toUtf8());
        qputenv("AERIAL_LOCK_CONTENT_ROOT", "different-profile");
        LocalContent mismatch;
        mismatch.freeze(cache.path(), root.path() + "/state", sources);
        QVERIFY(mismatch.forSource("fixture").isEmpty());
        qunsetenv("AERIAL_LOCK_CONTENT_ROOT"); qunsetenv("AERIAL_LOCK_CONTENT_GENERATION");
        LocalContent nextEpisode;
        nextEpisode.freeze(cache.path(), root.path() + "/state", sources);
        QCOMPARE(nextEpisode.forSource("fixture").size(), 2);
    }

    void plannerDurability()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        qint64 now = 100000;
        const auto clock = [&now] { return now; };
        auto state = std::make_unique<PlannerState>(root.path(), clock, [] { return 0; });
        QVERIFY(state->valid()); QVERIFY(state->initializeRotation());
        PlannerState duplicate(root.path(), clock, [] { return 0; }); QVERIFY(!duplicate.valid());
        const auto key = representation("first").key;
        QVERIFY(state->failed(key, "HttpError", 429, "120"));
        QCOMPARE(state->nextAttempt(key), now + 120); QVERIFY(!state->eligible(key));
        QVERIFY(state->reconcile({key}, {})); // Repeated ensure does not erase retry.
        const auto rotation = state->lastRotation();
        state.reset(); state = std::make_unique<PlannerState>(root.path(), clock, [] { return 0; });
        QVERIFY(state->valid()); QCOMPARE(state->nextAttempt(key), now + 120);
        QVERIFY(state->initializeRotation()); QCOMPARE(state->lastRotation(), rotation);
        now += 120; QVERIFY(state->eligible(key));
        QVERIFY(state->failed(key, "TransportError")); QCOMPARE(state->nextAttempt(key), now + 60);
        const auto httpDate = QDateTime::fromSecsSinceEpoch(now + 600, QTimeZone::UTC).toString(Qt::RFC2822Date).toLatin1();
        QVERIFY(state->failed(key, "HttpError", 503, httpDate)); QCOMPARE(state->nextAttempt(key), now + 600);
        QVERIFY(state->failed(key, "HttpError", 404)); QVERIFY(!state->eligible(key));
        now += 86400 * 30; QVERIFY(!state->eligible(key));
        QVERIFY(state->clearFailure(key)); QVERIFY(state->eligible(key));
        QVERIFY(state->rotated(key)); QVERIFY(state->retired(key));
        QVERIFY(state->recordPlayback(key, now - 1)); QCOMPARE(state->playedAt(key), now - 1);
        QVERIFY(state->recordPlayback(key, now - 100)); QCOMPARE(state->playedAt(key), now - 1);
        QVERIFY(!state->recordPlayback(key, now + 1));
        QVERIFY(state->reconcile({}, {})); QVERIFY(!state->retired(key)); QCOMPARE(state->playedAt(key), 0);
        QVERIFY(!state->failed("unsafe-path", "TransportError"));
    }

    void plannerSelectionAndCadence()
    {
        QFile defaults(QStringLiteral(AERIAL_TEST_SOURCE) + "/Config/defaults.json");
        QVERIFY(defaults.open(QIODevice::ReadOnly));
        auto settings = QJsonDocument::fromJson(defaults.readAll()).object();
        const QJsonObject source{{"id", "fixture"}, {"type", "community"}};
        settings["sources"] = QJsonArray{source};
        QTemporaryDir root; QVERIFY(root.isValid()); qint64 now = 100000;
        auto state = std::make_unique<PlannerState>(root.path(), [&now] { return now; }, [] { return 0; });
        QVERIFY(state->initializeRotation());
        auto large = representation("large"), small = representation("small"), unknown = representation("unknown");
        large.bytes = 10000; small.bytes = 1000;
        const QList<Representation> catalogue{unknown, large, small};
        auto plan = AcquisitionPlanner::evaluate(settings, catalogue, {}, *state, now);
        QCOMPARE(plan.priority, "critical"); QCOMPARE(plan.candidates.size(), 3);
        QCOMPARE(plan.candidates[0].key, small.key); QCOMPARE(plan.candidates[1].key, large.key); QCOMPARE(plan.candidates[2].bytes, -1);
        const QJsonArray complete{QJsonObject{{"key", small.key}, {"sourceId", "fixture"}, {"completedAt", now}}};
        now += 3600;
        plan = AcquisitionPlanner::evaluate(settings, catalogue, complete, *state, now);
        QCOMPARE(plan.priority, "variety"); QVERIFY(plan.rotationVictim.isEmpty());
        QCOMPARE(plan.candidates.size(), 2); QCOMPARE(plan.candidates[0].key, large.key);
        now += 7 * 86400;
        plan = AcquisitionPlanner::evaluate(settings, catalogue, complete, *state, now);
        QCOMPARE(plan.rotationVictim, small.key);
        QVERIFY(state->rotated(small.key));
        const QJsonArray replacement{QJsonObject{{"key", large.key}, {"sourceId", "fixture"}, {"completedAt", now}}};
        plan = AcquisitionPlanner::evaluate(settings, {small, large}, replacement, *state, now + 3600);
        QVERIFY(plan.candidates.isEmpty()); // An evicted representation does not churn hourly.
        state.reset(); state = std::make_unique<PlannerState>(root.path(), [&now] { return now; }, [] { return 0; });
        plan = AcquisitionPlanner::evaluate(settings, {small, large}, replacement, *state, now + 3600);
        QVERIFY(plan.candidates.isEmpty());
        now += 7 * 86400;
        plan = AcquisitionPlanner::evaluate(settings, {small, large}, replacement, *state, now);
        QCOMPARE(plan.candidates.size(), 1); QCOMPARE(plan.candidates[0].key, small.key);
        auto never = settings.value("cache").toObject(); never["rotationDays"] = 0; settings["cache"] = never;
        QVERIFY(AcquisitionPlanner::evaluate(settings, {small, large}, replacement, *state, now).candidates.isEmpty());
        auto automatic = settings.value("acquisition").toObject().value("automatic").toObject();
        automatic["enabled"] = false; automatic["allowedResolutions"] = QJsonArray{};
        auto acquisition = settings.value("acquisition").toObject(); acquisition["automatic"] = automatic; settings["acquisition"] = acquisition;
        QCOMPARE(AcquisitionPlanner::evaluate(settings, catalogue, {}, *state, now).deferReason, "Paused");
        QCOMPARE(AcquisitionPlanner::evaluate(settings, catalogue, {}, *state, now, true).candidates.size(), 3);
        auto band = source; band["minResolution"] = 2160; settings["sources"] = QJsonArray{band};
        QVERIFY(AcquisitionPlanner::evaluate(settings, catalogue, {}, *state, now, true).candidates.isEmpty());
        settings["sources"] = QJsonArray{};
        QCOMPARE(AcquisitionPlanner::evaluate(settings, catalogue, {}, *state, now, true).deferReason, "NoRemoteSources");
    }
};

QTEST_GUILESS_MAIN(ContentTests)
#include "companion-content-probe.moc"
