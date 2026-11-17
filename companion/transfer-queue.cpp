#include "transfer-queue.h"
#include "transfer-queue.moc"
#include "../common/content-snapshot.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <algorithm>
#include <limits>

namespace Aerial {
namespace {
QString manifestKey(const QJsonObject &source)
{
    return contentDigest(QJsonDocument(QJsonArray{"manifest", source.value("id"), source.value("type"), source.value("source")})
        .toJson(QJsonDocument::Compact));
}
bool terminal(const QJsonObject &job) { return job.contains("finishedAt"); }
QStringList ids(const QJsonObject &job)
{
    QStringList result;
    for (const auto &id : job.value("sources").toArray()) result.append(id.toString());
    return result;
}
}

TransferQueue::TransferQueue(ConfigRepository &config, CacheStore &cache, PlannerState &state,
    QNetworkAccessManager &transport, QString probe, QByteArray appleRoot, std::function<qint64()> clock, QObject *parent)
    : QObject(parent), config(config), cache(cache), state(state), transport(transport),
      probe(std::move(probe)), appleRoot(std::move(appleRoot)), clock(std::move(clock))
{
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, &TransferQueue::evaluate);
    connect(&config, &ConfigRepository::changed, this, [this] { invalidate(); });
}

TransferQueue::~TransferQueue()
{
    // Accepted jobs remain nonterminal on disk and are re-evaluated on restart.
    for (QObject *operation : {static_cast<QObject *>(manifest), static_cast<QObject *>(sizer), static_cast<QObject *>(transfer)}) {
        if (operation) { disconnect(operation, nullptr, this, nullptr); delete operation; }
    }
}

void TransferQueue::start()
{
    if (running) return;
    running = true;
    if (!cache.valid() || !state.valid() || !state.initializeRotation()) { publishState("error", "StorageError"); return; }
    const auto saved = state.jobs();
    for (auto it = saved.begin(); it != saved.end(); ++it)
        if (!terminal(it.value().toObject())) pending.append(it.key());
    std::sort(pending.begin(), pending.end(), [&saved](const QString &a, const QString &b) {
        const auto left = saved.value(a).toObject().value("createdAt").toInteger();
        const auto right = saved.value(b).toObject().value("createdAt").toInteger();
        return left == right ? a < b : left < right;
    });
    evaluate();
}

QString TransferQueue::submit(const QString &kind, const QStringList &sourceIds, int limit, bool dryRun)
{
    if (!running || !cache.valid() || !state.valid() || limit < 0) return {};
    const auto id = contentDigest(QUuid::createUuid().toByteArray());
    const QJsonObject record{{"kind", kind}, {"sources", QJsonArray::fromStringList(sourceIds)}, {"limit", limit},
        {"dryRun", dryRun}, {"createdAt", clock()}, {"state", "queued"}, {"received", 0}, {"totalKnown", false},
        {"total", 0}, {"ready", 0}, {"errors", QJsonObject{}}};
    if (!state.saveJob(id, record)) { publishState("error", "StorageError"); return {}; }
    pending.append(id); emit jobAdded(id); schedule(); return id;
}
QString TransferQueue::acquire(const QStringList &sourceIds, int limit) { return submit("manual", sourceIds, limit); }
QString TransferQueue::refresh(const QStringList &sourceIds) { return submit("refresh", sourceIds); }
QString TransferQueue::prune(bool dryRun) { return submit("prune", {}, 0, dryRun); }

QString TransferQueue::automaticReason() const
{
    const auto &doc = config.document();
    if (!doc.acquisitionAllowed()) return "InvalidConfig";
    const auto automatic = doc.effective.value("acquisition").toObject().value("automatic").toObject();
    if (!automatic.value("enabled").toBool()) return "Paused";
    if (automatic.value("allowedResolutions").toArray().isEmpty()) return "NoAllowedResolution";
    return policyReason;
}

