#include "transfer.h"
#include "transfer.moc"
#include "../common/content-snapshot.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <QTimer>
#include <fcntl.h>
#include <unistd.h>

namespace Aerial {
namespace {
QByteArray responseValidator(const HttpResult &response)
{
    const auto etag = response.headers.value("etag");
    if (etag.startsWith('"') && etag.endsWith('"') && etag.size() <= 1024) return etag;
    const auto modified = response.headers.value("last-modified");
    if (QDateTime::fromString(QString::fromLatin1(modified), Qt::RFC2822Date).isValid()) return modified;
    return {};
}
qint64 length(const QByteArray &bytes)
{
    bool ok = false;
    const auto number = bytes.toLongLong(&ok);
    return ok && number >= 0 ? number : -1;
}
}

Transfer::Transfer(CacheStore &cache, QNetworkAccessManager &transport, Representation representation,
                   QString probeExecutable, QByteArray appleRoot, std::function<qint64()> clock, QObject *parent)
    : QObject(parent), cache(cache), transport(transport), rep(std::move(representation)),
      probeExecutable(std::move(probeExecutable)), appleRoot(std::move(appleRoot)), clock(std::move(clock)) {}

Transfer::~Transfer()
{
    if (http) { disconnect(http, nullptr, this, nullptr); delete http; }
    if (worker) { disconnect(worker, nullptr, this, nullptr); delete worker; }
    if (file >= 0) close(file);
    if (hashInput >= 0) close(hashInput);
    cache.releaseReservation();
}

void Transfer::start()
{
    if (started) return;
    started = true;
    if (!cache.valid() || !contentKey(rep.key) || !SourceResolver::remoteUrl(rep.url)) { finish("InvalidRepresentation"); return; }
    if (cache.contains(rep.key)) { finish({}); return; }
    const auto saved = cache.metadata("partials/" + rep.key + ".json");
    file = cache.openPartial(rep.key);
    struct stat st{};
    if (file < 0 || fstat(file, &st)) { finish("StorageError"); return; }
    offset = st.st_size;
    validator = saved.value("validator").toString().toLatin1();
    expected = saved.value("total").toInteger(-1);
    if (saved.value("key") != rep.key || saved.value("url") != rep.url.toString(QUrl::FullyEncoded)
        || saved.value("received").toInteger(-1) != offset || validator.isEmpty()
        || (expected >= 0 && offset > expected) || (rep.bytes >= 0 && expected != rep.bytes)) {
        if (ftruncate(file, 0) || lseek(file, 0, SEEK_SET) < 0) { finish("StorageError"); return; }
        offset = 0; expected = rep.bytes; validator.clear();
    }
    resumeOffset = offset;
    if (!rep.checksum.isEmpty() && offset > 0) {
        hashInput = cache.openForProbe(rep.key);
        if (hashInput < 0) { finish("StorageError"); return; }
        hashPartial(); return;
    }
    request();
}

void Transfer::hashPartial()
{
    if (terminal) return;
    // Resume hashing yields after every disk chunk. Fresh bytes are hashed as
    // they arrive; publication never synchronously rescans a multi-GB clip on
    // the service thread, so cancellation/config/API dispatch stay responsive.
    QByteArray bytes(std::min<qint64>(65536, offset - hashed), Qt::Uninitialized);
    const auto count = ::read(hashInput, bytes.data(), bytes.size());
    if (count <= 0) { finish("StorageError"); return; }
    bytes.resize(count); checksum.addData(bytes); hashed += count;
    if (hashed == offset) { close(hashInput); hashInput = -1; request(); }
    else QTimer::singleShot(0, this, &Transfer::hashPartial);
}

void Transfer::request()
{
    HttpRequest query; query.url = rep.url; query.totalTimeoutMs = 0;
    if (resumeOffset > 0) {
        query.headers["Range"] = "bytes=" + QByteArray::number(resumeOffset) + '-';
        query.headers["If-Range"] = validator;
    }
    http = new HttpOperation(transport, query, appleRoot,
        [this](const HttpResult &response) { return headers(response); },
        [this](const QByteArray &bytes) { return chunk(bytes); }, this);
    connect(http, &HttpOperation::finished, this, [this] {
        lastResponse = http->result();
        const auto result = lastResponse;
        http->deleteLater(); http = nullptr;
        if (!result.error.isEmpty() && !(resumedComplete && result.status == 416)) {
            if (result.error == "InvalidRange" || result.error == "LengthMismatch") cache.discardPartial(rep.key);
            else persist();
            finish(result.error); return;
        }
        if (expected >= 0 && offset != expected) { persist(); finish("LengthMismatch"); return; }
        if (offset <= 0 || fsync(file)) { finish("StorageError"); return; }
        if (!rep.checksum.isEmpty()) {
            if (checksum.result().toHex() != rep.checksum) { cache.discardPartial(rep.key); finish("ChecksumMismatch"); return; }
        }
        probe();
    });
    http->start();
}

QString Transfer::headers(const HttpResult &response)
{
    if (response.status == 416 && resumeOffset > 0) {
        const QRegularExpression pattern("^bytes \\*/([0-9]+)$");
        const auto match = pattern.match(QString::fromLatin1(response.headers.value("content-range")));
        const auto total = match.hasMatch() ? length(match.captured(1).toLatin1()) : -1;
        resumedComplete = total == resumeOffset && expected == total && responseValidator(response) == validator;
        return resumedComplete ? QString() : "InvalidRange";
    }
    if (response.status != 200 && response.status != 206) return "HttpError";
    if (response.headers.contains("content-encoding") && response.headers.value("content-encoding") != "identity") return "InvalidEncoding";
    const auto newValidator = responseValidator(response);
    const auto declared = length(response.headers.value("content-length"));
    if (response.status == 206) {
        const QRegularExpression pattern("^bytes ([0-9]+)-([0-9]+)/([0-9]+)$");
        const auto match = pattern.match(QString::fromLatin1(response.headers.value("content-range")));
        if (!match.hasMatch()) return "InvalidRange";
        const auto start = length(match.captured(1).toLatin1()), end = length(match.captured(2).toLatin1()), total = length(match.captured(3).toLatin1());
        if (resumeOffset <= 0 || start != resumeOffset || end < start || end >= total || end != total - 1
            || validator.isEmpty() || validator != newValidator || (expected >= 0 && expected != total)
            || (rep.bytes >= 0 && rep.bytes != total) || (declared >= 0 && declared != end - start + 1)) return "InvalidRange";
        expected = total;
    } else {
        if (resumeOffset > 0 && (ftruncate(file, 0) || lseek(file, 0, SEEK_SET) < 0)) return "StorageError";
        offset = 0; resumeOffset = 0;
        checksum.reset();
        if (rep.bytes >= 0 && declared >= 0 && rep.bytes != declared) return "LengthMismatch";
        expected = rep.bytes >= 0 ? rep.bytes : declared;
        validator = newValidator;
    }
    if (!persist()) return "CacheFull";
    if (expected >= 0 && !cache.reserve(expected - offset)) return "CacheFull";
    emit progress();
    return {};
}

QString Transfer::chunk(const QByteArray &bytes)
{
    if (expected >= 0 && bytes.size() > expected - offset) return "LengthMismatch";
    if (!cache.append(file, bytes)) {
        struct stat st{}; if (fstat(file, &st) == 0) offset = st.st_size;
        return "CacheFull";
    }
    offset += bytes.size();
    if (!rep.checksum.isEmpty()) checksum.addData(bytes);
    if (!persist()) return "CacheFull";
    emit progress();
    return {};
}

bool Transfer::persist()
{
    if (file < 0) return false;
    if (fsync(file)) return false;
    return cache.writeMetadata("partials/" + rep.key + ".json", {{"version", 1}, {"key", rep.key},
        {"url", rep.url.toString(QUrl::FullyEncoded)}, {"validator", QString::fromLatin1(validator)},
        {"received", offset}, {"total", expected}});
}

void Transfer::probe()
{
    cache.releaseReservation();
    close(file); file = -1;
    worker = new LocalProbe(probeExecutable, this);
    connect(worker, &LocalProbe::finished, this, [this] {
        const auto result = worker->result();
        worker->deleteLater(); worker = nullptr;
        if (result.isEmpty()) { cache.discardPartial(rep.key); finish("ProbeFailed"); return; }
        if (!cache.complete(rep, result, clock())) { finish("CacheFull"); return; }
        finish({});
    });
    worker->start(cache.openForProbe(rep.key));
}

void Transfer::cancel(const QString &reason)
{
    if (terminal) return;
    if (http) http->cancel(reason);
    else if (worker) { disconnect(worker, nullptr, this, nullptr); worker->cancel(); finish(reason); }
    else finish(reason);
}

void Transfer::finish(const QString &error)
{
    if (terminal) return;
    terminal = true;
    failure = error;
    if (file >= 0) { close(file); file = -1; }
    if (hashInput >= 0) { close(hashInput); hashInput = -1; }
    cache.releaseReservation();
    emit finished();
}
}
