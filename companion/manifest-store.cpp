#include "manifest-store.h"
#include "manifest-store.moc"
#include "../common/content-snapshot.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>

namespace Aerial {
namespace {
QString signature(const QJsonObject &source)
{
    return contentDigest(QJsonDocument(QJsonArray{source.value("type"), source.value("source")}).toJson(QJsonDocument::Compact));
}

QJsonObject serialize(const Representation &rep)
{
    return {{"sourceId", rep.sourceId}, {"assetId", rep.assetId}, {"key", rep.key}, {"variant", rep.variant},
        {"url", rep.url.toString(QUrl::FullyEncoded)}, {"height", rep.height}, {"bytes", rep.bytes},
        {"checksum", QString::fromLatin1(rep.checksum)}, {"metadata", rep.metadata}};
}
}

ManifestStore::ManifestStore(CacheStore &cache, QNetworkAccessManager &transport, QByteArray appleRoot, QObject *parent)
    : QObject(parent), cache(cache), transport(transport), appleRoot(std::move(appleRoot)) {}

ManifestStore::~ManifestStore() { if (operation) { disconnect(operation, nullptr, this, nullptr); delete operation; } }

QString ManifestStore::path(const QJsonObject &source) const
{
    return "manifests/" + contentDigest(source.value("id").toString().toUtf8()) + ".json";
}

QList<Representation> ManifestStore::cached(const QJsonObject &source) const
{
    const auto saved = cache.metadata(path(source));
    if (saved.value("signature") != signature(source)) return {};
    QList<Representation> result;
    for (const auto &value : saved.value("representations").toArray()) {
        const auto object = value.toObject();
        Representation rep;
        rep.sourceId = object.value("sourceId").toString(); rep.assetId = object.value("assetId").toString();
        rep.key = object.value("key").toString(); rep.variant = object.value("variant").toString();
        rep.url = QUrl(object.value("url").toString()); rep.height = object.value("height").toInt();
        rep.bytes = object.value("bytes").toInteger(-1); rep.checksum = object.value("checksum").toString().toLatin1();
        rep.metadata = object.value("metadata").toObject();
        if (!contentKey(rep.key) || !SourceResolver::remoteUrl(rep.url) || rep.sourceId != source.value("id")
            || rep.assetId.isEmpty() || (rep.height != 1080 && rep.height != 2160)) return {};
        result.append(rep);
    }
    return result;
}

void ManifestStore::refresh(const QJsonObject &value)
{
    if (operation) return;
    source = value; links = 0; retried304 = false; failure.clear(); items.clear();
    prior = cache.metadata(path(source));
    if (prior.value("signature") != signature(source)) prior = {};
    fetch(SourceResolver::manifestUrl(source));
}

void ManifestStore::fetch(const QUrl &url, bool unconditional)
{
    body.clear();
    HttpRequest query; query.url = url;
    if (!unconditional) {
        query.validatorUrl = QUrl(prior.value("url").toString());
        const auto etag = prior.value("etag").toString().toLatin1(), modified = prior.value("modified").toString().toLatin1();
        if (!etag.isEmpty()) query.headers["If-None-Match"] = etag;
        else if (!modified.isEmpty()) query.headers["If-Modified-Since"] = modified;
    }
    operation = new HttpOperation(transport, query, appleRoot,
        [](const HttpResult &response) -> QString {
            if (response.status == 304) return {};
            if (response.status != 200) return "HttpError";
            bool ok = false;
            const auto length = response.headers.value("content-length").toLongLong(&ok);
            return ok && length > SourceResolver::maximumManifest ? "ManifestTooLarge" : QString();
        },
        [this](const QByteArray &chunk) -> QString {
            if (body.size() + chunk.size() > SourceResolver::maximumManifest) return "ManifestTooLarge";
            body += chunk; return {};
        }, this);
    connect(operation, &HttpOperation::finished, this, [this] {
        lastResponse = operation->result(); operation->deleteLater(); operation = nullptr;
        if (!lastResponse.error.isEmpty()) { settle(lastResponse.error); return; }
        if (lastResponse.status == 304) {
            if (!lastResponse.conditionallyRequested || lastResponse.url != QUrl(prior.value("url").toString())) {
                if (retried304) { settle("InvalidManifest"); return; }
                retried304 = true; fetch(lastResponse.url, true); return;
            }
            items = cached(source);
            settle(items.isEmpty() ? "InvalidManifest" : QString()); return;
        }
        const auto manifest = SourceResolver::parse(source, lastResponse.url, body);
        if (!manifest.valid()) { settle(manifest.error); return; }
        if (!manifest.next.isEmpty()) {
            if (++links > 2) { settle("ManifestLoop"); return; }
            fetch(manifest.next); return;
        }
        QJsonArray serialized;
        for (const auto &rep : manifest.representations) serialized.append(serialize(rep));
        if (!cache.writeMetadata(path(source), {{"version", 1}, {"signature", signature(source)},
            {"url", lastResponse.url.toString(QUrl::FullyEncoded)}, {"etag", QString::fromLatin1(lastResponse.headers.value("etag"))},
            {"modified", QString::fromLatin1(lastResponse.headers.value("last-modified"))}, {"representations", serialized}})) {
            settle("CacheFull"); return;
        }
        items = manifest.representations; settle({});
    });
    operation->start();
}

void ManifestStore::cancel() { if (operation) operation->cancel(); }
void ManifestStore::settle(const QString &error) { failure = error; emit finished(); }

RepresentationSizer::RepresentationSizer(QNetworkAccessManager &transport, QByteArray appleRoot, QObject *parent,
                                         int deadlineMs, int maximumConcurrent)
    : QObject(parent), transport(transport), appleRoot(std::move(appleRoot)),
      deadlineMs(std::max(1, deadlineMs)), maximumConcurrent(std::max(1, maximumConcurrent))
{
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, this, &RepresentationSizer::settle);
}
RepresentationSizer::~RepresentationSizer()
{
    for (auto *operation : operations) { disconnect(operation, nullptr, this, nullptr); delete operation; }
}

