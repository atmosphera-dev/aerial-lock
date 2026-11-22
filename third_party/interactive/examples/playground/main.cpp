// SPDX-License-Identifier: LGPL-3.0-or-later
#include "Appearance.h"
#include "AppearanceTraceController.h"
#include "PlaygroundState.h"
#include <QCommandLineParser>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlListReference>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <memory>
#include <unistd.h>

namespace {

void writeEvent(QJsonObject record)
{
    record.insert(QStringLiteral("protocolVersion"), 1);
    const QByteArray line = "IA_EVENT " + QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fflush(stdout);
}

void logToStderr(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const auto line = qFormatLogMessage(type, context, message).toLocal8Bit();
    std::fprintf(stderr, "%s\n", line.constData());
    std::fflush(stderr);
}

} // namespace

// Only atomics are accessed on the render thread. A frame's generation is
// captured at synchronization, so an in-flight old frame cannot acknowledge a
// newer appearance input. Window/item inspection stays on the GUI thread.
class FrameWitness : public QObject
{
    Q_OBJECT
public:
    FrameWitness(QQuickWindow *window, const std::atomic<quint64> *generation, QObject *parent)
        : QObject(parent)
    {
        connect(window, &QQuickWindow::beforeSynchronizing, this, [this, generation] {
            m_synchronized.store(generation->load());
        }, Qt::DirectConnection);
        // afterRendering only finishes command recording in Qt 6. Acknowledge
        // after submission, not while the previous frame is still pending.
        connect(window, &QQuickWindow::afterFrameEnd, this, [this, window] {
            emit rendered(window, m_synchronized.load());
        }, Qt::DirectConnection);
    }
signals:
    void rendered(QQuickWindow *window, quint64 generation);
private:
    std::atomic<quint64> m_synchronized{0};
};

class StartupScene : public QObject
{
    Q_OBJECT
public:
    StartupScene(QQuickWindow *window, bool nativeInput, bool automation)
        : m_window(window), m_nativeInput(nativeInput), m_automation(automation)
    {
        window->installEventFilter(this);
        connect(window, &QWindow::activeChanged, this, &StartupScene::requestFrame);
        auto *witness = new FrameWitness(window, &m_generation, this);
        connect(witness, &FrameWitness::rendered, this, [this](QQuickWindow *, quint64 generation) {
            if (m_started || !parentReady() || generation != m_generation.load())
                return;
            if (!m_nativeInput) {
                dispatch();
            } else if (!m_requested) {
                m_requested = true;
                m_waiting = true;
                if (m_automation)
                    emit inputRequired();
                else
                    std::fprintf(stderr, "Press Left Shift in the playground window to open the startup menu.\n");
            }
        }, Qt::QueuedConnection);
        requestFrame();
    }

signals:
    void inputRequired();

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Expose)
            requestFrame();
        if (m_waiting && parentReady() && event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            // QtWayland records the native seat/serial before delivering this
            // event. QtTest and constructed QKeyEvents have no native key data.
            if (key->spontaneous() && !key->isAutoRepeat() && key->key() == Qt::Key_Shift
                && key->nativeScanCode() != 0 && key->nativeVirtualKey() == 0xffe1) { // Shift_L
                m_waiting = false;
                QMetaObject::invokeMethod(this, [this] {
                    if (parentReady())
                        dispatch();
                    else
                        m_waiting = true;
                }, Qt::QueuedConnection);
            }
        }
        return QObject::eventFilter(object, event);
    }

private:
    bool parentReady() const
    {
        return m_window && m_window->isVisible() && m_window->isExposed() && m_window->isActive();
    }

    void requestFrame()
    {
        if (m_started || !m_window)
            return;
        ++m_generation;
        m_window->update();
    }

    void dispatch()
    {
        if (m_started)
            return;
        m_started = true;
        QMetaObject::invokeMethod(m_window, "startScene");
    }

    QPointer<QQuickWindow> m_window;
    const bool m_nativeInput;
    const bool m_automation;
    std::atomic<quint64> m_generation{0};
    bool m_requested = false;
    bool m_waiting = false;
    bool m_started = false;
};

