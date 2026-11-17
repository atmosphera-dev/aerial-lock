#pragma once
#include "cache-store.h"
#include "http-operation.h"
#include <QPointer>

namespace Aerial {
class ManifestStore : public QObject {
    Q_OBJECT
public:
    ManifestStore(CacheStore &cache, QNetworkAccessManager &transport, QByteArray appleRoot, QObject *parent = nullptr);
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
    void fetch(const QUrl &url);
    void settle(const QString &error);
    CacheStore &cache;
    QNetworkAccessManager &transport;
    QByteArray appleRoot, body;
    QJsonObject source, prior;
    QPointer<HttpOperation> operation;
    QList<Representation> items;
    QString failure;
    HttpResult lastResponse;
    int links = 0;
};

// Discover every candidate length before selecting "smallest". The range
// fallback cancels at headers, even if the peer ignores Range and sends 200.
class RepresentationSizer : public QObject {
    Q_OBJECT
public:
    RepresentationSizer(QNetworkAccessManager &transport, QByteArray appleRoot, QObject *parent = nullptr);
    ~RepresentationSizer();
    void start(QList<Representation> items);
    void cancel();
    QList<Representation> result() const { return items; }
signals:
    void finished();
private:
    void next();
    void request(bool range);
    QNetworkAccessManager &transport;
    QByteArray appleRoot;
    QList<Representation> items;
    qsizetype index = 0;
    QPointer<HttpOperation> operation;
    bool cancelled = false;
};
}
