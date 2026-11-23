#include "appearance-bridge.h"
#include "async-call.h"
#include "common/client/client-support.h"
#include <Appearance.h>
#include <QApplication>
#include <QDBusArgument>
#include <QDBusServiceWatcher>
#include <QEvent>
#include <QFont>
#include <QPalette>
#include <QQmlEngine>
#include <cmath>

namespace Aerial::Settings {
namespace {
constexpr auto Portal = "org.freedesktop.portal.Desktop";
constexpr auto Path = "/org/freedesktop/portal/desktop";
constexpr auto Interface = "org.freedesktop.portal.Settings";
constexpr auto Group = "org.freedesktop.appearance";
}
AppearanceBridge::AppearanceBridge(QQmlEngine &engine, QDBusConnection bus)
    : QObject(&engine), bus(std::move(bus)), appearance(engine.singletonInstance<InterActive::Appearance *>("InterActive.Foundation", "Appearance"))
{
    Q_ASSERT(appearance);
    publish();
    qApp->installEventFilter(this);
    auto *watcher = new QDBusServiceWatcher(Portal, this->bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this](const QString &, const QString &, const QString &next) { adopt(next); });
    const auto epoch = generation;
    discoverOwner(this, this->bus, Portal, [this, epoch] { return epoch == generation; }, [this](const QDBusMessage &reply) {
        const QDBusPendingReply<QString> result(reply);
        if (!result.isError()) adopt(result.value());
    });
}
void AppearanceBridge::adopt(const QString &next)
{
    ++generation;
    if (!owner.isEmpty()) bus.disconnect(owner, Path, Interface, "SettingChanged", this, SLOT(settingChanged(QString,QString,QDBusVariant,QDBusMessage)));
    owner = next; serials.clear(); scheme = 0; accent = QColor(); publish();
    if (owner.isEmpty()) return;
    // Subscribe to the pinned owner before either asynchronous snapshot read.
    if (!bus.connect(owner, Path, Interface, "SettingChanged", this, SLOT(settingChanged(QString,QString,QDBusVariant,QDBusMessage)))) return;
    read("color-scheme"); read("accent-color");
}
void AppearanceBridge::read(const QString &key)
{
    const auto epoch = generation, serial = serials.value(key);
    auto call = QDBusMessage::createMethodCall(owner, Path, Interface, "Read");
    call << QString(Group) << key; call.setAutoStartService(false);
    watch(this, bus.asyncCall(call, 5000), [this, epoch, serial, key](const QDBusMessage &reply) {
        if (epoch != generation || serial != serials.value(key)) return;
        const QDBusPendingReply<QDBusVariant> value(reply);
        accept(key, value.isError() ? QVariant() : value.value().variant());
    });
}
void AppearanceBridge::settingChanged(const QString &group, const QString &key, const QDBusVariant &value, const QDBusMessage &message)
{
    if (message.service() != owner || group != Group || (key != "color-scheme" && key != "accent-color")) return;
    ++serials[key]; accept(key, value.variant());
}
void AppearanceBridge::accept(const QString &key, const QVariant &value)
{
    const auto decoded = Client::unpack(value);
    if (key == "color-scheme") {
        scheme = decoded.metaType().id() == QMetaType::UInt && decoded.toUInt() <= 2 ? decoded.toUInt() : 0;
    } else {
        accent = QColor();
        if (decoded.metaType() == QMetaType::fromType<QDBusArgument>()) {
            const auto arg = decoded.value<QDBusArgument>();
            if (arg.currentSignature() == "(ddd)") {
                double r, g, b; arg.beginStructure(); arg >> r >> g >> b; arg.endStructure();
                if (std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && r >= 0 && r <= 1 && g >= 0 && g <= 1 && b >= 0 && b <= 1)
                    accent = QColor::fromRgbF(r, g, b);
            }
        }
    }
    publish();
}
QVariantMap AppearanceBridge::input() const
{
    const auto palette = QApplication::palette();
    const bool dark = scheme == 1 || (scheme != 2 && palette.color(QPalette::Window).lightnessF() < .5);
    return {{"schemaVersion", 1}, {"scheme", dark ? "dark" : "light"}, {"density", "compact"},
        {"tokens", QVariantMap{{"colors", QVariantMap{{"accent", accent.isValid() ? accent : palette.color(QPalette::Highlight)}}},
            {"typography", QVariantMap{{"family", QApplication::font().family()}}}}}};
}
void AppearanceBridge::publish() { appearance->setInput(input()); }
bool AppearanceBridge::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ApplicationFontChange) publish();
    return false;
}
}
