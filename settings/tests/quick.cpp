#include "fixture.h"
#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QSaveFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <QImage>
#include <QTouchEvent>
using namespace Aerial::Settings;
class QuickHarness : public SettingsController {
    Q_OBJECT
public:
    Aerial::Settings::Test::Fixture &fixture;
    QByteArray validConfig;
    std::unique_ptr<QPointingDevice> unrelatedTouch{QTest::createTouchDevice()};
    QPointer<QQuickWindow> touchWindow;
    QPointer<const QPointingDevice> touchDevice;
    QuickHarness(Aerial::Settings::Test::Fixture &fixture) : SettingsController(*fixture.service), fixture(fixture) {}
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
    Q_INVOKABLE void replacePreview(const QString &color)
    {
        const auto directory = QFileInfo(fixture.paths.config).absolutePath() + "/themes";
        if (!QDir().mkpath(directory + "/previews")) qFatal("preview directory failed");
        const auto theme = directory + "/Macintosh.qml", preview = directory + "/previews/Macintosh.png";
        if (color.isEmpty()) { QFile::remove(theme); QFile::remove(preview); return; }
        QFile file(theme); if (!file.open(QIODevice::WriteOnly)) qFatal("theme fixture failed");
        file.write("never execute this theme fixture"); file.close();
        QImage image(16, 10, QImage::Format_RGB32); image.fill(QColor(color));
        if (!image.save(preview)) qFatal("preview fixture failed");
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
        auto *harness = new QuickHarness(fixture);
        harness->setParent(engine);
        QQmlEngine::setObjectOwnership(harness, QQmlEngine::CppOwnership);
        engine->setProperty("aerialHarness", QVariant::fromValue<QObject *>(harness));
    }
};
QUICK_TEST_MAIN_WITH_SETUP(settings, Setup)
#include "quick.moc"
