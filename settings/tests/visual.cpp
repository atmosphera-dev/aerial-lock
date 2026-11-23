#include "fixture.h"
#include "window-controller.h"
#include <QApplication>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QImage>
#include <QFileInfo>
#include <QSaveFile>
#include <Appearance.h>
#include <QQuickWindow>
#include <QQuickItem>
#include <QJsonArray>
#include <QQmlEngine>
#include <QtQuickTest>
using namespace Aerial::Settings;
namespace {
bool saveJson(const QString &path, const QJsonDocument &document)
{
    QSaveFile file(path);
    const auto data = document.toJson();
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit()) return true;
    qCritical() << "could not publish visual evidence:" << path << file.errorString();
    return false;
}
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Aerial");
    app.setDesktopFileName("org.aeriallock.AerialSettings");
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--focus-peer") {
        app.setDesktopFileName("org.aeriallock.SettingsFocusWitness");
        QQuickWindow peer;
        peer.setTitle("Aerial focus witness");
        peer.resize(320, 240);
        peer.show();
        return app.exec();
    }
    const bool pages = argc == 4 && QString::fromLocal8Bit(argv[1]) == "--pages";
    if (argc != 3 && !pages) return 2;
    const auto scheme = QString::fromLocal8Bit(argv[pages ? 2 : 1]);
    if (scheme != "dark" && scheme != "light") return 2;
    const auto destination = QString::fromLocal8Bit(argv[pages ? 3 : 2]);
    if (pages && (!QDir().mkpath(destination)
            || !QDir(destination).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))) {
        qCritical() << "page captures require an empty output directory:" << destination;
        return 1;
    }
    // Like the acceptance wrapper, capture on a private bus without an
    // appearance portal. The production bridge alone owns the IA singleton.
    auto *bus = QDBusConnection::sessionBus().interface();
    const auto portal = bus ? bus->isServiceRegistered("org.freedesktop.portal.Desktop") : QDBusReply<bool>();
    if (!portal.isValid() || portal.value()) {
        qCritical() << "run settings-visual-probe through Tests/run-settings-test.py for a private appearance bus";
        return 1;
    }
    auto palette = QApplication::palette();
    palette.setColor(QPalette::Window, QColor(scheme == "dark" ? "#202124" : "#f4f5f7"));
    palette.setColor(QPalette::Highlight, QColor("#5795ec"));
    QApplication::setPalette(palette);
    registerTypes();
    Test::Fixture fixture;
    if (!fixture.start()) return 1;
    if (pages) {
        const QJsonArray sources{
            QJsonObject{{"id", "apple"}, {"type", "apple"}, {"name", "Apple TV"}, {"source", "http://127.0.0.1:9/apple.tar"}},
            QJsonObject{{"id", "community"}, {"type", "community"}, {"name", "Community 2026"}, {"source", "http://127.0.0.1:9/community.json"}},
            QJsonObject{{"id", "videos"}, {"type", "folder"}, {"name", "My videos"}, {"source", fixture.root.path() + "/Videos"}},
            QJsonObject{{"id", "drones"}, {"type", "community"}, {"name", "Drone flights"}, {"source", "http://127.0.0.1:9/drones.json"}}};
        fixture.cli({"config", "set", "sources", QString::fromUtf8(QJsonDocument(sources).toJson(QJsonDocument::Compact))});
        if (!QTest::qWaitFor([&] { return fixture.controller->sources()->rows().size() == 4; })) return 1;
        // Keep automatic networking off in the page fixture. Capture actual facts.
    }
    if (!pages) {
        fixture.controller->edit("automatic", true);
        if (!QTest::qWaitFor([&] { return !fixture.control("automatic").value("pending").toBool(); })) return 1;
    }
    WindowController window(*fixture.controller);
    window.open({}, pages ? SettingsController::OverviewPage : SettingsController::DownloadsPage);
    QQuickWindow *quick = nullptr;
    for (auto *candidate : app.topLevelWindows()) if ((quick = qobject_cast<QQuickWindow *>(candidate))) break;
    if (!quick || !QTest::qWaitForWindowExposed(quick)) return 1;
    auto *appearance = qmlEngine(quick)->singletonInstance<InterActive::Appearance *>("InterActive.Foundation", "Appearance");
    if (!appearance || appearance->scheme() != scheme) return 1;
    QProcess inventory;
    inventory.start("niri", {"msg", "-j", "windows"});
    if (!inventory.waitForFinished() || inventory.exitCode()) return 1;
    bool sized = false;
    QString nativeWindow;
    for (const auto &entry : QJsonDocument::fromJson(inventory.readAllStandardOutput()).array()) {
        const auto candidate = entry.toObject();
        if (candidate.value("pid").toInteger() != app.applicationPid()) continue;
        const auto id = QString::number(candidate.value("id").toInteger());
        nativeWindow = id;
        for (const QStringList &command : {QStringList{"move-window-to-floating", "--id", id}, QStringList{"set-window-width", "--id", id, "920"}, QStringList{"set-window-height", "--id", id, "760"}}) {
            QProcess resize;
            resize.start("niri", QStringList{"msg", "action"} + command);
            if (!resize.waitForFinished() || resize.exitCode()) return 1;
        }
        sized = true;
    }
    if (!sized) return 1;
    // A capture owns an expected surface, not an elapsed-time assumption.
    const auto ready = [&](int page) {
        const auto panel = fixture.controller->panel();
        const bool sourceOpen = fixture.controller->sources()->sheetOpen();
        const bool cleanupOpen = fixture.controller->cache()->cleanup().value("visible").toBool();
        const QStringList pages{"overviewPage", "sourcesPage", "downloadsPage", "lockPage", "aboutPage"};
        const auto expected = [&] {
            if (appearance->scheme() != scheme) return false;
            const auto name = (panel == "settings" || page == SettingsController::AboutPage) ? pages.value(page) : QString("servicePanel");
            auto *item = quick->findChild<QQuickItem *>(name);
            if (name.isEmpty() || !item || !item->isVisible() || item->width() <= 0 || item->height() <= 0
                || fixture.controller->panel() != panel || quick->property("page").toInt() != page) return false;
            for (const auto &sheet : {qMakePair(QString("sourceSheet"), sourceOpen), qMakePair(QString("cleanupDialog"), cleanupOpen)}) {
                auto *popup = quick->findChild<QObject *>(sheet.first);
                if (!popup || popup->property("visible").toBool() != sheet.second || popup->property("opened").toBool() != sheet.second) return false;
            }
            return true;
        };
        if (!QTest::qWaitFor(expected, 5000) || !QQuickTest::qWaitForPolish(quick, 5000)) return false;
        QSignalSpy frames(quick, &QQuickWindow::frameSwapped);
        quick->update();
        return QTest::qWaitFor([&] { return !frames.isEmpty(); }, 5000)
            && expected() && !QQuickTest::qIsPolishScheduled(quick);
    };
    const auto captureNative = [&](const QString &path) {
        QProcess focus; focus.start("niri", {"msg", "action", "focus-window", "--id", nativeWindow});
        if (!focus.waitForFinished() || focus.exitCode()) return false;
        QProcess capture; capture.start("niri", {"msg", "action", "screenshot-window", "--path", path});
        if (!capture.waitForFinished() || capture.exitCode()) return false;
        return QTest::qWaitFor([&] {
            QFile file(path); return file.open(QIODevice::ReadOnly) && file.readAll().endsWith(QByteArray::fromHex("49454e44ae426082"));
        }, 5000);
    };
    if (!ready(window.page())) return 1;
    if (pages) {
        const QStringList names{"overview", "sources", "downloads", "lock-screen", "about"};
        const auto resize = [&](int width, int height) {
            for (const auto &[axis, value] : {std::pair{"width", width}, std::pair{"height", height}}) {
                QProcess command;
                command.start("niri", {"msg", "action", QString("set-window-") + axis, "--id", nativeWindow, QString::number(value)});
                if (!command.waitForFinished() || command.exitCode()) return false;
            }
            return QTest::qWaitFor([&] { return quick->size() == QSize(width, height); }, 5000);
        };
        const auto metrics = [&] {
            QJsonArray items;
            std::function<void(QQuickItem *)> visit = [&](QQuickItem *item) {
                if (!item->isVisible()) return;
                if (!item->objectName().isEmpty()) {
                    const auto origin = item->mapToScene({0, 0});
                    items << QJsonObject{{"name", item->objectName()}, {"x", origin.x()}, {"y", origin.y()},
                        {"width", item->width()}, {"height", item->height()}, {"contentX", item->property("contentX").toDouble()},
                        {"contentY", item->property("contentY").toDouble()}, {"contentHeight", item->property("contentHeight").toDouble()}};
                }
                for (auto *child : item->childItems()) visit(child);
            };
            visit(quick->contentItem());
            return items;
        };
        QJsonArray frames;
        const auto shot = [&](const QString &name) {
            const auto path = destination + '/' + name;
            const QDeadlineTimer deadline(5000);
            do {
                if (!ready(quick->property("page").toInt())) break;
                const auto before = metrics();
                const auto size = quick->size();
                QTemporaryDir pending(destination + "/.capture-XXXXXX");
                if (!pending.isValid()) break;
                const auto png = pending.filePath("frame.png");
                if (!captureNative(png) || appearance->scheme() != scheme) break;
                // Preview decoding can finish between the repaint and Niri's
                // capture. Discard those bytes and only publish a stable pair.
                if (before != metrics() || size != quick->size()) continue;
                if (!QFile::rename(png, path + ".png") || !saveJson(path + ".json", QJsonDocument(before))) break;
                frames << QJsonObject{{"name", name}, {"image", name + ".png"}, {"geometry", name + ".json"},
                    {"width", size.width()}, {"height", size.height()}, {"scheme", scheme}};
                return true;
            } while (!deadline.hasExpired());
            qCritical() << "could not publish a stable capture:" << name;
            return false;
        };
        const auto page = [&](SettingsController::Page target) { window.open({}, target); };
        for (const bool minimum : {false, true}) {
            if (!resize(minimum ? 640 : 920, minimum ? 480 : 760)) return 1;
            const auto prefix = minimum ? QStringLiteral("min-") : QString();
            for (const auto target : {SettingsController::OverviewPage, SettingsController::SourcesPage,
                     SettingsController::DownloadsPage, SettingsController::LockPage, SettingsController::AboutPage}) {
                page(target);
                const auto name = prefix + names[target];
                if (!shot(name)) return 1;
                QQuickItem *viewport = nullptr;
                for (auto *candidate : quick->findChildren<QQuickItem *>("pageViewport"))
                    if (candidate->isVisible()) { viewport = candidate; break; }
                if (!viewport) return 1;
                const auto range = viewport->property("contentHeight").toDouble() - viewport->height();
                if (range > 0 && (target == SettingsController::DownloadsPage || target == SettingsController::LockPage)) {
                    if (!minimum || target == SettingsController::DownloadsPage) {
                        viewport->setProperty("contentY", range / 2);
                        if (!shot(name + "-middle")) return 1;
                    }
                }
                if (range > 0 && (target == SettingsController::DownloadsPage || target == SettingsController::LockPage
                        || (minimum && (target == SettingsController::OverviewPage || target == SettingsController::AboutPage)))) {
                    viewport->setProperty("contentY", range);
                    if (!shot(name + "-end")) return 1;
                    viewport->setProperty("contentY", 0);
                }
            }
        }
        if (!resize(920, 760)) return 1;
        page(SettingsController::SourcesPage);
        fixture.controller->sources()->begin(); fixture.controller->sources()->field("name", "Drone flights");
        fixture.controller->sources()->field("source", "https://example.org/drones/manifest.json");
        if (!shot("add-source")) return 1;
        fixture.controller->sources()->dismiss();
        auto *sources = fixture.controller->sources();
        sources->begin("drones");
        if (!shot("source-edit")) return 1;
        sources->dismiss();
        // A real stale revision leaves the unsubmitted sheet fields intact.
        sources->begin(); sources->field("name", "Keep my draft");
        sources->field("source", "https://example.org/draft.json");
        fixture.cli({"config", "set", "cache.rotationDays", "30"});
        sources->save();
        if (!QTest::qWaitFor([&] { return !sources->busy() && !sources->draft().value("error").toString().isEmpty(); })) return 1;
        if (!shot("source-conflict")) return 1;
        sources->dismiss();
        fixture.cli({"config", "set", "sources", "[]"});
        if (!QTest::qWaitFor([&] { return sources->rows().isEmpty() && sources->appleAvailable(); })) return 1;
        if (!shot("sources-empty")) return 1;
        for (const auto &type : {QStringLiteral("community"), QStringLiteral("folder"), QStringLiteral("file"), QStringLiteral("defaultApple")}) {
            sources->begin(); sources->field("type", type);
            if (sources->draft().value("type").toString() != (type == "defaultApple" ? "apple" : type)) return 1;
            if (!shot("source-choice-" + type)) return 1;
            sources->dismiss();
        }
        sources->begin(); sources->field("name", "Keep my draft"); sources->field("source", "not a URL"); sources->save();
        if (sources->draft().value("error").toString().isEmpty() || !shot("source-validation")) return 1;
        sources->dismiss();
        page(SettingsController::DownloadsPage);
        if (!shot("cache-empty-paused")) return 1;
        fixture.controller->cache()->prepareCleanup();
        if (!QTest::qWaitFor([&] { return fixture.controller->cache()->cleanup().value("state") == "confirm"; })) return 1;
        if (!shot("prune-confirmation")) return 1;
        fixture.controller->cache()->dismissCleanup();
        fixture.cli({"content", "resume"});
        if (!QTest::qWaitFor([&] { return fixture.controller->status().startsWith("Waiting"); })) return 1;
        if (!shot("cache-waiting")) return 1;
        page(SettingsController::LockPage);
        const auto customDir = QFileInfo(fixture.paths.config).absolutePath() + "/themes";
        QDir().mkpath(customDir);
        QFile custom(customDir + "/Macintosh.qml");
        if (!custom.open(QIODevice::WriteOnly)) return 1;
        custom.write("// Never loaded by the settings app.\n"); custom.close();
        if (!QTest::qWaitFor([&] { return fixture.controller->themes()->rows().first().toMap().value("preview").toString().isEmpty(); })) return 1;
        if (!shot("theme-name-only")) return 1;
        // Existing owner-test contract: an obstructed planner state root is an
        // honest storage error. Preserve the fixture's existing directory.
        const auto stop = [&] {
            fixture.daemon.terminate();
            return fixture.daemon.waitForFinished(5000) && QTest::qWaitFor([&] { return fixture.controller->panel() == "unavailable"; });
        };
        const auto start = [&] {
            fixture.daemon.start(QString(AERIAL_SETTINGS_TEST_SOURCE) + "/companion/aerial", {});
            return fixture.daemon.waitForStarted() && QTest::qWaitFor([&] { return fixture.controller->panel() == "settings"; });
        };
        if (!stop() || !QDir().rename(fixture.paths.state, fixture.paths.state + ".retained")) return 1;
        QFile obstruction(fixture.paths.state);
        if (!obstruction.open(QIODevice::WriteOnly)) return 1;
        obstruction.write("Owned visual fixture obstruction\n"); obstruction.close();
        page(SettingsController::DownloadsPage);
        if (!start() || !QTest::qWaitFor([&] { return fixture.controller->serviceProperties().value("DeferReason") == "StorageError"; })) return 1;
        if (!shot("cache-storage-error")) return 1;
        if (!stop() || !obstruction.remove() || !QDir().rename(fixture.paths.state + ".retained", fixture.paths.state) || !start()) return 1;
        const auto goodConfig = fixture.cli({"config", "show", "--json"});
        if (goodConfig.isEmpty()) return 1;
        QFile config(fixture.paths.config);
        if (!config.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
        config.write("{\n broken\n"); config.close();
        if (!QTest::qWaitFor([&] { return fixture.controller->panel() == "diagnostic"; })) return 1;
        if (!shot("settings-json-error")) return 1;
        if (!config.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
        config.write(goodConfig); config.close();
        if (!QTest::qWaitFor([&] { return fixture.controller->panel() == "settings"; })) return 1;
        const auto permissions = config.permissions();
        if (!config.setPermissions({})) return 1;
        if (!QTest::qWaitFor([&] { return fixture.controller->panel() == "diagnostic"; })) return 1;
        if (!shot("settings-unreadable")) return 1;
        if (!config.setPermissions(permissions) || !QTest::qWaitFor([&] { return fixture.controller->panel() == "settings"; })) return 1;
        page(SettingsController::SourcesPage); sources->begin(); sources->field("name", "Draft survives owner loss");
        if (!stop() || !shot("source-draft-owner-loss")) return 1;
        sources->dismiss();
        page(SettingsController::OverviewPage);
        if (!shot("unavailable") || !resize(640, 480) || !shot("min-unavailable")) return 1;
        if (!saveJson(destination + "/manifest.json",
                QJsonDocument(QJsonObject{{"schema", 1}, {"qt", qVersion()}, {"fixture", "owned-service"}, {"frames", frames}}))) return 1;
    } else {
        const auto image = quick->grabWindow();
        if (image.isNull() || appearance->scheme() != scheme || !image.save(destination)) return 1;
    }
    // Natural window teardown and reconstruction exercise the production owner,
    // independently of tray lifetime (which is qualified in P6).
    quick->close();
    if (!QTest::qWaitFor([&] { return !window.hasWindow(); })) return 1;
    window.open();
    if (!window.hasWindow()) return 1;
    return 0;
}
