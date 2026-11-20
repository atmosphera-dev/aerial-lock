#include "fixture.h"
#include "window-controller.h"
#include <QApplication>
#include <QImage>
#include <QPalette>
#include <QQuickWindow>
#include <QQuickItem>
#include <QJsonArray>
#include <QQmlComponent>
#include <QQmlEngine>
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
    const auto captureNative = [&](const QString &path) {
        QProcess focus; focus.start("niri", {"msg", "action", "focus-window", "--id", nativeWindow});
        if (!focus.waitForFinished() || focus.exitCode()) return false;
        QProcess capture; capture.start("niri", {"msg", "action", "screenshot-window", "--path", path});
        if (!capture.waitForFinished() || capture.exitCode()) return false;
        return QTest::qWaitFor([&] {
            QFile file(path); return file.open(QIODevice::ReadOnly) && file.readAll().endsWith(QByteArray::fromHex("49454e44ae426082"));
        }, 5000);
    };
    QTest::qWait(300);
    if (pages) {
        if (!QDir().mkpath(destination)) return 1;
        QJsonObject geometry;
        const QStringList names{"downloads", "sources", "cache", "lock", "about"};
        for (int i = 0; i < names.size(); ++i) {
            quick->setProperty("page", i); QTest::qWait(250);
            std::function<void(QQuickItem *)> inspect = [&](QQuickItem *item) {
                if (item->objectName() == "pageTitle" && item->isVisible()) {
                    const auto point = item->mapToScene({0, 0});
                    geometry[names[i]] = QJsonObject{{"titleY", point.y()}, {"titleHeight", item->height()}, {"headingHeight", item->parentItem()->height()}, {"headingY", item->parentItem()->mapToScene({0, 0}).y()}};
                }
                for (auto *child : item->childItems()) inspect(child);
            };
            inspect(quick->contentItem());
            if (!captureNative(destination + '/' + names[i] + ".png")) return 1;
        }
        quick->setProperty("page", 1);
        fixture.controller->sources()->begin(); fixture.controller->sources()->field("name", "Drone flights");
        fixture.controller->sources()->field("source", "https://example.org/drones/manifest.json");
        QTest::qWait(250);
        if (!captureNative(destination + "/source-sheet.png")) return 1;
        fixture.controller->sources()->dismiss();
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
