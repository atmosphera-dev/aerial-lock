#include "../companion/transfer.h"
#include "../companion/manifest-store.h"
#include "../companion/transfer-queue.h"
#include "../companion/policy-monitor.h"
#include "../common/content-snapshot.h"
#include <QCoreApplication>
#include <QDBusVirtualObject>
#include <QDBusMetaType>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QProcess>
#include <QSignalSpy>
#include <QSocketNotifier>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

using namespace Aerial;

class PolicyPeer : public QDBusVirtualObject {
public:
    const QString nm = "org.freedesktop.NetworkManager", up = "org.freedesktop.UPower";
    const QString manager = "/org/freedesktop/NetworkManager", power = "/org/freedesktop/UPower";
    const QString primary = manager + "/ActiveConnection/1", vpn = manager + "/ActiveConnection/2", device = manager + "/Devices/1";
    QDBusConnection bus;
    QMap<QString, QVariantMap> data;
    QList<QDBusMessage> held;
    QString hold;
    QStringList calls;
    PolicyPeer() : bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, "policy-fixture"))
    {
        data[manager] = {{"State", uint(70)}, {"Connectivity", uint(4)}, {"Metered", uint(2)},
            {"PrimaryConnection", QVariant::fromValue(QDBusObjectPath(primary))},
            {"ActiveConnections", QVariant::fromValue(QList<QDBusObjectPath>{QDBusObjectPath(primary)})}};
        data[primary] = {{"Type", "802-3-ethernet"}, {"State", uint(2)}, {"Default", true}, {"Vpn", false},
            {"Devices", QVariant::fromValue(QList<QDBusObjectPath>{QDBusObjectPath(device)})}};
        data[device] = {{"Metered", uint(2)}};
        data[power] = {{"OnBattery", false}};
        if (!bus.registerVirtualObject(manager, this, QDBusConnection::SubPath)
            || !bus.registerVirtualObject(power, this) || !bus.registerService(nm) || !bus.registerService(up)) qFatal("policy peer registration failed");
    }
    ~PolicyPeer()
    {
        bus.unregisterService(nm); bus.unregisterService(up);
        bus.unregisterObject(manager, QDBusConnection::UnregisterTree); bus.unregisterObject(power);
        QDBusConnection::disconnectFromBus("policy-fixture");
    }
    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        calls.append(message.interface() + "." + message.member());
        if (message.interface() != "org.freedesktop.DBus.Properties" || message.member() != "GetAll") {
            connection.send(message.createErrorReply(QDBusError::UnknownMethod, "read-only peer")); return true;
        }
        if (message.path() == hold) held.append(message);
        else connection.send(message.createReply(QVariantList{data.value(message.path())}));
        return true;
    }
    void update(const QString &path, const QVariantMap &patch, const QStringList &invalidated = {})
    {
        for (auto it = patch.begin(); it != patch.end(); ++it) data[path][it.key()] = it.value();
        for (const auto &key : invalidated) data[path].remove(key);
        auto message = QDBusMessage::createSignal(path, "org.freedesktop.DBus.Properties", "PropertiesChanged");
        const auto interface = path == manager ? nm : path == power ? up : path == device ? nm + ".Device" : nm + ".Connection.Active";
        message << interface << patch << invalidated;
        if (!bus.send(message)) qFatal("policy signal failed");
    }
};

