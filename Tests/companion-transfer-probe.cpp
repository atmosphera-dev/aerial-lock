#include "../companion/transfer.h"
#include "../companion/manifest-store.h"
#include "../common/content-snapshot.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <fcntl.h>
#include <unistd.h>

using namespace Aerial;

class HttpFixture : public QTcpServer {
public:
    QByteArray media;
    QList<QByteArray> requests;
    QMap<QByteArray, QByteArray> bodies;
    QString mode = "full";
    HttpFixture()
    {
        if (!listen(QHostAddress::LocalHost)) qFatal("fixture bind failed");
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                    *buffer += socket->readAll();
                    if (!buffer->contains("\r\n\r\n") || socket->property("answered").toBool()) return;
                    socket->setProperty("answered", true);
                    requests.append(*buffer);
                    const auto target = buffer->split(' ').value(1);
                    if (mode == "hold") return;
                    QByteArray status = "200 OK", body = bodies.value(target, media), extra = "ETag: \"fixture-one\"\r\n";
                    if (mode == "conditional" && buffer->contains("If-None-Match:")) { status = "304 Not Modified"; body.clear(); }
                     if (target == "/redirect") {
                         status = "302 Found"; body.clear(); extra = "Location: /clip\r\n";
                     } else if (target == "/emptyredirect") {
                         status = "302 Found"; body.clear(); extra = "Location: \r\n";
                    } else if (target == "/unsafe") {
                        status = "302 Found"; body.clear(); extra = "Location: file:///etc/passwd\r\n";
                    } else if (mode == "206" || mode == "validator-change" || mode == "missing-validator" || mode == "416") {
                        const QRegularExpression range("Range: bytes=([0-9]+)-", QRegularExpression::CaseInsensitiveOption);
                        const auto match = range.match(QString::fromLatin1(*buffer));
                        if (match.hasMatch()) {
                            const auto offset = match.captured(1).toLongLong();
                            if (mode == "416") {
                                status = "416 Range Not Satisfiable"; body.clear();
                                extra += "Content-Range: bytes */" + QByteArray::number(media.size()) + "\r\n";
                            } else {
                                status = "206 Partial Content"; body = media.mid(offset);
                                if (mode == "validator-change") extra = "ETag: \"fixture-two\"\r\n";
                                if (mode == "missing-validator") extra.clear();
                                extra += "Content-Range: bytes " + QByteArray::number(offset) + '-'
                                    + QByteArray::number(media.size() - 1) + '/' + QByteArray::number(media.size()) + "\r\n";
                            }
                        }
                    }
                    const auto declared = body.size() + (mode == "lying-length" ? 1 : 0);
                    const bool head = buffer->startsWith("HEAD ");
                    if (head) body.clear();
                    if (mode == "interrupted") body = body.left(body.size() / 2);
                    if (mode == "chunked") {
                        socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nTransfer-Encoding: chunked\r\n" + extra + "\r\n");
                        socket->write(QByteArray::number(body.size(), 16) + "\r\n" + body + "\r\n0\r\n\r\n");
                    } else if (head && mode == "head-unknown") {
                        socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\n" + extra + "\r\n");
                    } else {
                        socket->write("HTTP/1.1 " + status + "\r\nConnection: close\r\nContent-Length: "
                            + QByteArray::number(declared) + "\r\n" + extra + "\r\n");
                        socket->write(body);
                    }
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QUrl url(const QString &path = "/clip") const { return QUrl(QString("http://127.0.0.1:%1%2").arg(serverPort()).arg(path)); }
};

class TransferTests : public QObject {
    Q_OBJECT
public:
    QString probe;
    QByteArray media;
    std::unique_ptr<QTemporaryDir> root;
    Representation rep(const QUrl &url) const
    {
        Representation value;
        value.sourceId = "fixture"; value.assetId = "clip"; value.url = url;
        value.height = 1080; value.variant = "url-1080-H264";
        value.key = contentDigest(url.toEncoded()); value.bytes = media.size();
        return value;
    }
private slots:
    void initTestCase()
    {
        root = std::make_unique<QTemporaryDir>(); QVERIFY(root->isValid());
        QProcess ffmpeg;
        const auto path = root->path() + "/fixture.mp4";
        ffmpeg.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=96x64:rate=10",
            "-frames:v", "12", "-c:v", "libx264", "-movflags", "+faststart", "-y", path});
        QVERIFY(ffmpeg.waitForFinished(20000)); QCOMPARE(ffmpeg.exitCode(), 0);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); media = file.readAll(); QVERIFY(media.size() > 1000);
    }
    void fullAndProbe()
    {
        QTemporaryDir directory; CacheStore cache(directory.path() + "/cache"); QVERIFY(cache.valid()); cache.configure(1000000, 0);
        HttpFixture server; server.media = media; QNetworkAccessManager transport;
        auto representation = rep(server.url()); representation.checksum = contentDigest(media).toLatin1();
        Transfer transfer(cache, transport, representation, probe, {}, [] { return 123; });
        QSignalSpy done(&transfer, &Transfer::finished); transfer.start();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
        QVERIFY2(transfer.error().isEmpty(), qPrintable(transfer.error()));
        QVERIFY(cache.contains(representation.key));
        ContentSnapshot snapshot(cache.path()); QVERIFY(snapshot.valid()); QCOMPARE(snapshot.entries().size(), 1);
        const auto entry = snapshot.entries()[0].toObject();
        QCOMPARE(entry.value("assetId").toString(), "clip");
        QVERIFY(entry.value("probe").toObject().value("initialFrameDecoded").toBool());
        QCOMPARE(entry.value("probe").toObject().value("width").toInt(), 96);
        QFile file(entry.value("path").toString()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), media);
    }
    void resume_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::newRow("range206") << QString("206");
        QTest::newRow("restart200") << QString("full");
        QTest::newRow("changed-validator") << QString("validator-change");
        QTest::newRow("missing-validator") << QString("missing-validator");
    }
    void resume()
    {
        QFETCH(QString, mode);
        QTemporaryDir directory; auto cache = std::make_unique<CacheStore>(directory.path() + "/cache");
        QVERIFY(cache->valid()); cache->configure(1000000, 0);
        HttpFixture server; server.media = media; server.mode = "interrupted"; QNetworkAccessManager transport;
        const auto representation = rep(server.url());
        {
            Transfer transfer(*cache, transport, representation, probe, {}, [] { return 123; });
            QSignalSpy done(&transfer, &Transfer::finished); transfer.start();
            QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
            QVERIFY(!transfer.error().isEmpty()); QVERIFY(!cache->contains(representation.key));
            QCOMPARE(transfer.received(), media.size() / 2);
        }
        // Reconcile persisted partials with a new owner, not process memory.
        cache.reset(); cache = std::make_unique<CacheStore>(directory.path() + "/cache"); QVERIFY(cache->valid()); cache->configure(1000000, 0);
        server.mode = mode;
        {
            Transfer transfer(*cache, transport, representation, probe, {}, [] { return 124; });
            QSignalSpy done(&transfer, &Transfer::finished); transfer.start();
            QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
            QVERIFY(server.requests.last().contains("Range: bytes=" + QByteArray::number(media.size() / 2) + '-'));
            QVERIFY(server.requests.last().contains("If-Range: \"fixture-one\""));
            if (mode == "206" || mode == "full") QVERIFY2(transfer.error().isEmpty(), qPrintable(transfer.error()));
            else { QCOMPARE(transfer.error(), "InvalidRange"); QVERIFY(!cache->contains(representation.key)); }
        }
        if (mode != "206" && mode != "full") {
            server.mode = "full";
            Transfer retry(*cache, transport, representation, probe, {}, [] { return 125; });
            QSignalSpy done(&retry, &Transfer::finished); retry.start(); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
            QVERIFY2(retry.error().isEmpty(), qPrintable(retry.error())); QVERIFY(!server.requests.last().contains("Range:"));
        }
        QCOMPARE(cache->files().read("objects/" + representation.key + ".media", 1000000), media);
    }
    void unknownSizeCapAndValidation()
    {
        QTemporaryDir directory; CacheStore cache(directory.path() + "/cache"); QVERIFY(cache.valid());
        const auto baseline = cache.usage().bytes; cache.configure(baseline + 2000, 0);
        HttpFixture server; server.media = QByteArray(100000, 'x'); server.mode = "chunked";
        QNetworkAccessManager transport; auto representation = rep(server.url()); representation.bytes = -1;
        Transfer transfer(cache, transport, representation, probe, {}, [] { return 123; });
        QSignalSpy done(&transfer, &Transfer::finished); transfer.start(); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
        QCOMPARE(transfer.error(), "CacheFull"); QVERIFY(cache.usage().bytes <= baseline + 2000); QVERIFY(cache.entries().isEmpty());
        server.mode = "full"; cache.configure(1000000, 0);
        Transfer invalid(cache, transport, representation, probe, {}, [] { return 124; });
        QSignalSpy invalidDone(&invalid, &Transfer::finished); invalid.start(); QTRY_COMPARE_WITH_TIMEOUT(invalidDone.size(), 1, 20000);
        QCOMPARE(invalid.error(), "ProbeFailed"); QVERIFY(cache.entries().isEmpty());
        server.media = media;
        representation.checksum = QByteArray(64, '0');
        Transfer checksum(cache, transport, representation, probe, {}, [] { return 124; });
        QSignalSpy checksumDone(&checksum, &Transfer::finished); checksum.start(); QTRY_COMPARE_WITH_TIMEOUT(checksumDone.size(), 1, 20000);
        QCOMPARE(checksum.error(), "ChecksumMismatch"); QVERIFY(cache.entries().isEmpty());
    }
    void completeRange416()
    {
        for (const auto complete : {false, true}) {
            QTemporaryDir directory; CacheStore cache(directory.path() + "/cache"); cache.configure(1000000, 0); QVERIFY(cache.valid());
            HttpFixture server; server.media = media; server.mode = "416"; QNetworkAccessManager network;
            const auto representation = rep(server.url());
            const auto bytes = complete ? media : media.left(media.size() / 2);
            const int file = cache.openPartial(representation.key, true); QVERIFY(file >= 0);
            QVERIFY(cache.reserve(bytes.size())); QVERIFY(cache.append(file, bytes)); QCOMPARE(fsync(file), 0); close(file);
            cache.releaseReservation();
            QVERIFY(cache.writeMetadata("partials/" + representation.key + ".json", {{"version", 1}, {"key", representation.key},
                {"url", representation.url.toString(QUrl::FullyEncoded)}, {"validator", "\"fixture-one\""},
                {"received", bytes.size()}, {"total", media.size()}}));
            Transfer transfer(cache, network, representation, probe, {}, [] { return 123; });
            QSignalSpy done(&transfer, &Transfer::finished); transfer.start(); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
            QVERIFY(server.requests.last().contains("Range: bytes=" + QByteArray::number(bytes.size()) + '-'));
            if (complete) {
                QVERIFY2(transfer.error().isEmpty(), qPrintable(transfer.error())); QVERIFY(cache.contains(representation.key));
                QCOMPARE(cache.files().read("objects/" + representation.key + ".media", 1000000), media);
            } else { QCOMPARE(transfer.error(), "InvalidRange"); QVERIFY(cache.entries().isEmpty()); }
        }
    }
    void changedUrlAndLyingLength()
    {
        QTemporaryDir directory; CacheStore cache(directory.path() + "/cache"); cache.configure(1000000, 0); QVERIFY(cache.valid());
        HttpFixture server; server.media = media; server.mode = "interrupted"; QNetworkAccessManager network;
        const auto previous = rep(server.url("/old"));
        {
            Transfer transfer(cache, network, previous, probe, {}, [] { return 123; });
            QSignalSpy done(&transfer, &Transfer::finished); transfer.start(); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
            QVERIFY(!transfer.error().isEmpty()); QVERIFY(!cache.contains(previous.key));
        }
        server.mode = "full";
        const auto next = rep(server.url("/new")); QVERIFY(next.key != previous.key);
        Transfer changed(cache, network, next, probe, {}, [] { return 124; });
        QSignalSpy done(&changed, &Transfer::finished); changed.start(); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
        QVERIFY2(changed.error().isEmpty(), qPrintable(changed.error()));
        QVERIFY(!server.requests.last().contains("Range:"));
        QCOMPARE(cache.files().read("objects/" + next.key + ".media", 1000000), media);
        QVERIFY(cache.usage().partial > 0); // Old bytes are accounted, never reused for a new URL.
        server.mode = "lying-length";
        auto lying = rep(server.url("/lying")); lying.bytes = -1;
        Transfer mismatch(cache, network, lying, probe, {}, [] { return 125; });
        QSignalSpy mismatchDone(&mismatch, &Transfer::finished); mismatch.start(); QTRY_COMPARE_WITH_TIMEOUT(mismatchDone.size(), 1, 20000);
        QVERIFY(!mismatch.error().isEmpty()); QVERIFY(!cache.contains(lying.key)); QVERIFY(cache.contains(next.key));
    }
    void redirectsAndCancellation()
    {
        QTemporaryDir directory; CacheStore cache(directory.path() + "/cache"); QVERIFY(cache.valid()); cache.configure(1000000, 0);
        HttpFixture server; server.media = media; QNetworkAccessManager transport;
        Transfer transfer(cache, transport, rep(server.url("/redirect")), probe, {}, [] { return 123; });
        QSignalSpy done(&transfer, &Transfer::finished); transfer.start(); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
        QVERIFY2(transfer.error().isEmpty(), qPrintable(transfer.error())); QCOMPARE(server.requests.size(), 2);
        Transfer unsafe(cache, transport, rep(server.url("/unsafe")), probe, {}, [] { return 123; });
        QSignalSpy unsafeDone(&unsafe, &Transfer::finished); unsafe.start(); QTRY_COMPARE_WITH_TIMEOUT(unsafeDone.size(), 1, 20000);
        QCOMPARE(unsafe.error(), "UnsafeRedirect");
        Transfer empty(cache, transport, rep(server.url("/emptyredirect")), probe, {}, [] { return 123; });
        QSignalSpy emptyDone(&empty, &Transfer::finished); empty.start(); QTRY_COMPARE_WITH_TIMEOUT(emptyDone.size(), 1, 20000);
        QCOMPARE(empty.error(), "UnsafeRedirect");
        server.mode = "hold";
        Transfer held(cache, transport, rep(server.url("/hold")), probe, {}, [] { return 123; });
        QSignalSpy heldDone(&held, &Transfer::finished); held.start(); QTRY_COMPARE(server.requests.size(), 5);
        held.cancel("PolicyDeferred"); QCOMPARE(heldDone.size(), 1); QCOMPARE(held.error(), "PolicyDeferred");
    }
    void probeCrashAndPlaylist()
    {
        QTemporaryDir directory; CacheStore cache(directory.path() + "/cache"); QVERIFY(cache.valid()); cache.configure(1000000, 0);
        HttpFixture server; server.media = media; QNetworkAccessManager transport;
        Transfer failed(cache, transport, rep(server.url()), "/missing/probe", {}, [] { return 123; });
        QSignalSpy failedDone(&failed, &Transfer::finished); failed.start(); QTRY_COMPARE_WITH_TIMEOUT(failedDone.size(), 1, 20000);
        QCOMPARE(failed.error(), "ProbeFailed"); QVERIFY(cache.entries().isEmpty());
        QFile crash(directory.path() + "/crash-probe"); QVERIFY(crash.open(QIODevice::WriteOnly));
        crash.write("#!/bin/sh\nulimit -c 0\nkill -SEGV $$\n"); crash.close();
        QVERIFY(crash.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        Transfer crashed(cache, transport, rep(server.url()), crash.fileName(), {}, [] { return 123; });
        QSignalSpy crashedDone(&crashed, &Transfer::finished); crashed.start(); QTRY_COMPARE_WITH_TIMEOUT(crashedDone.size(), 1, 20000);
        QCOMPARE(crashed.error(), "ProbeFailed"); QVERIFY(cache.entries().isEmpty());
        server.media = "#EXTM3U\n#EXTINF:10\nhttp://127.0.0.1:9/must-not-open\n";
        auto representation = rep(server.url()); representation.bytes = -1;
        Transfer playlist(cache, transport, representation, probe, {}, [] { return 123; });
        QSignalSpy done(&playlist, &Transfer::finished); playlist.start(); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
        QCOMPARE(playlist.error(), "ProbeFailed"); QVERIFY(cache.entries().isEmpty());
    }
    void manifestsAndSmallestDiscovery()
    {
        QTemporaryDir directory; CacheStore cache(directory.path() + "/cache"); QVERIFY(cache.valid()); cache.configure(1000000, 0);
        HttpFixture server; QNetworkAccessManager transport;
        const QJsonObject source{{"id", "fixture"}, {"type", "community"}, {"source", server.url("/manifest.json").toString()}};
        server.bodies["/manifest.json"] = "{\"manifestUrl\":\"entries.json\"}";
        server.bodies["/entries.json"] = "{\"assets\":[{\"id\":\"large\",\"url-1080-H264\":\"large.mp4\"},{\"id\":\"small\",\"url-1080-H264\":\"small.mp4\"}]}";
        server.bodies["/large.mp4"] = QByteArray(9000, 'L'); server.bodies["/small.mp4"] = QByteArray(1000, 'S');
        ManifestStore store(cache, transport, {}); QSignalSpy refreshed(&store, &ManifestStore::finished);
        store.refresh(source); QTRY_COMPARE(refreshed.size(), 1); QVERIFY2(store.error().isEmpty(), qPrintable(store.error()));
        QCOMPARE(store.result().size(), 2); QCOMPARE(store.cached(source).size(), 2);
        const auto selected = SourceResolver::select(store.result(), source, 1080, {1080}, false);
        RepresentationSizer sizes(transport, {}); QSignalSpy sized(&sizes, &RepresentationSizer::finished);
        server.mode = "head-unknown"; sizes.start(selected); QTRY_COMPARE(sized.size(), 1);
        QCOMPARE(sizes.result()[0].assetId, "small"); QCOMPARE(sizes.result()[0].bytes, 1000);
        QCOMPARE(sizes.result()[1].assetId, "large"); QCOMPARE(sizes.result()[1].bytes, 9000);
        QCOMPARE(server.requests.size(), 6); QVERIFY(server.requests.last().contains("Range: bytes=0-0"));
        // A malformed refresh cannot destroy the validated last-good catalogue.
        server.mode = "full"; server.bodies["/entries.json"] = "not a manifest";
        store.refresh(source); QTRY_COMPARE(refreshed.size(), 2); QCOMPARE(store.error(), "InvalidManifest");
        QCOMPARE(store.cached(source).size(), 2);
        auto edited = source; edited["source"] = server.url("/changed.json").toString();
        QVERIFY(store.cached(edited).isEmpty());
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 4 && QByteArray(argv[1]) == "--export") {
        const auto root = QString::fromLocal8Bit(argv[2]);
        if (!root.startsWith('/') || !QDir().mkpath(root)) return 2;
        const auto mediaPath = root + "/fixture.mp4";
        QProcess encoder;
        encoder.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=96x64:rate=10",
            "-frames:v", "12", "-c:v", "libx264", "-movflags", "+faststart", "-y", mediaPath});
        if (!encoder.waitForFinished(20000) || encoder.exitCode()) return 1;
        QFile media(mediaPath); if (!media.open(QIODevice::ReadOnly)) return 1;
        HttpFixture server; server.media = media.readAll(); QNetworkAccessManager network;
        CacheStore cache(root + "/cache/aerial-lock/managed-v1"); if (!cache.valid()) return 1;
        cache.configure(1000000, 0);
        Representation rep; rep.sourceId = "fixture"; rep.assetId = "opaque/asset";
        rep.key = contentDigest("export-fixture"); rep.height = 1080; rep.url = server.url(); rep.bytes = server.media.size();
        rep.metadata = {{"title", "Mountain fixture"}, {"pointsOfInterest", QJsonObject{{"0", "Summit"}}}};
        Transfer transfer(cache, network, rep, QString::fromLocal8Bit(argv[3]), {}, [] { return 123; });
        QSignalSpy done(&transfer, &Transfer::finished); transfer.start();
        if (done.isEmpty() && !done.wait(20000)) return 1;
        if (!transfer.error().isEmpty()) return 1;
        OwnedDirectory config(root + "/config/aerial-lock", true);
        const QJsonObject source{{"id", "fixture"}, {"type", "community"}, {"name", "Fixture"}, {"source", server.url("/manifest.json").toString()}};
        return config.replace("config.json", QJsonDocument(QJsonObject{{"sources", QJsonArray{source}}}).toJson()) ? 0 : 1;
    }
    if (argc < 2) return 2;
    TransferTests tests; tests.probe = QString::fromLocal8Bit(argv[1]);
    QList<char *> arguments{argv[0]}; for (int i = 2; i < argc; ++i) arguments.append(argv[i]);
    return QTest::qExec(&tests, arguments.size(), arguments.data());
}
#include "companion-transfer-probe.moc"
