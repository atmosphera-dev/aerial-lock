// SPDX-License-Identifier: LGPL-3.0-or-later
#include "AppearanceTrace_p.h"
#include "PaletteState_p.h"
#include "Appearance.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <atomic>

using namespace InterActive;

// The recorder's own suite manages sessions internally; it never activates the
// shared IA_TEST_TRACE_* support (that support explicitly skips this binary).
class AppearanceTraceTests : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void disabledDoesNothing();
    void typedWorkCounters();
    void recordsKnownNesting();
    void attemptOutcomes();
    void reentrantAttempts();
    void scopeOutlivesSource();
    void threadsSeparated();
    void overflowCounted();
    void stopAdmitsNoNewScopes();
    void exportRefusesExistingPath();
    void exportIsParseable();

private:
    QTemporaryDir m_dir;

    quint32 beginSession(TraceMode mode = TraceMode::Trace, quint32 mib = 1)
    {
        AppearanceTrace::Config config;
        config.capacityMiB = mib;
        return AppearanceTrace::start(mode, config);
    }
};

void AppearanceTraceTests::init()
{
    QVERIFY(m_dir.isValid());
    QVERIFY2(!AppearanceTrace::sessionActive(), "a previous session leaked");
}

void AppearanceTraceTests::cleanup()
{
    if (AppearanceTrace::sessionActive()) {
        AppearanceTrace::stop();
        QVERIFY(AppearanceTrace::quiesce(1000));
    }
}

void AppearanceTraceTests::disabledDoesNothing()
{
    // The off path admits nothing and reads nothing.
    QVERIFY(!AppearanceTrace::enabled());
    QVERIFY(!AppearanceTrace::detailed());
    {
        TraceSpan span(TraceScope::FacadeResolve, this);
        QVERIFY(!span.active());
        TraceAttempt attempt(this);
        QVERIFY(!attempt.active());
        TraceAggregate leaf(TraceScope::AggContrastingText, 0);
    }
    QCOMPARE(AppearanceTrace::recordsWritten(), 0);
    QCOMPARE(AppearanceTrace::recordsDropped(), 0);
}

void AppearanceTraceTests::typedWorkCounters()
{
    // Counters neither need nor enable clocks/the buffered recorder.
    AppearanceTrace::work(AppearanceWork::FacadeUpdate); // off: must not leak into a subscription
    for (const auto mode : {TraceMode::Summary, TraceMode::Trace}) {
        AppearanceWorkRecorder work;
        QCOMPARE(work.count(AppearanceWork::FacadeUpdate), 0);
        {
            TraceSpan span(TraceScope::FacadeResolve, this, AppearanceWork::FacadeUpdate);
            QVERIFY(!span.active());
            TraceAggregate leaf(TraceScope::AggLocalDerivation, 0, AppearanceWork::LocalDerivation);
        }
        QCOMPARE(work.count(AppearanceWork::FacadeUpdate), 1);
        QCOMPARE(work.count(AppearanceWork::LocalDerivation), 1);
        AppearanceTrace::paletteRecipe({PaletteRecipeRole::Error, 2, 1, 3, true});
        const auto recipes = work.paletteRecipes();
        QCOMPARE(recipes.size(), 1);
        QCOMPARE(recipes.first().role, PaletteRecipeRole::Error);
        QCOMPARE(recipes.first().chromatic, 2);
        QCOMPARE(recipes.first().neutral, 1);
        QCOMPARE(recipes.first().tried, 3);
        QVERIFY(recipes.first().exhausted);
        work.clear();
        QCOMPARE(work.count(AppearanceWork::FacadeUpdate), 0);
        QVERIFY(work.paletteRecipes().isEmpty());

        QVERIFY(beginSession(mode) != 0);
        {
            TraceAttempt attempt(this);
            TraceSpan span(TraceScope::FacadeResolve, this, AppearanceWork::FacadeUpdate);
            QCOMPARE(span.active(), mode == TraceMode::Trace);
            TraceAggregate leaf(TraceScope::AggLocalDerivation, 0, AppearanceWork::LocalDerivation);
            AppearanceTrace::counter(TraceScope::PaletteBuild, this, 2, AppearanceWork::SharedPairMeasurement);
            attempt.finish(TraceOutcome::Accepted, 0, 1);
        }
        AppearanceTrace::stop();
        QVERIFY(!AppearanceTrace::enabled());
        QVERIFY(AppearanceTrace::quiesce(1000));
        QCOMPARE(work.count(AppearanceWork::SourceUpdate), 1);
        QCOMPARE(work.count(AppearanceWork::FacadeUpdate), 1);
        QCOMPARE(work.count(AppearanceWork::LocalDerivation), 1);
        QCOMPARE(work.count(AppearanceWork::SharedPairMeasurement), 1);
        // Stopping tracing must not unsubscribe the independent typed observer.
        AppearanceTrace::work(AppearanceWork::FacadeUpdate);
        QCOMPARE(work.count(AppearanceWork::FacadeUpdate), 2);
        QString error;
        const QString stem = mode == TraceMode::Trace ? QStringLiteral("work-trace") : QStringLiteral("work-summary");
        QVERIFY2(AppearanceTrace::exportTo(m_dir.filePath(stem + QStringLiteral(".json")),
            m_dir.filePath(stem + QStringLiteral(".sidecar.json")), &error), qPrintable(error));
    }
}

