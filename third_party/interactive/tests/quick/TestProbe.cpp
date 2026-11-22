// SPDX-License-Identifier: LGPL-3.0-or-later
#include "TestProbe.h"

#include <QTest>
#include <QScreen>
#include <QSignalSpy>
#include <QQmlEngine>
#include <QGuiApplication>
#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QSaveFile>
#include <QtMath>
#include <QMetaProperty>
#include <QDeadlineTimer>
#include <QKeyEvent>
#include <QPointer>
#include <QProcess>
#include <QFile>
#include <QDebug>
#include <QAccessible>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "ControlAppearance.h"
#include "ColorPlaneSurface.h"
#ifdef IA_TEST_PLAYGROUND
#include "PlaygroundState.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#endif

namespace {
bool isInputHostReady(const QQuickWindow *window)
{
    return window && window->isExposed() && window->isActive()
        && QGuiApplication::focusWindow() == window;
}

class NativeKeyWitness final : public QObject
{
public:
    explicit NativeKeyWitness(QWindow *host) : window(host) { qApp->installEventFilter(this); }
    ~NativeKeyWitness() override { qApp->removeEventFilter(this); }

    QPointer<QWindow> window;
    QString failure;
    int phase = 0;
    bool spaceHeld = false;

protected:
    bool eventFilter(QObject *receiver, QEvent *event) override
    {
        // Qt forwards the same event into items too; witness the QWindow boundary once.
        if (!qobject_cast<QWindow *>(receiver)
            || (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease))
            return false;
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->nativeScanCode() == 0 && key->nativeVirtualKey() == 0)
            return false;
        const bool press = event->type() == QEvent::KeyPress;
        if (key->nativeScanCode() == 65 && key->nativeVirtualKey() == 0x20)
            spaceHeld = press;
        qInfo().nospace() << "IA_NATIVE_KEY witness=" << (press ? "press" : "release")
                          << " receiver=" << receiver << " intended=" << window.data()
                          << " key=" << key->key() << " scan=" << key->nativeScanCode()
                          << " keysym=" << key->nativeVirtualKey()
                          << " spontaneous=" << key->spontaneous()
                          << " repeat=" << key->isAutoRepeat() << " timestamp=" << key->timestamp();
        if (!failure.isEmpty())
            return false;
        if (!window || receiver != window.data() || !key->spontaneous() || key->isAutoRepeat()
            || key->key() != Qt::Key_Space || key->nativeScanCode() != 65
            || key->nativeVirtualKey() != 0x20 || (press ? phase != 0 : phase != 1)) {
            failure = QStringLiteral("unexpected native key, receiver or press/release order");
        } else {
            ++phase;
        }
        return false; // Observe real input; never consume or synthesize its replacement.
    }
};
}

QRect TestProbe::screenAvailableGeometry(QQuickItem *item) const
{
    const auto *window = hostWindow(item);
    return window && window->screen() ? window->screen()->availableGeometry() : QRect();
}

QQuickWindow *TestProbe::hostWindow(QQuickItem *item) const
{
    auto *window = item ? item->window() : nullptr;
    // Invokable QObject returns otherwise acquire JavaScript ownership. These
    // windows belong to QuickTest/the popup, not to the script receiving them.
    if (window)
        QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
    return window;
}

bool TestProbe::exposed(QQuickItem *item) const
{
    const auto *window = hostWindow(item);
    return window && window->isExposed();
}

bool TestProbe::render(QQuickItem *item, int timeout) const
{
    if (!exposed(item))
        return false;
    auto *window = item->window();
    QSignalSpy rendered(window, &QQuickWindow::frameSwapped);
    // A static, already-exposed popup need not have another frame pending.
    // Subscribe before requesting one so an earlier frame cannot satisfy this.
    window->update();
    return rendered.wait(timeout);
}

bool TestProbe::activate(QQuickItem *item) const
{
    if (!exposed(item))
        return false;
    item->window()->requestActivate();
    return true;
}