void TransferQueue::ensure(const QStringList &requestedTopics)
{
    topics = requestedTopics;
    for (const auto &id : pending) if (job(id).value("kind") == "automatic") return;
    if (!active.isEmpty() && current.value("kind") == "automatic") return;
    if (!automaticReason().isEmpty()) {
        if (active.isEmpty()) publishState(automaticReason() == "Paused" ? "paused" : "deferred", automaticReason());
        return;
    }
    submit("automatic", {});
}

void TransferQueue::setPolicyReason(const QString &value)
{
    if (policyReason == value) return;
    policyReason = value;
    if (!active.isEmpty() && current.value("kind") != "automatic") { evaluate(); return; }
    invalidate();
}

void TransferQueue::invalidate()
{
    if (!running) return;
    // All config changes retire the current decision snapshot; manual jobs stay
    // accepted but must re-evaluate hard bands/source selection and new limits.
    invalidated = true;
    if (transfer) transfer->cancel("Reevaluate");
    else if (manifest) manifest->cancel();
    else if (sizer) sizer->cancel();
    else schedule();
    evaluate();
}

void TransferQueue::evaluate()
{
    if (!running) return;
    const auto settings = config.document().effective;
    const auto limits = settings.value("cache").toObject();
    cache.configure(limits.value("maxBytes").toInteger(), limits.value("minFreeBytes").toInteger());
    const auto usage = cache.usage();
    if (!transfer && usage.safe && usage.bytes > quint64(limits.value("maxBytes").toInteger())) {
        const auto maintenance = cache.prune(false, {}, usage.bytes - quint64(limits.value("maxBytes").toInteger()));
        if (maintenance.value("removedBytes").toInteger() > 0) emit contentChanged();
        if (maintenance.contains("error")) { publishState("error", maintenance.value("error").toString()); return; }
    }
    armCheck();
    ensure(topics); schedule(); emit changed();
}

void TransferQueue::armCheck()
{
    const auto settings = config.document().effective;
    const auto interval = settings.value("acquisition").toObject().value("checkIntervalSeconds").toInteger();
    next = clock() + std::min(std::max<qint64>(1, interval), std::numeric_limits<qint64>::max() - std::max<qint64>(0, clock()));
    // A failed item can wake sooner than the ordinary maintenance interval.
    for (const auto &rep : catalogue) {
        const auto retry = state.nextAttempt(rep.key);
        if (retry > clock()) next = std::min(next, retry);
    }
    for (const auto &value : settings.value("sources").toArray()) {
        const auto retry = state.nextAttempt(manifestKey(value.toObject()));
        if (retry > clock()) next = std::min(next, retry);
    }
    const auto milliseconds = std::min<qint64>(std::max<qint64>(1, next - clock()), std::numeric_limits<int>::max() / 1000) * 1000;
    timer.start(int(milliseconds));
}

void TransferQueue::schedule()
{
    if (scheduled || !running) return;
    scheduled = true;
    QTimer::singleShot(0, this, [this] { scheduled = false; pump(); });
}

bool TransferQueue::save()
{
    if (!state.saveJob(active, current)) { publishState("error", "StorageError"); return false; }
    emit jobChanged(active); return true;
}

void TransferQueue::publishState(const QString &value, const QString &defer)
{
    phase = value; reason = defer; emit changed();
}