void AppearanceTraceTests::recordsKnownNesting()
{
    QVERIFY(beginSession() != 0);
    {
        TraceSpan outer(TraceScope::FacadeResolve, this);
        QVERIFY(outer.active());
        {
            TraceSpan inner(TraceScope::LocalInstall, this);
            QVERIFY(inner.active());
        }
        TraceSpan sibling(TraceScope::NotificationDrain, this);
        QVERIFY(sibling.active());
    }
    AppearanceTrace::stop();
    QVERIFY(AppearanceTrace::quiesce(1000));
    QVERIFY(!AppearanceTrace::sessionIncomplete());

    const QString trace = m_dir.filePath(QStringLiteral("nesting.trace.json"));
    const QString sidecar = m_dir.filePath(QStringLiteral("nesting.sidecar.json"));
    QString error;
    QVERIFY2(AppearanceTrace::exportTo(trace, sidecar, &error), qPrintable(error));

    QFile file(trace);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto document = QJsonDocument::fromJson(file.readAll());
    QVERIFY(document.isObject());
    const auto events = document.object().value(QStringLiteral("traceEvents")).toArray();
    // Three scope records plus the sentinel.
    QCOMPARE(events.size(), 4);
    QHash<quint32, QJsonObject> spans;
    for (const auto &value : events) {
        const auto event = value.toObject();
        if (event.value(QStringLiteral("ph")) != QLatin1String("X"))
            continue;
        spans.insert(quint32(event.value(QStringLiteral("args")).toObject()
                                 .value(QStringLiteral("span")).toInt()), event);
    }
    QCOMPARE(spans.size(), 3);
    const auto find = [&](const char *name) {
        for (const auto &event : spans) {
            if (event.value(QStringLiteral("name")) == QLatin1String(name))
                return event;
        }
        return QJsonObject{};
    };
    const auto outer = find("facade-resolve");
    const auto inner = find("local-install");
    const auto sibling = find("notification-drain");
    QVERIFY(!outer.isEmpty() && !inner.isEmpty() && !sibling.isEmpty());
    const auto args = [](const QJsonObject &event, const char *key) {
        return quint32(event.value(QStringLiteral("args")).toObject()
                           .value(QLatin1String(key)).toInt());
    };
    QCOMPARE(args(inner, "parent"), args(outer, "span"));
    QCOMPARE(args(sibling, "parent"), args(outer, "span"));
    QCOMPARE(args(outer, "parent"), 0u);
    QVERIFY(args(outer, "span") != args(inner, "span"));
    QVERIFY(args(outer, "span") != args(sibling, "span"));
}