bool TestProbe::activateForInput(QQuickItem *item) const
{
    if (!exposed(item))
        return false;
    const QPointer<QQuickWindow> window = item->window();
    if (!isInputHostReady(window))
        window->requestActivate();
    // Unlike a pointer-only request, keyboard input needs the exact global owner.
    // Keep item focus selection with the subsequent real click/key operation.
    return QTest::qWaitFor([window] { return isInputHostReady(window); }, 5000);
}

bool TestProbe::inputHostReady(QQuickItem *item) const
{
    return isInputHostReady(item ? item->window() : nullptr);
}

bool TestProbe::click(QQuickItem *item, qreal x, qreal y) const
{
    if (!exposed(item))
        return false;
    QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(x, y)).toPoint());
    return true;
}

bool TestProbe::press(QQuickItem *item, qreal x, qreal y) const
{
    if (!exposed(item))
        return false;
    QTest::mousePress(item->window(), Qt::LeftButton, Qt::NoModifier,
                     item->mapToScene(QPointF(x, y)).toPoint());
    return true;
}

bool TestProbe::move(QQuickItem *item, qreal x, qreal y) const
{
    if (!exposed(item))
        return false;
    QTest::mouseMove(item->window(), item->mapToScene(QPointF(x, y)).toPoint());
    return true;
}

bool TestProbe::release(QQuickItem *item, qreal x, qreal y) const
{
    if (!exposed(item))
        return false;
    QTest::mouseRelease(item->window(), Qt::LeftButton, Qt::NoModifier,
                       item->mapToScene(QPointF(x, y)).toPoint());
    return true;
}

bool TestProbe::keyClick(QQuickItem *hostItem, int key, int modifiers) const
{
    if (!exposed(hostItem))
        return false;
    QTest::keyClick(hostItem->window(), static_cast<Qt::Key>(key), Qt::KeyboardModifiers(modifiers));
    return true;
}

bool TestProbe::isWayland() const
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

bool TestProbe::nativeKeyHostReady(QQuickItem *hostItem) const
{
    return isWayland() && inputHostReady(hostItem);
}

