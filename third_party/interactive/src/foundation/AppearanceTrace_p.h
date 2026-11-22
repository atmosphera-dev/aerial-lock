// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Private opt-in appearance diagnostic recorder. Foundation owns generic scopes,
// owners and update attempts only; sessions, files and CLI live with the
// application. Disabled by default: the off path performs no clock reads,
// allocations, formatting, locks or ID increments beyond one atomic load.
#include "interactive_foundation_export.h"
#include <QtGlobal>
#include <QVector>
#include <array>

class QString;

namespace InterActive {

// One admission word for counters and the existing summary/detailed recorder.
// Constant-initialized storage: disabled sites need only an inline relaxed load.
namespace AppearanceInstrumentation {
inline constexpr quint8 Summary = 1, Detailed = 2, Work = 4;
extern INTERACTIVE_FOUNDATION_EXPORT quint8 admission;
Q_ALWAYS_INLINE quint8 admitted()
{
    // This recorder already targets POSIX/GCC-compatible platforms. Use the
    // compiler atomic directly: Debug libstdc++'s load adds an out-of-line
    // memory-order validation helper even with a constant relaxed order.
    static_assert(__atomic_always_lock_free(sizeof(admission), nullptr));
    return __atomic_load_n(&admission, __ATOMIC_RELAXED);
}
}

enum class AppearanceWork : quint8 {
    SourceUpdate, NotificationCompare, NotificationDrain, FacadeUpdate,
    SharedPaletteBuild, SharedPairMeasurement, StateTableBuild, PrivateStateTableBuild,
    ColorResolution, LocalDerivation, LocalPairMeasurement, FontConstruction,
    FontMeasurement, GeometryCalculation, ControlDiagnosticsBuild, Count
};

enum class PaletteRecipeRole : quint8 { Error, Warning, Success };
struct PaletteRecipeWork {
    PaletteRecipeRole role;
    int chromatic = 0;
    int neutral = 0;
    int tried = 0;
    bool exhausted = false;
};

// Private diagnostic subscription. Counts are process-wide, like the former
// logging recorder; clear/read require the measured work to have finished.
class INTERACTIVE_FOUNDATION_EXPORT AppearanceWorkRecorder
{
public:
    AppearanceWorkRecorder();
    ~AppearanceWorkRecorder();
    AppearanceWorkRecorder(const AppearanceWorkRecorder &) = delete;
    AppearanceWorkRecorder &operator=(const AppearanceWorkRecorder &) = delete;
    int count(AppearanceWork kind) const;
    void clear();
    QVector<PaletteRecipeWork> paletteRecipes() const;
};

enum class TraceScope : quint16 {
    UpdateAttempt = 0,      // summary-class: recorded in summary and trace modes
    InputDelivery,          // summary-class
    FrameSync,              // summary-class
    FrameRender,            // summary-class
    FrameSubmit,            // summary-class
    AcceptedColor,          // summary-class
    // Detailed spans below this point require trace mode.
    QmlColorHandler,
    QmlSyncPicker,
    SetAppearanceToken,
    SetAppearance,
    Prepare,
    PaletteKeyCompare,
    PaletteBuild,
    PaletteReuse,
    ResolveNoncolors,
    Install,
    AggregateDispatch,
    NotificationCompare,
    NotificationDrain,
    GroupPublish,
    PaletteCalculate,
    SelectGroup,
    RoleDiagnostics,
    FacadeResolve,
    LocalInstall,
    IconRefresh,
    // Aggregated leaves: counted with total duration, not one record per call.
    AggContrastingText,
    AggFinalizeGroup,
    AggCheckCandidate,
    AggLocalDerivation,
    AggLocalPairMeasure,
    // Direct-recipe trial counters; appended so existing scope IDs are unchanged.
    AggPaletteProposal,
    AggForegroundFast,
    AggForegroundFallback,
    Count
};

enum class TraceOutcome : quint8 { None, Accepted, NoOp, Rejected };

enum class TraceMode : quint8 { Off, Summary, Trace };

// Bounded fixed-size record. Exported as Chrome trace events (one per line).
struct TraceRecord {
    quint64 timestampNs = 0;    // CLOCK_MONOTONIC nanoseconds since session origin
    quint64 durationNs = 0;     // scopes/aggregates; zero for instants/counters
    quint64 owner = 0;          // opaque object/window identity; never dereferenced
    quint64 aux = 0;            // role key, count, packed revisions, frame/input payload
    quint32 spanId = 0;
    quint32 parentSpanId = 0;
    quint32 updateId = 0;       // enclosing update attempt, if any
    quint32 inputId = 0;        // enclosing outermost input delivery, if any
    quint32 threadSlot = 0;
    quint16 scopeId = 0;
    quint8 epoch = 0;
    quint8 kind = 0;            // 0 scope, 1 instant, 2 counter, 3 aggregate
    quint8 flags = 0;           // bit0 outside window, bit1 preexisting parent unobserved
    quint8 reserved = 0;
};

class INTERACTIVE_FOUNDATION_EXPORT AppearanceTrace
{
public:
    struct Config {
        quint32 capacityMiB = 128;      // total recorder budget, clamped to [1, 256]
    };