void TransferQueue::pump()
{
    if (transfer || manifest || sizer) return;
    if (!active.isEmpty()) {
        if (!invalidated) return;
        current["state"] = "queued";
        if (!save()) return;
        pending.prepend(active); active.clear();
    }
    invalidated = false;
    if (pending.isEmpty()) {
        const auto blocked = automaticReason();
        if (!blocked.isEmpty()) publishState(blocked == "Paused" ? "paused" : "deferred", blocked);
        else if (phase == "planning" || phase == "downloading") publishState("idle");
        return;
    }
    active = pending.takeFirst(); current = state.jobs().value(active).toObject();
    if (terminal(current)) { active.clear(); schedule(); return; }
    current["state"] = "planning"; current["received"] = 0; current["totalKnown"] = false;
    if (!save()) return;
    publishState("planning");
    const auto &doc = config.document();
    sources.clear(); catalogue.clear(); attempted.clear(); available.clear(); sourceIndex = 0;
    if (current.value("kind") == "prune") {
        const auto result = cache.prune(current.value("dryRun").toBool());
        current["prune"] = result; emit contentChanged(); finish(result.value("error").toString()); return;
    }
    if (!doc.acquisitionAllowed()) { finish("InvalidConfig"); return; }
    if (!reconcile()) { finish("StorageError"); return; }
    if (current.value("kind") == "automatic" && !automaticReason().isEmpty()) { finish(automaticReason()); return; }
    if (doc.effective.value("cache").toObject().value("maxBytes").toInteger() == 0) { finish("CacheFull"); return; }
    const auto requested = ids(current);
    for (const auto &value : doc.effective.value("sources").toArray()) {
        const auto source = value.toObject();
        if (source.value("type") != "apple" && source.value("type") != "community") continue;
        if (requested.isEmpty() || requested.contains(source.value("id").toString())) sources.append(source);
    }
    if (sources.isEmpty()) { finish("NoRemoteSources"); return; }
    nextManifest();
}

void TransferQueue::nextManifest()
{
    if (invalidated) { schedule(); return; }
    if (sourceIndex == sources.size()) {
        if (current.value("kind") == "refresh") {
            finish(catalogue.isEmpty() ? "TransferFailed" : QString()); return;
        }
        sizeCandidates(); return;
    }
    const auto source = sources[sourceIndex++];
    const auto key = manifestKey(source);
    manifest = new ManifestStore(cache, transport, appleRoot, this);
    const auto cached = manifest->cached(source);
    if (current.value("kind") == "refresh") {
        state.clearFailure(key);
        for (const auto &rep : cached) state.clearFailure(rep.key);
    }
    if (!state.eligible(key)) {
        catalogue.append(cached); delete manifest; manifest = nullptr; nextManifest(); return;
    }
    connect(manifest, &ManifestStore::finished, this, [this, source, key, cached] {
        const auto failure = manifest->error();
        const auto response = manifest->response();
        const auto result = manifest->result();
        manifest->deleteLater(); manifest = nullptr;
        if (invalidated) { schedule(); return; }
        if (failure.isEmpty()) { catalogue.append(result); state.clearFailure(key); }
        else {
            catalogue.append(cached);
            if (failure != "CacheFull" && failure != "StorageError")
                state.failed(key, failure, response.status, response.headers.value("retry-after"));
            auto errors = current.value("errors").toObject(); errors[source.value("id").toString()] = failure;
            current["errors"] = errors;
        }
        if (!save()) return;
        nextManifest();
    });
    manifest->refresh(source);
}

QList<Representation> TransferQueue::selected() const
{
    const auto settings = config.document().effective;
    QList<int> allowed;
    for (const auto &value : settings.value("acquisition").toObject().value("automatic").toObject().value("allowedResolutions").toArray())
        allowed.append(value.toInt());
    QList<Representation> result;
    for (const auto &source : sources)
        result.append(SourceResolver::select(catalogue, source, settings.value("preferredResolution").toInt(), allowed,
            current.value("kind") == "manual"));
    return result;
}

bool TransferQueue::reconcile()
{
    QSet<QString> known, completed, manifests, representations;
    // Reconcile against the complete configured catalogue, not just this job's
    // selected sources; a manual request must not erase another source's retry.
    ManifestStore inventory(cache, transport, appleRoot);
    for (const auto &value : config.document().effective.value("sources").toArray()) {
        const auto source = value.toObject(); known.insert(manifestKey(source));
        if (source.value("type") != "apple" && source.value("type") != "community") continue;
        manifests.insert(inventory.path(source));
        for (const auto &rep : inventory.cached(source)) { known.insert(rep.key); representations.insert(rep.key); }
    }
    for (const auto &rep : catalogue) { known.insert(rep.key); representations.insert(rep.key); }
    for (const auto &entry : cache.entries()) completed.insert(entry.toObject().value("key").toString());
    return state.reconcile(known, completed) && cache.reconcile(manifests, representations);
}

