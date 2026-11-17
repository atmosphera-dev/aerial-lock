#pragma once

#include <QMap>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace Aerial {
using HttpHeaders = QMap<QByteArray, QByteArray>;
struct HttpResult {
    int status = 0;
    QUrl url;
    HttpHeaders headers;
    QString error;
};
struct HttpRequest {
    QUrl url;
    HttpHeaders headers;
    bool head = false;
};

// One bounded asynchronous HTTP transaction, including manually checked
// redirects. All errors are stable codes, never token-bearing URLs or text.
class HttpOperation : public QObject {
    Q_OBJECT
public:
    using HeaderConsumer = std::function<QString(const HttpResult &)>;
    using ChunkConsumer = std::function<QString(const QByteArray &)>;
    HttpOperation(QNetworkAccessManager &transport, HttpRequest request, const QByteArray &appleRoot,
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
    QNetworkAccessManager &transport;
    HttpRequest original;
    QByteArray appleRoot;
    HeaderConsumer onHeaders;
    ChunkConsumer onChunk;
    QPointer<QNetworkReply> reply;
    QTimer deadline;
    HttpResult response;
    QUrl current;
    int redirects = 0;
    bool started = false, terminal = false, deliveredHeaders = false, trustRetried = false;
};
}
