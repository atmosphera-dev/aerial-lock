#include "source-resolver.h"
#include "../common/content-snapshot.h"
#include "../common/config-document.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>

namespace Aerial {
bool SourceResolver::remoteUrl(const QUrl &url)
{
    return remoteContentUrl(url);
}

QUrl SourceResolver::manifestUrl(const QJsonObject &source)
{
    QUrl url(source.value("source").toString());
    if (!remoteUrl(url)) return {};
    if (!url.path().endsWith(".json") && !url.path().endsWith(".tar")) {
        auto path = url.path();
        if (!path.endsWith('/')) path += '/';
        url.setPath(path + "manifest.json");
    }
    return url;
}

QUrl SourceResolver::resolveRemote(const QUrl &base, const QUrl &reference)
{
    // Resolving an absent reference would silently reuse the base URL. This
    // boundary applies equally to media links, manifest wrappers and redirects.
    if (!reference.isValid() || reference.toString().trimmed().isEmpty()) return {};
    const auto url = base.resolved(reference);
    if (!remoteUrl(url) || (base.scheme() == "https" && url.scheme() != "https")) return {};
    return url;
}

ResolvedManifest SourceResolver::parse(const QJsonObject &source, const QUrl &base, const QByteArray &bytes)
{
    ResolvedManifest result;
    if (!remoteUrl(base) || bytes.size() > maximumManifest) { result.error = "InvalidManifest"; return result; }
    QByteArray json = bytes;
    QJsonParseError error;
    auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError) {
        // libarchive is used as a member reader, never an extractor. Even a
        // matching name must be a regular file, with no link target.
        archive *reader = archive_read_new();
        archive_read_support_format_tar(reader);
        archive_read_support_filter_none(reader);
        bool found = false, valid = true;
        if (archive_read_open_memory(reader, bytes.constData(), bytes.size()) != ARCHIVE_OK) valid = false;
        archive_entry *entry = nullptr;
        int count = 0, status = ARCHIVE_EOF;
        while (valid && (status = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
            if (++count > 10000) { valid = false; break; }
            const auto name = QByteArray(archive_entry_pathname(entry));
            if (name != "entries.json" && name != "./entries.json") continue;
            if (found || archive_entry_filetype(entry) != AE_IFREG || archive_entry_symlink(entry)
                || archive_entry_hardlink(entry) || archive_entry_size(entry) <= 0
                || archive_entry_size(entry) > maximumManifest) { valid = false; break; }
            json.clear();
            char buffer[65536];
            la_ssize_t size;
            while ((size = archive_read_data(reader, buffer, sizeof(buffer))) > 0) {
                if (json.size() + size > maximumManifest) { valid = false; break; }
                json.append(buffer, size);
            }
            if (size < 0 || json.size() != archive_entry_size(entry)) valid = false;
            found = true;
        }
        if (status != ARCHIVE_EOF) valid = false;
        archive_read_free(reader);
        if (!valid || !found) { result.error = "InvalidManifest"; return result; }
        document = QJsonDocument::fromJson(json, &error);
    }
    if (error.error != QJsonParseError::NoError || !document.isObject()) { result.error = "InvalidManifest"; return result; }
    const auto object = document.object();
    if (!object.contains("assets") && object.value("manifestUrl").isString()) {
        result.next = resolveRemote(base, QUrl(object.value("manifestUrl").toString()));
        if (result.next.isEmpty()) {
            result.next = QUrl(); result.error = "UnsafeManifestLink";
        }
        return result;
    }
    if (!object.value("assets").isArray() || object.value("assets").toArray().size() > 10000) { result.error = "InvalidManifest"; return result; }
    const QList<QPair<QString, int>> variants{{"url-1080-H264", 1080}, {"url-1080-SDR", 1080},
        {"url-1080-HDR", 1080}, {"url-4K-SDR", 2160}, {"url-4K-HDR", 2160}};
    QSet<QString> identities;
    for (const auto &item : object.value("assets").toArray()) {
        const auto asset = item.toObject();
        const auto id = asset.value("id").toString();
        if (id.isEmpty() || id.size() > 1024 || id.contains(QChar(0))) continue;
        QJsonObject metadata;
        for (const auto &key : {"accessibilityLabel", "title", "scene", "timeOfDay"}) {
            if (asset.value(key).isString()) metadata[key] = asset.value(key).toString().left(4096);
        }
        if (asset.value("pointsOfInterest").isObject()) {
            const auto poi = asset.value("pointsOfInterest").toObject();
            if (QJsonDocument(poi).toJson(QJsonDocument::Compact).size() <= 256 * 1024) metadata["pointsOfInterest"] = poi;
        }
        for (const auto &variant : variants) {
            if (!asset.value(variant.first).isString()) continue;
            auto reference = QUrl(asset.value(variant.first).toString());
            // Apple's current catalogue serves every representation over TLS.
            // Older catalogues may still spell that same CDN as HTTP. Upgrade
            // only its media links, never arbitrary providers or explicit
            // source descriptors, and never retry an HTTPS failure over HTTP.
            if (reference.scheme() == "http" && reference.host() == "sylvan.apple.com") reference.setScheme("https");
            const auto url = resolveRemote(base, reference);
            if (url.isEmpty()) continue;
            Representation rep;
            rep.sourceId = source.value("id").toString(); rep.assetId = id; rep.url = url;
            rep.height = variant.second; rep.variant = variant.first; rep.metadata = metadata;
            const auto size = asset.value("sizes").toObject().value(variant.first);
            if (size.isDouble() && size.toInteger(-1) > 0 && double(size.toInteger()) == size.toDouble()) rep.bytes = size.toInteger();
            const auto hash = asset.value("sha256").toObject().value(variant.first).toString();
            if (!hash.isEmpty()) {
                if (!contentKey(hash)) continue;
                rep.checksum = hash.toLatin1();
            }
            rep.key = contentDigest(QJsonDocument(QJsonArray{rep.sourceId, id, variant.first, url.toString(QUrl::FullyEncoded),
                asset.value("revision"), hash}).toJson(QJsonDocument::Compact));
            if (!identities.contains(rep.key)) { result.representations.append(rep); identities.insert(rep.key); }
        }
    }
    if (result.representations.isEmpty()) result.error = "NoEligibleAssets";
    return result;
}

QList<Representation> SourceResolver::select(const QList<Representation> &all, const QJsonObject &source,
                                            int preferred, const QList<int> &allowed, bool manual)
{
    // Keep provider order only for codec preference at the same height.
    if (!sourceEnabled(source)) return {};
    QMap<QString, QMap<int, Representation>> assets;
    const auto low = source.value("minResolution").toInt(0), high = source.value("maxResolution").toInt(2160);
    for (const auto &rep : all) {
        if (rep.sourceId != source.value("id").toString() || rep.height < low || rep.height > high
            || (!manual && !allowed.contains(rep.height))) continue;
        if (!assets[rep.assetId].contains(rep.height)) assets[rep.assetId].insert(rep.height, rep);
    }
    QList<Representation> result;
    for (auto it = assets.cbegin(); it != assets.cend(); ++it) {
        const auto &heights = it.value();
        auto chosen = heights.find(preferred);
        if (chosen == heights.end() && preferred > 0) chosen = heights.upperBound(preferred);
        if (chosen == heights.end()) chosen = std::prev(heights.end());
        result.append(chosen.value());
    }
    smallestFirst(result);
    return result;
}

void SourceResolver::smallestFirst(QList<Representation> &items)
{
    std::sort(items.begin(), items.end(), [](const Representation &a, const Representation &b) {
        if ((a.bytes >= 0) != (b.bytes >= 0)) return a.bytes >= 0;
        if (a.bytes >= 0 && a.bytes != b.bytes) return a.bytes < b.bytes;
        return a.key < b.key;
    });
}
}
