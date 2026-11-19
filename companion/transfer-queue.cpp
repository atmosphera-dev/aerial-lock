#include "transfer-queue.h"
#include "transfer-queue.moc"
#include "../common/content-snapshot.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <QScopeGuard>
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
    HttpTransport &transport, QString probe, QByteArray appleRoot, std::function<qint64()> clock, QObject *parent)
    : QObject(parent), config(config), cache(cache), state(state), transport(transport),
      probe(std::move(probe)), appleRoot(std::move(appleRoot)), clock(std::move(clock))
{
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, &TransferQueue::evaluate);
    persistenceTimer.setSingleShot(true);
    connect(&persistenceTimer, &QTimer::timeout, this, &TransferQueue::pump);
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
    invalidated = true; ++decision;
    if (transfer) transfer->cancel("Reevaluate");
    else if (manifest) manifest->cancel();
    else if (sizer) sizer->cancel();
    else schedule();
    evaluate();
}

void TransferQueue::evaluate()
{
    if (!running) return;
    // Every evaluation owns a future wakeup, including storage-error exits.
    const auto wakeup = qScopeGuard([this] { armCheck(); emit changed(); });
    if (!transfer && !cache.valid() && !cache.recover()) { publishState("error", "StorageError"); return; }
    if (!state.valid() || !state.initializeRotation()) { publishState("error", "StorageError"); return; }
    if (!transfer && state.permanentlyFailed(state.rotation().value("replacement").toString()) && !abandonRotation()) {
        publishState("error", "StorageError"); return;
    }
    const auto settings = config.document().effective;
    const auto limits = settings.value("cache").toObject();
    cache.configure(limits.value("maxBytes").toInteger(), limits.value("minFreeBytes").toInteger());
    QSet<QString> configured;
    for (const auto &value : settings.value("sources").toArray()) {
        const auto source = value.toObject();
        if (source.value("type") == "apple" || source.value("type") == "community") configured.insert(source.value("id").toString());
    }
    cache.setSources(configured);
    const auto usage = cache.usage();
    if (!transfer && usage.safe && usage.bytes > quint64(limits.value("maxBytes").toInteger())) {
        const auto maintenance = cache.prune(false, {}, usage.bytes - quint64(limits.value("maxBytes").toInteger()));
        if (maintenance.value("removedBytes").toInteger() > 0) emit contentChanged();
        if (maintenance.contains("error")) { publishState("error", maintenance.value("error").toString()); return; }
    }
    ensure(topics); schedule();
}