bool TestProbe::nativeKeyClick(QQuickItem *hostItem, int key) const
{
    const auto fail = [](const QString &reason) {
        qWarning().noquote() << "IA_NATIVE_KEY FAIL:" << reason;
        return false;
    };
    if (key != Qt::Key_Space)
        return fail(QStringLiteral("only Space is supported; no key dispatched"));
    if (!nativeKeyHostReady(hostItem))
        return fail(QStringLiteral("intended Wayland host is not exposed, active and focused; no key dispatched"));
    const QString socket = qEnvironmentVariable("IA_TEST_YDOTOOL_SOCKET");
    if (socket.isEmpty())
        return fail(QStringLiteral("missing IA_TEST_YDOTOOL_SOCKET; no key dispatched"));
#ifdef Q_OS_UNIX
    struct stat socketStat {}, directoryStat {};
    const QByteArray socketPath = QFile::encodeName(socket);
    const QByteArray directoryPath = QFile::encodeName(QFileInfo(socket).absolutePath());
    if (!QDir::isAbsolutePath(socket) || ::lstat(socketPath.constData(), &socketStat) != 0
        || !S_ISSOCK(socketStat.st_mode) || socketStat.st_uid != ::geteuid()
        || (socketStat.st_mode & 0777) != 0600
        || ::lstat(directoryPath.constData(), &directoryStat) != 0
        || !S_ISDIR(directoryStat.st_mode) || directoryStat.st_uid != ::geteuid()
        || (directoryStat.st_mode & 0777) != 0700)
        return fail(QStringLiteral("invalid private ydotool socket/owner/permissions; no key dispatched"));
#else
    return fail(QStringLiteral("native keyboard backend requires Unix; no key dispatched"));
#endif
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("YDOTOOL_SOCKET"), socket);
    environment.insert(QStringLiteral("TMPDIR"), qEnvironmentVariable("TMPDIR"));
    environment.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
    NativeKeyWitness witness(hostItem->window());
    QProcess process;
    process.setProcessEnvironment(environment);
    bool finished = false;
    QObject::connect(&process, &QProcess::finished, &process,
                     [&finished](int, QProcess::ExitStatus) { finished = true; });
    QObject::connect(&process, &QProcess::errorOccurred, &process,
                     [&witness, &process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && witness.failure.isEmpty())
            witness.failure = QStringLiteral("ydotool failed to start: ") + process.errorString();
    });
    const QDeadlineTimer deadline(5000, Qt::PreciseTimer);
    qInfo() << "IA_NATIVE_KEY dispatch=Space evdev=57 deadline_ms=5000";
    process.start(QStringLiteral("/usr/bin/ydotool"),
                  {QStringLiteral("key"), QStringLiteral("57:1"), QStringLiteral("57:0")});
    const bool observed = QTest::qWaitFor([&] {
        return !witness.failure.isEmpty()
            || (finished && (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0
                             || witness.phase == 2));
    }, deadline);
    if (!observed && witness.failure.isEmpty())
        witness.failure = QStringLiteral("native press/release or process completion missed the 5s deadline");
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(500); // Bounded failure cleanup, never an action retry.
    }
    if (finished && (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        && witness.failure.isEmpty())
        witness.failure = QStringLiteral("ydotool failed: ") + QString::fromLocal8Bit(process.readAllStandardError())
            + QString::fromLocal8Bit(process.readAllStandardOutput());
    if (witness.spaceHeld) {
        // Release only our observed held Space after failure; never replay a press.
        qWarning() << "IA_NATIVE_KEY cleanup=release-observed-held-Space (not a retry)";
        QProcess release;
        release.setProcessEnvironment(environment);
        release.start(QStringLiteral("/usr/bin/ydotool"), {QStringLiteral("key"), QStringLiteral("57:0")});
        const bool released = QTest::qWaitFor([&] { return release.state() == QProcess::NotRunning; }, 500);
        if (!released) {
            release.kill();
            release.waitForFinished(500);
        }
        if (!released || release.exitStatus() != QProcess::NormalExit || release.exitCode() != 0 || witness.spaceHeld)
            qWarning() << "IA_NATIVE_KEY cleanup release not confirmed; stop the owned keyboard backend";
    }
    if (!witness.failure.isEmpty())
        return fail(witness.failure);
    qInfo() << "IA_NATIVE_KEY OK press=1 release=1 process_exit=0";
    return finished && witness.phase == 2;
}

bool TestProbe::typeAscii(QQuickItem *hostItem, const QString &text) const
{
    if (!exposed(hostItem))
        return false;
    // This fixture uses printable ASCII. Reject unsupported input rather than
    // substituting characters or claiming to exercise IME/Unicode composition.
    for (const QChar character : text) {
        if (character.unicode() < 0x20 || character.unicode() > 0x7e)
            return false;
    }
    for (const QChar character : text)
        QTest::keyClick(hostItem->window(), character.toLatin1());
    return true;
}

QStringList TestProbe::appearanceProperties(QObject *control) const
{
    QStringList result;
    if (!control)
        return result;
    // Inspect the consumer's actual meta-object, not a second source parser.
    // Foundation defines the vocabulary; the test specifies each rendered set.
    const auto &facade = InterActive::ControlAppearance::staticMetaObject;
    const auto *meta = control->metaObject();
    for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
        const auto property = meta->property(i);
        if ((facade.indexOfProperty(property.name()) >= 0
             && QByteArray(property.name()) != "tone" && QByteArray(property.name()) != "validation")
            || QByteArray(property.name()) == "appearanceDiagnostics")
            result.append(QString::fromLatin1(property.name()));
    }
    return result;
}