class Automation : public QObject
{
    Q_OBJECT
public:
    Automation(QQuickWindow *window, PlaygroundState *state, InterActive::Appearance *appearance,
               const QString &scene, const QString &screenshot, QObject *parent)
        : QObject(parent), m_window(window), m_state(state), m_appearance(appearance), m_scene(scene),
          m_screenshot(screenshot)
    {
        m_deadline.setSingleShot(true);
        m_deadline.setInterval(15000);
        connect(&m_deadline, &QTimer::timeout, this, [this] {
            fail(QStringLiteral("Timed out waiting for exposed, rendered scene: ") + m_pending);
        });
        connect(window, SIGNAL(observationChanged()), this, SLOT(refresh()));
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
            if (!m_failed)
                closed();
        });
    }

    ~Automation() override
    {
        if (m_stdinFlags >= 0)
            ::fcntl(STDIN_FILENO, F_SETFL, m_stdinFlags);
    }

    void start()
    {
        // A one-shot screenshot uses the same ready observation without stdin:
        // EOF from a noninteractive caller must not cancel capture readiness.
        if (!m_screenshot.isEmpty()) {
            observe(QStringLiteral("ready"));
            return;
        }
        m_stdinFlags = ::fcntl(STDIN_FILENO, F_GETFL);
        if (m_stdinFlags < 0 || ::fcntl(STDIN_FILENO, F_SETFL, m_stdinFlags | O_NONBLOCK) < 0) {
            fail(QStringLiteral("Cannot make automation stdin nonblocking: ") + QString::fromLocal8Bit(std::strerror(errno)));
            return;
        }
        m_notifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, [this] { readCommands(); });
        observe(QStringLiteral("ready"));
    }

public slots:
    void requestNativeInput()
    {
        if (!m_closed && !m_failed && m_pending == QLatin1String("ready"))
            writeEvent({{QStringLiteral("event"), QStringLiteral("inputRequired")},
                        {QStringLiteral("key"), QStringLiteral("Shift_L")}});
    }

    void refresh()
    {
        if (m_failed || m_closed || m_pending.isEmpty() || !m_window)
            return;
        const bool ready = m_window->property("sceneReady").toBool();
        auto *content = qvariant_cast<QQuickItem *>(m_window->property("activePopupContent"));
        auto *host = content ? content->window() : nullptr;
        const QQmlListReference openContents(m_window, "openPopupContents");
        if (!openContents.isReadable()) {
            fail(QStringLiteral("The gallery did not expose its popup observation list."));
            return;
        }
        QList<QQuickItem *> contents;
        QList<QPointer<QQuickWindow>> hosts{m_window};
        bool missingHost = false;
        for (qsizetype index = 0; index < openContents.count(); ++index) {
            auto *item = qobject_cast<QQuickItem *>(openContents.at(index));
            if (!item)
                continue;
            contents.append(item);
            auto *popupWindow = item->window();
            if (!popupWindow)
                missingHost = true;
            else if (!hosts.contains(popupWindow))
                hosts.append(popupWindow);
        }
        const bool changed = ready != m_sceneReady || contents != m_contents || hosts != m_hosts;
        if (contents != m_contents) {
            for (const auto &connection : m_contentConnections)
                disconnect(connection);
            m_contentConnections.clear();
            m_contents = contents;
            for (auto *item : contents)
                m_contentConnections.append(connect(item, &QQuickItem::windowChanged, this, &Automation::refresh));
        }
        m_hosts = hosts;
        m_sceneReady = ready;
        for (const auto &window : m_hosts)
            watch(window);
        if (changed)
            requestFrames();
        QQuickWindow *expectedActiveHost = host ? host : m_window.data();
        if (!ready || missingHost || (content && !host) || !expectedActiveHost->isActive())
            return;

        const auto generation = m_generation.load();
        for (const auto &window : m_hosts) {
            if (!window || !window->isExposed() || m_frames.value(window) != generation)
                return;
            if (window->rendererInterface()->graphicsApi() != QSGRendererInterface::OpenGL) {
                fail(QStringLiteral("Automation requires the actual OpenGL RHI renderer."));
                return;
            }
        }

        const QString event = m_pending;
        m_pending.clear();
        m_deadline.stop();
        writeEvent({{QStringLiteral("event"), event},
                    {QStringLiteral("page"), QStringLiteral("foundations")},
                    {QStringLiteral("scene"), m_scene},
                    {QStringLiteral("input"), QJsonObject::fromVariantMap(m_state->appearanceInput())},
                    {QStringLiteral("resolvedScheme"), m_appearance->scheme()},
                    {QStringLiteral("viewport"), QJsonObject{{QStringLiteral("width"), m_window->width()},
                                                            {QStringLiteral("height"), m_window->height()}}},
                    {QStringLiteral("dpr"), m_window->devicePixelRatio()},
                    {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
                    {QStringLiteral("renderer"), QStringLiteral("opengl")},
                    {QStringLiteral("popupWindow"), bool(host && host != m_window && host->isExposed())}});
        if (event == QLatin1String("ready") && !m_screenshot.isEmpty()) {
            const QImage image = m_window->grabWindow();
            if (image.isNull() || !image.save(m_screenshot, "PNG")) {
                fail(QStringLiteral("Cannot save playground screenshot: ") + m_screenshot);
                return;
            }
            QCoreApplication::quit();
            return;
        }
        QMetaObject::invokeMethod(this, [this] { processNext(); }, Qt::QueuedConnection);
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Expose || event->type() == QEvent::Show
            || event->type() == QEvent::Hide) {
            QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection);
        }
        return QObject::eventFilter(object, event);
    }