void AppearanceTraceTests::attemptOutcomes()
{
    QVERIFY(beginSession(TraceMode::Summary) != 0);
    Appearance appearance;
    appearance.setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}});
    appearance.setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}}); // no-op
    appearance.setInput({{QStringLiteral("schemaVersion"), 7}});                // rejected
    AppearanceTrace::stop();
    QVERIFY(AppearanceTrace::quiesce(1000));

    const QString trace = m_dir.filePath(QStringLiteral("outcomes.trace.json"));
    const QString sidecar = m_dir.filePath(QStringLiteral("outcomes.sidecar.json"));
    QString error;
    QVERIFY2(AppearanceTrace::exportTo(trace, sidecar, &error), qPrintable(error));

    QFile file(trace);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto events = QJsonDocument::fromJson(file.readAll())
        .object().value(QStringLiteral("traceEvents")).toArray();
    int accepted = 0, noops = 0, rejected = 0;
    for (const auto &value : events) {
        const auto event = value.toObject();
        if (event.value(QStringLiteral("name")) != QLatin1String("update-attempt"))
            continue;
        const auto outcome = event.value(QStringLiteral("args")).toObject()
                                 .value(QStringLiteral("outcome")).toString();
        if (outcome == QLatin1String("accepted")) ++accepted;
        else if (outcome == QLatin1String("noop")) ++noops;
        else if (outcome == QLatin1String("rejected")) ++rejected;
        else QFAIL("attempt has no classified outcome");
    }
    QCOMPARE(accepted, 1);
    QCOMPARE(noops, 1);
    QCOMPARE(rejected, 1);
}

void AppearanceTraceTests::reentrantAttempts()
{
    QVERIFY(beginSession(TraceMode::Summary) != 0);
    Appearance appearance;
    bool once = false;
    QObject::connect(&appearance, &Appearance::resolvedChanged, &appearance, [&] {
        if (once)
            return;
        once = true;
        // Nested replacement: the outer attempt must still record correctly.
        appearance.setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}});
    });
    appearance.setInput({{QStringLiteral("contrast"), QStringLiteral("high")}});
    QVERIFY(once);
    // Replacement semantics: the nested input replaces the outer one entirely.
    QCOMPARE(appearance.scheme(), QStringLiteral("dark"));
    QCOMPARE(appearance.contrast(), QStringLiteral("normal"));
    AppearanceTrace::stop();
    QVERIFY(AppearanceTrace::quiesce(1000));
    QVERIFY(!AppearanceTrace::sessionIncomplete());

    const QString trace = m_dir.filePath(QStringLiteral("reentrant.trace.json"));
    const QString sidecar = m_dir.filePath(QStringLiteral("reentrant.sidecar.json"));
    QString error;
    QVERIFY2(AppearanceTrace::exportTo(trace, sidecar, &error), qPrintable(error));
    QFile file(trace);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto events = QJsonDocument::fromJson(file.readAll())
        .object().value(QStringLiteral("traceEvents")).toArray();
    int attempts = 0;
    QSet<quint32> ids;
    for (const auto &value : events) {
        const auto event = value.toObject();
        if (event.value(QStringLiteral("name")) != QLatin1String("update-attempt"))
            continue;
        ++attempts;
        ids.insert(quint32(event.value(QStringLiteral("args")).toObject()
                               .value(QStringLiteral("update")).toInt()));
    }
    QCOMPARE(attempts, 2);
    QCOMPARE(ids.size(), 2);            // distinct attempt IDs through reentrancy
}

