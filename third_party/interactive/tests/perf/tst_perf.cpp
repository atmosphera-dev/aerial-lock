// SPDX-License-Identifier: LGPL-3.0-or-later
#include "Appearance.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QTimer>
#include <QtTest>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <time.h>

namespace {

// Arm before the operation. Ignore an already-in-flight frame: the acknowledged
// swap must follow a scene-graph synchronization after this waiter was armed.
class NextFrame
{
public:
    explicit NextFrame(QQuickWindow *window) : window(window)
    {
        sync = QObject::connect(window, &QQuickWindow::beforeSynchronizing, &loop,
            [this] { synchronized.store(true); }, Qt::DirectConnection);
        swap = QObject::connect(window, &QQuickWindow::frameSwapped, &loop, [this] {
            if (synchronized.load()) {
                swapped.store(true);
                QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
            }
        }, Qt::DirectConnection);
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    }
    ~NextFrame()
    {
        QObject::disconnect(sync);
        QObject::disconnect(swap);
    }
    bool wait()
    {
        timer.start(15000);
        window->update(); // Even an unchanged hover needs a submitted frame.
        loop.exec();
        return swapped.load();
    }
private:
    QQuickWindow *window;
    QEventLoop loop;
    QTimer timer;
    QMetaObject::Connection sync, swap;
    std::atomic_bool synchronized = false;
    std::atomic_bool swapped = false;
};

void movePointer(QQuickWindow *window, const QPointF &position)
{
    // Synchronous real QQuickWindow event dispatch, including application filters
    // and Qt Quick's delivery agent; no per-event QTest delay or native-input tool.
    QMouseEvent event(QEvent::MouseMove, position, position,
        window->mapToGlobal(position), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &event);
}

QList<QQuickItem *> sceneControls(QQuickItem *root)
{
    QList<QQuickItem *> result;
    // Loader sets visual parentage, not necessarily QObject ownership. Enumerate
    // the actual scene, which is also what pointer hit testing and rendering use.
    for (auto *child : root->childItems()) {
        if (child->objectName() == QLatin1String("perfControl"))
            result.append(child);
        result.append(sceneControls(child));
    }
    return result;
}

QJsonObject distribution(QList<double> rounds)
{
    QJsonArray samples;
    for (double value : rounds)
        samples.append(value);
    std::sort(rounds.begin(), rounds.end());
    // Nearest-rank percentiles: with five rounds, p95 is the slowest round.
    return {{QStringLiteral("median"), rounds[rounds.size() / 2]},
        {QStringLiteral("p95"), rounds[qsizetype(std::ceil(0.95 * rounds.size())) - 1]},
        {QStringLiteral("rounds"), samples}};
}

qint64 processCpuNanoseconds()
{
    timespec time{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time) != 0)
        return -1;
    return qint64(time.tv_sec) * 1000000000 + time.tv_nsec;
}

struct BatchSamples
{
    QList<double> wall, cpu;

    bool record(const QElapsedTimer &clock, qint64 cpuStart)
    {
        const qint64 wallNs = clock.nsecsElapsed();
        const qint64 cpuEnd = processCpuNanoseconds();
        if (cpuStart < 0 || cpuEnd < cpuStart)
            return false;
        wall.append(wallNs / 1e6);
        cpu.append((cpuEnd - cpuStart) / 1e6);
        return true;
    }

    QJsonObject json(const QString &wallUnit) const
    {
        return {{wallUnit, distribution(wall)}, {QStringLiteral("cpu_ms"), distribution(cpu)}};
    }
};

} // namespace

class PerformanceTests : public QObject
{
    Q_OBJECT
private slots:
    void mixedControls();
};