void TransferQueue::armCheck()
{
    const auto settings = config.document().effective;
    const auto interval = settings.value("acquisition").toObject().value("checkIntervalSeconds").toInteger();
    next = clock() + std::min(std::max<qint64>(1, interval), std::numeric_limits<qint64>::max() - std::max<qint64>(0, clock()));
    if (!cache.valid() || (phase == "error" && reason == "StorageError"))
        next = std::min(next, clock() < std::numeric_limits<qint64>::max() ? clock() + 1 : clock());
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

QJsonObject TransferQueue::job(const QString &id) const
{
    if (id != active) return state.jobs().value(id).toObject();
    auto visible = current;
    if (afterPersistence) {
        // An in-memory terminal transition is not a durable job result.
        visible.remove("finishedAt"); visible["state"] = "recovering"; visible["errorCode"] = "StorageError";
    }
    return visible;
}

void TransferQueue::save(std::function<void()> continuation)
{
    const auto id = active;
    const auto epoch = decision;
    // Settings retire planning decisions, not an already-terminal receipt.
    // Once durable, that receipt must finish its owner rather than be requeued.
    const bool finishing = terminal(current);
    afterPersistence = [this, id, epoch, finishing, continuation = std::move(continuation)] {
        if (id != active || (!finishing && epoch != decision)) { schedule(); return; }
        continuation();
    };
    retryPersistence();
}

void TransferQueue::retryPersistence()
{
    if (!afterPersistence) return;
    if (!state.saveJob(active, current)) {
        if (state.capacityExceeded()) {
            // A permanently oversized transition cannot be retried like ENOSPC.
            // The last durable job reserved this exact terminal escape path.
            current = state.jobs().value(active).toObject();
            afterPersistence = {};
            if (current.isEmpty()) {
                // No accepted record means no reserved terminal headroom. Do
                // not recursively try to create a failure receipt in a full
                // state file; release this in-memory owner instead.
                persistenceTimer.stop();
                releaseActive();
                publishState("error", "StorageError"); armCheck();
                return;
            }
            finish("StorageError");
            return;
        }
        publishState("error", "StorageError");
        persistenceTimer.start(1000);
        return;
    }
    persistenceTimer.stop();
    auto continuation = std::move(afterPersistence);
    afterPersistence = {};
    emit jobChanged(active);
    continuation();
}

void TransferQueue::publishState(const QString &value, const QString &defer)
{
    phase = value; reason = defer; emit changed();
}

void TransferQueue::releaseActive()
{
    const auto id = active;
    active.clear(); current = {}; ++decision;
    if (!id.isEmpty()) emit jobChanged(id);
}

void TransferQueue::pump()
{
    if (transfer || manifest || sizer) return;
    if (afterPersistence) { retryPersistence(); return; }
    if (!active.isEmpty()) {
        if (!invalidated) return;
        current["state"] = "queued";
        save([this] { pending.prepend(active); releaseActive(); schedule(); });
        return;
    }
    invalidated = false;
    if (pending.isEmpty()) {
        const auto blocked = automaticReason();
        if (!blocked.isEmpty()) publishState(blocked == "Paused" ? "paused" : "deferred", blocked);
        else if (phase == "planning" || phase == "downloading") publishState("idle");
        return;
    }
    active = pending.takeFirst(); ++decision; current = state.jobs().value(active).toObject();
    if (current.isEmpty() || terminal(current)) { releaseActive(); schedule(); return; }
    current["state"] = "planning"; current["received"] = 0; current["totalKnown"] = false;
    save([this] { plan(); });
}

void TransferQueue::plan()
{
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
        save([this] { nextManifest(); });
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

QString TransferQueue::finalizeRotation()
{
    const auto rotation = state.rotation();
    if (rotation.isEmpty()) return {};
    const auto victim = rotation.value("victim").toString(), replacement = rotation.value("replacement").toString();
    if (!cache.contains(replacement)) return {};
    if (cache.contains(victim) && !cache.evict(victim)) return cache.valid() ? "PlaybackProtected" : "StorageError";
    if (!state.finishRotation()) return "StorageError";
    emit contentChanged(); return {};
}

bool TransferQueue::abandonRotation()
{
    const auto rotation = state.rotation();
    if (rotation.isEmpty()) return true;
    // Retire an already-removed victim and release its binding in one durable
    // state update. Failed pre-eviction attempts must not consume the cadence.
    return cache.contains(rotation.value("victim").toString()) ? state.clearRotation() : state.finishRotation();
}

int TransferQueue::readyCount() const
{
    int count = 0;
    QSet<QString> published;
    if (!cache.valid()) {
        // A durable commit awaiting pointer repair is not yet visible to the
        // locker. Count the public snapshot, not the failed writer's inventory.
        const ContentSnapshot snapshot(cache.path());
        for (const auto &entry : snapshot.entries()) published.insert(entry.toObject().value("key").toString());
    }
    for (const auto &rep : selected())
        if (cache.valid() ? cache.contains(rep.key) : published.contains(rep.key)) ++count;
    return count;
}

void TransferQueue::nextTransfer()
{
    if (active.isEmpty()) return;
    if (invalidated) { schedule(); return; }
    if (current.value("kind") == "automatic" && !automaticReason().isEmpty()) { finish(automaticReason()); return; }
    const auto settings = config.document().effective;
    const auto rotationError = finalizeRotation();
    if (!rotationError.isEmpty()) { finish(rotationError); return; }
    const auto limit = current.value("limit").toInt();
    auto pendingRotation = state.rotation();
    if (!pendingRotation.isEmpty()) {
        const auto eligible = selected();
        const bool configured = std::any_of(eligible.begin(), eligible.end(), [&](const Representation &rep) {
            return rep.key == pendingRotation.value("replacement").toString();
        });
        if (!configured || state.permanentlyFailed(pendingRotation.value("replacement").toString())) {
            if (!abandonRotation()) { finish("StorageError"); return; }
            pendingRotation = {};
        }
    }
    // Releasing a binding may retire a removed victim. Plan only after those
    // durable facts are updated, so the victim is not immediately re-fetched.
    auto plan = AcquisitionPlanner::evaluate(settings, catalogue, cache.entries(), state, clock(),
        current.value("kind") == "manual", ids(current), topics);
    plan.candidates.erase(std::remove_if(plan.candidates.begin(), plan.candidates.end(), [&](const Representation &rep) {
        return attempted.contains(rep.key) || (limit > 0 && available.value(rep.sourceId) >= limit)
            || (!pendingRotation.isEmpty() && rep.key != pendingRotation.value("replacement").toString());
    }), plan.candidates.end());
    if (plan.candidates.isEmpty()) { finish(readyCount() ? QString() : plan.deferReason.isEmpty() ? "NoEligibleAssets" : plan.deferReason); return; }
    const bool automatic = current.value("kind") == "automatic";
    // Preserve planner priority while using the cache's complete acquisition
    // budget, including partials and publication metadata.
    auto candidate = std::find_if(plan.candidates.begin(), plan.candidates.end(), [this](const Representation &rep) {
        return rep.bytes < 0 || cache.canAcquire(rep);
    });
    if (candidate == plan.candidates.end()) {
        QSet<QString> configured, useful;
        for (const auto &value : settings.value("sources").toArray()) configured.insert(value.toObject().value("id").toString());
        for (const auto &value : cache.entries()) {
            const auto entry = value.toObject();
            if (configured.contains(entry.value("sourceId").toString())) useful.insert(entry.value("key").toString());
        }
        if (useful.size() < cache.entries().size()) {
            const auto reclaimed = cache.prune(false, useful);
            if (reclaimed.contains("error")) { finish("StorageError"); return; }
            if (reclaimed.value("removedBytes").toInteger() > 0) emit contentChanged();
            candidate = std::find_if(plan.candidates.begin(), plan.candidates.end(), [this](const Representation &rep) { return cache.canAcquire(rep); });
        }
    }
    QString victim = pendingRotation.value("victim").toString();
    if (victim.isEmpty() && automatic) victim = plan.rotationVictim;
    if (candidate == plan.candidates.end() && !victim.isEmpty() && cache.entries().size() > 1) {
        candidate = std::find_if(plan.candidates.begin(), plan.candidates.end(), [&](const Representation &rep) {
            return cache.canAcquire(rep, victim);
        });
        if (candidate != plan.candidates.end()) {
            if (!state.beginRotation(victim, candidate->key)) { finish("StorageError"); return; }
            if (!cache.evict(victim)) {
                if (!abandonRotation()) { finish("StorageError"); return; }
                finish(cache.valid() ? "PlaybackProtected" : "StorageError"); return;
            }
            emit contentChanged();
            if (!cache.canAcquire(*candidate)) { finish("CacheFull"); return; }
        }
    }
    if (candidate == plan.candidates.end()) { finish("CacheFull"); return; }
    const auto rep = *candidate;
    if (!victim.isEmpty() && state.rotation().isEmpty() && !state.beginRotation(victim, rep.key)) { finish("StorageError"); return; }
    attempted.insert(rep.key);
    current["sourceId"] = rep.sourceId; current["assetId"] = rep.assetId; current["assetKey"] = rep.key;
    current["state"] = "downloading"; current["totalKnown"] = rep.bytes >= 0; current["total"] = std::max<qint64>(0, rep.bytes);
    save([this, rep] { startTransfer(rep); });
}

void TransferQueue::startTransfer(const Representation &rep)
{
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
            if (!state.failed(rep.key, failure, http.status, http.headers.value("retry-after"))) { finish("StorageError"); return; }
            if (state.permanentlyFailed(rep.key) && state.rotation().value("replacement") == rep.key && !abandonRotation()) {
                finish("StorageError"); return;
            }
            auto errors = current.value("errors").toObject(); errors[rep.sourceId] = failure; current["errors"] = errors;
        }
        save([this, failure] {
            if (failure == "CacheFull" || failure == "Cancelled") { finish(failure); return; }
            const auto id = active;
            const auto epoch = decision;
            QTimer::singleShot(0, this, [this, id, epoch] {
                if (!active.isEmpty() && active == id && decision == epoch) nextTransfer();
            });
        });
    });
    transfer->start();
}