    static Q_ALWAYS_INLINE bool enabled()
    {
        return AppearanceInstrumentation::admitted()
            & (AppearanceInstrumentation::Summary | AppearanceInstrumentation::Detailed);
    }
    static Q_ALWAYS_INLINE bool detailed()
    {
        return AppearanceInstrumentation::admitted()
            & AppearanceInstrumentation::Detailed;
    }
    static Q_ALWAYS_INLINE void work(AppearanceWork kind)
    {
        if (AppearanceInstrumentation::admitted()
            & AppearanceInstrumentation::Work)
            workAdmitted(kind);
    }
    static Q_ALWAYS_INLINE void paletteRecipe(const PaletteRecipeWork &result)
    {
        if (AppearanceInstrumentation::admitted()
            & AppearanceInstrumentation::Work)
            paletteRecipeAdmitted(result);
    }
    static const char *workName(AppearanceWork kind);

    // Session lifecycle. start refuses while a previous session is still active
    // or unquiesced. stop stops admitting new scopes; admitted scopes finish
    // into their original buffers flagged outside the window. exportTo requires
    // a stopped, quiesced session and refuses existing output paths.
    static quint32 start(TraceMode mode, const Config &config);
    static void stop();
    static bool quiesce(int timeoutMs);
    static bool exportTo(const QString &tracePath, const QString &sidecarPath, QString *error);

    static bool sessionActive();        // a session exists (maybe stopped)
    static bool sessionIncomplete();    // any dropped/overflow/truncation
    static quint64 recordsWritten();
    static quint64 recordsDropped();
    static quint32 sessionEpoch();

    // Post-stop read access for the session owner's summary export: copies the
    // coarse per-attempt wall durations (microseconds) of accepted/no-op/rejected
    // update attempts. The session must be stopped and quiesced first.
    static QVector<quint64> attemptDurationsUs();

    static const char *scopeName(TraceScope scope);
    static quint64 monotonicNow();      // raw CLOCK_MONOTONIC ns; callers must gate on enabled()