private:
    void watch(QQuickWindow *window)
    {
        if (m_frames.contains(window))
            return;
        m_frames.insert(window, 0);
        window->installEventFilter(this);
        connect(window, &QWindow::activeChanged, this, [this] {
            // IA visuals depend on activation. Invalidate frames synchronized
            // before this transition, including those from the other hosts.
            if (m_window && !m_closed && !m_failed && !m_pending.isEmpty()) {
                requestFrames();
                refresh();
            }
        });
        auto *witness = new FrameWitness(window, &m_generation, this);
        connect(witness, &FrameWitness::rendered, this, [this](QQuickWindow *renderedWindow, quint64 generation) {
            if (m_frames.contains(renderedWindow) && generation == m_generation.load()) {
                m_frames[renderedWindow] = generation;
                refresh();
            }
        }, Qt::QueuedConnection);
        connect(window, &QObject::destroyed, this, [this, window, witness] {
            m_frames.remove(window);
            witness->deleteLater();
        });
        window->update();
    }

    void requestFrames()
    {
        ++m_generation;
        m_window->update();
        for (const auto &window : m_hosts) {
            if (window && window != m_window)
                window->update();
        }
    }

    void observe(const QString &event)
    {
        m_pending = event;
        requestFrames();
        m_deadline.start();
        refresh();
    }

    void readCommands()
    {
        char bytes[8192];
        while (!m_failed && !m_closed) {
            const ssize_t count = ::read(STDIN_FILENO, bytes, sizeof(bytes));
            if (count > 0) {
                m_buffer.append(bytes, qsizetype(count));
                if (m_buffer.size() > 1024 * 1024) {
                    fail(QStringLiteral("Automation input exceeded 1 MiB."));
                    return;
                }
                qsizetype end;
                while ((end = m_buffer.indexOf('\n')) >= 0) {
                    const QByteArray line = m_buffer.left(end);
                    m_buffer.remove(0, end + 1);
                    QJsonParseError error;
                    const auto document = QJsonDocument::fromJson(line, &error);
                    if (error.error != QJsonParseError::NoError || !document.isObject()) {
                        fail(QStringLiteral("Malformed command: expected one JSON object per line."));
                        return;
                    }
                    const auto command = document.object();
                    const auto name = command.value(QStringLiteral("command"));
                    const bool close = name.isString() && name.toString() == QLatin1String("close") && command.size() == 1;
                    const bool appearance = name.isString() && name.toString() == QLatin1String("setAppearance")
                        && command.size() == 2 && command.value(QStringLiteral("input")).isObject();
                    if (!close && !appearance) {
                        fail(QStringLiteral("Unsupported command; use setAppearance with an input object, or close."));
                        return;
                    }
                    if (close) {
                        requestClose();
                        return;
                    }
                    m_commands.push_back(command);
                    if (m_commands.size() > 256) {
                        fail(QStringLiteral("Too many pending automation commands."));
                        return;
                    }
                }
                continue;
            }
            if (count == 0) {
                m_notifier->setEnabled(false);
                if (!m_buffer.isEmpty()) {
                    fail(QStringLiteral("EOF in an unterminated JSON command."));
                    return;
                }
                requestClose();
                return;
            }
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            fail(QStringLiteral("Cannot read automation stdin: ") + QString::fromLocal8Bit(std::strerror(errno)));
            return;
        }
        processNext();
    }

    void processNext()
    {
        if (m_failed || m_closed || !m_pending.isEmpty() || m_commands.empty())
            return;
        const auto command = m_commands.front();
        m_commands.pop_front();
        if (!m_state->setAppearance(command.value(QStringLiteral("input")).toObject().toVariantMap())) {
            fail(m_state->appearanceError());
            return;
        }
        observe(QStringLiteral("appearanceChanged"));
    }

    void requestClose()
    {
        // Shutdown must not wait for an obscured/unexposed window to render.
        // Drivers serialize appearance commands with their acknowledgements;
        // close/EOF also provide cancellation when a driver abandons a wait.
        m_pending.clear();
        m_commands.clear();
        m_deadline.stop();
        m_notifier->setEnabled(false);
        closed();
        m_window->close();
        QCoreApplication::quit();
    }

    void closed()
    {
        if (m_closed)
            return;
        m_closed = true;
        writeEvent({{QStringLiteral("event"), QStringLiteral("closed")}});
    }

    void fail(const QString &message)
    {
        if (m_failed || m_closed)
            return;
        m_failed = true;
        m_deadline.stop();
        if (m_notifier)
            m_notifier->setEnabled(false);
        writeEvent({{QStringLiteral("event"), QStringLiteral("error")},
                    {QStringLiteral("message"), message}});
        std::fprintf(stderr, "%s\n", qPrintable(message));
        // start() can fail before exec(); a queued exit also covers that path.
        QMetaObject::invokeMethod(qApp, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    }

    QPointer<QQuickWindow> m_window;
    PlaygroundState *const m_state;
    InterActive::Appearance *const m_appearance;
    const QString m_scene;
    const QString m_screenshot;
    QList<QQuickItem *> m_contents;
    QList<QPointer<QQuickWindow>> m_hosts;
    QList<QMetaObject::Connection> m_contentConnections;
    QHash<QQuickWindow *, quint64> m_frames;
    std::atomic<quint64> m_generation{0};
    QTimer m_deadline;
    QString m_pending;
    bool m_sceneReady = false;
    bool m_closed = false;
    bool m_failed = false;
    QSocketNotifier *m_notifier = nullptr;
    int m_stdinFlags = -1;
    QByteArray m_buffer;
    std::deque<QJsonObject> m_commands;
};

