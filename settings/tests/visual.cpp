#include "fixture.h"
#include "window-controller.h"
#include <QApplication>
#include <QImage>
#include <QPalette>
#include <QQuickWindow>
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
    if (argc != 3) return 2;
    const bool dark = QString::fromLocal8Bit(argv[1]) == "dark";
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
    fixture.controller->edit("automatic", true);
    if (!QTest::qWaitFor([&] { return !fixture.control("automatic").value("pending").toBool(); })) return 1;
    WindowController window(*fixture.controller);
    window.open();
    QQuickWindow *quick = nullptr;
    for (auto *candidate : app.topLevelWindows()) if ((quick = qobject_cast<QQuickWindow *>(candidate))) break;
    if (!quick || !QTest::qWaitForWindowExposed(quick)) return 1;
    QProcess inventory;
    inventory.start("niri", {"msg", "-j", "windows"});
    if (!inventory.waitForFinished() || inventory.exitCode()) return 1;
    bool sized = false;
    for (const auto &entry : QJsonDocument::fromJson(inventory.readAllStandardOutput()).array()) {
        const auto candidate = entry.toObject();
        if (candidate.value("pid").toInteger() != app.applicationPid()) continue;
        const auto id = QString::number(candidate.value("id").toInteger());
        for (const QStringList &command : {QStringList{"move-window-to-floating", "--id", id}, QStringList{"set-window-width", "--id", id, "960"}, QStringList{"set-window-height", "--id", id, "941"}}) {
            QProcess resize;
            resize.start("niri", QStringList{"msg", "action"} + command);
            if (!resize.waitForFinished() || resize.exitCode()) return 1;
        }
        sized = true;
    }
    if (!sized) return 1;
    QTest::qWait(300);
    const auto image = quick->grabWindow();
    if (image.isNull() || !image.save(QString::fromLocal8Bit(argv[2]))) return 1;
    QQmlComponent paletteProbe(qmlEngine(quick));
    paletteProbe.setData("import QtQuick\nimport \"qrc:/settings/qml/components\" as UI\nQtObject { readonly property color selected: UI.Style.selectionText\n readonly property color accent: UI.Style.accentText\n readonly property color direct: UI.Style.colors.highlightedText }", QUrl("qrc:/settings/palette-receipt.qml"));
    std::unique_ptr<QObject> paletteObject(paletteProbe.create());
    if (!paletteObject) return 1;
    QFile paletteReceipt(QString::fromLocal8Bit(argv[2]) + ".palette.json");
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
