#include "http-operation.h"
#include "source-resolver.h"

#include <QNetworkReply>
#include <QSslCertificate>
#include <QSslConfiguration>

namespace Aerial {
HttpOperation::HttpOperation(QNetworkAccessManager &transport, HttpRequest request, const QByteArray &appleRoot,
                             HeaderConsumer headers, ChunkConsumer chunk, QObject *parent)
    : QObject(parent), transport(transport), original(std::move(request)), appleRoot(appleRoot),
      onHeaders(std::move(headers)), onChunk(std::move(chunk)), current(original.url)
{
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, this, [this] { cancel("Timeout"); });
}

HttpOperation::~HttpOperation()
{
    if (reply) { disconnect(reply, nullptr, this, nullptr); reply->abort(); reply->deleteLater(); }
}

void HttpOperation::start()
{
    if (started) return;
    started = true;
    deadline.start(5 * 60 * 1000);
    if (!SourceResolver::remoteUrl(current)) { complete("UnsafeUrl"); return; }
    request();
}

void HttpOperation::request(bool appleTrust)
{
    deliveredHeaders = false;
    response = {};
    response.url = current;
    QNetworkRequest query(current);
    query.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    query.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    query.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    query.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    query.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    // Qt's connection pool retains the first request's SSL configuration,
    // including after a failed handshake. Each hop/retry must use its own
    // normal or Apple-only trust, never pooled trust from a previous request.
    query.setAttribute(QNetworkRequest::ConnectionCacheExpiryTimeoutSecondsAttribute, -1);
    query.setRawHeader("Accept-Encoding", "identity");
    query.setRawHeader("User-Agent", "aerial-lock-companion/1");
    query.setTransferTimeout(30000);
    for (auto it = original.headers.cbegin(); it != original.headers.cend(); ++it) query.setRawHeader(it.key(), it.value());
    if (appleTrust) {
        auto ssl = QSslConfiguration::defaultConfiguration();
        ssl.addCaCertificates(QSslCertificate::fromData(appleRoot));
        query.setSslConfiguration(ssl);
    }
    reply = original.head ? transport.head(query) : transport.get(query);
    reply->setReadBufferSize(65536);
    connect(reply, &QNetworkReply::metaDataChanged, this, &HttpOperation::metadata);
    connect(reply, &QNetworkReply::readyRead, this, &HttpOperation::drain);
    connect(reply, &QNetworkReply::finished, this, [this] {
        if (terminal || !reply) return;
        metadata();
        if (terminal) return;
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300 && status <= 399 && status != 304) {
            const auto next = SourceResolver::resolveRemote(current, reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
            if (++redirects > 10 || next.isEmpty()) { complete("UnsafeRedirect"); return; }
            reply->deleteLater(); reply = nullptr;
            current = next;
            trustRetried = false;
            // Rebuild normal trust on every redirect. Apple-only CA never
            // enters the manager's shared SSL configuration or another host.
            request(); return;
        }
        if (reply->error() == QNetworkReply::SslHandshakeFailedError && !trustRetried
            && current.scheme() == "https" && current.host() == "sylvan.apple.com" && !appleRoot.isEmpty()) {
            trustRetried = true;
            reply->deleteLater(); reply = nullptr;
            request(true); return;
        }
        drain();
        if (terminal) return;
        if (status >= 400) complete("HttpError");
        else if (reply->error() != QNetworkReply::NoError) complete(reply->error() == QNetworkReply::SslHandshakeFailedError ? "TlsError" : "TransportError");
        else if (!status) complete("InvalidResponse");
        else complete();
    });
}

void HttpOperation::metadata()
{
    if (terminal || deliveredHeaders || !reply) return;
    response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!response.status || (response.status >= 300 && response.status <= 399 && response.status != 304)) return;
    response.headers.clear();
    for (const auto &header : reply->rawHeaderPairs()) response.headers.insert(header.first.toLower(), header.second);
    deliveredHeaders = true;
    if (onHeaders) {
        const auto error = onHeaders(response);
        if (!error.isEmpty()) cancel(error);
    }
}

void HttpOperation::drain()
{
    metadata();
    if (terminal || !reply || !deliveredHeaders || original.head || response.status >= 400) return;
    while (!terminal && reply && reply->bytesAvailable() > 0) {
        const auto bytes = reply->read(65536);
        if (bytes.isEmpty()) break;
        if (onChunk) {
            const auto error = onChunk(bytes);
            if (!error.isEmpty()) cancel(error);
        }
    }
}

void HttpOperation::cancel(const QString &reason)
{
    if (terminal) return;
    // Mark terminal before abort(): QNetworkReply may emit finished inline.
    complete(reason);
}

void HttpOperation::complete(const QString &error)
{
    if (terminal) return;
    terminal = true;
    deadline.stop();
    response.error = error;
    if (reply) {
        disconnect(reply, nullptr, this, nullptr);
        if (!reply->isFinished()) reply->abort();
        reply->deleteLater(); reply = nullptr;
    }
    emit finished();
}
}

#include "http-operation.moc"