QVariantMap TestProbe::accessibleState(QQuickItem *item) const
{
    auto *interface = QAccessible::queryAccessibleInterface(item);
    if (!interface || !interface->isValid())
        return {};
    const auto state = interface->state();
    auto *actions = interface->actionInterface();
    auto *value = interface->valueInterface();
    return {{QStringLiteral("role"), int(interface->role())},
            {QStringLiteral("name"), interface->text(QAccessible::Name)},
            {QStringLiteral("description"), interface->text(QAccessible::Description)},
            {QStringLiteral("focusable"), bool(state.focusable)},
            {QStringLiteral("editable"), bool(state.editable)},
            {QStringLiteral("readOnly"), bool(state.readOnly)},
            {QStringLiteral("textInterface"), interface->textInterface() != nullptr},
            {QStringLiteral("minimum"), value ? value->minimumValue() : QVariant{}},
            {QStringLiteral("maximum"), value ? value->maximumValue() : QVariant{}},
            {QStringLiteral("value"), value ? value->currentValue() : QVariant{}},
            {QStringLiteral("checked"), bool(state.checked)},
            {QStringLiteral("checkable"), bool(state.checkable)},
            {QStringLiteral("focused"), bool(state.focused)},
            {QStringLiteral("disabled"), bool(state.disabled)},
            {QStringLiteral("pressAction"), QAccessibleActionInterface::pressAction()},
            {QStringLiteral("increaseAction"), QAccessibleActionInterface::increaseAction()},
            {QStringLiteral("toggleAction"), QAccessibleActionInterface::toggleAction()},
            {QStringLiteral("actions"), actions ? actions->actionNames() : QStringList{}}};
}

bool TestProbe::accessibleAction(QQuickItem *item, const QString &action) const
{
    auto *interface = QAccessible::queryAccessibleInterface(item);
    auto *actions = interface && interface->isValid() ? interface->actionInterface() : nullptr;
    if (!actions || !actions->actionNames().contains(action))
        return false;
    actions->doAction(action);
    return true; // Dispatch only; the caller must assert the actual control outcome.
}

QString TestProbe::captureDirectory() const
{
    const QString path = qEnvironmentVariable("IA_CAPTURE_DIR");
    const QFileInfo directory(path);
    if (path.isEmpty() || !QDir::isAbsolutePath(path) || !directory.isDir() || !directory.isWritable()) {
        qWarning("IA_CAPTURE_DIR must name an absolute, existing, writable directory");
        return {};
    }
    return QDir::cleanPath(path);
}

bool TestProbe::createCaptureMode(const QString &mode) const
{
    const QString root = captureDirectory();
    if (root.isEmpty() || (mode != QLatin1String("light-normal") && mode != QLatin1String("light-high")
                          && mode != QLatin1String("dark-normal") && mode != QLatin1String("dark-high")))
        return false;
    return QDir(root).mkpath(mode);
}

QString TestProbe::captureDataUrl(QQuickItem *item, qreal margin) const
{
    if (!exposed(item) || margin < 0 || item->width() <= 0 || item->height() <= 0)
        return {};
    auto *window = item->window();
    const QImage frame = window->grabWindow();
    if (frame.isNull() || window->width() <= 0 || window->height() <= 0)
        return {};
    // Window capture also sees item-hosted popups (which live in the overlay,
    // outside the fixture's item subtree), and preserves real viewport clipping.
    // Capture the allocated control rectangle, matching snapshot()'s logical
    // image size. Virtual boundingRect() can describe only text/ink (TextInput),
    // which would crop the field and then stretch that crop in the shared sheet.
    const QRectF bounds = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()))
                              .adjusted(-margin, -margin, margin, margin);
    const qreal sx = qreal(frame.width()) / window->width();
    const qreal sy = qreal(frame.height()) / window->height();
    const QRect pixels(QPoint(qFloor(bounds.left() * sx), qFloor(bounds.top() * sy)),
                       QPoint(qCeil(bounds.right() * sx) - 1, qCeil(bounds.bottom() * sy) - 1));
    if (!frame.rect().contains(pixels))
        return {}; // Never silently trim an outside-painted focus ring.
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !frame.copy(pixels).save(&buffer, "PNG"))
        return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