    // Low-level recording entry points; the RAII wrappers below are the API.
    static quint32 spanBegin(TraceScope scope, const void *owner, bool summaryClass);
    static void spanEnd(quint32 spanId, quint64 startNs, TraceScope scope, const void *owner, quint64 aux);
    static Q_ALWAYS_INLINE void instant(TraceScope scope, const void *owner, quint64 aux, bool summaryClass)
    {
        if (enabled())
            instantAdmitted(scope, owner, aux, summaryClass);
    }
    static Q_ALWAYS_INLINE void counter(TraceScope scope, const void *owner, quint64 value,
                        AppearanceWork workKind = AppearanceWork::Count)
    {
        const auto admission = AppearanceInstrumentation::admitted();
        if (admission & AppearanceInstrumentation::Work && workKind != AppearanceWork::Count)
            workAdmitted(workKind);
        if (admission & (AppearanceInstrumentation::Summary | AppearanceInstrumentation::Detailed))
            counterAdmitted(scope, owner, value);
    }
    static void workAdmitted(AppearanceWork kind);
    static void paletteRecipeAdmitted(const PaletteRecipeWork &result);
    static void counterAdmitted(TraceScope scope, const void *owner, quint64 value);
    static void instantAdmitted(TraceScope scope, const void *owner, quint64 aux, bool summaryClass);
    static void aggregateAdd(TraceScope scope, quint16 roleKey, quint64 durationNs);
    static quint32 attemptBegin(const void *owner);
    static void attemptEnd(quint32 spanId, quint64 startNs, const void *owner,
                           TraceOutcome outcome, quint64 revisionBefore, quint64 revisionAfter);
    static void pushInput(quint32 inputId);
    static void popInput(quint32 inputId);
    static void flushAggregates(quint32 updateId);
};

// Detailed scope; a no-op unless trace mode is accepting.
class INTERACTIVE_FOUNDATION_EXPORT TraceSpan
{
public:
    Q_ALWAYS_INLINE TraceSpan(TraceScope scope, const void *owner, AppearanceWork work = AppearanceWork::Count)
        : m_owner(owner), m_scope(scope)
    {
        const auto admission = AppearanceInstrumentation::admitted();
        if (admission & AppearanceInstrumentation::Work && work != AppearanceWork::Count)
            AppearanceTrace::workAdmitted(work);
        if (admission & AppearanceInstrumentation::Detailed)
            begin();
    }
    Q_ALWAYS_INLINE ~TraceSpan()
    {
        if (m_spanId)
            AppearanceTrace::spanEnd(m_spanId, m_startNs, m_scope, m_owner, m_aux);
    }
    TraceSpan(const TraceSpan &) = delete;
    TraceSpan &operator=(const TraceSpan &) = delete;
    bool active() const { return m_spanId != 0; }
    Q_ALWAYS_INLINE void setAux(quint64 aux) { m_aux = aux; }
private:
    void begin();
    quint64 m_startNs = 0;
    quint64 m_aux = 0;
    const void *m_owner = nullptr;
    quint32 m_spanId = 0;
    TraceScope m_scope = TraceScope::Count;
};

// Update attempt scope; recorded in both summary and trace modes. finish()
// stores the outcome; the span record is written at destruction so early
// returns still produce a correctly timed record with the declared outcome.
class INTERACTIVE_FOUNDATION_EXPORT TraceAttempt
{
public:
    Q_ALWAYS_INLINE explicit TraceAttempt(const void *owner) : m_owner(owner)
    {
        const auto admission = AppearanceInstrumentation::admitted();
        if (admission & AppearanceInstrumentation::Work)
            AppearanceTrace::workAdmitted(AppearanceWork::SourceUpdate);
        if (admission & (AppearanceInstrumentation::Summary | AppearanceInstrumentation::Detailed))
            begin();
    }
    Q_ALWAYS_INLINE ~TraceAttempt()
    {
        if (m_spanId)
            AppearanceTrace::attemptEnd(m_spanId, m_startNs, m_owner, m_outcome, m_revisionBefore, m_revisionAfter);
    }
    TraceAttempt(const TraceAttempt &) = delete;
    TraceAttempt &operator=(const TraceAttempt &) = delete;
    Q_ALWAYS_INLINE void finish(TraceOutcome outcome, quint64 revisionBefore, quint64 revisionAfter)
    {
        m_outcome = outcome;
        m_revisionBefore = revisionBefore;
        m_revisionAfter = revisionAfter;
    }
    quint32 id() const { return m_spanId; }
    bool active() const { return m_spanId != 0; }
private:
    void begin();
    quint64 m_startNs = 0;
    quint64 m_revisionBefore = 0;
    quint64 m_revisionAfter = 0;
    const void *m_owner = nullptr;
    quint32 m_spanId = 0;
    TraceOutcome m_outcome = TraceOutcome::None;
};

// Aggregated high-frequency leaf; accumulates count and duration per
// (scope, role) and flushes when the enclosing attempt ends or the session stops.
class INTERACTIVE_FOUNDATION_EXPORT TraceAggregate
{
public:
    Q_ALWAYS_INLINE TraceAggregate(TraceScope scope, quint16 roleKey, AppearanceWork work = AppearanceWork::Count)
        : m_roleKey(roleKey), m_scope(scope)
    {
        const auto admission = AppearanceInstrumentation::admitted();
        if (admission & AppearanceInstrumentation::Work && work != AppearanceWork::Count)
            AppearanceTrace::workAdmitted(work);
        if (admission & AppearanceInstrumentation::Detailed)
            m_startNs = AppearanceTrace::monotonicNow();
    }
    Q_ALWAYS_INLINE ~TraceAggregate()
    {
        if (m_startNs)
            end();
    }
    TraceAggregate(const TraceAggregate &) = delete;
    TraceAggregate &operator=(const TraceAggregate &) = delete;
private:
    void end();
    quint64 m_startNs = 0;
    quint16 m_roleKey = 0;
    TraceScope m_scope = TraceScope::Count;
};

// Outermost input delivery context for the diagnostic notify wrapper.
class INTERACTIVE_FOUNDATION_EXPORT TraceInput
{
public:
    // Returns input id (0 when tracing is off); call end with the same token.
    static Q_ALWAYS_INLINE quint32 begin(const void *window, quint64 packedEvent)
    {
        return AppearanceTrace::enabled() ? beginAdmitted(window, packedEvent) : 0;
    }
    static Q_ALWAYS_INLINE void end(quint32 inputId, const void *window, quint64 packedEvent)
    {
        if (inputId)
            endAdmitted(inputId, window, packedEvent);
    }
private:
    static quint32 beginAdmitted(const void *window, quint64 packedEvent);
    static void endAdmitted(quint32 inputId, const void *window, quint64 packedEvent);
};

} // namespace InterActive