void TransferQueue::sizeCandidates()
{
    if (!reconcile()) { finish("StorageError"); return; }
    sizer = new RepresentationSizer(transport, appleRoot, this);
    auto candidates = selected();
    // Completed representations need neither HEAD traffic nor transfer work.
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [this](const Representation &rep) { return cache.contains(rep.key); }), candidates.end());
    connect(sizer, &RepresentationSizer::finished, this, [this] {
        const auto sizes = sizer->result(); sizer->deleteLater(); sizer = nullptr;
        if (invalidated) { schedule(); return; }
        for (const auto &sized : sizes)
            for (auto &rep : catalogue) if (rep.key == sized.key) rep.bytes = sized.bytes;
        for (const auto &rep : selected()) if (cache.contains(rep.key)) ++available[rep.sourceId];
        nextTransfer();
    });
    sizer->start(candidates);
}

bool TransferQueue::finalizeRotation()
{
    const auto rotation = state.rotation();
    if (rotation.isEmpty()) return true;
    const auto victim = rotation.value("victim").toString(), replacement = rotation.value("replacement").toString();
    if (!cache.contains(replacement)) return true;
    if (cache.contains(victim) && !cache.evict(victim)) return false;
    if (!state.finishRotation()) return false;
    emit contentChanged(); return true;
}

int TransferQueue::readyCount() const
{
    int count = 0;
    for (const auto &rep : selected()) if (cache.contains(rep.key)) ++count;
    return count;
}

