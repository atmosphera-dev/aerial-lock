// SPDX-License-Identifier: LGPL-3.0-or-later
#include "AppearanceTraceController.h"
#include "Appearance.h"
#include "TokenGroups.h"
#include <QCryptographicHash>
#include <QFile>
#include <QInputEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QWindow>
#include <cstdio>

using namespace InterActive;

// --- PlaygroundApplication ---------------------------------------------------

bool PlaygroundApplication::notify(QObject *receiver, QEvent *event)
{
    const auto type = event->type();
    const bool pointer = type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease
        || type == QEvent::MouseMove || type == QEvent::MouseButtonDblClick;
    if (pointer && static_cast<QInputEvent *>(event)->spontaneous()
        && qobject_cast<QWindow *>(receiver)) {
        auto *input = static_cast<QInputEvent *>(event);
        // Pack: event type (8) | spontaneous (1) | native timestamp ms (55).
        const quint64 packed = (quint64(type) & 0xFF) | (1ull << 8)
            | ((input->timestamp() & 0x7FFFFFFFFFFFFFull) << 9);
        const quint32 id = TraceInput::begin(receiver, packed);
        const bool handled = QGuiApplication::notify(receiver, event);
        TraceInput::end(id, receiver, packed);
        return handled;
    }
    return QGuiApplication::notify(receiver, event);
}

// --- AppearanceTraceController ----------------------------------------------

AppearanceTraceController::AppearanceTraceController(const AppearanceProfileOptions &options,
                                                     QGuiApplication *app, InterActive::Appearance *appearance,
                                                     QObject *parent)
    : QObject(parent), m_options(options), m_app(app), m_appearance(appearance)
{
    m_armTimer.setSingleShot(true);
    m_stopTimer.setSingleShot(true);
    connect(&m_armTimer, &QTimer::timeout, this, &AppearanceTraceController::begin);
    connect(&m_stopTimer, &QTimer::timeout, this, &AppearanceTraceController::end);
    // Track the latest installed appearance revision for frame correlation.
    connect(m_appearance, &InterActive::Appearance::resolvedChanged, this, [this] {
        m_revision.fetch_add(1, std::memory_order_relaxed);
    });
    // Accepted-color sequence: full-precision fixed-size records, no per-event
    // formatting. Only actual installed accent changes count.
    connect(m_appearance->colors(), &ColorTokens::accentChanged, this, [this] {
        if (!m_recording)
            return;
        const QColor accent = m_appearance->colors()->accent();
        float r = 0, g = 0, b = 0, a = 0;
        accent.getRgbF(&r, &g, &b, &a);
        m_accepted.append({AppearanceTrace::monotonicNow(), r, g, b, a});
        AppearanceTrace::instant(TraceScope::AcceptedColor, m_appearance,
            quint64(m_accepted.size()), true);
    });
}

quint32 AppearanceTraceBridge::handlerBegin(const QString &name, quintptr owner)
{
    return pickerHandlerBegin(name == QLatin1String("syncAccentPicker") ? Synchronization : ColorHandler,
                              Accent, owner);
}

quint32 AppearanceTraceBridge::pickerHandlerBegin(HandlerKind kind, ColorRole role, quintptr owner)
{
    if (!AppearanceTrace::detailed())
        return 0;
    if ((kind != Synchronization && kind != ColorHandler) || role < Accent || role > Danger)
        return 0;
    // QML handler spans map to fixed generic scopes; no per-call strings.
    const TraceScope scope = kind == Synchronization
        ? TraceScope::QmlSyncPicker : TraceScope::QmlColorHandler;
    const quint64 startNs = AppearanceTrace::monotonicNow();
    const quint32 spanId = AppearanceTrace::spanBegin(scope, reinterpret_cast<const void *>(owner), false);
    if (!spanId)
        return 0;
    if (m_spans.size() >= 256) {
        AppearanceTrace::spanEnd(spanId, startNs, scope, reinterpret_cast<const void *>(owner), quint64(role));
        return 0;                       // bounded; unmatched ends drop silently
    }
    const quint32 token = m_nextToken++;
    if (!m_nextToken)
        m_nextToken = 1;
    m_spans.insert(token, HandlerSpan{spanId, startNs, quint16(scope), quint64(role)});
    return token;
}

void AppearanceTraceBridge::handlerEnd(quint32 token, const QString &, quintptr owner)
{
    pickerHandlerEnd(token, owner);
}

void AppearanceTraceBridge::pickerHandlerEnd(quint32 token, quintptr owner)
{
    if (!token)
        return;
    const auto found = m_spans.constFind(token);
    if (found == m_spans.constEnd())
        return;
    const HandlerSpan span = found.value();
    m_spans.erase(found);
    AppearanceTrace::spanEnd(span.spanId, span.startNs, TraceScope(span.scope),
                             reinterpret_cast<const void *>(owner), span.role);
}

void AppearanceTraceController::arm()
{
    std::fprintf(stdout, "IA_PROFILE {\"event\":\"armed\",\"delayMs\":%d,\"durationMs\":%d}\n",
                 m_options.delayMs, m_options.durationMs);
    std::fflush(stdout);
    m_armTimer.start(m_options.delayMs);
}