void PerformanceTests::mixedControls()
{
    BatchSamples create, accent, hover, idle;
    // Run the identical workload once before the five measured rounds. Fresh
    // scene-graph/shader/font caches otherwise skew the first hover batch.
    for (int round = -1; round < 5; ++round) {
        BatchSamples warmup;
        QElapsedTimer clock;
        qint64 cpuStart = processCpuNanoseconds();
        QVERIFY2(cpuStart >= 0, "CLOCK_PROCESS_CPUTIME_ID is unavailable");
        clock.start();
        QQuickView view;
        // Set the view's format before setSource/show can create its native
        // window/surface. Frame completion remains measured, not refresh pacing.
        QSurfaceFormat format = view.requestedFormat();
        format.setSwapInterval(0);
        view.setFormat(format);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        {
            NextFrame first(&view);
            view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/InterActive/Perf/Scene.qml")));
            QVERIFY2(view.status() == QQuickView::Ready, qPrintable(view.errors().isEmpty()
                ? QStringLiteral("Scene did not load") : view.errors().first().toString()));
            view.show();
            QVERIFY2(first.wait(), "No first frame swapped within 15 seconds");
        }
        QVERIFY2((round < 0 ? warmup : create).record(clock, cpuStart), "Could not measure process CPU time");
        QVERIFY(view.isExposed());
        QCOMPARE(view.format().swapInterval(), 0);
        QCOMPARE(view.rendererInterface()->graphicsApi(), QSGRendererInterface::OpenGL);
        const auto controls = sceneControls(view.rootObject());
        QCOMPARE(controls.size(), 300);
        for (const auto *control : controls) {
            QVERIFY(control->isVisible());
            QVERIFY(control->width() > 0 && control->height() > 0);
            QVERIFY(view.rootObject()->contains(control->mapToScene(
                QPointF(control->width() / 2, control->height() / 2))));
        }
        auto *appearance = view.engine()->singletonInstance<InterActive::Appearance *>(
            "InterActive.Foundation", "Appearance");
        QVERIFY(appearance);
        cpuStart = processCpuNanoseconds();
        clock.restart();
        for (int i = 0; i < 60; ++i) {
            NextFrame frame(&view);
            // Appearance normalizes input to RGB; QColor equality also compares
            // the color specification, not just the displayed RGB components.
            const QColor color = QColor::fromHsv((i * 6 + std::max(0, round)) % 360, 150, 210).toRgb();
            appearance->setInput({{QStringLiteral("tokens"), QVariantMap{
                {QStringLiteral("colors"), QVariantMap{{QStringLiteral("accent"), color}}}}}});
            QVERIFY2(frame.wait(), "Accent frame did not swap within 15 seconds");
            QCOMPARE(appearance->colors()->accent(), color);
        }
        QVERIFY2((round < 0 ? warmup : accent).record(clock, cpuStart), "Could not measure process CPU time");
        cpuStart = processCpuNanoseconds();
        clock.restart();
        for (const auto *control : controls) {
            NextFrame frame(&view);
            movePointer(&view, control->mapToScene(QPointF(control->width() / 2, control->height() / 2)));
            QVERIFY2(frame.wait(), "Hover frame did not swap within 15 seconds");
            QVERIFY2(control->property("hovered").toBool(), "Pointer move did not reach the control");
        }
        QVERIFY2((round < 0 ? warmup : hover).record(clock, cpuStart), "Could not measure process CPU time");
        {
            NextFrame frame(&view);
            movePointer(&view, QPointF(20, view.height() - 24));
            QVERIFY(frame.wait()); // Leave the last control before timing empty-area dispatch.
        }
        cpuStart = processCpuNanoseconds();
        clock.restart();
        for (int i = 0; i < 1000; ++i)
            movePointer(&view, QPointF(20 + i % 100, view.height() - 24));
        // Wall ns / 1000 events / 1000 ns/us has the same numeric divisor
        // as ms for this fixed batch. CPU remains total ms for all 1000 events.
        QVERIFY2((round < 0 ? warmup : idle).record(clock, cpuStart), "Could not measure process CPU time");
    }
    const QJsonDocument results(QJsonObject{
        {QStringLiteral("create_ms"), create.json(QStringLiteral("wall_ms"))},
        {QStringLiteral("accent_update_ms"), accent.json(QStringLiteral("wall_ms"))},
        {QStringLiteral("hover_move_ms"), hover.json(QStringLiteral("wall_ms"))},
        {QStringLiteral("idle_move_us"), idle.json(QStringLiteral("wall_us"))}});
    const QByteArray json = results.toJson(QJsonDocument::Indented);
    const QString path = qEnvironmentVariable("IA_PERF_OUT");
    if (path.isEmpty()) {
        std::fputs(json.constData(), stdout);
    } else {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::NewOnly), qPrintable(file.errorString()));
        QCOMPARE(file.write(json), qint64(json.size()));
        QVERIFY(file.flush());
    }
}

QTEST_MAIN(PerformanceTests)
#include "tst_perf.moc"
