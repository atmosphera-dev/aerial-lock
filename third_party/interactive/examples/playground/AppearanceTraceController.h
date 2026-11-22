// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Playground-owned diagnostic session owner. Foundation's recorder knows generic
// scopes only; this controller owns CLI state, arm/start/stop timing, window and
// input observation, and post-recording export.
#include "AppearanceTrace_p.h"
#include "interactive_playground_scene_export.h"
#include <QGuiApplication>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <atomic>

class QQuickWindow;
namespace InterActive { class Appearance; }

struct AppearanceProfileOptions {
    QString summaryPath;
    QString tracePath;              // empty: summary-only session
    int delayMs = 5000;
    int durationMs = 24000;
    quint32 capacityMiB = 128;
    bool enabled = false;
};

// Diagnostic notify wrapper: passes every delivery through unchanged. Only
// outermost spontaneous window input creates an input ID; nested item delivery
// retains that context.
class PlaygroundApplication : public QGuiApplication
{
    Q_OBJECT
public:
    using QGuiApplication::QGuiApplication;
    bool notify(QObject *receiver, QEvent *event) override;
};

// QML bridge for handler spans: playground-owned, wraps generic recorder scopes
// so library instrumentation never imports its consumer. begin returns an opaque
// token (0 when tracing is off); end consumes it. The token table is bounded.
class INTERACTIVE_PLAYGROUND_SCENE_EXPORT AppearanceTraceBridge : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AppearanceTraceBridge)
    QML_UNCREATABLE("Session-owned diagnostic bridge")
public:
    explicit AppearanceTraceBridge(QObject *parent = nullptr) : QObject(parent) {}
    enum HandlerKind { ColorHandler, Synchronization };
    Q_ENUM(HandlerKind)
    enum ColorRole { Accent, Amber, Danger };
    Q_ENUM(ColorRole)
    Q_INVOKABLE quint32 handlerBegin(const QString &name, quintptr owner);
    Q_INVOKABLE void handlerEnd(quint32 token, const QString &name, quintptr owner);
    Q_INVOKABLE quint32 pickerHandlerBegin(HandlerKind kind, ColorRole role, quintptr owner);
    Q_INVOKABLE void pickerHandlerEnd(quint32 token, quintptr owner);
private:
    struct HandlerSpan { quint32 spanId; quint64 startNs; quint16 scope; quint64 role; };
    QHash<quint32, HandlerSpan> m_spans;
    quint32 m_nextToken = 1;
};

class AppearanceTraceController : public QObject
{
    Q_OBJECT
public:
    AppearanceTraceController(const AppearanceProfileOptions &options,
                              QGuiApplication *app, InterActive::Appearance *appearance,
                              QObject *parent = nullptr);

    void arm();

signals:
    void sessionEvent(const QString &name); // IA_PROFILE markers

private:
    struct WindowState {
        std::atomic<quint64> frame{0};
        std::atomic<quint64> revision{0};
    };

    void begin();
    void end();
    void attachWindow(QQuickWindow *window);
    void writeSummary(const QString &path);

    const AppearanceProfileOptions m_options;
    QGuiApplication *const m_app;
    InterActive::Appearance *const m_appearance;
    std::atomic<quint64> m_revision{0};   // installed revision marker at sync
    QTimer m_armTimer;
    QTimer m_stopTimer;
    quint32 m_epoch = 0;
    bool m_recording = false;
    // Fixed-size full-precision accepted-color records; serialized at export.
    struct AcceptedColor { quint64 timestampNs; float r, g, b, a; };
    QList<AcceptedColor> m_accepted;
    QList<QPointer<QQuickWindow>> m_windows;
    QHash<QQuickWindow *, WindowState *> m_windowState;
    QList<QMetaObject::Connection> m_connections;
};
