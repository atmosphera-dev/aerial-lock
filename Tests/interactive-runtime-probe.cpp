// Real consumer: deliberately links only Qt, so IA must load through its plugins.
#include "interactive-runtime.h"
#include <QDebug>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <cstdio>
#include <memory>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    Aerial::Settings::configureInterActive(engine);
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import InterActive
import InterActive.Foundation as IA
import InterActive.Components
Window {
    width: 480; height: 200; visible: true
    color: IA.Appearance.colors.canvas
    IAStatusBanner {
        anchors.fill: parent
        title: "Private runtime"
        message: "All three modules loaded"
        actionText: "Verify"
        onActionRequested: title = "Consumer observed action"
    }
})", QUrl("qrc:/private-runtime.qml"));
    std::unique_ptr<QObject> root(component.create());
    if (!root) {
        qCritical().noquote() << component.errorString();
        return 1;
    }
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    if (!window || !QTest::qWaitForWindowExposed(window)) return 2;
    QSignalSpy frames(window, &QQuickWindow::frameSwapped);
    window->update();
    if (!frames.wait(5000)) return 3;
    auto *button = root->findChild<QQuickItem *>("iaBannerAction");
    auto *title = root->findChild<QQuickItem *>("iaBannerTitle");
    if (!button || !title || button->width() <= 0 || !button->isVisible()) return 4;
    const auto center = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    if (title->property("text").toString() != "Consumer observed action") return 5;
    window->close();
    root.reset();
    std::puts("INTERACTIVE_CONSUMER_PASS frame and action reached real controls");
    return 0;
}
