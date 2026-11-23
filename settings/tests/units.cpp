#include "settings-controller.h"
#include "tray-controller.h"
#include "window-controller.h"
#include "common/config-document.h"
#include <QApplication>
#include <QDBusConnectionInterface>
#include <QDBusVirtualObject>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>
using namespace Aerial;
using namespace Aerial::Settings;

// Independent standard Application peer for deterministic forwarding failures.
class ForwardPeer : public QDBusVirtualObject {
public:
    QString profile, mode;
    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &m, const QDBusConnection &bus) override
    {
        if (m.interface() == "org.freedesktop.DBus.Properties" && m.member() == "Get") {
            bus.send(m.createReply({QVariant::fromValue(QDBusVariant(profile))}));
            return true;
        }
        if (m.interface() == "org.freedesktop.Application") {
            if (mode == "exit") QTimer::singleShot(0, qApp, &QCoreApplication::quit);
            else bus.send(m.createErrorReply("org.freedesktop.DBus.Error.Failed", "Forward deliberately rejected"));
            return true;
        }
        return false;
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    const auto args = app.arguments(); auto bus = QDBusConnection::sessionBus();
    if (args.size() == 4 && args[1] == "--forward-peer") {
        ForwardPeer peer; peer.profile = args[2]; peer.mode = args[3];
        if (!bus.registerVirtualObject("/org/aeriallock/AerialSettings", &peer)
            || !bus.registerService("org.aeriallock.AerialSettings")) return 1;
        puts("READY"); fflush(stdout); return app.exec();
    }
    // Modes: tray (About enable/Hide), failure (failed unit stays actionable),
    // idle (Turn on, save without restart, explicit restart).
    if (args.size() != 4) return 2;
    const auto source = args[1], output = args[2], mode = args[3];
    registerTypes();
    const auto paths = ConfigPaths::environment(source);
    Client::ServiceConnection service(bus, paths, source);
    const bool idle = mode == "idle" || mode == "idle-existing";
    SettingsController settings(service);
    WindowController window(settings);
    ApplicationActivation activation(window, readConfig(paths).profile);
    TrayController tray(settings, bus);
    if (!bus.registerObject("/org/aeriallock/AerialSettings", &activation, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties)
        || !bus.registerService("org.aeriallock.AerialSettings")) return 1;
    QObject::connect(&activation, &ApplicationActivation::trayRequested, &tray, &TrayController::show);
    QObject::connect(&tray, &TrayController::openRequested, &window, [&](int page, const QVariantMap &platform) { window.open(platform, page); });
    service.start();
    QJsonObject receipt;
    const auto require = [&](bool value, const char *label) {
        if (!value) fprintf(stderr, "%s; manager=%s\n", label, qPrintable(settings.systemd()->error()));
        return value;
    };
    if (!require(QTest::qWaitFor([&] { return settings.panel() == "settings" && settings.systemd()->available() && (idle || settings.tray().value("available").toBool()); }, 15000), "native dependencies")) return 1;
    window.open({}, mode == "idle" ? SettingsController::OverviewPage
        : idle ? SettingsController::LockPage : SettingsController::AboutPage);
    QQuickWindow *quick = nullptr;
    for (auto *candidate : app.topLevelWindows()) if ((quick = qobject_cast<QQuickWindow *>(candidate))) break;
    if (!quick || !QTest::qWaitForWindowExposed(quick)) return 1;
    const auto capture = [&](const QString &name) {
        QTest::qWait(250);
        const auto path = QFileInfo(output).absolutePath() + '/' + QFileInfo(output).completeBaseName() + '-' + name + ".png";
        const auto image = quick->grabWindow();
        if (image.isNull() || !image.save(path)) return false;
        auto images = receipt.value("captures").toArray(); images.append(path); receipt["captures"] = images;
        return true;
    };
    std::function<QQuickItem *(QQuickItem *, const QString &)> find = [&](QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name && item->isVisible() && item->width() > 0) return item;
        for (auto *child : item->childItems()) if (auto *found = find(child, name)) return found;
        return nullptr;
    };
    const auto control = [&](const QString &name) {
        QQuickItem *item = nullptr;
        QTest::qWaitFor([&] { return (item = find(quick->contentItem()->parentItem() ? quick->contentItem()->parentItem() : quick->contentItem(), name)); }, 15000);
        return item;
    };
    const auto click = [&](QQuickItem *item) {
        // Scroll the page like a user would, so the press lands on the control.
        for (auto *parent = item->parentItem(); parent; parent = parent->parentItem()) {
            if (!parent->inherits("QQuickFlickable")) continue;
            auto *content = parent->property("contentItem").value<QQuickItem *>();
            const auto top = item->mapToItem(content, QPointF(0, 0)).y();
            const auto maxY = qMax(0., parent->property("contentHeight").toDouble() - parent->height());
            parent->setProperty("contentY", qBound(0., top - parent->height() / 3, maxY));
            break;
        }
        QTest::qWait(50);
        QTest::mouseClick(quick, Qt::LeftButton, {}, item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
    };
    QSignalSpy operations(settings.systemd(), &SystemdController::operationFinished);
    const auto settle = [&](const char *label) {
        return require(QTest::qWaitFor([&] { return !operations.isEmpty(); }, 20000), label) && operations.last()[2].toBool();
    };
    if (idle) {
        const auto idleUnit = [&] { return settings.systemd()->units().value("aerial-idle.service").toMap(); };
        if (!require(QTest::qWaitFor([&] { return settings.systemd()->idle().value("action") == "enableStart"; }, 15000), "idle initially off")) return 1;
        if (!capture("idle-off")) return 1;
        const auto idleAction = [&] {
            auto *notice = control(window.page() == SettingsController::OverviewPage ? "notice-idle" : "idleNotice");
            return notice ? find(notice, "iaBannerAction") : nullptr;
        };
        auto *action = idleAction(); if (!require(action, "idle Turn on button")) return 1;
        QString existingBeforeEnable;
        if (mode == "idle-existing") {
            if (!require(QTest::qWaitFor([&] { return idleUnit().value("active") == "active" && !idleUnit().value("invocation").toString().isEmpty(); }, 15000), "existing invocation before Turn on")) return 1;
            existingBeforeEnable = idleUnit().value("invocation").toString();
        }
        click(action);
        auto *turnOn = control("idle-turn-on"); if (!require(turnOn, "idle one-owner sheet")) return 1;
        if (!capture("idle-turn-on")) return 1;
        click(turnOn);
        if (!require(settle("Turn on enables and starts idle"), "Turn on succeeds")) return 1;
        if (mode == "idle-existing") {
            if (!require(idleUnit().value("invocation").toString() == existingBeforeEnable, "Turn on preserves the already running invocation")) return 1;
            receipt["beforeEnableInvocation"] = existingBeforeEnable;
            if (!require(idleUnit().value("active") == "active" && !settings.systemd()->idle().value("fresh").toBool(), "no-op StartUnit does not confirm settings")) return 1;
            receipt["enabledExistingInvocation"] = QJsonObject::fromVariantMap(idleUnit());
            const auto existing = idleUnit().value("invocation").toString();
            if (!require(!existing.isEmpty(), "existing idle invocation is nonempty before Restart")) return 1;
            operations.clear(); action = idleAction(); if (!require(action, "existing idle Restart button")) return 1;
            click(action);
            if (!require(settle("Restart existing idle"), "existing idle restart succeeds")) return 1;
            if (!require(QTest::qWaitFor([&] { const auto id = idleUnit().value("invocation").toString(); return !id.isEmpty() && id != existing; }, 15000), "existing idle gets a new invocation")) return 1;
        }
        if (!require(idleUnit().value("active") == "active" && !settings.systemd()->idle().value("fresh").toBool(), "unit lifecycle cannot confirm the selected configuration profile")) return 1;
        receipt["turnedOn"] = QJsonObject::fromVariantMap(idleUnit());
        const auto before = idleUnit().value("invocation").toString();
        if (!require(!before.isEmpty(), "idle invocation is nonempty before Restart")) return 1;
        const int requestedTimeout = mode == "idle-existing" ? 3540 : 3600;
        operations.clear(); settings.edit("idle.timeoutSeconds", requestedTimeout);
        if (!require(QTest::qWaitFor([&] { return service.ready() && service.snapshot().effective.value("idle").toObject().value("timeoutSeconds").toInt() == requestedTimeout
            && !settings.controls().value("idle.timeoutSeconds").toMap().value("pending").toBool(); }, 15000), "idle timeout save reaches the service snapshot")) return 1;
        if (!require(QTest::qWaitFor([&] { return settings.systemd()->idle().value("action") == "restart"; }, 15000), "save asks for restart")) return 1;
        QTest::qWait(3000);
        if (!require(operations.isEmpty() && idleUnit().value("invocation") == before && idleUnit().value("active") == "active", "save never restarts idle")) return 1;
        receipt["afterSave"] = QJsonObject::fromVariantMap(idleUnit());
        if (!capture("idle-restart")) return 1;
        action = idleAction(); if (!require(action, "idle Restart button")) return 1;
        click(action);
        if (!require(settle("Restart completes"), "Restart succeeds")) return 1;
        if (!require(QTest::qWaitFor([&] { const auto id = idleUnit().value("invocation").toString(); return !id.isEmpty() && id != before; }, 15000) && !settings.systemd()->idle().value("fresh").toBool(), "restart reaches a new invocation without claiming configuration acknowledgement")) return 1;
        receipt["restarted"] = QJsonObject::fromVariantMap(idleUnit());
    } else {
        auto *toggle = control("about-tray"); if (!toggle) return 1;
        click(toggle);
        const bool ok = settle("About enable completes");
        if (!require(!operations.isEmpty(), "About enable must emit its completion")) return 1;
        receipt["enableResult"] = ok;
        receipt["enableError"] = settings.systemd()->error();
        receipt["enabledUnit"] = QJsonObject::fromVariantMap(settings.systemd()->units().value("aerial-tray.service").toMap());
        if (mode == "failure") {
            if (!require(!ok && !settings.systemd()->error().isEmpty() && window.hasWindow(), "failure stays actionable")) return 1;
            if (!capture("about-unit-error")) return 1;
        } else {
            if (!require(ok && settings.tray().value("persistent").toBool() && tray.enabled(), "About enables startup")) return 1;
            if (!capture("about-tray-on")) return 1;
            operations.clear(); click(toggle);
            if (!require(settle("Hide completes") && window.hasWindow() && !tray.enabled(), "Hide disables and keeps window")) return 1;
            operations.clear();
            settings.showTray(true); settings.showTray(false);
            if (!require(QTest::qWaitFor([&] { return operations.size() >= 2 && !tray.keepsAlive(); }, 20000), "serialized enable/Hide settles")) return 1;
            for (const auto &operation : operations) {
                if (!require(operation[0].toString() == "aerial-tray.service" && operation[2].toBool(), "serialized intent completes successfully for the tray unit")) return 1;
            }
            if (!require(operations[0][1].toString() == "enableStart" && operations[1][1].toString() == "disable", "serialized enable then Hide outcomes")) return 1;
            if (!require(!settings.tray().value("persistent").toBool() && window.hasWindow(), "race keeps Hide and window")) return 1;
            tray.show();
            if (!require(!tray.enabled() && !settings.tray().value("visible").toBool(), "late startup forward keeps completed Hide")) return 1;
            receipt["aboutClickAndHideRace"] = true;
            const auto invocation = [&] { return settings.systemd()->units().value("aerial.service").toMap().value("invocation").toString(); };
            if (!require(QTest::qWaitFor([&] { return !invocation().isEmpty(); }, 15000), "background service invocation")) return 1;
            const auto before = invocation();
            auto *restart = control("aboutRestart"); if (!require(restart, "About background service Restart")) return 1;
            operations.clear(); click(restart);
            if (!require(settle("About background service Restart completes") && operations.last()[0].toString() == "aerial.service"
                && operations.last()[1].toString() == "restart", "About restarts background service")) return 1;
            if (!require(QTest::qWaitFor([&] { return service.ready() && !invocation().isEmpty() && invocation() != before; }, 15000), "new service invocation and reconnect")) return 1;
            receipt["aboutServiceRestart"] = QJsonObject{{"before", before}, {"after", invocation()}};
        }
    }
    quick->close(); if (!QTest::qWaitFor([&] { return !window.hasWindow(); })) return 1;
    QFile result(output); if (!result.open(QIODevice::WriteOnly)) return 1;
    result.write(QJsonDocument(receipt).toJson()); return 0;
}
