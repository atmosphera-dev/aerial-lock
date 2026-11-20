#include "fixture.h"
#include "window-controller.h"
#include <QApplication>
#include <QImage>
#include <QFileInfo>
#include <QPalette>
#include <QQuickWindow>
#include <QQuickItem>
#include <QJsonArray>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtQuickTest>
using namespace Aerial::Settings;
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
    const bool dark = QString::fromLocal8Bit(argv[pages ? 2 : 1]) == "dark";
    const auto destination = QString::fromLocal8Bit(argv[pages ? 3 : 2]);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(dark ? "#070722" : "#e6e8fa"));
    palette.setColor(QPalette::Base, QColor(dark ? "#11112d" : "#f4f5ff"));
    palette.setColor(QPalette::WindowText, QColor(dark ? "#f3edf7" : "#0e0e43"));
    palette.setColor(QPalette::Text, palette.color(QPalette::WindowText));
    palette.setColor(QPalette::PlaceholderText, QColor(dark ? "#7c80b4" : "#6c70a3"));
    palette.setColor(QPalette::Mid, QColor(dark ? "#21215f" : "#c6c9f2"));
    palette.setColor(QPalette::Accent, QColor(dark ? "#fff59b" : "#5d65f5"));
    palette.setColor(QPalette::Highlight, QColor(dark ? "#a9aefe" : "#5d65f5"));
    palette.setColor(QPalette::HighlightedText, QColor(dark ? "#0e0e43" : "#ffffff"));
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
    window.open();
    QQuickWindow *quick = nullptr;
    for (auto *candidate : app.topLevelWindows()) if ((quick = qobject_cast<QQuickWindow *>(candidate))) break;
    if (!quick || !QTest::qWaitForWindowExposed(quick)) return 1;
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
        for (const QStringList &command : {QStringList{"move-window-to-floating", "--id", id}, QStringList{"set-window-width", "--id", id, "960"}, QStringList{"set-window-height", "--id", id, "941"}}) {
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
        const QStringList pages{"downloadsPage", "sourcesPage", "cachePage", "lockPage", "aboutPage"};
        const auto expected = [&] {
            const auto name = panel == "settings" ? pages.value(page) : QString("servicePanel");
            auto *item = quick->findChild<QQuickItem *>(name);
            if (name.isEmpty() || !item || !item->isVisible() || item->width() <= 0 || item->height() <= 0
                || fixture.controller->panel() != panel || quick->property("page").toInt() != page) return false;
            for (const auto &sheet : {qMakePair(QString("sourceSheet"), sourceOpen), qMakePair(QString("cleanupSheet"), cleanupOpen)}) {
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
        if (!ready(quick->property("page").toInt())) return false;
        QProcess focus; focus.start("niri", {"msg", "action", "focus-window", "--id", nativeWindow});
        if (!focus.waitForFinished() || focus.exitCode()) return false;
        QProcess capture; capture.start("niri", {"msg", "action", "screenshot-window", "--path", path});
        if (!capture.waitForFinished() || capture.exitCode()) return false;
        return QTest::qWaitFor([&] {
            QFile file(path); return file.open(QIODevice::ReadOnly) && file.readAll().endsWith(QByteArray::fromHex("49454e44ae426082"));
        }, 5000);
    };
    if (!ready(0)) return 1;
    if (pages) {
        if (!QDir().mkpath(destination)) return 1;
        QJsonObject geometry;
        const QStringList names{"downloads", "sources", "cache", "lock", "about"};
        for (int i = 0; i < names.size(); ++i) {
            quick->setProperty("page", i);
            if (!ready(i)) return 1;
            bool sidebarAligned = false;
            std::function<void(QQuickItem *)> inspect = [&](QQuickItem *item) {
                if (item->objectName() == "sidebarStatus" && item->isVisible()) {
                    const auto gap = quick->height() - item->mapToScene({0, item->height()}).y();
                    sidebarAligned = qAbs(gap - 16) <= 1;
                }
                if (item->objectName() == "pageTitle" && item->isVisible()) {
                    const auto point = item->mapToScene({0, 0});
                    geometry[names[i]] = QJsonObject{{"titleY", point.y()}, {"titleHeight", item->height()}, {"headingHeight", item->parentItem()->height()}, {"headingY", item->parentItem()->mapToScene({0, 0}).y()}};
                }
                for (auto *child : item->childItems()) inspect(child);
            };
            inspect(quick->contentItem());
            if (!sidebarAligned) return 1;
            if (!captureNative(destination + '/' + names[i] + ".png")) return 1;
            if (i == 3) {
                // The supported reference-height window scrolls on this page.
                // Also retain a full lower-window frame, never a stitched crop.
                std::function<QQuickItem *(QQuickItem *)> scrollable = [&](QQuickItem *item) -> QQuickItem * {
                    if (!item->isVisible()) return nullptr;
                    if (item->inherits("QQuickFlickable") && item->property("contentHeight").toDouble() > item->height()) return item;
                    for (auto *child : item->childItems()) if (auto *found = scrollable(child)) return found;
                    return nullptr;
                };
                auto *item = scrollable(quick->contentItem());
                if (!item) return 1;
                const auto top = item->property("contentY");
                item->setProperty("contentY", item->property("contentHeight").toDouble() - item->height());
                if (!captureNative(destination + "/lock-options.png")) return 1;
                item->setProperty("contentY", top);
            }
        }
        quick->setProperty("page", 1);
        fixture.controller->sources()->begin(); fixture.controller->sources()->field("name", "Drone flights");
        fixture.controller->sources()->field("source", "https://example.org/drones/manifest.json");
        if (!captureNative(destination + "/source-sheet.png")) return 1;
        fixture.controller->sources()->dismiss();
        const auto shot = [&](const QString &name) {
            return captureNative(destination + '/' + name + ".png");
        };
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
        quick->setProperty("page", 2);
        if (!shot("cache-empty-paused")) return 1;
        fixture.controller->cache()->prepareCleanup();
        if (!QTest::qWaitFor([&] { return fixture.controller->cache()->cleanup().value("state") == "confirm"; })) return 1;
        if (!shot("prune-confirmation")) return 1;
        fixture.controller->cache()->dismissCleanup();
        fixture.cli({"content", "resume"});
        if (!QTest::qWaitFor([&] { return fixture.controller->status().startsWith("Waiting"); })) return 1;
        if (!shot("cache-waiting")) return 1;
        quick->setProperty("page", 3);
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
        quick->setProperty("page", 2);
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
        quick->setProperty("page", 1); sources->begin(); sources->field("name", "Draft survives owner loss");
        if (!stop() || !shot("source-draft-owner-loss")) return 1;
        sources->dismiss();
        if (!shot("service-unavailable")) return 1;
        QFile receipt(destination + "/geometry.json"); if (!receipt.open(QIODevice::WriteOnly)) return 1;
        receipt.write(QJsonDocument(geometry).toJson());
    } else {
        const auto image = quick->grabWindow();
        if (image.isNull() || !image.save(destination)) return 1;
    }
    QQmlComponent paletteProbe(qmlEngine(quick));
    paletteProbe.setData("import QtQuick\nimport \"qrc:/settings/qml/components\" as UI\nQtObject { readonly property color selected: UI.Style.selectionText\n readonly property color accent: UI.Style.accentText\n readonly property color direct: UI.Style.colors.highlightedText }", QUrl("qrc:/settings/palette-receipt.qml"));
    std::unique_ptr<QObject> paletteObject(paletteProbe.create());
    if (!paletteObject) return 1;
    QFile paletteReceipt(destination + (pages ? "/palette.json" : ".palette.json"));
    if (!paletteReceipt.open(QIODevice::WriteOnly)) return 1;
    paletteReceipt.write(QJsonDocument(QJsonObject{{"requestedHighlightedText", palette.color(QPalette::HighlightedText).name()},
        {"applicationHighlightedText", QApplication::palette().color(QPalette::HighlightedText).name()},
        {"styleHighlightedText", paletteObject->property("selected").value<QColor>().name()},
        {"styleAccentText", paletteObject->property("accent").value<QColor>().name()},
        {"styleDirectHighlightedText", paletteObject->property("direct").value<QColor>().name()}}).toJson());
    if (paletteObject->property("selected").value<QColor>() != palette.color(QPalette::HighlightedText)
        || paletteObject->property("accent").value<QColor>() != palette.color(QPalette::HighlightedText)) return 1;
    // Natural window teardown and reconstruction exercise the production owner,
    // independently of tray lifetime (which is qualified in P6).
    quick->close();
    if (!QTest::qWaitFor([&] { return !window.hasWindow(); })) return 1;
    window.open();
    if (!window.hasWindow()) return 1;
    return 0;
}
