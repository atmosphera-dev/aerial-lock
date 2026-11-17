#pragma once

#include <QJsonObject>
#include <QList>
#include <QUrl>

namespace Aerial {
struct Representation {
    QString sourceId, assetId, key, variant;
    QUrl url;
    int height = 0;
    qint64 bytes = -1;
    QByteArray checksum;
    QJsonObject metadata;
};

struct ResolvedManifest {
    QList<Representation> representations;
    QUrl next;
    QString error;
    bool valid() const { return error.isEmpty() && (!next.isEmpty() || !representations.isEmpty()); }
};

class SourceResolver {
public:
    // Parsing bounds limit untrusted metadata, not the user's content cache.
    static constexpr qint64 maximumManifest = 32 * 1024 * 1024;
    static bool remoteUrl(const QUrl &url);
    static QUrl resolveRemote(const QUrl &base, const QUrl &reference);
    static QUrl manifestUrl(const QJsonObject &source);
    static ResolvedManifest parse(const QJsonObject &source, const QUrl &base, const QByteArray &bytes);
    static QList<Representation> select(const QList<Representation> &all, const QJsonObject &source,
                                       int preferred, const QList<int> &allowed, bool manual);
    static void smallestFirst(QList<Representation> &items);
};
}
