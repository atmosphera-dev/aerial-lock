#include "sources-controller.h"
#include "portal-client.h"
#include <QJsonDocument>
#include <QUrl>
#include <QUuid>
#include <QFileInfo>

namespace Aerial::Settings {
namespace {
int indexOf(const QJsonArray &array, const QString &id)
{
    for (int i = 0; i < array.size(); ++i) if (array[i].toObject().value("id").toString() == id) return i;
    return -1;
}
QString failure(const QString &name)
{
    if (name.endsWith(".AcceptedResultUnavailable")) return "The previous service accepted this source change, but its result is unavailable. Check the source list before trying again.";
    return name.endsWith(".Conflict") ? "Changed elsewhere. Your draft is kept. Close and reopen to edit the latest source list."
        : "Couldn't save sources: " + name.section('.', -1) + ". Your draft is kept.";
}
}
SourcesController::SourcesController(Client::ServiceConnection &service, PortalClient &portal, QObject *parent) : QObject(parent), service(service), portal(portal)
{
    connect(&portal, &PortalClient::finished, this, [this](quint64 id, const QString &path, bool cancelled, const QString &error) {
        if (id != chooser) return;
        chooser = 0;
        if (!opened || cancelled) return;
        if (!error.isEmpty()) { problem = error; emit changed(); }
        else field("source", path);
    });
    connect(&service, &Client::ServiceConnection::changed, this, [this] {
        auto rows = computeRows();
        if (rows != shown) { shown = std::move(rows); emit rowsChanged(); }
        emit changed();
    });
    connect(&service, &Client::ServiceConnection::mutationFinished, this, [this](quint64 id, const QVariant &, const QString &error) {
        if (id == refresh) { refresh = 0; actionProblem = error.isEmpty() ? QString() : error.endsWith(".AcceptedResultUnavailable")
            ? "The previous service accepted the retry. Check Recent activity before trying again." : "Couldn't retry: " + error.section('.', -1); emit changed(); }
        if (id != pending) return;
        pending = 0;
        auto &issue = savingSheet ? problem : actionProblem;
        if (error.isEmpty()) { issue.clear(); if (savingSheet) { opened = false; fields = {}; captured = {}; } }
        else issue = savingSheet || error.endsWith(".AcceptedResultUnavailable") ? failure(error) : "Couldn't update sources: " + error.section('.', -1);
        this->service.refresh(); emit changed();
    });
    shown = computeRows();
}
QJsonArray SourcesController::sources() const
{
    auto array = service.snapshot().effective.value("sources").toArray();
    for (int i = 0; i < array.size(); ++i) { auto item = array[i].toObject(); item.remove("safeName"); array[i] = item; }
    return array;
}
QVariantList SourcesController::computeRows() const
{
    QVariantList result;
    QMap<QString, QString> localPaths;
    for (const auto &entry : sources()) { const auto item = entry.toObject(); localPaths[item.value("id").toString()] = item.value("source").toString(); }
    for (const auto &entry : service.snapshot().sources) {
        auto row = entry.toMap();
        const auto type = row.value("type").toString();
        const bool remote = type == "apple" || type == "community";
        const auto error = row.value("errorCode").toString();
        QString description = type == "apple" ? "Apple aerials" : type == "community" ? "Community pack" : type == "folder" ? "Folder" : "Video file";
        if (remote) {
            description += ", " + row.value("readyClips").toString() + " downloaded";
            if (row.value("activeDownloads").toUInt()) description += ", " + row.value("activeDownloads").toString() + " downloading";
        } else {
            const auto id = row.value("id").toString();
            if (localPaths.contains(id)) description += ", " + localPaths.value(id);
        }
        const bool failing = row.value("enabled", true).toBool() && !error.isEmpty();
        if (failing) description = "Couldn't get the video list: " + error + (row.value("httpStatus").toUInt() ? " (" + row.value("httpStatus").toString() + ")" : "");
        row["description"] = description; row["canRetry"] = remote && failing;
        row["glyph"] = type == "apple" ? "tv" : type == "folder" ? "folder" : type == "file" ? "movie" : "world";
        result << row;
    }
    return result;
}
bool SourcesController::appleAvailable() const
{
    for (const auto &entry : sources()) if (entry.toObject().value("type") == "apple") return false;
    return !service.snapshot().properties.value("DefaultSources").toList().isEmpty();
}
QVariantMap SourcesController::draft() const
{
    auto result = fields.toVariantMap();
    result["editing"] = !editing.isEmpty(); result["pending"] = pending != 0;
    result["error"] = problem;
    return result;
}
void SourcesController::begin(const QString &id)
{
    if (!service.ready() || pending) return;
    const auto next = sources(); const auto i = indexOf(next, id);
    if (!id.isEmpty() && i < 0) return;
    captured = next; revision = service.snapshot().revision; owner = service.uniqueOwner(); editing = id;
    fields = i < 0 ? QJsonObject{{"type", "community"}, {"name", ""}, {"source", ""}, {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}} : captured[i].toObject();
    problem.clear(); opened = true; emit changed();
}
void SourcesController::field(const QString &key, const QVariant &value)
{
    if (!opened || pending || !QStringList{"name", "source", "type", "maxResolution"}.contains(key)) return;
    if (key == "type" && chooser) { const auto id = chooser; chooser = 0; portal.cancel(id); }
    if (key == "type" && value.toString() == "defaultApple") {
        if (!appleAvailable() || !editing.isEmpty() || fields.value("type") == "apple") return;
        const auto defaults = service.snapshot().properties.value("DefaultSources").toList();
        for (const auto &entry : defaults) {
            const auto item = QJsonObject::fromVariantMap(entry.toMap());
            if (item.value("type") == "apple") {
                const auto generated = fields.value("id"); fields = item; fields.remove("safeName");
                if (indexOf(sources(), fields.value("id").toString()) >= 0) fields["id"] = generated;
                break;
            }
        }
    } else if (key == "maxResolution" && (!value.isValid() || value.toInt() == 0)) fields.remove(key);
    else fields[key] = QJsonValue::fromVariant(value);
    problem.clear(); emit changed();
}
void SourcesController::dismiss()
{
    if (pending || (!opened && !chooser)) return;
    opened = false;
    if (chooser) { const auto id = chooser; chooser = 0; portal.cancel(id); }
    fields = {}; captured = {}; problem.clear(); emit changed();
}
void SourcesController::choosePath()
{
    if (!opened || chooser || !QStringList{"folder", "file"}.contains(fields.value("type").toString())) return;
    chooser = portal.choose(fields.value("type") == "folder");
}
void SourcesController::submit(const QJsonArray &array, const QString &rev, const QString &capturedOwner, bool sheet)
{
    if (pending) return;
    auto &issue = sheet ? problem : actionProblem;
    const auto &snapshot = service.snapshot();
    // The effective list is a lossy read projection when normalization drops
    // invalid raw entries. Never use it as a replacement write in that state.
    if (snapshot.user.contains("sources") && (!snapshot.user.value("sources").isArray()
        || snapshot.user.value("sources").toArray().size() != snapshot.effective.value("sources").toArray().size())) {
        issue = "Some configured sources were omitted as invalid. Repair the sources in your config file before editing this list. No entries were changed."; emit changed(); return;
    }
    if (!service.ready() || capturedOwner != service.uniqueOwner()) { issue = "The service changed or is unavailable." + (sheet ? QString(" Your draft is kept. Close and reopen before saving.") : QString()); emit changed(); return; }
    savingSheet = sheet; issue.clear(); pending = service.updateSettings(rev, {{"sources", array}}); emit changed();
}
void SourcesController::save()
{
    if (!opened || pending || chooser) return;
    problem.clear();
    const auto type = fields.value("type").toString(); const auto address = fields.value("source").toString();
    const auto existing = indexOf(captured, editing);
    const bool unchangedPath = existing >= 0 && captured[existing].toObject().value("type") == fields.value("type")
        && captured[existing].toObject().value("source") == fields.value("source");
    const QUrl url(address, QUrl::TolerantMode);
    if (fields.value("name").toString().trimmed().isEmpty()) problem = "Enter a name.";
    else if (type == "apple" || type == "community") {
        if (!remoteContentUrl(url)) problem = "Enter an HTTP or HTTPS address without a username/password prefix or fragment.";
    } else if ((type != "folder" && type != "file") || address.isEmpty() || (!unchangedPath && !address.startsWith('/'))) problem = "Choose a local folder or video file.";
    else if (!unchangedPath && !(type == "folder" ? QFileInfo(address).isDir() : QFileInfo(address).isFile())) problem = type == "folder" ? "Choose an existing local folder." : "Choose an existing local video file.";
    if (fields.contains("minResolution") && fields.contains("maxResolution") && fields["minResolution"].toInt() > fields["maxResolution"].toInt()) problem = "Highest quality must not be below this source's minimum quality.";
    if (!problem.isEmpty()) { emit changed(); return; }
    auto list = captured; const auto index = indexOf(list, editing);
    if (index < 0) list.append(fields); else list[index] = fields;
    submit(list, revision, owner, true);
}
void SourcesController::toggle(const QString &id, bool enabled)
{
    auto list = sources(); const auto index = indexOf(list, id); if (index < 0) return;
    auto item = list[index].toObject(); item["enabled"] = enabled; list[index] = item;
    submit(list, service.snapshot().revision, service.uniqueOwner(), false);
}
void SourcesController::remove(const QString &id)
{
    auto list = sources(); const auto index = indexOf(list, id); if (index < 0) return;
    list.removeAt(index); submit(list, service.snapshot().revision, service.uniqueOwner(), false);
}
void SourcesController::retry(const QString &id) { if (service.ready() && !refresh) { actionProblem.clear(); refresh = service.refreshSources({id}); emit changed(); } }
void SourcesController::beginReorder(const QString &id)
{
    dragged = sources(); dragId = id; dragRevision = service.snapshot().revision; dragOwner = service.uniqueOwner();
}
void SourcesController::dropBefore(const QString &id)
{
    const auto from = indexOf(dragged, dragId); const auto to = id.isEmpty() ? dragged.size() : indexOf(dragged, id);
    if (from >= 0 && to >= 0 && from != to && from + 1 != to) {
        auto moved = dragged.takeAt(from); dragged.insert(to > from ? to - 1 : to, moved);
        submit(dragged, dragRevision, dragOwner, false);
    }
    dragId.clear(); dragged = {};
}
}