bool TestProbe::writeCaptureManifest(const QString &text) const
{
    const QString root = captureDirectory();
    if (root.isEmpty())
        return false;
    QSaveFile file(QDir(root).filePath(QStringLiteral("manifest.txt")));
    const QByteArray bytes = text.toUtf8();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

quint64 TestProbe::colorPlaneRebuilds(QQuickItem *surface) const
{
    const auto *plane = qobject_cast<InterActive::ColorPlaneSurface *>(surface);
    return plane ? plane->rebuildCount() : 0;
}

QVariantMap TestProbe::colorPlaneCache(QQuickItem *surface) const
{
    const auto *plane = qobject_cast<InterActive::ColorPlaneSurface *>(surface);
    if (!plane) return {};
    return {{"rebuilds", plane->rebuildCount()}, {"lastRebuildNs", plane->lastRebuildNanoseconds()},
            {"pixelWidth", plane->cachedPixelSize().width()}, {"pixelHeight", plane->cachedPixelSize().height()},
            {"dpr", plane->cachedDpr()}};
}

QColor TestProbe::paintedRasterPixel(QQuickItem *item, int x, int y, qreal dpr) const
{
    auto *painted = qobject_cast<QQuickPaintedItem *>(item);
    if (!painted || !qIsFinite(dpr) || dpr <= 0) return {};
    QImage image(QSize(qCeil(item->width() * dpr), qCeil(item->height() * dpr)), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull() || !image.rect().contains(x, y)) return {};
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painted->paint(&painter);
    painter.end();
    return image.pixelColor(x, y);
}

qreal TestProbe::effectiveDpr(QQuickItem *item) const
{
    return item && item->window() ? item->window()->effectiveDevicePixelRatio() : 1;
}

bool TestProbe::touch(QQuickItem *item, const QString &phase, qreal x, qreal y)
{
    if (!exposed(item)) return false;
    if (!m_touchDevice) m_touchDevice = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    const QPoint position = item->mapToScene(QPointF(x,y)).toPoint();
    if (phase == "press") QTest::touchEvent(item->window(), m_touchDevice).press(0, position, item->window()).commit();
    else if (phase == "move") QTest::touchEvent(item->window(), m_touchDevice).move(0, position, item->window()).commit();
    else if (phase == "release") QTest::touchEvent(item->window(), m_touchDevice).release(0, position, item->window()).commit();
    else if (phase == "cancel") {
        const QEventPoint point(0, QEventPoint::Released, position, item->window()->mapToGlobal(position));
        QTouchEvent event(QEvent::TouchCancel, m_touchDevice, Qt::NoModifier, QList<QEventPoint>{point});
        QCoreApplication::sendEvent(item->window(), &event);
    } else return false;
    return true;
}

bool TestProbe::captureWindow(QQuickItem *item, const QString &name) const
{
    const QString root = captureDirectory();
    if (root.isEmpty() || !exposed(item) || name.contains('/') || !name.endsWith(".png"))
        return false;
    return item->window()->grabWindow().save(QDir(root).filePath(name));
}

QColor TestProbe::renderedPixel(QQuickItem *item, qreal x, qreal y) const
{
    if (!exposed(item)) return {};
    const QImage frame=item->window()->grabWindow();
    const QPointF scene=item->mapToScene(QPointF(x,y));
    const QPoint pixel(qFloor(scene.x()*frame.width()/item->window()->width()),
                       qFloor(scene.y()*frame.height()/item->window()->height()));
    return frame.rect().contains(pixel) ? frame.pixelColor(pixel) : QColor{};
}

int TestProbe::idleFrames(QQuickItem *item, int milliseconds) const
{
    if (!exposed(item)) return -1;
    QSignalSpy frames(item->window(), &QQuickWindow::frameSwapped);
    QTest::qWait(milliseconds);
    return frames.count();
}

#ifdef IA_TEST_PLAYGROUND
namespace {
PlaygroundState *probeState(const QObject *probe)
{
    auto *engine = qmlEngine(probe);
    return engine ? engine->singletonInstance<PlaygroundState *>("InterActive.Playground", "PlaygroundState") : nullptr;
}
}

bool TestProbe::freshPaletteSamples()
{
    auto *state = probeState(this);
    if (!state || !m_samples.isValid())
        return false;
    m_sampleLock.reset();
    state->setPaletteSamplePath(m_samples.filePath(QStringLiteral("samples-%1.txt").arg(++m_sampleSequence)));
    return !QFileInfo::exists(state->paletteSamplePath());
}

bool TestProbe::paletteSampleExists() const
{
    const auto *state = probeState(this);
    return state && QFileInfo::exists(state->paletteSamplePath());
}

QString TestProbe::paletteSampleBytes() const
{
    const auto *state = probeState(this);
    if (!state)
        return {};
    QFile file(state->paletteSamplePath());
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QVariantList TestProbe::paletteSamples() const
{
    QVariantList result;
    const auto bytes = paletteSampleBytes().toUtf8();
    for (const auto &line : bytes.split('\n')) {
        if (!line.isEmpty()) {
            const auto document = QJsonDocument::fromJson(line);
            if (!document.isObject())
                return {QStringLiteral("INVALID SAMPLE LINE")};
            result.append(document.object().toVariantMap());
        }
    }
    return result;
}

bool TestProbe::paletteSampleLock(bool locked)
{
    if (!locked) {
        m_sampleLock.reset();
        return true;
    }
    const auto *state = probeState(this);
    if (!state)
        return false;
    m_sampleLock = std::make_unique<QLockFile>(state->paletteSamplePath() + QStringLiteral(".lock"));
    m_sampleLock->setStaleLockTime(0);
    return m_sampleLock->tryLock(0);
}

bool TestProbe::writePaletteHistory(const QVariantList &accents, bool malformed)
{
    const auto *state = probeState(this);
    // Test fixtures cannot write a production/CLI destination.
    const QString path = m_samples.filePath(QStringLiteral("samples-%1.txt").arg(m_sampleSequence));
    if (!state || !m_samples.isValid() || m_sampleSequence <= 0 || state->paletteSamplePath() != path)
        return false;
    QByteArray bytes;
    if (malformed)
        bytes += "{invalid history}\n";
    for (const auto &value : accents) {
        const QColor accent(value.toString());
        if (!accent.isValid())
            return false;
        const auto colorObject = [](const QColor &color) {
            const auto rgba = color.rgba64();
            return QJsonObject{{"hexArgb", color.name(QColor::HexArgb)},
                {"rgba16", QJsonArray{rgba.red(), rgba.green(), rgba.blue(), rgba.alpha()}}};
        };
        const QJsonObject record{{"version", 1}, {"utc", "2026-10-04T20:00:00.000Z"},
            {"scheme", "dark"}, {"contrast", "high"}, {"accent", colorObject(accent)},
            {"amber", colorObject(QColor("#aa5500"))}, {"danger", colorObject(QColor("#440011"))},
            {"corrections", QJsonObject{{"amber", false}, {"danger", false}}}};
        bytes += QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const bool written = file.write(bytes) == bytes.size() && file.flush();
    file.close();
    return written && file.error() == QFileDevice::NoError;
}

bool TestProbe::paletteHistoryReadable(bool readable)
{
    const auto *state = probeState(this);
    const QString path = m_samples.filePath(QStringLiteral("samples-%1.txt").arg(m_sampleSequence));
    if (!state || !m_samples.isValid() || m_sampleSequence <= 0 || state->paletteSamplePath() != path)
        return false;
    return QFile::setPermissions(path, readable ? QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                              : QFileDevice::Permissions{});
}
#endif