class HttpFixture : public QTcpServer {
public:
    QByteArray media;
    QList<QByteArray> requests;
    QMap<QByteArray, QByteArray> bodies;
    QMap<QByteArray, QString> modes;
    QMap<QByteArray, QByteArray> redirects;
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
                    const auto mode = modes.value(target, this->mode);
                    if (mode == "hold" && !buffer->startsWith("HEAD ")) return;
                    QByteArray status = "200 OK", body = bodies.value(target, media), extra = "ETag: \"fixture-one\"\r\n";
                    if (mode == "retry") { status = "429 Too Many Requests"; body.clear(); extra += "Retry-After: 120\r\n"; }
                    if (mode == "conditional" && buffer->contains("If-None-Match:")) { status = "304 Not Modified"; body.clear(); }
                    if (target == "/redirect") {
                        status = "302 Found"; body.clear(); extra = "Location: /clip\r\n";
                    } else if (redirects.contains(target)) {
                        status = "302 Found"; body.clear(); extra = "Location: " + redirects.value(target) + "\r\n";
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
                    if (mode == "missing") { status = "404 Not Found"; body.clear(); }
                    const auto declared = body.size() + (mode == "lying-length" ? 1 : 0);
                    const bool head = buffer->startsWith("HEAD ");
                    if (head) body.clear();
                    if (mode == "interrupted") body = body.left(body.size() / 2);
                    if (mode == "hold-partial" && !head) {
                        socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: "
                            + QByteArray::number(declared) + "\r\n" + extra + "\r\n" + body.left(body.size() / 2));
                        return;
                    }
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
    ConfigPaths queueConfig(const QString &root, const QJsonArray &sources, bool enabled = false)
    {
        OwnedDirectory directory(root + "/config", true);
        const QJsonObject automatic{{"enabled", enabled}};
        if (!directory.replace("config.json", QJsonDocument(QJsonObject{{"sources", sources},
            {"cache", QJsonObject{{"maxBytes", 1000000}}},
            {"acquisition", QJsonObject{{"automatic", automatic}}}}).toJson())) qFatal("queue config write failed");
        return {QStringLiteral(AERIAL_TEST_SOURCE), root + "/config/config.json", root + "/cache", root + "/state", root + "/run"};
    }
    QJsonObject source(HttpFixture &server, const QString &id = "fixture")
    {
        return {{"id", id}, {"type", "community"}, {"name", id}, {"source", server.url("/" + id + ".json").toString()}};
    }
    QByteArray manifest(const QStringList &assets)
    {
        QJsonArray entries;
        for (const auto &id : assets) entries.append(QJsonObject{{"id", id}, {"url-1080-H264", id + ".mp4"}});
        return QJsonDocument(QJsonObject{{"assets", entries}}).toJson();
    }
    static bool ended(TransferQueue &queue, const QString &job) { return queue.job(job).contains("finishedAt"); }
    struct CommandResult { int code; QByteArray out, error; };
    CommandResult cli(const QProcessEnvironment &environment, const QStringList &arguments, bool legacy = false)
    {
        QProcess process; process.setProcessEnvironment(environment);
        process.start(QStringLiteral(AERIAL_TEST_SOURCE) + (legacy ? "/Scripts/aerial-lock-fetch" : "/cli/aerialctl"), arguments);
        if (!process.waitForStarted(5000)) qFatal("CLI failed to start");
        QElapsedTimer timer; timer.start();
        while (process.state() != QProcess::NotRunning && timer.elapsed() < 20000) QTest::qWait(10);
        if (process.state() != QProcess::NotRunning) qFatal("CLI did not finish: %s", qPrintable(arguments.join(' ')));
        return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
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
        auto representation = rep(server.url()); representation.checksum = contentDigest(media).toLatin1();
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
    void resumeThroughChangedRedirect()
    {
        QTemporaryDir directory; CacheStore cache(directory.path() + "/cache"); cache.configure(1000000, 0);
        HttpFixture server; server.media = media; QNetworkAccessManager network;
        server.redirects["/origin"] = "/expired-location"; server.modes["/expired-location"] = "interrupted";
        const auto representation = rep(server.url("/origin"));
        {
            Transfer transfer(cache, network, representation, probe, {}, [] { return 123; });
            QSignalSpy done(&transfer, &Transfer::finished); transfer.start(); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
            QVERIFY(!transfer.error().isEmpty()); QCOMPARE(transfer.received(), media.size() / 2);
        }
        QCOMPARE(cache.metadata("partials/" + representation.key + ".json").value("url").toString(), representation.url.toString(QUrl::FullyEncoded));
        server.requests.clear(); server.redirects["/origin"] = "/current-location";
        server.modes["/expired-location"] = "missing"; server.modes["/current-location"] = "206";
        Transfer resumed(cache, network, representation, probe, {}, [] { return 124; });
        QSignalSpy done(&resumed, &Transfer::finished); resumed.start(); QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
        QVERIFY2(resumed.error().isEmpty(), qPrintable(resumed.error()));
        QCOMPARE(server.requests.size(), 2);
        QVERIFY(server.requests.first().startsWith("GET /origin "));
        QVERIFY(server.requests.last().startsWith("GET /current-location "));
        QVERIFY(server.requests.last().contains("If-Range: \"fixture-one\""));
        QCOMPARE(cache.files().read("objects/" + representation.key + ".media", 1000000), media);
    }
    void queueRefreshAfterPermanentUrlFailure()
    {
        QTemporaryDir directory; HttpFixture server; server.media = media;
        server.bodies["/fixture.json"] = manifest({"old"}); server.modes["/old.mp4"] = "missing";
        const auto paths = queueConfig(directory.path(), {source(server)});
        ConfigRepository config(paths); CacheStore cache(paths.cache); QNetworkAccessManager network;
        qint64 now = 100000;
        PlannerState state(paths.state, [&now] { return now; }, [] { return 0; });
        TransferQueue queue(config, cache, state, network, probe, {}, [&now] { return now; }); queue.start();
        const auto failed = queue.acquire({}, 1); QTRY_VERIFY_WITH_TIMEOUT(ended(queue, failed), 20000);
        QVERIFY(cache.entries().isEmpty()); QVERIFY(!queue.job(failed).value("errors").toObject().isEmpty());
        const auto old = SourceResolver::parse(source(server), server.url("/fixture.json"), server.bodies["/fixture.json"]).representations.first();
        QVERIFY(!state.eligible(old.key));
        const auto countOldGets = [&] {
            int count = 0;
            for (const auto &request : server.requests) if (request.startsWith("GET /old.mp4 ")) ++count;
            return count;
        };
        const auto initialOldGets = countOldGets(); QVERIFY(initialOldGets > 0);
        // A later ordinary evaluation refreshes metadata, making a changed
        // representation eligible without clearing the permanent old URL retry.
        server.bodies["/fixture.json"] = manifest({"new"}); now += 3600;
        const auto refreshed = queue.acquire({}, 1); QTRY_VERIFY_WITH_TIMEOUT(ended(queue, refreshed), 20000);
        QCOMPARE(queue.job(refreshed).value("state").toString(), "succeeded");
        QCOMPARE(cache.entries().size(), 1); QCOMPARE(cache.entries()[0].toObject().value("assetId").toString(), "new");
        QCOMPARE(countOldGets(), initialOldGets); // Includes the bounded range size probe, not a second payload retry.
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
    void queueManualSmallestAndDurableJobs()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        HttpFixture server; server.media = media;
        server.bodies["/fixture.json"] = manifest({"large", "small"});
        server.bodies["/large.mp4"] = media + QByteArray(5000, 0);
        server.bodies["/small.mp4"] = media;
        const auto paths = queueConfig(directory.path(), {source(server)});
        ConfigRepository config(paths); CacheStore cache(paths.cache); QNetworkAccessManager network;
        qint64 now = 100000;
        auto state = std::make_unique<PlannerState>(paths.state, [&now] { return now; }, [] { return 0; });
        auto queue = std::make_unique<TransferQueue>(config, cache, *state, network, probe, QByteArray{}, [&now] { return now; });
        queue->setPolicyReason("Metered"); queue->start();
        const auto id = queue->acquire({}, 1); QVERIFY(!id.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(ended(*queue, id), 20000);
        QCOMPARE(queue->job(id).value("state").toString(), "succeeded");
        QCOMPARE(cache.entries().size(), 1); QCOMPARE(cache.entries()[0].toObject().value("assetId").toString(), "small");
        QStringList gets;
        for (const auto &request : server.requests) if (request.startsWith("GET ") && !request.contains(".json ")) gets.append(QString::fromLatin1(request.split(' ')[1]));
        QCOMPARE(gets, QStringList{"/small.mp4"});
        queue.reset(); state.reset();
        state = std::make_unique<PlannerState>(paths.state, [&now] { return now; }, [] { return 0; });
        queue = std::make_unique<TransferQueue>(config, cache, *state, network, probe, QByteArray{}, [&now] { return now; }); queue->start();
        QCOMPARE(queue->job(id).value("state").toString(), "succeeded");
        QCOMPARE(queue->job(id).value("ready").toInt(), 1);
        const auto repeat = queue->acquire({}, 1);
        QTRY_VERIFY_WITH_TIMEOUT(ended(*queue, repeat), 20000);
        QCOMPARE(cache.entries().size(), 1);
        int mediaGets = 0;
        for (const auto &request : server.requests) if (request.startsWith("GET ") && !request.contains(".json ")) ++mediaGets;
        QCOMPARE(mediaGets, 1); // --limit counts already verified eligible media.
    }
    void inheritedDefaultAcquisition()
    {
        // Use the ordinary shipped descriptor without rewriting it or adding a
        // user config. Only transport routing is injected; no live CDN traffic.
        class RoutedTransport : public QNetworkAccessManager {
        public:
            QString host;
            QUrl endpoint;
            QList<QUrl> requested;
        protected:
            QNetworkReply *createRequest(Operation operation, const QNetworkRequest &query, QIODevice *data) override
            {
                if (query.url().host() != host) qFatal("default fixture refused an unmapped remote host");
                requested.append(query.url());
                auto routed = query;
                auto url = query.url(); url.setScheme(endpoint.scheme()); url.setHost(endpoint.host()); url.setPort(endpoint.port());
                routed.setUrl(url);
                return QNetworkAccessManager::createRequest(operation, routed, data);
            }
        } network;
        QTemporaryDir directory; HttpFixture server; server.media = media;
        const ConfigPaths paths{QStringLiteral(AERIAL_TEST_SOURCE), directory.path() + "/config/config.json",
            directory.path() + "/cache", directory.path() + "/state", directory.path() + "/run"};
        ConfigRepository config(paths); QVERIFY(!config.document().present); QVERIFY(config.document().acquisitionAllowed());
        const auto sources = config.document().effective.value("sources").toArray(); QCOMPARE(sources.size(), 1);
        const auto source = sources.first().toObject(); QCOMPARE(source.value("id").toString(), "apple-tv");
        const auto url = SourceResolver::manifestUrl(source);
        network.host = url.host(); network.endpoint = server.url(); network.setProxy(QNetworkProxy::NoProxy);
        server.bodies[url.path().toLatin1()] = manifest({"large", "small"});
        const auto large = url.resolved(QUrl("large.mp4")).path().toLatin1();
        const auto small = url.resolved(QUrl("small.mp4")).path().toLatin1();
        server.bodies[large] = media + QByteArray(4096, 0); server.bodies[small] = media;
        CacheStore cache(paths.cache); PlannerState state(paths.state, [] { return 100000; }, [] { return 0; });
        TransferQueue queue(config, cache, state, network, probe, {}, [] { return 100000; });
        queue.start();
        QTRY_COMPARE_WITH_TIMEOUT(cache.entries().size(), 2, 20000);
        QTRY_VERIFY(queue.status() != "downloading" && queue.status() != "planning");
        QList<QByteArray> payloads;
        for (const auto &request : server.requests) if (request.startsWith("GET ") && request.split(' ').value(1) != url.path().toLatin1())
            payloads.append(request.split(' ').value(1));
        QCOMPARE(payloads, (QList<QByteArray>{small, large}));
        QVERIFY(!QFileInfo::exists(paths.config));
        ContentSnapshot snapshot(cache.path()); QVERIFY(snapshot.valid()); QCOMPARE(snapshot.entries().size(), 2);
        for (const auto &entry : snapshot.entries()) {
            QCOMPARE(entry.toObject().value("sourceId").toString(), "apple-tv");
            QVERIFY(entry.toObject().value("probe").toObject().value("initialFrameDecoded").toBool());
        }
    }
    void compatibilityFrontend()
    {
        PolicyPeer peer; QTemporaryDir directory; HttpFixture server; server.media = media;
        server.bodies["/first.json"] = manifest({"a1", "a2"}); server.bodies["/second.json"] = manifest({"b1", "b2"});
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("PATH", QStringLiteral(AERIAL_TEST_SOURCE) + "/cli:" + environment.value("PATH"));
        environment.insert("AERIAL_LOCK_SHELL", QStringLiteral(AERIAL_TEST_SOURCE));
        environment.insert("DBUS_SYSTEM_BUS_ADDRESS", qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"));
        for (const auto *key : {"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME", "XDG_RUNTIME_DIR"})
            environment.insert(key, directory.path() + '/' + key);
        OwnedDirectory config(environment.value("XDG_CONFIG_HOME") + "/aerial-lock", true);
        const auto raw = QJsonDocument(QJsonObject{{"sources", QJsonArray{source(server, "first"), source(server, "second")}},
            {"acquisition", QJsonObject{{"automatic", QJsonObject{{"enabled", false}}}}}}).toJson();
        QVERIFY(config.replace("config.json", raw));
        for (const auto &args : {QStringList{"--limit"}, QStringList{"--limit", "-1"}, QStringList{"--limit", "1;touch /must-not-exist"},
                 QStringList{"--other"}, QStringList{"source-id"}}) QCOMPARE(cli(environment, args, true).code, 2);
        QCOMPARE(cli(environment, {"--help"}, true).code, 0); QVERIFY(server.requests.isEmpty());
        QProcess process; process.setProcessEnvironment(environment);
        process.start(QStringLiteral(AERIAL_TEST_SOURCE) + "/companion/aerial-companion", {}); QVERIFY(process.waitForStarted(5000));
        struct Retire { QProcess &p; ~Retire() { if (p.state() != QProcess::NotRunning) { p.terminate(); if (!p.waitForFinished(5000)) { p.kill(); p.waitForFinished(5000); } } } } retire{process};
        QTRY_VERIFY(QDBusConnection::sessionBus().interface()->isServiceRegistered("org.aeriallock.Companion1").value());
        QCOMPARE(cli(environment, {"--limit", "1"}, true).code, 0);
        const auto cache = environment.value("XDG_CACHE_HOME") + "/aerial-lock/managed-v1";
        const auto entries = ContentSnapshot(cache).entries(); QCOMPARE(entries.size(), 2);
        QMap<QString, int> counts;
        for (const auto &entry : entries) ++counts[entry.toObject().value("sourceId").toString()];
        QCOMPARE(counts.value("first"), 1); QCOMPARE(counts.value("second"), 1);
        QCOMPARE(config.read("config.json", 100000), raw); // The frontend never writes preferences.
        const auto before = server.requests.size();
        QCOMPARE(cli(environment, {"--limit", "1"}, true).code, 0);
        for (int i = before; i < server.requests.size(); ++i) QVERIFY(!server.requests[i].startsWith("GET /a") && !server.requests[i].startsWith("GET /b"));
        QCOMPARE(cli(environment, {"config", "set", "sources", "[]"}).code, 0);
        QCOMPARE(cli(environment, {}, true).code, 1);
        QCOMPARE(ContentSnapshot(cache).entries().size(), 2); // Existing managed files are not discarded.
    }
    void resumeChecksumYieldsToCancellation()
    {
        QTemporaryDir directory; CacheStore cache(directory.path() + "/cache"); cache.configure(20000000, 0);
        HttpFixture server; QNetworkAccessManager network;
        auto representation = rep(server.url());
        const QByteArray bytes(8 * 1024 * 1024, 'x');
        representation.bytes = bytes.size(); representation.checksum = contentDigest(bytes).toLatin1();
        const auto file = cache.openPartial(representation.key, true); QVERIFY(file >= 0);
        QVERIFY(cache.reserve(bytes.size())); QVERIFY(cache.append(file, bytes)); close(file); cache.releaseReservation();
        QVERIFY(cache.writeMetadata("partials/" + representation.key + ".json", {{"version", 1}, {"key", representation.key},
            {"url", representation.url.toString(QUrl::FullyEncoded)}, {"validator", "\"fixture-one\""}, {"received", bytes.size()}, {"total", bytes.size()}}));
        Transfer transfer(cache, network, representation, probe, {}, [] { return 123; });
        QSignalSpy finished(&transfer, &Transfer::finished); transfer.start();
        QTimer::singleShot(0, &transfer, [&] { transfer.cancel(); });
        QTRY_COMPARE(finished.size(), 1); QCOMPARE(transfer.error(), "Cancelled");
        QVERIFY(server.requests.isEmpty()); QVERIFY(cache.entries().isEmpty());
        QCOMPARE(cache.metadata("partials/" + representation.key + ".json").value("received").toInteger(), qint64(bytes.size()));
    }
    void queueBackoffSurvivesEnsureAndRestart()
    {
        QTemporaryDir directory; HttpFixture server; server.media = media;
        server.bodies["/fixture.json"] = manifest({"clip"}); server.modes["/clip.mp4"] = "retry";
        const auto paths = queueConfig(directory.path(), {source(server)});
        ConfigRepository config(paths); CacheStore cache(paths.cache); QNetworkAccessManager network;
        qint64 now = 100000;
        auto state = std::make_unique<PlannerState>(paths.state, [&now] { return now; }, [] { return 0; });
        auto queue = std::make_unique<TransferQueue>(config, cache, *state, network, probe, QByteArray{}, [&now] { return now; }); queue->start();
        auto id = queue->acquire({}, 1); QTRY_VERIFY_WITH_TIMEOUT(ended(*queue, id), 20000);
        QVERIFY(cache.entries().isEmpty());
        const auto parsed = SourceResolver::parse(source(server), server.url("/fixture.json"), server.bodies["/fixture.json"]);
        QCOMPARE(parsed.representations.size(), 1); const auto key = parsed.representations[0].key;
        QCOMPARE(state->nextAttempt(key), now + 120);
        queue->ensure(); queue->ensure(); queue.reset(); state.reset();
        state = std::make_unique<PlannerState>(paths.state, [&now] { return now; }, [] { return 0; });
        queue = std::make_unique<TransferQueue>(config, cache, *state, network, probe, QByteArray{}, [&now] { return now; }); queue->start();
        server.modes.remove("/clip.mp4");
        id = queue->acquire({}, 1); QTRY_VERIFY_WITH_TIMEOUT(ended(*queue, id), 20000);
        QVERIFY(cache.entries().isEmpty()); QCOMPARE(state->nextAttempt(key), now + 120);
        now += 120;
        id = queue->acquire({}, 1); QTRY_VERIFY_WITH_TIMEOUT(ended(*queue, id), 20000);
        QCOMPARE(queue->job(id).value("state").toString(), "succeeded"); QCOMPARE(cache.entries().size(), 1);
    }
    void queuePerSourceLimitsAndQueuedCancellation()
    {
        QTemporaryDir directory; HttpFixture server; server.media = media;
        server.bodies["/first.json"] = manifest({"a1", "a2"});
        server.bodies["/second.json"] = manifest({"b1", "b2"});
        const auto paths = queueConfig(directory.path(), {source(server, "first"), source(server, "second")});
        ConfigRepository config(paths); CacheStore cache(paths.cache); QNetworkAccessManager network;
        PlannerState state(paths.state, [] { return 100000; }, [] { return 0; });
        TransferQueue queue(config, cache, state, network, probe, {}, [] { return 100000; }); queue.start();
        const auto id = queue.acquire({}, 1); QTRY_VERIFY_WITH_TIMEOUT(ended(queue, id), 20000);
        QCOMPARE(queue.job(id).value("state").toString(), "succeeded"); QCOMPARE(cache.entries().size(), 2);
        QMap<QString, int> counts;
        for (const auto &entry : cache.entries()) ++counts[entry.toObject().value("sourceId").toString()];
        QCOMPARE(counts.value("first"), 1); QCOMPARE(counts.value("second"), 1);
        for (const auto &path : {"/a1.mp4", "/a2.mp4", "/b1.mp4", "/b2.mp4"}) server.modes[path] = "hold";
        const auto active = queue.acquire({}, 2), queued = queue.acquire({}, 2);
        QVERIFY(queue.cancel(queued)); QCOMPARE(queue.job(queued).value("state").toString(), "cancelled");
        QTRY_COMPARE(queue.status(), "downloading"); QVERIFY(queue.cancel(active));
        QTRY_VERIFY(ended(queue, active)); QCOMPARE(queue.job(active).value("state").toString(), "cancelled");
        QCOMPARE(cache.entries().size(), 2); QVERIFY(!queue.cancel(queued));
    }
    void queuePolicyPauseManualBypassAndRestart()
    {
        QTemporaryDir directory; HttpFixture server; server.media = media;
        server.bodies["/fixture.json"] = manifest({"clip"}); server.modes["/clip.mp4"] = "hold";
        const auto paths = queueConfig(directory.path(), {source(server)}, true);
        ConfigRepository config(paths); CacheStore cache(paths.cache); QNetworkAccessManager network;
        qint64 now = 100000;
        auto state = std::make_unique<PlannerState>(paths.state, [&now] { return now; }, [] { return 0; });
        auto queue = std::make_unique<TransferQueue>(config, cache, *state, network, probe, QByteArray{}, [&now] { return now; });
        queue->start(); QTRY_COMPARE(queue->status(), "downloading");
        queue->setPolicyReason("Metered");
        QTRY_COMPARE(queue->status(), "deferred"); QVERIFY(cache.entries().isEmpty());
        const auto manual = queue->acquire({}, 1); QTRY_COMPARE(queue->status(), "downloading");
        QVERIFY(!ended(*queue, manual));
        queue.reset(); state.reset(); // Accepted work survives its process owner.
        server.modes.remove("/clip.mp4");
        state = std::make_unique<PlannerState>(paths.state, [&now] { return now; }, [] { return 0; });
        queue = std::make_unique<TransferQueue>(config, cache, *state, network, probe, QByteArray{}, [&now] { return now; });
        queue->setPolicyReason("Metered"); queue->start();
        QTRY_VERIFY_WITH_TIMEOUT(ended(*queue, manual), 20000);
        QCOMPARE(queue->job(manual).value("state").toString(), "succeeded"); QCOMPARE(cache.entries().size(), 1);
    }
    void policyQueue_data()
    {
        QTest::addColumn<QString>("kind");
        for (const auto *kind : {"ethernet", "wifi", "cellular", "bluetooth", "vpn", "other", "unknown",
                 "ac", "battery", "power-unknown", "metered", "offline", "enabled", "resolution"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void serviceCliPolicy_data() { policyQueue_data(); }
    void serviceCliRestart_data()
    {
        QTest::addColumn<bool>("differentProfile");
        QTest::newRow("same-profile") << false;
        QTest::newRow("different-profile") << true;
    }
    void serviceCliRestart()
    {
        QFETCH(bool, differentProfile);
        PolicyPeer peer; QTemporaryDir directory; HttpFixture server; server.media = media;
        server.bodies["/fixture.json"] = manifest({"clip"});
        server.modes["/clip.mp4"] = "hold-partial";
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("AERIAL_LOCK_SHELL", QStringLiteral(AERIAL_TEST_SOURCE));
        environment.insert("DBUS_SYSTEM_BUS_ADDRESS", qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"));
        for (const auto *key : {"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME", "XDG_RUNTIME_DIR"})
            environment.insert(key, directory.path() + '/' + key);
        OwnedDirectory config(environment.value("XDG_CONFIG_HOME") + "/aerial-lock", true);
        QVERIFY(config.replace("config.json", QJsonDocument(QJsonObject{{"sources", QJsonArray{source(server)}},
            {"acquisition", QJsonObject{{"automatic", QJsonObject{{"enabled", false}}}}}}).toJson()));
        QProcess process, caller;
        process.setProcessEnvironment(environment); caller.setProcessEnvironment(environment);
        struct Retire {
            QProcess &process;
            ~Retire() {
                if (process.state() == QProcess::NotRunning) return;
                ::kill(pid_t(process.processId()), SIGCONT);
                process.terminate(); if (!process.waitForFinished(5000)) { process.kill(); process.waitForFinished(5000); }
            }
        } retireOwner{process}, retireCaller{caller};
        const auto binary = QStringLiteral(AERIAL_TEST_SOURCE) + "/companion/aerial-companion";
        process.start(binary, {}); QVERIFY(process.waitForStarted(5000));
        auto bus = QDBusConnection::sessionBus();
        QTRY_VERIFY(bus.interface()->isServiceRegistered("org.aeriallock.Companion1").value());
        const auto oldOwner = bus.interface()->serviceOwner("org.aeriallock.Companion1").value();
        caller.start(QStringLiteral(AERIAL_TEST_SOURCE) + "/cli/aerialctl", {"fetch", "--limit", "1", "--wait", "--json"});
        QVERIFY(caller.waitForStarted(5000));
        const auto cache = environment.value("XDG_CACHE_HOME") + "/aerial-lock/managed-v1";
        QTRY_VERIFY_WITH_TIMEOUT(!QDir(cache + "/partials").entryList({"*.part"}, QDir::Files).isEmpty(), 10000);
        // Freeze only the CLI observer, not accepted service work. Replace the
        // real owner while it sleeps, then require a durable-result resnapshot.
        QCOMPARE(::kill(pid_t(caller.processId()), SIGSTOP), 0);
        process.terminate(); QVERIFY(process.waitForFinished(5000));
        QTRY_VERIFY(!bus.interface()->isServiceRegistered("org.aeriallock.Companion1").value());
        server.modes.remove("/clip.mp4");
        if (differentProfile) {
            const auto bundle = directory.path() + "/different-bundle";
            QVERIFY(QDir().mkpath(bundle + "/Config"));
            for (const auto *file : {"defaults.json", "settings-schema.json"})
                QVERIFY(QFile::copy(QStringLiteral(AERIAL_TEST_SOURCE) + "/Config/" + file, bundle + "/Config/" + file));
            auto changed = environment; changed.insert("AERIAL_LOCK_SHELL", bundle);
            process.setProcessEnvironment(changed);
        }
        process.start(binary, {}); QVERIFY(process.waitForStarted(5000));
        QTRY_VERIFY(bus.interface()->isServiceRegistered("org.aeriallock.Companion1").value());
        QVERIFY(bus.interface()->serviceOwner("org.aeriallock.Companion1").value() != oldOwner);
        QTRY_COMPARE_WITH_TIMEOUT(ContentSnapshot(cache).entries().size(), 1, 20000);
        QCOMPARE(::kill(pid_t(caller.processId()), SIGCONT), 0);
        QTRY_COMPARE_WITH_TIMEOUT(caller.state(), QProcess::NotRunning, 10000);
        const auto error = caller.readAllStandardError();
        QVERIFY2(caller.exitCode() == (differentProfile ? 1 : 0), error.constData());
        if (differentProfile) { QVERIFY(error.contains("ProfileMismatch")); return; }
        const auto result = QJsonDocument::fromJson(caller.readAllStandardOutput()).object();
        QCOMPARE(result.value("State").toString(), "succeeded");
        QCOMPARE(result.value("ReadyClips").toInt(), 1);
    }
    void serviceCliTransactions()
    {
        PolicyPeer peer; QTemporaryDir directory; HttpFixture server; server.media = media;
        // Manifest order and preferred quality differ from the automatic ceiling.
        server.bodies["/fixture.json"] = QJsonDocument(QJsonObject{{"assets", QJsonArray{
            QJsonObject{{"id", "one"}, {"url-1080-H264", "one1080.mp4"}, {"url-4K-SDR", "one2160.mp4"}},
            QJsonObject{{"id", "two"}, {"url-1080-H264", "two.mp4"}}}}}).toJson();
        auto selectedSource = source(server); selectedSource["maxResolution"] = 1080;
        // Token-bearing settings may be inspected explicitly, never in status.
        selectedSource["source"] = server.url("/fixture.json?token=private-token").toString();
        server.bodies["/fixture.json?token=private-token"] = server.bodies["/fixture.json"];
        server.bodies["/two.mp4"] = media + QByteArray(5000, '\0');
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("AERIAL_LOCK_SHELL", QStringLiteral(AERIAL_TEST_SOURCE));
        environment.insert("DBUS_SYSTEM_BUS_ADDRESS", qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"));
        for (const auto *key : {"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME", "XDG_RUNTIME_DIR"}) environment.insert(key, directory.path() + '/' + key);
        OwnedDirectory config(environment.value("XDG_CONFIG_HOME") + "/aerial-lock", true);
        QVERIFY(config.replace("config.json", QJsonDocument(QJsonObject{{"sources", QJsonArray{selectedSource}},
            {"cache", QJsonObject{{"maxBytes", 1000000}}}, {"acquisition", QJsonObject{{"automatic", QJsonObject{{"enabled", false}}}}}}).toJson()));
        QProcess process; process.setProcessEnvironment(environment);
        process.start(QStringLiteral(AERIAL_TEST_SOURCE) + "/companion/aerial-companion", {}); QVERIFY(process.waitForStarted(5000));
        struct Retire { QProcess &p; ~Retire() { if (p.state() != QProcess::NotRunning) { p.terminate(); if (!p.waitForFinished(5000)) { p.kill(); p.waitForFinished(5000); } } } } retire{process};
        auto bus = QDBusConnection::sessionBus();
        QTRY_VERIFY(bus.interface()->isServiceRegistered("org.aeriallock.Companion1").value());
        auto result = cli(environment, {"config", "show", "--json"}); QCOMPARE(result.code, 0); QVERIFY(result.out.contains("private-token"));
        result = cli(environment, {"status", "--json"}); QCOMPARE(result.code, 0); QVERIFY(!result.out.contains("private-token"));
        QCOMPARE(cli(environment, {"config", "set", "cache.maxBytes", "-1"}).code, 1);
        QCOMPARE(cli(environment, {"config", "set", "language", "\"fr\""}).code, 0);
        QCOMPARE(cli(environment, {"config", "reset", "language"}).code, 0);
        result = cli(environment, {"config", "show", "--json"}); QVERIFY(!QJsonDocument::fromJson(result.out).object().contains("language"));
        auto mismatch = environment; mismatch.insert("XDG_STATE_HOME", directory.path() + "/different-state");
        QCOMPARE(cli(mismatch, {"config", "set", "language", "\"de\""}).code, 1);
        QCOMPARE(cli(environment, {"fetch", "--limit", "-1"}).code, 2);
        QCOMPARE(cli(environment, {"config", "set", "cache.maxBytes", "not-json"}).code, 2);

        // Kill a waiting client while the service owns a resumable partial.
        server.modes["/one1080.mp4"] = "hold-partial";
        QProcess caller; caller.setProcessEnvironment(environment);
        caller.start(QStringLiteral(AERIAL_TEST_SOURCE) + "/cli/aerialctl", {"fetch", "--limit", "1", "--wait"});
        QVERIFY(caller.waitForStarted(5000));
        const auto cache = environment.value("XDG_CACHE_HOME") + "/aerial-lock/managed-v1";
        QTRY_VERIFY_WITH_TIMEOUT(!QDir(cache + "/partials").entryList({"*.part"}, QDir::Files).isEmpty(), 10000);
        QTest::qWait(50); caller.terminate(); QVERIFY(caller.waitForFinished(5000));
        result = cli(environment, {"jobs", "--json"}); QCOMPARE(result.code, 0);
        QString active;
        for (const auto &row : QJsonDocument::fromJson(result.out).array())
            if (row.toObject().value("State") == "downloading") active = row.toObject().value("path").toString();
        QVERIFY(!active.isEmpty()); QCOMPARE(process.state(), QProcess::Running);
        QCOMPARE(cli(environment, {"cancel", active}).code, 0);
        result = cli(environment, {"jobs", "--json"}); QVERIFY(result.out.contains("cancelled"));
        const auto partials = QDir(cache + "/partials").entryList({"*.part"}, QDir::Files); QVERIFY(!partials.isEmpty());
        QFile partial(cache + "/partials/" + partials.first()); QVERIFY(partial.open(QIODevice::ReadOnly)); QCOMPARE(partial.readAll(), media.left(media.size() / 2)); partial.close();
        server.modes.remove("/one1080.mp4");
        result = cli(environment, {"content", "fetch", "--limit", "1", "--wait", "--json"}); QCOMPARE(result.code, 0);
        QVERIFY(!result.out.contains("private-token")); QCOMPARE(ContentSnapshot(cache).entries().size(), 1);
        for (const auto &request : server.requests) QVERIFY(!request.startsWith("GET /one2160.mp4"));
        // Remove the explicit source band: manual fetch bypasses automatic1080.
        selectedSource.remove("maxResolution");
        QCOMPARE(cli(environment, {"config", "set", "sources", QString::fromUtf8(QJsonDocument(QJsonArray{selectedSource}).toJson(QJsonDocument::Compact))}).code, 0);
        result = cli(environment, {"fetch", "--limit", "1", "--wait", "--json"}); QCOMPARE(result.code, 0);
        bool higher = false; for (const auto &request : server.requests) higher |= request.startsWith("GET /one2160.mp4"); QVERIFY(higher);
        result = cli(environment, {"content", "refresh", "fixture", "--json"}); QCOMPARE(result.code, 0); QVERIFY(result.out.contains("accepted"));
        result = cli(environment, {"cache", "prune", "--dry-run", "--json"}); QCOMPARE(result.code, 0);
        result = cli(environment, {"jobs", "--watch", "--json"}); QCOMPARE(result.code, 0); QVERIFY(result.out.contains("prune"));
        QVERIFY(result.out.contains("removedBytes")); // Nested a{sv} details survive JSON output.
        QCOMPARE(cli(environment, {"config", "set", "sources", "[]"}).code, 0);
        QCOMPARE(cli(environment, {"fetch", "--wait"}).code, 1);
        result = cli(environment, {"status", "--json"}); QCOMPARE(QJsonDocument::fromJson(result.out).object().value("ReadyClips").toInt(), 0);
        QCOMPARE(cli(environment, {"config", "set", "cache.maxBytes", "0"}).code, 0);
        QCOMPARE(cli(environment, {"fetch", "--wait"}).code, 1);
        process.terminate(); QVERIFY(process.waitForFinished(5000));
        QTRY_VERIFY(!bus.interface()->isServiceRegistered("org.aeriallock.Companion1").value());
        process.start(QStringLiteral(AERIAL_TEST_SOURCE) + "/companion/aerial-companion", {}); QVERIFY(process.waitForStarted(5000));
        QTRY_VERIFY(bus.interface()->isServiceRegistered("org.aeriallock.Companion1").value());
        result = cli(environment, {"status", "--json"}); QCOMPARE(result.code, 0);
        QVERIFY(QJsonDocument::fromJson(result.out).object().value("ManagedBytes").toInteger() > 0);
        QVERIFY(!process.readAllStandardError().contains("private-token"));
    }
    void serviceCliPolicy()
    {
        QFETCH(QString, kind);
        PolicyPeer peer;
        const QMap<QString, QString> types{{"ethernet", "802-3-ethernet"}, {"wifi", "802-11-wireless"},
            {"cellular", "gsm"}, {"bluetooth", "bluetooth"}, {"vpn", "vpn"}, {"other", "bridge"}, {"unknown", ""}};
        if (types.contains(kind)) peer.data[peer.primary]["Type"] = types.value(kind);
        if (kind == "battery") peer.data[peer.power]["OnBattery"] = true;
        if (kind == "power-unknown") peer.data[peer.power].clear();
        if (kind == "metered") {
            peer.data[peer.vpn] = {{"Type", "vpn"}, {"Vpn", true}, {"Default", true}, {"State", uint(2)}};
            peer.data[peer.manager]["ActiveConnections"] = QVariant::fromValue(QList<QDBusObjectPath>{QDBusObjectPath(peer.primary), QDBusObjectPath(peer.vpn)});
            peer.data[peer.device]["Metered"] = uint(1);
        }
        if (kind == "offline") peer.data[peer.manager]["State"] = uint(20);
        QTemporaryDir directory; HttpFixture server; server.media = media;
        server.bodies["/fixture.json"] = manifest({"one", "two"});
        const auto sources = QJsonArray{source(server)};
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("AERIAL_LOCK_SHELL", QStringLiteral(AERIAL_TEST_SOURCE));
        environment.insert("DBUS_SYSTEM_BUS_ADDRESS", qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"));
        for (const auto *key : {"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME", "XDG_RUNTIME_DIR"})
            environment.insert(key, directory.path() + '/' + key);
        OwnedDirectory config(environment.value("XDG_CONFIG_HOME") + "/aerial-lock", true);
        QVERIFY(config.replace("config.json", QJsonDocument(QJsonObject{{"sources", sources},
            {"cache", QJsonObject{{"maxBytes", 1000000}}}, {"acquisition", QJsonObject{{"automatic", QJsonObject{{"enabled", false}}}}}}).toJson()));
        QProcess process; process.setProcessEnvironment(environment);
        const auto binary = QStringLiteral(AERIAL_TEST_SOURCE) + "/companion/aerial-companion";
        process.start(binary, {}); QVERIFY(process.waitForStarted(5000));
        struct Retire { QProcess &process; ~Retire() { if (process.state() != QProcess::NotRunning) { process.terminate(); if (!process.waitForFinished(5000)) { process.kill(); process.waitForFinished(5000); } } } } retire{process};
        auto bus = QDBusConnection::sessionBus();
        QTRY_VERIFY(bus.interface()->isServiceRegistered("org.aeriallock.Companion1").value());
        auto result = cli(environment, {"status", "--json"}); QCOMPARE(result.code, 0);
        QVERIFY2(result.error.isEmpty(), result.error.constData());
        const auto initial = QJsonDocument::fromJson(result.out).object();
        QCOMPARE(initial.value("Profile").toString().size(), 64);
        QString setting, value = "false", allowed = "true";
        if (types.contains(kind)) setting = "connectionTypes." + kind;
        else if (kind == "ac" || kind == "battery" || kind == "power-unknown") setting = "power." + (kind == "power-unknown" ? QString("unknown") : kind);
        else if (kind == "metered") setting = "allowMetered";
        else if (kind == "enabled") setting = "enabled";
        else if (kind == "resolution") { setting = "allowedResolutions"; value = "[]"; allowed = "[1080]"; }
        const auto set = [&](const QString &key, const QString &value) { return cli(environment, {"config", "set", "acquisition.automatic." + key, value}); };
        if (!setting.isEmpty()) QCOMPARE(set(setting, value).code, 0);
        if (kind != "enabled") QCOMPARE(set("enabled", "true").code, 0);
        QTest::qWait(100);
        QCOMPARE(cli(environment, {"content", "ensure"}).code, 3);
        QVERIFY(server.requests.isEmpty());
        // The manual CLI bypasses every automatic predicate, not hard bands or
        // integrity. Its terminal result is the actual decoded local object.
        result = cli(environment, {"fetch", "fixture", "--limit", "1", "--wait", "--json"});
        QCOMPARE(result.code, 0); QVERIFY2(result.error.isEmpty(), result.error.constData());
        const auto receipt = QJsonDocument::fromJson(result.out).object();
        QCOMPARE(receipt.value("ReadyClips").toInt(), 1); QCOMPARE(receipt.value("State").toString(), "succeeded");
        const auto cache = environment.value("XDG_CACHE_HOME") + "/aerial-lock/managed-v1";
        { ContentSnapshot snapshot(cache); QVERIFY(snapshot.valid()); QCOMPARE(snapshot.entries().size(), 1);
            const auto entry = snapshot.entries()[0].toObject();
            QVERIFY(entry.value("probe").toObject().value("initialFrameDecoded").toBool());
            QFile file(entry.value("path").toString()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), media); }
        if (kind == "offline") peer.update(peer.manager, {{"State", uint(70)}});
        else QCOMPARE(set(setting, allowed).code, 0);
        QTRY_COMPARE_WITH_TIMEOUT(ContentSnapshot(cache).entries().size(), 2, 20000);
        result = cli(environment, {"cache", "status", "--json"}); QCOMPARE(result.code, 0);
        const auto status = QJsonDocument::fromJson(result.out).object();
        QCOMPARE(status.value("ReadyClips").toInt(), 2); QVERIFY(status.value("ManagedBytes").toInteger() > media.size() * 2);
        // A new service exports terminal objects from the durable job store.
        process.terminate(); QVERIFY(process.waitForFinished(5000));
        QTRY_VERIFY(!bus.interface()->isServiceRegistered("org.aeriallock.Companion1").value());
        process.start(binary, {}); QVERIFY(process.waitForStarted(5000));
        QTRY_VERIFY(bus.interface()->isServiceRegistered("org.aeriallock.Companion1").value());
        result = cli(environment, {"jobs", "--json"}); QCOMPARE(result.code, 0);
        bool retained = false;
        for (const auto &row : QJsonDocument::fromJson(result.out).array()) {
            const auto job = row.toObject();
            if (job.value("Kind") == "manual" && job.value("State") == "succeeded") retained = true;
        }
        QVERIFY(retained);
        for (const auto &call : peer.calls) QCOMPARE(call, "org.freedesktop.DBus.Properties.GetAll");
    }
    void policyQueue()
    {
        QFETCH(QString, kind);
        PolicyPeer peer;
        const QMap<QString, QString> types{{"ethernet", "802-3-ethernet"}, {"wifi", "802-11-wireless"},
            {"cellular", "gsm"}, {"bluetooth", "bluetooth"}, {"vpn", "vpn"}, {"other", "bridge"}, {"unknown", ""}};
        if (types.contains(kind)) peer.data[peer.primary]["Type"] = types.value(kind);
        if (kind == "battery") peer.data[peer.power]["OnBattery"] = true;
        if (kind == "power-unknown") peer.data[peer.power].clear();
        if (kind == "metered") {
            // NM's manager says unmetered, but the VPN's underlying device says
            // metered. Only a real graph read discovers that restriction.
            peer.data[peer.vpn] = {{"Type", "vpn"}, {"Vpn", true}, {"Default", true}, {"State", uint(2)}};
            peer.data[peer.manager]["ActiveConnections"] = QVariant::fromValue(QList<QDBusObjectPath>{QDBusObjectPath(peer.primary), QDBusObjectPath(peer.vpn)});
            peer.data[peer.device]["Metered"] = uint(1);
        }
        if (kind == "offline") peer.data[peer.manager]["State"] = uint(20);
        PolicyMonitor monitor(QDBusConnection::sessionBus()); monitor.start();
        QTRY_VERIFY(monitor.ready());
        QTemporaryDir directory; HttpFixture server; server.media = media;
        server.bodies["/fixture.json"] = manifest({"one", "two"});
        const auto paths = queueConfig(directory.path(), {source(server)}, true);
        ConfigRepository config(paths); CacheStore cache(paths.cache); QNetworkAccessManager network;
        QJsonObject disallowed, allowed;
        if (types.contains(kind)) {
            disallowed = {{"connectionTypes", QJsonObject{{kind, false}}}};
            allowed = {{"connectionTypes", QJsonObject{{kind, true}}}};
        } else if (kind == "ac" || kind == "battery" || kind == "power-unknown") {
            const auto key = kind == "power-unknown" ? QString("unknown") : kind;
            disallowed = {{"power", QJsonObject{{key, false}}}}; allowed = {{"power", QJsonObject{{key, true}}}};
        } else if (kind == "metered") allowed = {{"allowMetered", true}};
        else if (kind == "enabled") { disallowed = {{"enabled", false}}; allowed = {{"enabled", true}}; }
        else if (kind == "resolution") { disallowed = {{"allowedResolutions", QJsonArray{}}}; allowed = {{"allowedResolutions", QJsonArray{1080}}}; }
        const auto patch = [&](const QJsonObject &automatic) {
            return config.update(config.document().revision, {{"acquisition", QJsonObject{{"automatic", automatic}}}}).ok();
        };
        QVERIFY(patch(disallowed));
        PlannerState state(paths.state, [] { return 100000; }, [] { return 0; });
        TransferQueue queue(config, cache, state, network, probe, {}, [] { return 100000; });
        const auto update = [&] { queue.setPolicyReason(monitor.deferReason(config.document().effective)); };
        connect(&monitor, &PolicyMonitor::changed, &queue, update);
        connect(&config, &ConfigRepository::changed, &queue, update);
        update(); queue.start();
        QTRY_VERIFY(queue.status() == "deferred" || queue.status() == "paused");
        QVERIFY(server.requests.isEmpty());
        const auto manual = queue.acquire({}, 1);
        QTRY_VERIFY_WITH_TIMEOUT(ended(queue, manual), 20000);
        QCOMPARE(queue.job(manual).value("state").toString(), "succeeded");
        QCOMPARE(cache.entries().size(), 1);
        // Config changes and observer changes must trigger the same production
        // planner. Resume fills the second clip, not just a reported status.
        if (kind == "offline") peer.update(peer.manager, {{"State", uint(70)}});
        else QVERIFY(patch(allowed));
        QTRY_COMPARE_WITH_TIMEOUT(cache.entries().size(), 2, 20000);
        for (const auto &call : peer.calls) QCOMPARE(call, "org.freedesktop.DBus.Properties.GetAll");
    }
    void policySnapshotsAndOwnerReplacement()
    {
        PolicyPeer peer;
        peer.hold = peer.manager;
        PolicyMonitor monitor(QDBusConnection::sessionBus()); monitor.start();
        QTRY_COMPARE(peer.held.size(), 1); QVERIFY(!monitor.ready());
        const auto old = peer.data[peer.manager];
        peer.update(peer.manager, {{"Metered", uint(1)}});
        QTest::qWait(30);
        peer.hold.clear();
        peer.bus.send(peer.held.takeFirst().createReply(QVariantList{old}));
        QTRY_VERIFY(monitor.ready());
        QCOMPARE(monitor.observations().value("metering").toString(), "metered");
        peer.update(peer.power, {{"OnBattery", true}});
        QTRY_COMPARE(monitor.observations().value("power").toString(), "battery");
        peer.update(peer.power, {}, {"OnBattery"});
        QTRY_COMPARE(monitor.observations().value("power").toString(), "unknown");
        QVERIFY(peer.bus.unregisterService(peer.nm));
        QTRY_COMPARE(monitor.observations().value("network").toString(), "unknown");
        QCOMPARE(monitor.observations().value("metering").toString(), "unknown");
        peer.data[peer.manager]["State"] = uint(20);
        QVERIFY(peer.bus.registerService(peer.nm));
        QTRY_COMPARE(monitor.observations().value("network").toString(), "offline");
        QTRY_VERIFY(monitor.ready());
        peer.update(peer.manager, {{"Connectivity", uint(2)}, {"State", uint(70)}});
        QTRY_COMPARE(monitor.observations().value("network").toString(), "offline");
    }
    void policyTransferPauseAndResume()
    {
        PolicyPeer peer; PolicyMonitor monitor(QDBusConnection::sessionBus()); monitor.start();
        QTRY_VERIFY(monitor.ready());
        QTemporaryDir directory; HttpFixture server; server.media = media;
        server.bodies["/fixture.json"] = manifest({"clip"}); server.modes["/clip.mp4"] = "hold-partial";
        const auto paths = queueConfig(directory.path(), {source(server)}, true);
        ConfigRepository config(paths); CacheStore cache(paths.cache); QNetworkAccessManager network;
        PlannerState state(paths.state, [] { return 100000; }, [] { return 0; });
        TransferQueue queue(config, cache, state, network, probe, {}, [] { return 100000; });
        connect(&monitor, &PolicyMonitor::changed, &queue, [&] { queue.setPolicyReason(monitor.deferReason(config.document().effective)); });
        queue.setPolicyReason(monitor.deferReason(config.document().effective)); queue.start();
        QTRY_COMPARE(queue.status(), "downloading");
        const auto parsed = SourceResolver::parse(source(server), server.url("/fixture.json"), server.bodies["/fixture.json"]);
        QCOMPARE(parsed.representations.size(), 1);
        const auto key = parsed.representations[0].key;
        const auto received = [&] {
            struct stat file{};
            return cache.files().identity("partials/" + key + ".part", file) ? file.st_size : -1;
        };
        QTRY_COMPARE(received(), qint64(media.size() / 2));
        peer.update(peer.device, {{"Metered", uint(1)}});
        QTRY_COMPARE(queue.status(), "deferred");
        QCOMPARE(queue.deferReason(), "Metered"); QVERIFY(cache.entries().isEmpty());
        QCOMPARE(cache.metadata("partials/" + key + ".json").value("received").toInteger(), qint64(media.size() / 2));
        const auto requests = server.requests.size();
        QTest::qWait(50); QCOMPARE(server.requests.size(), requests);
        server.modes["/clip.mp4"] = "206";
        peer.update(peer.device, {{"Metered", uint(2)}});
        QTRY_COMPARE_WITH_TIMEOUT(cache.entries().size(), 1, 20000);
        QVERIFY(server.requests.last().contains("Range: bytes=" + QByteArray::number(media.size() / 2) + '-'));
        QCOMPARE(cache.files().read("objects/" + key + ".media", 1000000), media);
    }
    void queueRotationCadenceAndPins()
    {
        QTemporaryDir directory; HttpFixture server; server.media = media;
        server.bodies["/fixture.json"] = manifest({"one"});
        const auto paths = queueConfig(directory.path(), {source(server)}, true);
        ConfigRepository config(paths); CacheStore cache(paths.cache); QNetworkAccessManager network;
        qint64 now = 100000;
        PlannerState state(paths.state, [&now] { return now; }, [] { return 0; });
        TransferQueue queue(config, cache, state, network, probe, {}, [&now] { return now; });
        queue.start(); QTRY_COMPARE_WITH_TIMEOUT(cache.entries().size(), 1, 20000);
        QTRY_COMPARE(queue.status(), "idle");
        const auto old = cache.entries()[0].toObject().value("key").toString();
        auto reader = std::make_unique<ContentSnapshot>(paths.cache); QVERIFY(reader->valid());
        server.bodies["/fixture.json"] = manifest({"one", "two"});
        now += 7 * 86400; queue.evaluate();
        QTRY_COMPARE_WITH_TIMEOUT(cache.entries().size(), 2, 20000);
        QTRY_VERIFY(!state.rotation().isEmpty()); QVERIFY(cache.contains(old));
        QCOMPARE(state.lastRotation(), qint64(100000));
        reader.reset(); now += 3600; queue.evaluate();
        QTRY_VERIFY_WITH_TIMEOUT(state.rotation().isEmpty(), 20000);
        QCOMPARE(cache.entries().size(), 1); QVERIFY(!cache.contains(old)); QVERIFY(state.retired(old));
        QCOMPARE(state.lastRotation(), now);
        const auto requests = server.requests.size(); now += 3600; queue.evaluate();
        QTRY_COMPARE(queue.status(), "idle");
        QCOMPARE(cache.entries().size(), 1); QVERIFY(!cache.contains(old));
        for (qsizetype i = requests; i < server.requests.size(); ++i) QVERIFY(!server.requests[i].startsWith("GET /one.mp4"));
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 2 && QByteArray(argv[1]) == "--policy-peer") {
        // Separate private-bus peer for installed-executable VM acceptance.
        // The product sees only standard read-only NM/UPower properties.
        PolicyPeer peer;
        QByteArray pending;
        QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
        QObject::connect(&input, &QSocketNotifier::activated, &app, [&] {
            char buffer[4096]; const auto length = ::read(STDIN_FILENO, buffer, sizeof(buffer));
            if (length <= 0) { app.quit(); return; }
            pending.append(buffer, length);
            while (pending.contains('\n')) {
                const auto line = pending.left(pending.indexOf('\n')); pending.remove(0, line.size() + 1);
                const auto patch = QJsonDocument::fromJson(line).object();
                QVariantMap manager;
                for (const auto &name : {"State", "Connectivity", "Metered"})
                    if (patch.contains(name)) manager[name] = uint(patch.value(name).toInt());
                if (!manager.isEmpty()) peer.update(peer.manager, manager);
                if (patch.contains("Type")) peer.update(peer.primary, {{"Type", patch.value("Type").toString()}});
                if (patch.contains("DeviceMetered")) peer.update(peer.device, {{"Metered", uint(patch.value("DeviceMetered").toInt())}});
                if (patch.contains("OnBattery")) {
                    if (patch.value("OnBattery").isNull()) peer.update(peer.power, {}, {"OnBattery"});
                    else peer.update(peer.power, {{"OnBattery", patch.value("OnBattery").toBool()}});
                }
                fprintf(stdout, "POLICY_PEER_UPDATED\n"); fflush(stdout);
            }
        });
        fprintf(stdout, "POLICY_PEER_READY\n"); fflush(stdout);
        return app.exec();
    }
    if (argc == 3 && QByteArray(argv[1]) == "--size-apple-catalogue") {
        // VM-only bounded live-provider evidence. The service must be stopped:
        // this takes the real cache owner lock, but never requests clip bodies.
        const auto paths = ConfigPaths::environment("/usr/share/aerial-lock");
        const auto document = readConfig(paths);
        const auto sources = document.effective.value("sources").toArray();
        if (!document.acquisitionAllowed() || sources.size() != 1) return 2;
        const auto source = sources[0].toObject();
        if (source.value("type") != "apple" || source.value("maxResolution").toInt() != 1080
            || QUrl(source.value("source").toString()).host() != "sylvan.apple.com") return 2;
        class TraceTransport : public QNetworkAccessManager {
        public:
            QJsonArray requests;
            QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *body) override
            {
                requests.append(QJsonObject{{"method", operation == HeadOperation ? "HEAD" : "GET"},
                    {"url", request.url().toString(QUrl::FullyEncoded)},
                    {"range", QString::fromLatin1(request.rawHeader("Range"))}});
                return QNetworkAccessManager::createRequest(operation, request, body);
            }
        } network;
        CacheStore cache(paths.cache); if (!cache.valid()) return 1;
        QFile certificate(paths.bundle + "/Config/apple-root.pem");
        if (!certificate.open(QIODevice::ReadOnly)) return 1;
        const auto appleRoot = certificate.readAll();
        ManifestStore store(cache, network, appleRoot);
        const auto catalogue = store.cached(source);
        const auto candidates = SourceResolver::select(catalogue, source,
            document.effective.value("preferredResolution").toInt(), {1080}, true);
        if (candidates.isEmpty()) return 1;
        RepresentationSizer sizer(network, appleRoot);
        QObject::connect(&sizer, &RepresentationSizer::finished, &app, [&] {
            QJsonArray sizes;
            for (const auto &rep : sizer.result()) sizes.append(QJsonObject{{"key", rep.key}, {"assetId", rep.assetId},
                {"url", rep.url.toString(QUrl::FullyEncoded)}, {"height", rep.height}, {"bytes", rep.bytes}});
            QFile output(QString::fromLocal8Bit(argv[2]));
            if (!output.open(QIODevice::WriteOnly)) { app.exit(1); return; }
            const auto bytes = QJsonDocument(QJsonObject{{"candidates", sizes}, {"requests", network.requests}}).toJson();
            app.exit(output.write(bytes) == bytes.size() ? 0 : 1);
        });
        QTimer::singleShot(900000, &app, [&] { sizer.cancel(); app.exit(1); });
        sizer.start(candidates);
        return app.exec();
    }
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