void AppearanceTraceTests::scopeOutlivesSource()
{
    QVERIFY(beginSession() != 0);
    auto *owner = new QObject;
    {
        TraceSpan span(TraceScope::FacadeResolve, owner);
        delete owner;                   // the record keeps an opaque label only
    }
    AppearanceTrace::stop();
    QVERIFY(AppearanceTrace::quiesce(1000));
    QVERIFY(!AppearanceTrace::sessionIncomplete());
    QCOMPARE(AppearanceTrace::recordsWritten(), 1);
    // A stopped session still owns its buffers until exported.
    QString error;
    QVERIFY2(AppearanceTrace::exportTo(m_dir.filePath(QStringLiteral("outlives.trace.json")),
                                     m_dir.filePath(QStringLiteral("outlives.sidecar.json")), &error),
             qPrintable(error));
}

void AppearanceTraceTests::threadsSeparated()
{
    QVERIFY(beginSession() != 0);
    {
        TraceSpan gui(TraceScope::FacadeResolve, this);
        QVERIFY(gui.active());
        QThread worker;
        worker.start();
        std::atomic<bool> done{false};
        auto *task = QThread::create([&] {
            TraceSpan span(TraceScope::IconRefresh, this);
            QVERIFY(span.active());
        });
        task->start();
        task->wait();
        done = true;
        worker.quit();
        worker.wait();
    }
    AppearanceTrace::stop();
    QVERIFY(AppearanceTrace::quiesce(1000));
    QVERIFY(!AppearanceTrace::sessionIncomplete());

    const QString trace = m_dir.filePath(QStringLiteral("threads.trace.json"));
    const QString sidecar = m_dir.filePath(QStringLiteral("threads.sidecar.json"));
    QString error;
    QVERIFY2(AppearanceTrace::exportTo(trace, sidecar, &error), qPrintable(error));
    QFile file(trace);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto events = QJsonDocument::fromJson(file.readAll())
        .object().value(QStringLiteral("traceEvents")).toArray();
    QSet<int> threads;
    for (const auto &value : events) {
        const auto event = value.toObject();
        if (event.value(QStringLiteral("ph")) == QLatin1String("X"))
            threads.insert(event.value(QStringLiteral("tid")).toInt());
    }
    QCOMPARE(threads.size(), 2);        // GUI and worker have distinct slots

    QFile sidecarFile(sidecar);
    QVERIFY(sidecarFile.open(QIODevice::ReadOnly));
    const auto sidecarObject = QJsonDocument::fromJson(sidecarFile.readAll()).object();
    QCOMPARE(sidecarObject.value(QStringLiteral("threads")).toObject().size(), 2);
    QVERIFY(!sidecarObject.value(QStringLiteral("incomplete")).toBool());
}

void AppearanceTraceTests::overflowCounted()
{
    // 1 MiB holds ~16k records; nesting beyond 64 also drops explicitly.
    QVERIFY(beginSession(TraceMode::Trace, 1) != 0);
    for (int i = 0; i < 40000; ++i) {
        TraceSpan span(TraceScope::FacadeResolve, this);
    }
    AppearanceTrace::stop();
    QVERIFY(AppearanceTrace::quiesce(1000));
    QVERIFY(AppearanceTrace::sessionIncomplete());
    QVERIFY(AppearanceTrace::recordsDropped() > 0);

    const QString trace = m_dir.filePath(QStringLiteral("overflow.trace.json"));
    const QString sidecar = m_dir.filePath(QStringLiteral("overflow.sidecar.json"));
    QString error;
    QVERIFY2(AppearanceTrace::exportTo(trace, sidecar, &error), qPrintable(error));
    QFile sidecarFile(sidecar);
    QVERIFY(sidecarFile.open(QIODevice::ReadOnly));
    const auto sidecarObject = QJsonDocument::fromJson(sidecarFile.readAll()).object();
    QVERIFY(sidecarObject.value(QStringLiteral("incomplete")).toBool());
    QVERIFY(sidecarObject.value(QStringLiteral("recordsDropped")).toString().toULongLong() > 0);
}

