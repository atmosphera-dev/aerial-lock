// SPDX-License-Identifier: LGPL-3.0-or-later
#include "AppearanceTrace_p.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace InterActive {

quint8 AppearanceInstrumentation::admission = 0;

namespace {

std::array<std::atomic<int>, size_t(AppearanceWork::Count)> workCounts{};
std::mutex workMutex;
QVector<PaletteRecipeWork> recipeWork;

constexpr quint32 ChunkRecords = 1024;                  // 64 KiB per chunk
constexpr quint32 ChunkBytes = ChunkRecords * sizeof(TraceRecord);
constexpr quint32 MaxThreads = 32;
constexpr quint32 MaxSpanDepth = 64;
constexpr quint32 MaxAggregateKeys = 16;

const char *const scopeNames[] = {
    "update-attempt", "input-delivery", "frame-sync", "frame-render", "frame-submit",
    "accepted-color",
    "qml-color-handler", "qml-sync-picker", "set-appearance-token", "set-appearance",
    "prepare", "palette-key-compare", "palette-build", "palette-reuse",
    "resolve-noncolors", "install", "aggregate-dispatch", "notification-compare",
    "notification-drain", "group-publish", "palette-calculate", "select-group",
    "role-diagnostics", "facade-resolve", "local-install", "icon-refresh",
    "agg-contrasting-text", "agg-finalize-group", "agg-check-candidate",
    "agg-local-derivation", "agg-local-pair-measure",
    "agg-palette-proposal", "agg-foreground-fast", "agg-foreground-fallback",
};
static_assert(sizeof(scopeNames) / sizeof(scopeNames[0]) == size_t(TraceScope::Count));

struct AggregateSlot {
    quint16 scope = quint16(TraceScope::Count);
    quint16 roleKey = 0;
    quint32 count = 0;
    quint64 totalNs = 0;
};

struct ThreadContext {
    std::atomic<qint64> activeSpans{0};   // quiesce polling
    qint64 tid = 0;
    char name[16]{};
    quint32 slot = 0;
    // Current append chunk: [cursor, end) into the arena.
    std::atomic<quint64> cursor{0};
    quint64 end = 0;
    quint64 written = 0;
    quint32 spanStack[MaxSpanDepth]{};
    quint32 depth = 0;
    quint32 updateStack[MaxSpanDepth]{};
    quint32 updateDepth = 0;
    quint32 currentInputId = 0;
    AggregateSlot aggregates[MaxAggregateKeys];
    bool registered = false;
};

struct SessionState {
    std::atomic<quint8> mode{quint8(TraceMode::Off)};
    std::atomic<bool> accepting{false};
    quint8 epoch = 0;
    quint64 originNs = 0;               // CLOCK_MONOTONIC at start
    quint64 originRealtimeNs = 0;       // CLOCK_REALTIME anchor, for the sidecar
    quint64 stopNs = 0;
    quint64 capacityBytes = 0;
    TraceRecord *arena = nullptr;
    quint64 totalRecords = 0;
    std::atomic<quint64> nextChunk{0};
    // Integrity counters live outside event capacity: exhaustion cannot erase them.
    std::atomic<quint64> dropped{0};
    std::atomic<quint64> nestingOverflow{0};
    std::atomic<quint64> chunkOverflow{0};
    std::atomic<quint32> nextUpdateId{0};
    std::atomic<quint32> nextInputId{0};
    std::atomic<quint32> nextSpanSuffix{0};
    std::mutex registryMutex;
    ThreadContext threads[MaxThreads];
    quint32 threadCount = 0;
    TraceMode stopMode = TraceMode::Off;
    bool active = false;
};

SessionState &S()
{
    static SessionState state;
    return state;
}

thread_local ThreadContext *t_context = nullptr;

quint64 rawNow()
{
    timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return quint64(ts.tv_sec) * 1000000000ull + quint64(ts.tv_nsec);
}

// Registration is the only locked path and happens once per thread per process.
ThreadContext *context()
{
    if (t_context)
        return t_context;
    auto &s = S();
    if (!s.accepting.load(std::memory_order_acquire))
        return nullptr;                 // cannot register outside an accepting session
    std::lock_guard<std::mutex> lock(s.registryMutex);
    if (s.threadCount == MaxThreads) {
        s.dropped.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    ThreadContext *ctx = &s.threads[s.threadCount];
    ctx->slot = s.threadCount;
    ++s.threadCount;
    ctx->tid = ::syscall(SYS_gettid);
    pthread_getname_np(pthread_self(), ctx->name, sizeof(ctx->name));
    ctx->registered = true;
    // Grant the first chunk immediately.
    const quint64 chunk = s.nextChunk.fetch_add(1, std::memory_order_relaxed);
    const quint64 first = chunk * ChunkRecords;
    if (first < s.totalRecords) {
        ctx->cursor.store(first, std::memory_order_relaxed);
        ctx->end = std::min(first + ChunkRecords, s.totalRecords);
    } else {
        ctx->end = 0;                   // arena already exhausted; writes drop
    }
    t_context = ctx;
    return ctx;
}

void destroyContext()
{
    if (t_context) {
        t_context->registered = false;
        t_context = nullptr;
    }
}

struct ContextJanitor {
    ~ContextJanitor() { destroyContext(); }
};
thread_local ContextJanitor t_janitor;

bool claimChunk(ThreadContext *ctx)
{
    auto &s = S();
    const quint64 chunk = s.nextChunk.fetch_add(1, std::memory_order_relaxed);
    const quint64 first = chunk * ChunkRecords;
    if (first >= s.totalRecords) {
        s.chunkOverflow.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    ctx->cursor.store(first, std::memory_order_relaxed);
    ctx->end = std::min(first + ChunkRecords, s.totalRecords);
    return true;
}

quint32 currentUpdate(const ThreadContext *ctx)
{
    return ctx->updateDepth > 0 ? ctx->updateStack[ctx->updateDepth - 1] : 0;
}

// The single append path. No formatting, no locks; one atomic RMW only when the
// thread crosses a chunk boundary.
TraceRecord *allocate(ThreadContext *ctx)
{
    auto &s = S();
    for (;;) {
        const quint64 at = ctx->cursor.load(std::memory_order_relaxed);
        if (at < ctx->end) {
            ctx->cursor.store(at + 1, std::memory_order_relaxed);
            ++ctx->written;
            return &s.arena[at];
        }
        if (!claimChunk(ctx))
            return nullptr;
    }
}

void writeRecord(ThreadContext *ctx, const TraceRecord &record)
{
    auto &s = S();
    if (TraceRecord *slot = allocate(ctx)) {
        *slot = record;
    } else {
        s.dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

bool summaryClass(TraceScope scope)
{
    return scope <= TraceScope::AcceptedColor;
}

} // namespace

AppearanceWorkRecorder::AppearanceWorkRecorder()
{
    Q_ASSERT(!(AppearanceInstrumentation::admitted()
        & AppearanceInstrumentation::Work));
    clear();
    __atomic_fetch_or(&AppearanceInstrumentation::admission, AppearanceInstrumentation::Work, __ATOMIC_RELAXED);
}

AppearanceWorkRecorder::~AppearanceWorkRecorder()
{
    __atomic_fetch_and(&AppearanceInstrumentation::admission, quint8(~AppearanceInstrumentation::Work), __ATOMIC_RELAXED);
}

int AppearanceWorkRecorder::count(AppearanceWork kind) const
{
    return workCounts[size_t(kind)].load(std::memory_order_relaxed);
}

void AppearanceWorkRecorder::clear()
{
    for (auto &count : workCounts)
        count.store(0, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(workMutex);
    recipeWork.clear();
}

QVector<PaletteRecipeWork> AppearanceWorkRecorder::paletteRecipes() const
{
    std::lock_guard<std::mutex> lock(workMutex);
    return recipeWork;
}

void AppearanceTrace::workAdmitted(AppearanceWork kind)
{
    workCounts[size_t(kind)].fetch_add(1, std::memory_order_relaxed);
}

void AppearanceTrace::paletteRecipeAdmitted(const PaletteRecipeWork &result)
{
    std::lock_guard<std::mutex> lock(workMutex);
    recipeWork.append(result);
}

const char *AppearanceTrace::workName(AppearanceWork kind)
{
    static constexpr const char *names[]{
        "source-update", "notification-compare", "notification-drain", "facade-update",
        "shared-palette-build", "shared-pair-measurement", "state-table-build", "private-state-table-build",
        "color-resolution", "local-derivation", "local-pair-measurement", "font-construction",
        "font-measurement", "geometry-calculation", "control-diagnostics-build"};
    static_assert(std::size(names) == size_t(AppearanceWork::Count));
    return kind < AppearanceWork::Count ? names[size_t(kind)] : "unknown";
}

quint64 AppearanceTrace::monotonicNow()
{
    return rawNow();
}

const char *AppearanceTrace::scopeName(TraceScope scope)
{
    return scope < TraceScope::Count ? scopeNames[size_t(scope)] : "unknown";
}

quint32 AppearanceTrace::start(TraceMode mode, const Config &config)
{
    auto &s = S();
    if (mode == TraceMode::Off || s.active)
        return 0;
    if (!quiesce(250))
        return 0;                       // an admitted scope is still in flight
    const quint32 mib = std::clamp(config.capacityMiB, 1u, 256u);
    const quint64 bytes = quint64(mib) << 20;
    if (s.arena && s.capacityBytes < bytes) {
        ::munmap(s.arena, s.capacityBytes);
        s.arena = nullptr;
    }
    if (!s.arena) {
        void *memory = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
        if (memory == MAP_FAILED)
            return 0;
        s.arena = static_cast<TraceRecord *>(memory);
        s.capacityBytes = bytes;
    }
    s.totalRecords = bytes / sizeof(TraceRecord);
    s.nextChunk.store(0, std::memory_order_relaxed);
    s.dropped.store(0, std::memory_order_relaxed);
    s.nestingOverflow.store(0, std::memory_order_relaxed);
    s.chunkOverflow.store(0, std::memory_order_relaxed);
    s.nextUpdateId.store(0, std::memory_order_relaxed);
    s.nextInputId.store(0, std::memory_order_relaxed);
    s.nextSpanSuffix.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(s.registryMutex);
        for (auto &thread : s.threads) {
            thread.cursor.store(0, std::memory_order_relaxed);
            thread.end = 0;
            thread.depth = 0;
            thread.updateDepth = 0;
            thread.written = 0;
            thread.currentInputId = 0;
            for (auto &slot : thread.aggregates)
                slot = AggregateSlot{};
        }
    }
    ++s.epoch;
    timespec ts;
    ::clock_gettime(CLOCK_REALTIME, &ts);
    s.originRealtimeNs = quint64(ts.tv_sec) * 1000000000ull + quint64(ts.tv_nsec);
    s.originNs = rawNow();
    s.stopNs = 0;
    s.stopMode = mode;
    s.active = true;
    s.mode.store(quint8(mode), std::memory_order_release);
    s.accepting.store(true, std::memory_order_release);
    __atomic_fetch_or(&AppearanceInstrumentation::admission, mode == TraceMode::Trace
        ? AppearanceInstrumentation::Detailed : AppearanceInstrumentation::Summary, __ATOMIC_RELAXED);
    return s.epoch;
}

void AppearanceTrace::stop()
{
    auto &s = S();
    if (!s.active)
        return;
    // Stop admitting first; admitted scopes still finish into their buffers.
    __atomic_fetch_and(&AppearanceInstrumentation::admission, AppearanceInstrumentation::Work, __ATOMIC_RELAXED);
    s.accepting.store(false, std::memory_order_release);
    s.stopNs = rawNow();
}

bool AppearanceTrace::quiesce(int timeoutMs)
{
    auto &s = S();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        bool idle = true;
        for (auto &thread : s.threads) {
            if (thread.activeSpans.load(std::memory_order_acquire) != 0) {
                idle = false;
                break;
            }
        }
        if (idle)
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool AppearanceTrace::sessionActive() { return S().active; }
bool AppearanceTrace::sessionIncomplete()
{
    const auto &s = S();
    return s.dropped.load(std::memory_order_relaxed) != 0
        || s.chunkOverflow.load(std::memory_order_relaxed) != 0
        || s.nestingOverflow.load(std::memory_order_relaxed) != 0;
}
quint64 AppearanceTrace::recordsWritten()
{
    const auto &s = S();
    quint64 total = 0;
    for (const auto &thread : s.threads)
        total += thread.written;
    return total;
}
quint64 AppearanceTrace::recordsDropped()
{
    return S().dropped.load(std::memory_order_relaxed)
        + S().nestingOverflow.load(std::memory_order_relaxed)
        + S().chunkOverflow.load(std::memory_order_relaxed);
}
quint32 AppearanceTrace::sessionEpoch() { return S().epoch; }

QVector<quint64> AppearanceTrace::attemptDurationsUs()
{
    QVector<quint64> durations;
    const auto &s = S();
    if (!s.active || s.accepting.load(std::memory_order_acquire) || !s.arena)
        return durations;               // requires a stopped session
    const quint64 limit = std::min(s.nextChunk.load(std::memory_order_relaxed) * ChunkRecords,
                                   s.totalRecords);
    for (quint64 i = 0; i < limit; ++i) {
        const TraceRecord &r = s.arena[i];
        if (r.epoch != s.epoch || r.kind != 0 || r.spanId == 0)
            continue;
        if (r.scopeId == quint16(TraceScope::UpdateAttempt))
            durations.append(r.durationNs / 1000);
    }
    return durations;
}

quint32 AppearanceTrace::spanBegin(TraceScope scope, const void *owner, bool summaryClassScope)
{
    auto &s = S();
    if (!s.accepting.load(std::memory_order_acquire))
        return 0;
    if (!summaryClassScope && s.mode.load(std::memory_order_acquire) != quint8(TraceMode::Trace))
        return 0;
    ThreadContext *ctx = context();
    if (!ctx)
        return 0;
    if (ctx->depth == MaxSpanDepth) {
        s.nestingOverflow.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    const quint32 suffix = s.nextSpanSuffix.fetch_add(1, std::memory_order_relaxed);
    const quint32 spanId = ((ctx->slot + 1) << 24) | (suffix & 0x00FFFFFF);
    ctx->activeSpans.fetch_add(1, std::memory_order_release);
    ctx->spanStack[ctx->depth++] = spanId;
    return spanId;
}

// spanEnd with an explicit update override is used by attemptEnd, which must
// record the attempt's own ID even after popping it from the update stack.
static void spanEndImpl(quint32 spanId, quint64 startNs, TraceScope scope,
                        const void *owner, quint64 aux, quint32 updateOverride)
{
    if (!spanId)
        return;
    auto &s = S();
    ThreadContext *ctx = t_context;
    if (!ctx)
        return;
    // Restore the per-thread stack through reentrancy: find and unwind to us.
    quint32 parent = 0;
    quint8 flags = 0;
    if (ctx->depth > 0) {
        quint32 found = ctx->depth;
        for (quint32 i = ctx->depth; i > 0; --i) {
            if (ctx->spanStack[i - 1] == spanId) {
                found = i - 1;
                break;
            }
        }
        if (found < ctx->depth) {
            parent = found > 0 ? ctx->spanStack[found - 1] : 0;
            ctx->depth = found;
        } else {
            ctx->depth = 0;             // reentrant unwind past a stale stack
        }
    }
    if (parent == 0)
        flags |= 0x2;                   // recording root: parent was not observed
    const quint64 end = rawNow();
    if (s.stopNs && end > s.stopNs)
        flags |= 0x1;                   // admitted before stop, finished outside
    TraceRecord record;
    record.timestampNs = startNs >= s.originNs ? startNs - s.originNs : 0;
    record.durationNs = end > startNs ? end - startNs : 0;
    record.owner = quint64(reinterpret_cast<quintptr>(owner));
    record.aux = aux;
    record.spanId = spanId;
    record.parentSpanId = parent;
    record.updateId = updateOverride ? updateOverride : currentUpdate(ctx);
    record.inputId = ctx->currentInputId;
    record.threadSlot = ctx->slot;
    record.scopeId = quint16(scope);
    record.epoch = s.epoch;
    record.kind = 0;
    record.flags = flags;
    writeRecord(ctx, record);
    ctx->activeSpans.fetch_sub(1, std::memory_order_release);
}

void AppearanceTrace::spanEnd(quint32 spanId, quint64 startNs, TraceScope scope,
                              const void *owner, quint64 aux)
{
    spanEndImpl(spanId, startNs, scope, owner, aux, 0);
}

void AppearanceTrace::instantAdmitted(TraceScope scope, const void *owner, quint64 aux, bool summaryClassScope)
{
    auto &s = S();
    if (!s.accepting.load(std::memory_order_acquire))
        return;
    if (!summaryClassScope && s.mode.load(std::memory_order_acquire) != quint8(TraceMode::Trace))
        return;
    ThreadContext *ctx = context();
    if (!ctx)
        return;
    TraceRecord record;
    const quint64 now = rawNow();
    record.timestampNs = now >= s.originNs ? now - s.originNs : 0;
    record.owner = quint64(reinterpret_cast<quintptr>(owner));
    record.aux = aux;
    record.spanId = 0;
    record.parentSpanId = ctx->depth > 0 ? ctx->spanStack[ctx->depth - 1] : 0;
    record.updateId = currentUpdate(ctx);
    record.inputId = ctx->currentInputId;
    record.threadSlot = ctx->slot;
    record.scopeId = quint16(scope);
    record.epoch = s.epoch;
    record.kind = 1;
    record.flags = (record.parentSpanId == 0 ? 0x2 : 0x0) | (s.stopNs && now > s.stopNs ? 0x1 : 0x0);
    writeRecord(ctx, record);
}

void AppearanceTrace::counterAdmitted(TraceScope scope, const void *owner, quint64 value)
{
    auto &s = S();
    if (!s.accepting.load(std::memory_order_acquire))
        return;
    ThreadContext *ctx = context();
    if (!ctx)
        return;
    TraceRecord record;
    const quint64 now = rawNow();
    record.timestampNs = now >= s.originNs ? now - s.originNs : 0;
    record.owner = quint64(reinterpret_cast<quintptr>(owner));
    record.aux = value;
    record.parentSpanId = ctx->depth > 0 ? ctx->spanStack[ctx->depth - 1] : 0;
    record.updateId = currentUpdate(ctx);
    record.inputId = ctx->currentInputId;
    record.threadSlot = ctx->slot;
    record.scopeId = quint16(scope);
    record.epoch = s.epoch;
    record.kind = 2;
    record.flags = (record.parentSpanId == 0 ? 0x2 : 0x0) | (s.stopNs && now > s.stopNs ? 0x1 : 0x0);
    writeRecord(ctx, record);
}

void AppearanceTrace::aggregateAdd(TraceScope scope, quint16 roleKey, quint64 durationNs)
{
    auto &s = S();
    ThreadContext *ctx = t_context;
    if (!ctx)
        return;
    for (auto &slot : ctx->aggregates) {
        if (slot.scope == quint16(scope) && slot.roleKey == roleKey) {
            ++slot.count;
            slot.totalNs += durationNs;
            return;
        }
    }
    for (auto &slot : ctx->aggregates) {
        if (slot.scope == quint16(TraceScope::Count)) {
            slot.scope = quint16(scope);
            slot.roleKey = roleKey;
            slot.count = 1;
            slot.totalNs = durationNs;
            return;
        }
    }
    // Table full: flush it into records and retry once, so counts stay exact.
    flushAggregates(currentUpdate(ctx));
    aggregateAdd(scope, roleKey, durationNs);
}

void AppearanceTrace::flushAggregates(quint32 updateId)
{
    ThreadContext *ctx = t_context;
    if (!ctx)
        return;
    auto &s = S();
    for (auto &slot : ctx->aggregates) {
        if (slot.scope == quint16(TraceScope::Count) || slot.count == 0)
            continue;
        TraceRecord record;
        const quint64 now = rawNow();
        record.timestampNs = now >= s.originNs ? now - s.originNs : 0;
        record.durationNs = slot.totalNs;
        record.aux = (quint64(slot.roleKey) << 32) | slot.count;
        record.parentSpanId = ctx->depth > 0 ? ctx->spanStack[ctx->depth - 1] : 0;
        record.updateId = updateId;
        record.inputId = ctx->currentInputId;
        record.threadSlot = ctx->slot;
        record.scopeId = slot.scope;
        record.epoch = s.epoch;
        record.kind = 3;
        record.flags = (record.parentSpanId == 0 ? 0x2 : 0x0) | (s.stopNs && now > s.stopNs ? 0x1 : 0x0);
        writeRecord(ctx, record);
        slot = AggregateSlot{};
    }
}

quint32 AppearanceTrace::attemptBegin(const void *owner)
{
    auto &s = S();
    if (!s.accepting.load(std::memory_order_acquire))
        return 0;
    ThreadContext *ctx = context();
    if (!ctx)
        return 0;
    if (ctx->updateDepth == MaxSpanDepth) {
        s.nestingOverflow.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    const quint32 spanId = spanBegin(TraceScope::UpdateAttempt, owner, true);
    if (!spanId)
        return 0;
    const quint32 updateId = s.nextUpdateId.fetch_add(1, std::memory_order_relaxed) + 1;
    ctx->updateStack[ctx->updateDepth++] = updateId;
    return spanId;
}

void AppearanceTrace::attemptEnd(quint32 spanId, quint64 startNs, const void *owner,
                                 TraceOutcome outcome, quint64 revisionBefore, quint64 revisionAfter)
{
    if (!spanId)
        return;
    ThreadContext *ctx = t_context;
    if (!ctx || ctx->updateDepth == 0)
        return;
    // Pop our attempt; reentrant attempts nest strictly, and spanEndImpl's
    // unwind already restored the shared span stack to our position.
    const quint32 updateId = ctx->updateStack[--ctx->updateDepth];
    flushAggregates(updateId);
    const quint64 aux = (quint64(quint8(outcome)) << 56)
        | ((revisionBefore & 0xFFFFFFFull) << 28) | (revisionAfter & 0xFFFFFFFull);
    spanEndImpl(spanId, startNs, TraceScope::UpdateAttempt, owner, aux, updateId);
}

void AppearanceTrace::pushInput(quint32 inputId)
{
    if (ThreadContext *ctx = t_context)
        ctx->currentInputId = inputId;
}

void AppearanceTrace::popInput(quint32 inputId)
{
    if (ThreadContext *ctx = t_context) {
        if (ctx->currentInputId == inputId)
            ctx->currentInputId = 0;
    }
}

// --- RAII wrappers -----------------------------------------------------------

void TraceSpan::begin()
{
    m_startNs = AppearanceTrace::monotonicNow();
    m_spanId = AppearanceTrace::spanBegin(m_scope, m_owner, false);
    if (!m_spanId)
        m_startNs = 0;
}

void TraceAttempt::begin()
{
    m_startNs = AppearanceTrace::monotonicNow();
    m_spanId = AppearanceTrace::attemptBegin(m_owner);
    if (!m_spanId)
        m_startNs = 0;
}

void TraceAggregate::end()
{
    const quint64 end = AppearanceTrace::monotonicNow();
    AppearanceTrace::aggregateAdd(m_scope, m_roleKey, end > m_startNs ? end - m_startNs : 0);
}

quint32 TraceInput::beginAdmitted(const void *window, quint64 packedEvent)
{
    auto &s = S();
    if (!s.accepting.load(std::memory_order_acquire))
        return 0;
    ThreadContext *ctx = context();
    if (!ctx || ctx->currentInputId != 0)
        return 0;                       // nested delivery retains outer context
    const quint32 inputId = s.nextInputId.fetch_add(1, std::memory_order_relaxed) + 1;
    ctx->currentInputId = inputId;
    AppearanceTrace::instant(TraceScope::InputDelivery, window,
        (quint64(inputId) << 32) | (packedEvent & 0xFFFFFFFFull), true);
    return inputId;
}

void TraceInput::endAdmitted(quint32 inputId, const void *window, quint64 packedEvent)
{
    if (!inputId)
        return;
    AppearanceTrace::instant(TraceScope::InputDelivery, window,
        (quint64(inputId) << 32) | (1ull << 63) | (packedEvent & 0xFFFFFFFFull), true);
    AppearanceTrace::popInput(inputId);
}

// --- Export ------------------------------------------------------------------

bool AppearanceTrace::exportTo(const QString &tracePath, const QString &sidecarPath, QString *error)
{
    auto &s = S();
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!s.active)
        return fail(QStringLiteral("No trace session exists."));
    if (s.accepting.load(std::memory_order_acquire))
        return fail(QStringLiteral("Stop the session before export."));
    if (!quiesce(1000))
        return fail(QStringLiteral("Trace scopes are still in flight; refusing to export."));
    // After quiesce no thread is recording; flush leftover per-thread aggregates
    // (work that never ran inside an attempt) into the arena as orphan records.
    for (auto &thread : s.threads) {
        if (!thread.registered)
            continue;
        for (auto &slot : thread.aggregates) {
            if (slot.scope == quint16(TraceScope::Count) || slot.count == 0)
                continue;
            TraceRecord record;
            record.timestampNs = s.stopNs > s.originNs ? s.stopNs - s.originNs : 0;
            record.durationNs = slot.totalNs;
            record.aux = (quint64(slot.roleKey) << 32) | slot.count;
            record.threadSlot = thread.slot;
            record.scopeId = slot.scope;
            record.epoch = s.epoch;
            record.kind = 3;
            record.flags = 0x2 | 0x1;   // root, and outside the requested window
            writeRecord(&thread, record);
            slot = AggregateSlot{};
        }
    }
    if (QFileInfo::exists(tracePath) || QFileInfo::exists(sidecarPath))
        return fail(QStringLiteral("Refusing to overwrite an existing trace or sidecar path."));

    const int fd = ::open(tracePath.toLocal8Bit().constData(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0)
        return fail(QStringLiteral("Cannot create trace output: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));

    quint64 exported = 0;
    bool writeFailed = false;
    const auto writeAll = [&](const char *data, size_t size) {
        while (size > 0 && !writeFailed) {
            const ssize_t written = ::write(fd, data, size);
            if (written <= 0) {
                writeFailed = true;
                return;
            }
            data += written;
            size -= size_t(written);
        }
    };
    char line[512];
    writeAll("{\"traceEvents\":[\n", 17);

    const quint64 limit = std::min(s.nextChunk.load(std::memory_order_relaxed) * ChunkRecords, s.totalRecords);
    for (quint64 i = 0; i < limit && !writeFailed; ++i) {
        const TraceRecord &r = s.arena[i];
        if (r.epoch != s.epoch)
            continue;                   // stale slot from a previous session
        if (r.scopeId >= quint16(TraceScope::Count))
            continue;                   // never-written slots are zeroed: scope 0 with zero spanId
        if (r.kind == 0 && r.spanId == 0)
            continue;
        if (r.kind == 3 && r.aux == 0)
            continue;
        const char *name = scopeName(TraceScope(r.scopeId));
        const double tsUs = double(r.timestampNs) / 1000.0;
        int length = 0;
        switch (r.kind) {
        case 0: // scope -> complete event
            if (r.scopeId == quint16(TraceScope::UpdateAttempt)) {
                // Decode the packed aux into named fields: JSON numbers are
                // doubles and cannot carry the full 64-bit payload.
                static const char *const outcomes[] = {"none", "accepted", "noop", "rejected"};
                const quint8 outcome = quint8(r.aux >> 56);
                length = std::snprintf(line, sizeof(line),
                    "{\"ph\":\"X\",\"ts\":%.3f,\"dur\":%.3f,\"pid\":%d,\"tid\":%u,\"name\":\"%s\","
                    "\"args\":{\"span\":%u,\"parent\":%u,\"update\":%u,\"input\":%u,\"owner\":\"0x%llx\","
                    "\"outcome\":\"%s\",\"revisionBefore\":%llu,\"revisionAfter\":%llu,"
                    "\"epoch\":%u,\"flags\":%u}},\n",
                    tsUs, double(r.durationNs) / 1000.0, int(::getpid()), r.threadSlot, name,
                    r.spanId, r.parentSpanId, r.updateId, r.inputId,
                    (unsigned long long)r.owner,
                    outcome < 4 ? outcomes[outcome] : "unknown",
                    (unsigned long long)((r.aux >> 28) & 0xFFFFFFFull),
                    (unsigned long long)(r.aux & 0xFFFFFFFull), r.epoch, r.flags);
                break;
            }
            length = std::snprintf(line, sizeof(line),
                "{\"ph\":\"X\",\"ts\":%.3f,\"dur\":%.3f,\"pid\":%d,\"tid\":%u,\"name\":\"%s\","
                "\"args\":{\"span\":%u,\"parent\":%u,\"update\":%u,\"input\":%u,\"owner\":\"0x%llx\","
                "\"aux\":%llu,\"epoch\":%u,\"flags\":%u}},\n",
                tsUs, double(r.durationNs) / 1000.0, int(::getpid()), r.threadSlot, name,
                r.spanId, r.parentSpanId, r.updateId, r.inputId,
                (unsigned long long)r.owner, (unsigned long long)r.aux, r.epoch, r.flags);
            break;
        case 1: // instant
            length = std::snprintf(line, sizeof(line),
                "{\"ph\":\"i\",\"ts\":%.3f,\"pid\":%d,\"tid\":%u,\"name\":\"%s\",\"s\":\"t\","
                "\"args\":{\"parent\":%u,\"update\":%u,\"input\":%u,\"owner\":\"0x%llx\","
                "\"aux\":%llu,\"epoch\":%u,\"flags\":%u}},\n",
                tsUs, int(::getpid()), r.threadSlot, name, r.parentSpanId, r.updateId, r.inputId,
                (unsigned long long)r.owner, (unsigned long long)r.aux, r.epoch, r.flags);
            break;
        case 2: // counter
        case 3: // aggregate
            length = std::snprintf(line, sizeof(line),
                "{\"ph\":\"C\",\"ts\":%.3f,\"pid\":%d,\"tid\":%u,\"name\":\"%s\","
                "\"args\":{\"parent\":%u,\"update\":%u,\"input\":%u,"
                "\"value\":%llu,\"count\":%llu,\"roleKey\":%llu,\"dur_ns\":%llu,\"epoch\":%u,\"flags\":%u}},\n",
                tsUs, int(::getpid()), r.threadSlot, name, r.parentSpanId, r.updateId, r.inputId,
                (unsigned long long)(r.kind == 2 ? r.aux : 0),
                (unsigned long long)(r.kind == 3 ? (r.aux & 0xFFFFFFFFull) : 0),
                (unsigned long long)(r.kind == 3 ? (r.aux >> 32) : 0),
                (unsigned long long)(r.kind == 3 ? r.durationNs : 0), r.epoch, r.flags);
            break;
        }
        if (length > 0)
            writeAll(line, size_t(length));
        ++exported;
    }
    // Terminate the array: the trailing comma is consumed by an explicit sentinel.
    static const char trailer[] = "{\"ph\":\"M\",\"ts\":0,\"pid\":0,\"tid\":0,\"name\":\"trace_end\"}\n]}\n";
    writeAll(trailer, sizeof(trailer) - 1);

    ::fsync(fd);
    const int closeResult = ::close(fd);
    if (writeFailed || closeResult != 0) {
        // Preserve the partial file as evidence of failure, but report failure.
        return fail(QStringLiteral("Trace export failed while writing; partial file preserved."));
    }

    QJsonObject sidecar;
    sidecar.insert(QStringLiteral("epoch"), s.epoch);
    sidecar.insert(QStringLiteral("mode"), s.stopMode == TraceMode::Trace ? QStringLiteral("trace") : QStringLiteral("summary"));
    sidecar.insert(QStringLiteral("clock"), QStringLiteral("CLOCK_MONOTONIC"));
    sidecar.insert(QStringLiteral("originMonotonicNs"), QString::number(s.originNs));
    sidecar.insert(QStringLiteral("originRealtimeNs"), QString::number(s.originRealtimeNs));
    sidecar.insert(QStringLiteral("stopMonotonicNs"), QString::number(s.stopNs));
    sidecar.insert(QStringLiteral("pid"), qint64(::getpid()));
    sidecar.insert(QStringLiteral("capacityBytes"), QString::number(s.capacityBytes));
    sidecar.insert(QStringLiteral("recordsExported"), QString::number(exported));
    sidecar.insert(QStringLiteral("recordsDropped"), QString::number(s.dropped.load()));
    sidecar.insert(QStringLiteral("nestingOverflow"), QString::number(s.nestingOverflow.load()));
    sidecar.insert(QStringLiteral("chunkOverflow"), QString::number(s.chunkOverflow.load()));
    sidecar.insert(QStringLiteral("incomplete"), sessionIncomplete());
    QJsonObject threads;
    for (const auto &thread : s.threads) {
        if (!thread.registered)
            continue;
        QJsonObject entry;
        entry.insert(QStringLiteral("tid"), qint64(thread.tid));
        entry.insert(QStringLiteral("name"), QString::fromLocal8Bit(thread.name));
        threads.insert(QString::number(thread.slot), entry);
    }
    sidecar.insert(QStringLiteral("threads"), threads);

    QFile sidecarFile(sidecarPath);
    if (!sidecarFile.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail(QStringLiteral("Cannot create trace sidecar at a fresh path."));
    sidecarFile.write(QJsonDocument(sidecar).toJson(QJsonDocument::Compact));
    sidecarFile.write("\n");
    if (!sidecarFile.flush())
        return fail(QStringLiteral("Trace sidecar write failed."));
    sidecarFile.close();
    s.active = false;
    return true;
}

} // namespace InterActive
