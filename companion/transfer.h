#pragma once

#include "cache-store.h"
#include "http-operation.h"
#include "local-probe.h"
#include <QPointer>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <functional>

namespace Aerial {
// A serialized queue owns these operations. Cancellation/policy pause preserves
// only a validator-bound partial; successful termination, size/checksum and an
// isolated decode all precede index publication.
class Transfer : public QObject {
    Q_OBJECT
public:
    Transfer(CacheStore &cache, HttpTransport &transport, Representation representation,
             QString probeExecutable, QByteArray appleRoot, std::function<qint64()> clock, QObject *parent = nullptr);
    ~Transfer();
    void start();
    void cancel(const QString &reason = "Cancelled");
    QString error() const { return failure; }
    qint64 received() const { return offset; }
    qint64 total() const { return expected; }
    HttpResult response() const { return lastResponse; }
signals:
    void progress();
    void finished();
private:
    QString headers(const HttpResult &response);
    QString chunk(const QByteArray &bytes);
    void request();
    void hashPartial();
    bool persist();
    void probe();
    void finish(const QString &error);
    CacheStore &cache;
    HttpTransport &transport;
    Representation rep;
    QString probeExecutable;
    QByteArray appleRoot;
    std::function<qint64()> clock;
    QPointer<HttpOperation> http;
    QPointer<LocalProbe> worker;
    HttpResult lastResponse;
    QString failure;
    QByteArray validator;
    QUrl effectiveUrl;
    qint64 offset = 0, expected = -1, resumeOffset = 0;
    qint64 checkpointOffset = 0;
    QElapsedTimer checkpointAge;
    int file = -1;
    int hashInput = -1;
    qint64 hashed = 0;
    QCryptographicHash checksum{QCryptographicHash::Sha256};
    bool terminal = false, resumedComplete = false, started = false;
};
}