void TransferQueue::finish(const QString &error)
{
    if ((error == "Cancelled" || error == "CacheFull" || error == "InvalidConfig" || error == "NoRemoteSources")
        && !abandonRotation()) {
        // Cleanup precedes a terminal receipt. Until the binding is durable,
        // observers see recovering rather than a completed job holding state.
        afterPersistence = [this, error] { finish(error); };
        publishState("error", "StorageError"); persistenceTimer.start(1000);
        return;
    }
    const bool deferred = error == "Paused" || error == "NoAllowedResolution" || error == policyReason || error == "PlaybackProtected" || error == "Backoff";
    current["ready"] = readyCount(); current["errorCode"] = error;
    current["state"] = error.isEmpty() ? "succeeded" : error == "Cancelled" ? "cancelled" : deferred ? "deferred" : "failed";
    current["finishedAt"] = clock();
    save([this, error, deferred] {
        releaseActive();
        armCheck();
        publishState(error.isEmpty() ? "idle" : deferred ? "deferred" : "error", error);
        schedule();
        if (invalidated) evaluate(); // Apply edits received while the receipt was waiting for storage.
    });
}

bool TransferQueue::cancel(const QString &id)
{
    if (!active.isEmpty() && id == active) {
        invalidated = false; ++decision;
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