void TransferQueue::nextTransfer()
{
    if (invalidated) { schedule(); return; }
    if (current.value("kind") == "automatic" && !automaticReason().isEmpty()) { finish(automaticReason()); return; }
    const auto settings = config.document().effective;
    if (!finalizeRotation()) { finish("PlaybackProtected"); return; }
    auto plan = AcquisitionPlanner::evaluate(settings, catalogue, cache.entries(), state, clock(),
        current.value("kind") == "manual", ids(current), topics);
    const auto limit = current.value("limit").toInt();
    auto pendingRotation = state.rotation();
    if (!pendingRotation.isEmpty()) {
        const auto eligible = selected();
        const bool configured = std::any_of(eligible.begin(), eligible.end(), [&](const Representation &rep) {
            return rep.key == pendingRotation.value("replacement").toString();
        });
        if (!configured) {
            if (!cache.contains(pendingRotation.value("victim").toString())) state.rotated(pendingRotation.value("victim").toString());
            if (!state.clearRotation()) { finish("StorageError"); return; }
            pendingRotation = {};
        }
    }
    plan.candidates.erase(std::remove_if(plan.candidates.begin(), plan.candidates.end(), [&](const Representation &rep) {
        return attempted.contains(rep.key) || (limit > 0 && available.value(rep.sourceId) >= limit)
            || (!pendingRotation.isEmpty() && rep.key != pendingRotation.value("replacement").toString());
    }), plan.candidates.end());
    if (plan.candidates.isEmpty()) { finish(readyCount() ? QString() : plan.deferReason.isEmpty() ? "NoEligibleAssets" : plan.deferReason); return; }
    const bool automatic = current.value("kind") == "automatic";
    // Select the smallest fitting eligible candidate, not the first catalogue
    // row. Publication overhead is computed by the cache from its actual index.
    auto candidate = std::find_if(plan.candidates.begin(), plan.candidates.end(), [this](const Representation &rep) {
        return rep.bytes < 0 || cache.canAcquire(rep);
    });
    QString victim = pendingRotation.value("victim").toString();
    if (victim.isEmpty() && automatic) victim = plan.rotationVictim;
    if (candidate == plan.candidates.end() && !victim.isEmpty() && cache.entries().size() > 1) {
        const auto &rep = plan.candidates.first();
        if (!state.beginRotation(victim, rep.key)) { finish("StorageError"); return; }
        if (!cache.evict(victim)) { finish("PlaybackProtected"); return; }
        emit contentChanged();
        candidate = std::find_if(plan.candidates.begin(), plan.candidates.end(), [this](const Representation &item) { return item.bytes < 0 || cache.canAcquire(item); });
    }
    if (candidate == plan.candidates.end()) { finish("CacheFull"); return; }
    const auto rep = *candidate;
    if (!victim.isEmpty() && state.rotation().isEmpty() && !state.beginRotation(victim, rep.key)) { finish("StorageError"); return; }
    attempted.insert(rep.key);
    current["sourceId"] = rep.sourceId; current["assetId"] = rep.assetId; current["assetKey"] = rep.key;
    current["state"] = "downloading"; current["totalKnown"] = rep.bytes >= 0; current["total"] = std::max<qint64>(0, rep.bytes);
    if (!save()) return;
    publishState("downloading");
    transfer = new Transfer(cache, transport, rep, probe, appleRoot, clock, this);
    connect(transfer, &Transfer::progress, this, [this] {
        current["received"] = transfer->received(); current["totalKnown"] = transfer->total() >= 0;
        current["total"] = std::max<qint64>(0, transfer->total());
        // Progress is observable in memory; only terminal/decision transitions
        // need durable job writes. The transfer persists its resumable bytes.
        emit jobChanged(active); emit changed();
    });
    connect(transfer, &Transfer::finished, this, [this, rep] {
        const auto failure = transfer->error();
        const auto http = transfer->response();
        current["received"] = transfer->received();
        transfer->deleteLater(); transfer = nullptr;
        if (invalidated) { schedule(); return; }
        if (failure.isEmpty()) {
            state.clearFailure(rep.key); state.reintroduced(rep.key); ++available[rep.sourceId]; emit contentChanged();
        } else if (failure != "CacheFull" && failure != "Cancelled") {
            state.failed(rep.key, failure, http.status, http.headers.value("retry-after"));
            auto errors = current.value("errors").toObject(); errors[rep.sourceId] = failure; current["errors"] = errors;
        }
        if (!save()) return;
        if (failure == "CacheFull") { finish(failure); return; }
        if (failure == "Cancelled") { finish(failure); return; }
        QTimer::singleShot(0, this, &TransferQueue::nextTransfer);
    });
    transfer->start();
}

void TransferQueue::finish(const QString &error)
{
    const bool deferred = error == "Paused" || error == "NoAllowedResolution" || error == policyReason || error == "PlaybackProtected" || error == "Backoff";
    current["ready"] = readyCount(); current["errorCode"] = error;
    current["state"] = error.isEmpty() ? "succeeded" : error == "Cancelled" ? "cancelled" : deferred ? "deferred" : "failed";
    current["finishedAt"] = clock();
    if (!save()) return;
    active.clear();
    armCheck();
    publishState(error.isEmpty() ? "idle" : deferred ? "deferred" : "error", error);
    schedule();
}

bool TransferQueue::cancel(const QString &id)
{
    if (id == active) {
        invalidated = false;
        current["cancelRequested"] = true;
        if (transfer) transfer->cancel();
        else if (manifest) { disconnect(manifest, nullptr, this, nullptr); delete manifest; manifest = nullptr; finish("Cancelled"); }
        else if (sizer) { disconnect(sizer, nullptr, this, nullptr); delete sizer; sizer = nullptr; finish("Cancelled"); }
        else finish("Cancelled");
        return true;
    }
    const auto record = job(id);
    if (record.isEmpty() || terminal(record)) return false;
    auto cancelled = record; cancelled["state"] = "cancelled"; cancelled["errorCode"] = "Cancelled"; cancelled["finishedAt"] = clock();
    if (!state.saveJob(id, cancelled)) return false;
    pending.removeAll(id); emit jobChanged(id); return true;
}
}