int main(int argc, char **argv)
{
    qInstallMessageHandler(logToStderr);
    PlaygroundApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("interactive-playground"));
    QGuiApplication::setDesktopFileName(QStringLiteral("interactive-playground"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("InterActive first mixed-control overview; other pages await visual review."));
    parser.addHelpOption();
    parser.addOptions({
        {QStringLiteral("page"), "Implemented page: foundations", "page", "foundations"},
        {QStringLiteral("appearance"), "Appearance: default, light, dark", "appearance", "default"},
        {QStringLiteral("density"), "Density: compact, touch (omitted means library default)", "density"},
        {QStringLiteral("contrast"), "Text contrast: normal, high (omitted means library default)", "contrast"},
        {QStringLiteral("text-scale"), "Positive text scale (library diagnostics report unsupported values)", "scale"},
        {QStringLiteral("rtl"), "Mirror the layout right-to-left"},
        {QStringLiteral("popup"), "Popup host: item, window", "host", "item"},
        {QStringLiteral("width"), "Initial viewport width in logical pixels", "width", "1200"},
        {QStringLiteral("height"), "Initial viewport height in logical pixels", "height", "820"},
        {QStringLiteral("scene"), "Scene: default, menu, dialog, partial-profile", "scene", "default"},
        {QStringLiteral("automation"), "Enable newline-delimited JSON commands and IA_EVENT records"},
        {QStringLiteral("screenshot"), "Save the ready main window as PNG, then exit (absolute path)", "path"},
        {QStringLiteral("palette-samples-file"), "Append calibration samples on Save to this absolute path", "path"},
        {QStringLiteral("appearance-profile-summary"), "Opt-in diagnostic session: write the summary to a fresh path", "path"},
        {QStringLiteral("appearance-trace"), "Also record buffered detailed spans to a fresh path", "path"},
        {QStringLiteral("appearance-profile-delay-ms"), "Arm before recording (default 5000)", "ms", "5000"},
        {QStringLiteral("appearance-profile-duration-ms"), "Recording interval (default 24000)", "ms", "24000"},
        {QStringLiteral("appearance-trace-capacity-mib"), "Recorder budget in MiB, 1-256 (default 128)", "mib", "128"}
    });
    const bool automationRequested = app.arguments().contains(QStringLiteral("--automation"));
    const auto startupError = [automationRequested](const QString &message) {
        if (automationRequested)
            writeEvent({{QStringLiteral("event"), QStringLiteral("error")}, {QStringLiteral("message"), message}});
        std::fprintf(stderr, "%s\n", qPrintable(message));
        return 2;
    };
    if (!parser.parse(app.arguments()))
        return startupError(parser.errorText());
    if (parser.isSet(QStringLiteral("help"))) {
        std::fprintf(automationRequested ? stderr : stdout, "%s", qPrintable(parser.helpText()));
        return 0;
    }
    if (!parser.positionalArguments().isEmpty())
        return startupError(QStringLiteral("Unexpected positional arguments."));
    if (parser.value(QStringLiteral("page")) != QLatin1String("foundations"))
        return startupError(QStringLiteral("Only --page foundations is available at the first visual-review checkpoint."));
    const QString scene = parser.value(QStringLiteral("scene"));
    if (!QStringList{"default", "menu", "dialog", "partial-profile"}.contains(scene))
        return startupError(QStringLiteral("Unknown --scene; use default, menu, dialog, or partial-profile."));
    const QString appearanceOption = parser.value(QStringLiteral("appearance"));
    if (!QStringList{"default", "light", "dark"}.contains(appearanceOption))
        return startupError(QStringLiteral("Unknown --appearance; use default, light, or dark."));
    const QString popup = parser.value(QStringLiteral("popup"));
    if (popup != QLatin1String("item") && popup != QLatin1String("window"))
        return startupError(QStringLiteral("Unknown --popup; use item or window."));
    bool widthOk = false, heightOk = false;
    const int width = parser.value(QStringLiteral("width")).toInt(&widthOk);
    const int height = parser.value(QStringLiteral("height")).toInt(&heightOk);
    if (!widthOk || !heightOk || width < 320 || height < 320)
        return startupError(QStringLiteral("--width and --height must be integers of at least 320 logical pixels."));

    const QString screenshot = parser.value(QStringLiteral("screenshot"));
    if (parser.isSet(QStringLiteral("screenshot")) && (screenshot.isEmpty() || !QDir::isAbsolutePath(screenshot)))
        return startupError(QStringLiteral("--screenshot requires an absolute nonempty path."));

    QString samplesFile;
    if (parser.isSet(QStringLiteral("palette-samples-file"))) {
        samplesFile = parser.value(QStringLiteral("palette-samples-file"));
        if (samplesFile.isEmpty() || !QDir::isAbsolutePath(samplesFile))
            return startupError(QStringLiteral("--palette-samples-file requires an absolute nonempty path."));
    } else {
        const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (!documents.isEmpty() && QDir::isAbsolutePath(documents))
            samplesFile = QDir(documents).filePath(QStringLiteral("InterActive/palette-samples.txt"));
    }

    AppearanceProfileOptions profileOptions;
    if (parser.isSet(QStringLiteral("appearance-profile-summary"))) {
        profileOptions.enabled = true;
        profileOptions.summaryPath = parser.value(QStringLiteral("appearance-profile-summary"));
    }
    if (parser.isSet(QStringLiteral("appearance-trace"))) {
        if (!profileOptions.enabled)
            return startupError(QStringLiteral("--appearance-trace requires --appearance-profile-summary."));
        profileOptions.tracePath = parser.value(QStringLiteral("appearance-trace"));
    }
    if (profileOptions.enabled) {
        bool ok = false;
        profileOptions.delayMs = parser.value(QStringLiteral("appearance-profile-delay-ms")).toInt(&ok);
        if (!ok || profileOptions.delayMs < 0)
            return startupError(QStringLiteral("--appearance-profile-delay-ms must be a non-negative integer."));
        profileOptions.durationMs = parser.value(QStringLiteral("appearance-profile-duration-ms")).toInt(&ok);
        if (!ok || profileOptions.durationMs < 1)
            return startupError(QStringLiteral("--appearance-profile-duration-ms must be a positive integer."));
        const auto mib = parser.value(QStringLiteral("appearance-trace-capacity-mib")).toUInt(&ok);
        if (!ok || mib < 1 || mib > 256)
            return startupError(QStringLiteral("--appearance-trace-capacity-mib must be an integer in [1, 256]."));
        profileOptions.capacityMiB = mib;
        if (QFileInfo::exists(profileOptions.summaryPath)
            || (!profileOptions.tracePath.isEmpty() && QFileInfo::exists(profileOptions.tracePath)))
            return startupError(QStringLiteral("Appearance profile output paths must not exist."));
    }

    QVariantMap input;
    if (scene == QLatin1String("partial-profile")) {
        input.insert(QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"),
            QVariantMap{{QStringLiteral("accent"), QStringLiteral("#8657b8")}}}});
    }
    if (appearanceOption != QLatin1String("default"))
        input.insert(QStringLiteral("scheme"), appearanceOption);
    if (parser.isSet(QStringLiteral("density"))) {
        const QString density = parser.value(QStringLiteral("density"));
        if (density != QLatin1String("compact") && density != QLatin1String("touch"))
            return startupError(QStringLiteral("Unknown --density; use compact or touch."));
        input.insert(QStringLiteral("density"), density);
    }
    if (parser.isSet(QStringLiteral("contrast"))) {
        const QString contrast = parser.value(QStringLiteral("contrast"));
        if (contrast != QLatin1String("normal") && contrast != QLatin1String("high"))
            return startupError(QStringLiteral("Unknown --contrast; use normal or high."));
        input.insert(QStringLiteral("contrast"), contrast);
    }
    if (parser.isSet(QStringLiteral("text-scale"))) {
        bool ok = false;
        const double scale = parser.value(QStringLiteral("text-scale")).toDouble(&ok);
        if (!ok || !std::isfinite(scale) || scale <= 0)
            return startupError(QStringLiteral("--text-scale must be a finite positive number."));
        input.insert(QStringLiteral("fontScale"), scale);
    }

    QGuiApplication::setLayoutDirection(parser.isSet(QStringLiteral("rtl")) ? Qt::RightToLeft : Qt::LeftToRight);

    // Declaration order keeps the render observer alive through engine/window
    // destruction, including the render threads' final signal deliveries.
    std::unique_ptr<Automation> automation;
    std::unique_ptr<StartupScene> startup;
    // The trace controller also outlives engine/window teardown: its render
    // callbacks may fire during window destruction and must stay disconnected
    // and quiesced before buffers are released.
    std::unique_ptr<AppearanceTraceController> traceController;
    QQmlApplicationEngine engine;
    auto *appearance = engine.singletonInstance<InterActive::Appearance *>("InterActive.Foundation", "Appearance");
    auto *state = engine.singletonInstance<PlaygroundState *>("InterActive.Playground", "PlaygroundState");
    if (!appearance || !state)
        return startupError(QStringLiteral("Cannot obtain engine-local appearance and playground state."));
    if (!state->setAppearance(input))
        return startupError(state->appearanceError());
    state->setPaletteSamplePath(samplesFile);
    if (scene == QLatin1String("default") && !state->applyInitialPaletteSuggestions())
        return startupError(state->appearanceError());
    engine.setInitialProperties({{QStringLiteral("width"), width}, {QStringLiteral("height"), height},
                                 {QStringLiteral("initialScene"), scene},
                                 {QStringLiteral("windowPopups"), popup == QLatin1String("window")},
                                 {QStringLiteral("rightToLeft"), parser.isSet(QStringLiteral("rtl"))}});
    engine.loadFromModule("InterActive.Playground", "Main");
    if (engine.rootObjects().size() != 1)
        return startupError(QStringLiteral("The playground scene did not load."));
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window)
        return startupError(QStringLiteral("The playground root must be a QQuickWindow."));

    const bool nativeInput = scene == QLatin1String("menu") && popup == QLatin1String("window")
        && QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
    startup = std::make_unique<StartupScene>(window, nativeInput, automationRequested);
    if (profileOptions.enabled) {
        traceController = std::make_unique<AppearanceTraceController>(profileOptions, &app, appearance, nullptr);
        traceController->arm();
    }
    if (automationRequested || !screenshot.isEmpty()) {
        automation = std::make_unique<Automation>(window, state, appearance, scene, screenshot, nullptr);
        QObject::connect(startup.get(), &StartupScene::inputRequired,
                         automation.get(), &Automation::requestNativeInput);
        automation->start();
    }
    return app.exec();
}

#include "main.moc"