void AppearanceTraceTests::stopAdmitsNoNewScopes()
{
    QVERIFY(beginSession() != 0);
    {
        TraceSpan admitted(TraceScope::FacadeResolve, this);
        QVERIFY(admitted.active());
        AppearanceTrace::stop();
        // Admitted before stop: still finishes into the buffer, flagged outside.
    }
    {
        TraceSpan late(TraceScope::FacadeResolve, this);
        QVERIFY(!late.active());        // new scopes are not admitted after stop
    }
    QVERIFY(AppearanceTrace::quiesce(1000));

    const QString trace = m_dir.filePath(QStringLiteral("stop.trace.json"));
    const QString sidecar = m_dir.filePath(QStringLiteral("stop.sidecar.json"));
    QString error;
    QVERIFY2(AppearanceTrace::exportTo(trace, sidecar, &error), qPrintable(error));
    QFile file(trace);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto events = QJsonDocument::fromJson(file.readAll())
        .object().value(QStringLiteral("traceEvents")).toArray();
    int scopes = 0;
    for (const auto &value : events) {
        const auto event = value.toObject();
        if (event.value(QStringLiteral("name")) == QLatin1String("facade-resolve")) {
            ++scopes;
            QVERIFY(event.value(QStringLiteral("args")).toObject()
                        .value(QStringLiteral("flags")).toInt() & 1);
        }
    }
    QCOMPARE(scopes, 1);
}

void AppearanceTraceTests::exportRefusesExistingPath()
{
    QVERIFY(beginSession() != 0);
    { TraceSpan span(TraceScope::FacadeResolve, this); }
    AppearanceTrace::stop();
    QVERIFY(AppearanceTrace::quiesce(1000));

    QFile existing(m_dir.filePath(QStringLiteral("existing.trace.json")));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("occupied");
    existing.close();

    QString error;
    QVERIFY(!AppearanceTrace::exportTo(existing.fileName(),
                                       m_dir.filePath(QStringLiteral("fresh.sidecar.json")), &error));
    QVERIFY(!error.isEmpty());
    // The failed export leaves the session stopped but not consumed.
    QVERIFY(AppearanceTrace::exportTo(m_dir.filePath(QStringLiteral("fresh.trace.json")),
                                      m_dir.filePath(QStringLiteral("fresh.sidecar.json")), &error));
}

void AppearanceTraceTests::exportIsParseable()
{
    QVERIFY(beginSession(TraceMode::Summary) != 0);
    Appearance appearance;
    appearance.setInput({{QStringLiteral("scheme"), QStringLiteral("dark")}});
    AppearanceTrace::stop();
    QVERIFY(AppearanceTrace::quiesce(1000));

    const QString trace = m_dir.filePath(QStringLiteral("parseable.trace.json"));
    const QString sidecar = m_dir.filePath(QStringLiteral("parseable.sidecar.json"));
    QString error;
    QVERIFY2(AppearanceTrace::exportTo(trace, sidecar, &error), qPrintable(error));

    QFile file(trace);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(parseError.errorString()));
    QVERIFY(document.isObject());
    QVERIFY(document.object().contains(QStringLiteral("traceEvents")));

    QFile sidecarFile(sidecar);
    QVERIFY(sidecarFile.open(QIODevice::ReadOnly));
    const auto sidecarObject = QJsonDocument::fromJson(sidecarFile.readAll()).object();
    QCOMPARE(sidecarObject.value(QStringLiteral("clock")).toString(),
             QStringLiteral("CLOCK_MONOTONIC"));
    QCOMPARE(sidecarObject.value(QStringLiteral("mode")).toString(), QStringLiteral("summary"));
    QVERIFY(sidecarObject.value(QStringLiteral("originMonotonicNs")).toString().toULongLong() > 0);
    QVERIFY(sidecarObject.value(QStringLiteral("originRealtimeNs")).toString().toULongLong() > 0);
}

QTEST_MAIN(AppearanceTraceTests)
#include "tst_appearance_trace.moc"
