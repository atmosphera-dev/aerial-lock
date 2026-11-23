#include "fixture.h"
#include "interactive-runtime.h"
#include <Appearance.h>
#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QSaveFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QImage>
#include <QTouchEvent>
#include <QApplication>
using namespace Aerial::Settings;
class QuickHarness : public SettingsController {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *referenceSources READ referenceSources CONSTANT)
public:
    Aerial::Settings::Test::Fixture &fixture;
    InterActive::Appearance &appearanceState;
    QByteArray validConfig;
    std::unique_ptr<QPointingDevice> unrelatedTouch{QTest::createTouchDevice()};
    QPointer<QQuickWindow> touchWindow;
    QPointer<const QPointingDevice> touchDevice;
    SourceRows referenceRows{this};
    QAbstractItemModel *referenceSources() { return &referenceRows; }
    QuickHarness(Aerial::Settings::Test::Fixture &fixture, InterActive::Appearance &appearanceState)
        : SettingsController(*fixture.service), fixture(fixture), appearanceState(appearanceState)
    {
        if (!appearance(false)) qFatal("could not initialize the test appearance");
        QVariantList rows;
        const auto row = [](const char *id, const char *name, const char *description, const char *glyph, bool enabled, bool problem) {
            return QVariantMap{{"id", id}, {"name", name}, {"description", description}, {"glyph", glyph}, {"enabled", enabled}, {"problem", problem}, {"canRetry", problem}, {"pathDetail", qstrcmp(id, "folder") == 0}};
        };
        rows << row("apple", "Apple TV", "Apple aerials, 9 of 112 downloaded", "tv", true, false)
            << row("community", "Community 2026", "Community pack, 5 of 40 downloaded", "world", true, false)
            << row("folder", "My videos", "Folder, /home/alex/Videos/Aerial footage/Iceland 2025", "folder", true, false)
            << row("drone", "Drone flights", "Couldn't reach example.org. Try again from the menu.", "world", false, true);
        for (int i = 0; i < rows.size(); ++i) { auto entry = rows[i].toMap(); entry["canMoveUp"] = i > 0; entry["canMoveDown"] = i + 1 < rows.size(); rows[i] = entry; }
        referenceRows.synchronize(rows);
    }
    Q_INVOKABLE QObject *iconIn(QObject *object)
    {
        const auto *item = qobject_cast<QQuickItem *>(object);
        if (!item || !item->isVisible()) return nullptr;
        if (item->metaObject()->indexOfProperty("source") >= 0 && item->metaObject()->indexOfProperty("available") >= 0
            && item->metaObject()->indexOfProperty("mirrored") >= 0) return object;
        // Follow the painted tree, not ownership (which also reaches hidden popup visuals).
        for (auto *child : item->childItems()) if (auto *icon = iconIn(child)) return icon;
        return nullptr;
    }
    Q_INVOKABLE bool nativeRendering(QObject *object)
    {
        auto *item = qobject_cast<QQuickItem *>(object);
        return item && item->window() && QGuiApplication::platformName().startsWith("wayland")
            && item->window()->rendererInterface()->graphicsApi() != QSGRendererInterface::Software;
    }
    Q_INVOKABLE int inkPixels(QObject *object)
    {
        auto *item = qobject_cast<QQuickItem *>(object);
        if (!item || !item->window()) return 0;
        const auto frame = item->window()->grabWindow();
        const auto origin = item->mapToScene({0, 0});
        const double scale = double(frame.width()) / item->window()->width();
        const auto area = frame.copy(QRect(qRound(origin.x() * scale), qRound(origin.y() * scale),
            qRound(item->width() * scale), qRound(item->height() * scale)));
        if (area.isNull()) return 0;
        const auto background = area.pixelColor(0, 0);
        int ink = 0;
        for (int y = 0; y < area.height(); ++y) for (int x = 0; x < area.width(); ++x) {
            const auto color = area.pixelColor(x, y);
            if (qMax(qAbs(color.red() - background.red()), qMax(qAbs(color.green() - background.green()), qAbs(color.blue() - background.blue()))) > 32) ++ink;
        }
        return ink;
    }
    Q_INVOKABLE QVariantMap statusProjection(const QVariantMap &properties, const QVariantList &jobs)
    {
        auto snapshot = fixture.service->snapshot();
        snapshot.properties = properties; snapshot.jobs = jobs;
        return StatusModel::project(snapshot);
    }
    Q_INVOKABLE bool resizeSurface(QObject *object, int width, int height)
    {
        auto *item = qobject_cast<QQuickItem *>(object);
        if (!item || !item->window()) return false;
        auto *window = item->window();
        if (QGuiApplication::platformName().startsWith("wayland") && !qEnvironmentVariableIsEmpty("NIRI_SOCKET")) {
            QProcess inventory; inventory.start("niri", {"msg", "-j", "windows"});
            if (!inventory.waitForFinished(5000) || inventory.exitCode()) return false;
            QStringList matches;
            for (const auto &entry : QJsonDocument::fromJson(inventory.readAllStandardOutput()).array()) {
                const auto candidate = entry.toObject();
                if (candidate.value("pid").toInteger() == QCoreApplication::applicationPid()) matches << QString::number(candidate.value("id").toInteger());
            }
            if (matches.size() != 1) return false;
            for (const auto *action : {"move-window-to-floating", "focus-window"}) {
                QProcess command; command.start("niri", {"msg", "action", action, "--id", matches.first()});
                if (!command.waitForFinished(5000) || command.exitCode()) return false;
            }
            window->setMaximumSize({16777215, 16777215});
            window->setMinimumSize({width, height}); window->setMaximumSize({width, height});
        }
        window->resize(width, height);
        return QTest::qWaitFor([&] { return window->width() == width && window->height() == height; }, 5000)
            && QQuickTest::qWaitForPolish(window, 5000);
    }
    Q_INVOKABLE void releaseSurface(QObject *object)
    {
        auto *item = qobject_cast<QQuickItem *>(object);
        if (item && item->window() && QGuiApplication::platformName().startsWith("wayland") && !qEnvironmentVariableIsEmpty("NIRI_SOCKET")) {
            item->window()->setMinimumSize({0, 0}); item->window()->setMaximumSize({16777215, 16777215});
        }
    }
    Q_INVOKABLE bool appearance(bool dark)
    {
        const QString scheme = dark ? "dark" : "light";
        appearanceState.setInput({{"schemaVersion", 1}, {"scheme", scheme}, {"density", "compact"},
            {"tokens", QVariantMap{{"colors", QVariantMap{{"accent", QColor("#5795ec")}}},
                {"typography", QVariantMap{{"family", QApplication::font().family()}}}}}});
        return appearanceState.scheme() == scheme;
    }
    Q_INVOKABLE bool capture(QObject *object, const QString &name)
    {
        const auto directory = qEnvironmentVariable("AERIAL_REVAMP_CAPTURES");
        if (directory.isEmpty()) return true;
        // A software scene graph silently omits ShaderEffect artwork. Such
        // frames may exercise layouts, but cannot become visual evidence.
        if (!nativeRendering(object)) return false;
        auto *item = qobject_cast<QQuickItem *>(object);
        if (!item || !item->window() || !QDir().mkpath(directory)) return false;
        QJsonArray geometry;
        std::function<void(QQuickItem *)> visit = [&](QQuickItem *child) {
            if (!child->isVisible()) return;
            if (!child->objectName().isEmpty()) {
                const auto point = child->mapToScene({0, 0});
                geometry << QJsonObject{{"name", child->objectName()}, {"x", point.x()}, {"y", point.y()},
                    {"width", child->width()}, {"height", child->height()}, {"implicitWidth", child->implicitWidth()}};
            }
            for (auto *descendant : child->childItems()) visit(descendant);
        };
        // Item popups are reparented to the window overlay. Inventory the same
        // full frame we grab, including their actual header/body/footer items.
        visit(item->window()->contentItem());
        QFile report(directory + '/' + name + ".json");
        if (!report.open(QIODevice::WriteOnly) || report.write(QJsonDocument(geometry).toJson()) < 0) return false;
        return item->window()->grabWindow().save(directory + '/' + name + ".png");
    }
    Q_INVOKABLE QString edgeViolation(QObject *object, int sidebarWidth, int edge)
    {
        auto *item = qobject_cast<QQuickItem *>(object);
        if (!item || !item->window()) return "no exposed item";
        const auto image = item->window()->grabWindow();
        if (image.isNull()) return "no frame";
        return checkEdgePixels(image, {item->mapToScene({0, 0}), item->size()}, item->window()->size(), sidebarWidth, edge);
    }
    Q_INVOKABLE QString edgePixelFixture(qreal scale)
    {
        const QSize size(160, 100);
        QImage image(size * scale, QImage::Format_RGB32);
        image.fill(Qt::white);
        // At 2x the offending pixel is between the old even-coordinate samples.
        image.setPixelColor(21, 20, Qt::black);
        return checkEdgePixels(image, {{0, 0}, size}, size, 60, 24);
    }
    static QString checkEdgePixels(const QImage &image, const QRectF &surface, const QSize &logicalWindow, int sidebarWidth, int edge)
    {
        if (logicalWindow.isEmpty() || surface.isEmpty()) return "empty test surface";
        const auto scaleX = qreal(image.width()) / logicalWindow.width(), scaleY = qreal(image.height()) / logicalWindow.height();
        const QRect physical = QRectF(surface.x() * scaleX, surface.y() * scaleY,
            surface.width() * scaleX, surface.height() * scaleY).toAlignedRect();
        if (!image.rect().contains(physical))
            return "surface has not reached the requested size";
        const auto distance = [](const QColor &a, const QColor &b) { return qMax(qAbs(a.red() - b.red()), qMax(qAbs(a.green() - b.green()), qAbs(a.blue() - b.blue()))); };
        const auto canvas = image.pixelColor(physical.topLeft()), content = image.pixelColor(physical.topRight());
        for (int py = physical.top(); py <= physical.bottom(); ++py) for (int px = physical.left(); px <= physical.right(); ++px) {
            const auto x = (px + .5) / scaleX - surface.x(), y = (py + .5) / scaleY - surface.y();
            if (x < 0 || y < 0 || x >= surface.width() || y >= surface.height()) continue;
            const bool verticalEdge = y < edge || y >= surface.height() - edge;
            const bool sidebar = x < sidebarWidth;
            const bool sidebarEdge = sidebar && (verticalEdge || x < edge || x >= sidebarWidth - edge);
            const bool pageEdge = x >= sidebarWidth + 1 && (verticalEdge || x < sidebarWidth + 1 + edge
                || x >= surface.width() - edge);
            const bool barLane = !verticalEdge && x >= surface.width() - 16 && x < surface.width() - 8;
            if (!(sidebarEdge || (pageEdge && !barLane))) continue;
            const auto color = image.pixelColor(px, py), background = sidebar ? canvas : content;
            const auto delta = distance(color, background);
            if (delta <= 3) continue;
            // Same one-pixel fringe classifier as the approved mock's measure.py:
            // low-coverage antialias pixels beside a stronger horizontal pixel.
            const int chroma = qMax(color.red(), qMax(color.green(), color.blue())) - qMin(color.red(), qMin(color.green(), color.blue()));
            const bool fringe = delta < 96 && (chroma > 12 || delta < 16)
                && ((px > physical.left() && distance(image.pixelColor(px - 1, py), background) > delta)
                    || (px < physical.right() && distance(image.pixelColor(px + 1, py), background) > delta));
            if (!fringe) return QString("unexpected edge pixel at %1,%2: %3 (background %4)").arg(px).arg(py).arg(color.name(), background.name());
        }
        return {};
    }
    Q_INVOKABLE QVariantList pixels(QObject *object, const QVariantList &points)
    {
        auto *item = qobject_cast<QQuickItem *>(object);
        if (!item || !item->window()) qFatal("pixel probe requires an exposed item");
        auto *window = item->window();
        const auto image = window->grabWindow();
        if (image.isNull()) qFatal("pixel probe could not grab the window");
        QVariantList result;
        for (const auto &entry : points) {
            const auto point = entry.toMap();
            const auto scene = item->mapToScene({point.value("x").toDouble(), point.value("y").toDouble()});
            const QPoint pixel(qRound(scene.x() * image.width() / window->width()), qRound(scene.y() * image.height() / window->height()));
            if (!image.rect().contains(pixel)) qFatal("pixel probe point is outside the window image");
            result << image.pixelColor(pixel);
        }
        return result;
    }
    Q_INVOKABLE void observeTouch(QObject *object)
    {
        auto *item = qobject_cast<QQuickItem *>(object);
        if (!item || !item->window()) qFatal("touch observation requires an exposed item");
        if (touchWindow) touchWindow->removeEventFilter(this);
        touchWindow = item->window(); touchDevice.clear(); touchWindow->installEventFilter(this);
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == touchWindow && event->type() == QEvent::TouchBegin)
            touchDevice = static_cast<QTouchEvent *>(event)->pointingDevice();
        return QObject::eventFilter(watched, event);
    }
    Q_INVOKABLE void cancelTouch(QObject *object)
    {
        auto *item = qobject_cast<QQuickItem *>(object);
        if (!item || item->window() != touchWindow || !touchDevice) qFatal("touch cancel requires its observed sequence device");
        QTouchEvent event(QEvent::TouchCancel, touchDevice);
        QCoreApplication::sendEvent(touchWindow, &event);
    }
    Q_INVOKABLE QVariant saved(const QString &key)
    {
        QJsonValue value(QJsonDocument::fromJson(fixture.cli({"config", "show", "--json"})).object());
        for (const auto &part : key.split('.')) value = value.toObject().value(part);
        return value.toVariant();
    }
    Q_INVOKABLE void externalEdit(const QString &key, const QString &json) { fixture.cli({"config", "set", key, json}); }
    Q_INVOKABLE void configureTray(bool available, bool on, bool persistent = false) { setTrayState({{"available", available}, {"on", on}, {"persistent", persistent}, {"message", "Desktop tray fixture"}}); }
    Q_INVOKABLE QString replacePreview(const QString &color)
    {
        const auto directory = QFileInfo(fixture.paths.config).absolutePath() + "/themes";
        if (!QDir().mkpath(directory + "/previews")) qFatal("preview directory failed");
        const auto theme = directory + "/Macintosh.qml", preview = directory + "/previews/Macintosh.png";
        if (color.isEmpty()) { QFile::remove(theme); QFile::remove(preview); return {}; }
        QFile file(theme); if (!file.open(QIODevice::WriteOnly)) qFatal("theme fixture failed");
        file.write("never execute this theme fixture"); file.close();
        QImage image(16, 10, QImage::Format_RGB32); image.fill(QColor(color));
        if (!image.save(preview)) qFatal("preview fixture failed");
        QFile png(preview); if (!png.open(QIODevice::ReadOnly)) qFatal("preview fixture readback failed");
        return "data:image/png;base64," + QString::fromLatin1(png.readAll().toBase64());
    }
    Q_INVOKABLE void stopService() { fixture.daemon.terminate(); if (!fixture.daemon.waitForFinished(5000)) qFatal("fixture did not stop"); }
    Q_INVOKABLE void startService() { fixture.daemon.start(QString(AERIAL_SETTINGS_TEST_SOURCE) + "/companion/aerial", {}); if (!fixture.daemon.waitForStarted()) qFatal("fixture did not start"); }
    Q_INVOKABLE void breakConfig()
    {
        QFile file(fixture.paths.config); if (!file.open(QIODevice::ReadOnly)) qFatal("fixture config read failed"); validConfig = file.readAll(); file.close();
        QSaveFile broken(fixture.paths.config); if (!broken.open(QIODevice::WriteOnly)) qFatal("fixture config open failed"); broken.write("{\n\"broken\":\n}"); if (!broken.commit()) qFatal("fixture publish failed");
    }
    Q_INVOKABLE bool brokenBytesUnchanged()
    {
        QFile file(fixture.paths.config); return file.open(QIODevice::ReadOnly) && file.readAll() == "{\n\"broken\":\n}";
    }
    Q_INVOKABLE void repairConfig()
    {
        QSaveFile repaired(fixture.paths.config); if (!repaired.open(QIODevice::WriteOnly)) qFatal("fixture config open failed"); repaired.write(validConfig); if (!repaired.commit()) qFatal("fixture publish failed");
    }
};
class Setup : public QObject {
    Q_OBJECT
public:
    Aerial::Settings::Test::Fixture fixture;
public slots:
    void applicationAvailable()
    {
        registerTypes();
        if (!fixture.start()) qFatal("settings QML service fixture failed");
        qmlRegisterSingletonType<QuickHarness>("AerialLock.SettingsTest", 1, 0, "Harness", [](QQmlEngine *engine, QJSEngine *) -> QObject * {
            return engine->property("aerialHarness").value<QObject *>();
        });
    }
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        configureInterActive(*engine);
        // Render fixtures own this engine's appearance. Live portal binding
        // and palette fallbacks have their own integration probe.
        auto *appearance = engine->singletonInstance<InterActive::Appearance *>("InterActive.Foundation", "Appearance");
        if (!appearance) qFatal("missing test appearance singleton");
        auto *harness = new QuickHarness(fixture, *appearance);
        harness->setParent(engine);
        QQmlEngine::setObjectOwnership(harness, QQmlEngine::CppOwnership);
        engine->setProperty("aerialHarness", QVariant::fromValue<QObject *>(harness));
    }
};
QUICK_TEST_MAIN_WITH_SETUP(settings, Setup)
#include "quick.moc"