void AppearanceTraceController::begin()
{
    for (auto *window : m_app->allWindows()) {
        if (auto *quick = qobject_cast<QQuickWindow *>(window))
            attachWindow(quick);
    }
    AppearanceTrace::Config config;
    config.capacityMiB = m_options.capacityMiB;
    m_epoch = AppearanceTrace::start(
        m_options.tracePath.isEmpty() ? TraceMode::Summary : TraceMode::Trace, config);
    if (!m_epoch) {
        std::fprintf(stderr, "IA_PROFILE {\"event\":\"start-failed\"}\n");
        std::fflush(stderr);
        emit sessionEvent(QStringLiteral("start-failed"));
        return;
    }
    m_recording = true;
    m_stopTimer.start(m_options.durationMs);
    std::fprintf(stdout, "IA_PROFILE {\"event\":\"recording\",\"epoch\":%u}\n", m_epoch);
    std::fflush(stdout);
    emit sessionEvent(QStringLiteral("recording"));
}

void AppearanceTraceController::end()
{
    if (!m_recording)
        return;
    m_recording = false;
    // Stop admitting; admitted scopes finish into their original buffers.
    AppearanceTrace::stop();
    for (const auto &connection : m_connections)
        disconnect(connection);
    m_connections.clear();
    const bool idle = AppearanceTrace::quiesce(1000);
    std::fprintf(stdout, "IA_PROFILE {\"event\":\"stopping\",\"quiesced\":%s}\n", idle ? "true" : "false");
    std::fflush(stdout);

    // Summary first: attemptDurationsUs reads the stopped session's arena,
    // and exportTo() releases it.
    writeSummary(m_options.summaryPath);
    if (!m_options.tracePath.isEmpty()) {
        QString error;
        if (!AppearanceTrace::exportTo(m_options.tracePath,
                                       m_options.tracePath + QStringLiteral(".sidecar.json"), &error)) {
            std::fprintf(stderr, "IA_PROFILE {\"event\":\"export-failed\",\"message\":\"%s\"}\n",
                         qPrintable(error));
            std::fflush(stderr);
            emit sessionEvent(QStringLiteral("export-failed"));
            return;
        }
    }
    std::fprintf(stdout, "IA_PROFILE {\"event\":\"exported\",\"incomplete\":%s}\n",
                 AppearanceTrace::sessionIncomplete() ? "true" : "false");
    std::fflush(stdout);
    emit sessionEvent(QStringLiteral("exported"));
}

void AppearanceTraceController::attachWindow(QQuickWindow *window)
{
    if (m_windowState.contains(window))
        return;
    auto *state = new WindowState;
    m_windowState.insert(window, state);
    m_windows.append(window);
    // Render-thread callbacks record only captured scalar IDs and atomics;
    // window identity is an opaque label, never dereferenced off the GUI thread.
    m_connections.append(connect(window, &QQuickWindow::beforeSynchronizing, window,
        [this, window, state] {
            const quint64 frame = state->frame.fetch_add(1, std::memory_order_relaxed) + 1;
            state->revision.store(m_revision.load(std::memory_order_relaxed), std::memory_order_relaxed);
            AppearanceTrace::instant(TraceScope::FrameSync, window,
                (frame << 32) | (state->revision.load(std::memory_order_relaxed) & 0xFFFFFFFFull), true);
        }, Qt::DirectConnection));
    m_connections.append(connect(window, &QQuickWindow::afterFrameEnd, window,
        [window, state] {
            AppearanceTrace::instant(TraceScope::FrameSubmit, window,
                state->frame.load(std::memory_order_relaxed) << 32, true);
        }, Qt::DirectConnection));
    m_connections.append(connect(window, &QObject::destroyed, this, [this, window, state] {
        m_windows.removeAll(window);
        m_windowState.remove(window);
        delete state;
    }));
}

void AppearanceTraceController::writeSummary(const QString &path)
{
    QJsonObject summary;
    summary.insert(QStringLiteral("epoch"), qint64(m_epoch));
    summary.insert(QStringLiteral("recordsWritten"),
                   QString::number(AppearanceTrace::recordsWritten()));
    summary.insert(QStringLiteral("recordsDropped"),
                   QString::number(AppearanceTrace::recordsDropped()));
    summary.insert(QStringLiteral("incomplete"), AppearanceTrace::sessionIncomplete());
    // The canonical accepted-color sequence: serialized after the interval from
    // fixed-size records, hashed for cheap pair comparison.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QJsonArray colors;
    for (const auto &accepted : m_accepted) {
        // The canonical sequence hashes color payload only; timestamps vary.
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(&accepted.r),
                                    sizeof(float) * 4));
        colors.append(QJsonArray{QString::number(accepted.timestampNs),
                                 double(accepted.r), double(accepted.g),
                                 double(accepted.b), double(accepted.a)});
    }
    summary.insert(QStringLiteral("acceptedColors"), colors);
    summary.insert(QStringLiteral("acceptedColorSha256"),
                   QString::fromLatin1(hash.result().toHex()));
    // Coarse per-attempt wall timing distribution, available in both summary
    // and trace modes at the same setInput entry/exit points.
    const QVector<quint64> durations = AppearanceTrace::attemptDurationsUs();
    QJsonArray attemptArray;
    for (quint64 duration : durations)
        attemptArray.append(QString::number(duration));
    summary.insert(QStringLiteral("attemptDurationsUs"), attemptArray);
    summary.insert(QStringLiteral("attemptCount"), qint64(durations.size()));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        std::fprintf(stderr, "IA_PROFILE {\"event\":\"summary-failed\",\"path-in-use\":true}\n");
        std::fflush(stderr);
        return;
    }
    file.write(QJsonDocument(summary).toJson(QJsonDocument::Compact));
    file.write("\n");
    file.flush();
}