void RepresentationSizer::start(QList<Representation> values)
{
    if (started) return;
    started = true; index = 0; items = std::move(values);
    deadline.start(deadlineMs); next();
}
void RepresentationSizer::cancel()
{
    settle();
}
void RepresentationSizer::settle()
{
    if (settled) return;
    settled = true; deadline.stop();
    const auto active = operations; operations.clear();
    for (auto *operation : active) { disconnect(operation, nullptr, this, nullptr); delete operation; }
    SourceResolver::smallestFirst(items);
    emit finished();
}
void RepresentationSizer::next()
{
    if (settled) return;
    while (index < items.size() && operations.size() < maximumConcurrent) {
        const auto item = index++;
        if (items[item].bytes < 0) request(item, false);
    }
    if (index == items.size() && operations.isEmpty()) settle();
}
void RepresentationSizer::request(qsizetype item, bool range)
{
    HttpRequest query; query.url = items[item].url; query.head = !range;
    if (range) query.headers["Range"] = "bytes=0-0";
    auto *operation = new HttpOperation(transport, query, appleRoot,
        [this, item, range](const HttpResult &response) -> QString {
            if (response.status == 200 || response.status == 206) {
                bool ok = false;
                auto length = response.headers.value("content-length").toLongLong(&ok);
                if (range && response.status == 206) {
                    const auto match = QRegularExpression("^bytes 0-0/([0-9]+)$").match(QString::fromLatin1(response.headers.value("content-range")));
                    length = match.hasMatch() ? match.captured(1).toLongLong(&ok) : -1;
                }
                if (ok && length > 0) items[item].bytes = length;
            }
            return range ? QString("HeadersOnly") : QString();
        }, [](const QByteArray &) { return QString("HeadersOnly"); }, this);
    operations.insert(item, operation);
    connect(operation, &HttpOperation::finished, this, [this, item, range, operation] {
        operations.remove(item); operation->deleteLater();
        if (!range && items[item].bytes < 0) { request(item, true); return; }
        next();
    });
    operation->start();
}
}
