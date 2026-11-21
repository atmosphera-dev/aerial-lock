#pragma once
#include <QDBusArgument>
#include <QDBusVariant>
#include <QVariantMap>

namespace Aerial::Settings {
struct TrayImage { int width = 0, height = 0; QByteArray bytes; };
using TrayImages = QList<TrayImage>;
struct TrayTooltip { QString icon; TrayImages images; QString title, text; };
struct MenuLayout { int id = 0; QVariantMap properties; QList<QDBusVariant> children; };
struct MenuProperties { int id = 0; QVariantMap properties; };
using MenuPropertiesList = QList<MenuProperties>;
struct MenuRemoved { int id = 0; QStringList names; };
using MenuRemovedList = QList<MenuRemoved>;
struct MenuEvent { int id = 0; QString event; QDBusVariant data; uint timestamp = 0; };
using MenuEvents = QList<MenuEvent>;
using MenuIds = QList<int>;
#define AERIAL_TRAY_WIRE(Type) \
    QDBusArgument &operator<<(QDBusArgument &, const Type &); \
    const QDBusArgument &operator>>(const QDBusArgument &, Type &);
AERIAL_TRAY_WIRE(TrayImage)
AERIAL_TRAY_WIRE(TrayTooltip)
AERIAL_TRAY_WIRE(MenuLayout)
AERIAL_TRAY_WIRE(MenuProperties)
AERIAL_TRAY_WIRE(MenuRemoved)
AERIAL_TRAY_WIRE(MenuEvent)
#undef AERIAL_TRAY_WIRE
void registerTrayTypes();
}
Q_DECLARE_METATYPE(Aerial::Settings::TrayImage)
Q_DECLARE_METATYPE(Aerial::Settings::TrayImages)
Q_DECLARE_METATYPE(Aerial::Settings::TrayTooltip)
Q_DECLARE_METATYPE(Aerial::Settings::MenuLayout)
Q_DECLARE_METATYPE(Aerial::Settings::MenuProperties)
Q_DECLARE_METATYPE(Aerial::Settings::MenuPropertiesList)
Q_DECLARE_METATYPE(Aerial::Settings::MenuRemoved)
Q_DECLARE_METATYPE(Aerial::Settings::MenuRemovedList)
Q_DECLARE_METATYPE(Aerial::Settings::MenuEvent)
Q_DECLARE_METATYPE(Aerial::Settings::MenuEvents)
