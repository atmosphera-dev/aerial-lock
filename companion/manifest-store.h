#pragma once
#include "cache-store.h"
#include "http-operation.h"
#include <QPointer>

namespace Aerial {
class ManifestStore : public QObject {
    Q_OBJECT
public:
    ManifestStore(CacheStore &cache, HttpTransport &transport, QByteArray appleRoot, QObject *parent = nullptr);
    ~ManifestStore();
    QList<Representation> cached(const QJsonObject &source) const;
    void refresh(const QJsonObject &source);
    void cancel();
    QList<Representation> result() const { return items; }
    QString error() const { return failure; }
    HttpResult response() const { return lastResponse; }
    QString path(const QJsonObject &source) const;
signals:
    void finished();
private:
    void fetch(const QUrl &url, bool unconditional = false);
    void settle(const QString &error);
    CacheStore &cache;
    HttpTransport &transport;
    QByteArray appleRoot, body;
    QJsonObject source, prior;
    QPointer<HttpOperation> operation;
    QList<Representation> items;
    QString failure;
    HttpResult lastResponse;
    int links = 0;
    bool retried304 = false;
};

// Bounded parallel sizing retains discovered lengths when the pass deadline
// expires. Unresolved candidates remain unknown, ordered after known lengths.
class RepresentationSizer : public QObject {
    Q_OBJECT
public:
    RepresentationSizer(HttpTransport &transport, QByteArray appleRoot, QObject *parent = nullptr,
                        int deadlineMs = 30000, int maximumConcurrent = 4);
    ~RepresentationSizer();
    void start(QList<Representation> items);
    void cancel();
    QList<Representation> result() const { return items; }
signals:
    void finished();
private:
    void next();
    void request(qsizetype item, bool range);
    void settle();
    HttpTransport &transport;
    QByteArray appleRoot;
    QList<Representation> items;
    qsizetype index = 0;
    QMap<qsizetype, HttpOperation *> operations;
    QTimer deadline;
    int deadlineMs, maximumConcurrent;
    bool started = false, settled = false;
};
}
