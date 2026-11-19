#pragma once

#include <QMap>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace Aerial {
// A fresh manager per wire attempt owns the connection pool and its trust.
// Injection creates managers, never shares a mutable TLS pool across requests.
class HttpTransport {
public:
    using Factory = std::function<QNetworkAccessManager *(QObject *)>;
    explicit HttpTransport(Factory factory = {}) : factory(std::move(factory)) {}
    QNetworkAccessManager *create(QObject *parent) const
    {
        return factory ? factory(parent) : new QNetworkAccessManager(parent);
    }
private:
    Factory factory;
};

using HttpHeaders = QMap<QByteArray, QByteArray>;
struct HttpResult {
    int status = 0;
    QUrl url;
    HttpHeaders headers;
    QString error;
    bool conditionallyRequested = false;
};
struct HttpRequest {
    QUrl url;
    QUrl validatorUrl;
    HttpHeaders headers;
    bool head = false;
    // Metadata has a total deadline. Media disables only that lifetime bound;
    // inactivity still terminates stalled transfers regardless of validators.
    int totalTimeoutMs = 5 * 60 * 1000;
    int inactivityTimeoutMs = 30000;
};

// One bounded asynchronous HTTP transaction, including manually checked
// redirects. All errors are stable codes, never token-bearing URLs or text.
class HttpOperation : public QObject {
    Q_OBJECT
public:
    using HeaderConsumer = std::function<QString(const HttpResult &)>;
    using ChunkConsumer = std::function<QString(const QByteArray &)>;
    HttpOperation(HttpTransport &transport, HttpRequest request, const QByteArray &appleRoot,
                  HeaderConsumer headers, ChunkConsumer chunk, QObject *parent = nullptr);
    ~HttpOperation();
    void start();
    void cancel(const QString &reason = "Cancelled");
    HttpResult result() const { return response; }
signals:
    void finished();
private:
    void request(bool appleTrust = false);
    void metadata();
    void drain();
    void complete(const QString &error = {});
    HttpTransport &transport;
    HttpRequest original;
    QByteArray appleRoot;
    HeaderConsumer onHeaders;
    ChunkConsumer onChunk;
    QPointer<QNetworkReply> reply;
    QPointer<QNetworkAccessManager> attempt;
    QTimer deadline;
    HttpResult response;
    QUrl current;
    int redirects = 0;
    bool started = false, terminal = false, deliveredHeaders = false, trustRetried = false;
};
}
