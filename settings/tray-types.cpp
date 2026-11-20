#include "tray-types.h"
#include <QDBusMetaType>
namespace Aerial::Settings {
// The read/write operators must preserve each protocol's exact structure order.
QDBusArgument &operator<<(QDBusArgument &a, const TrayImage &v) { a.beginStructure(); a << v.width << v.height << v.bytes; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, TrayImage &v) { a.beginStructure(); a >> v.width >> v.height >> v.bytes; a.endStructure(); return a; }
QDBusArgument &operator<<(QDBusArgument &a, const TrayTooltip &v) { a.beginStructure(); a << v.icon << v.images << v.title << v.text; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, TrayTooltip &v) { a.beginStructure(); a >> v.icon >> v.images >> v.title >> v.text; a.endStructure(); return a; }
QDBusArgument &operator<<(QDBusArgument &a, const MenuLayout &v) { a.beginStructure(); a << v.id << v.properties << v.children; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, MenuLayout &v) { a.beginStructure(); a >> v.id >> v.properties >> v.children; a.endStructure(); return a; }
QDBusArgument &operator<<(QDBusArgument &a, const MenuProperties &v) { a.beginStructure(); a << v.id << v.properties; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, MenuProperties &v) { a.beginStructure(); a >> v.id >> v.properties; a.endStructure(); return a; }
QDBusArgument &operator<<(QDBusArgument &a, const MenuRemoved &v) { a.beginStructure(); a << v.id << v.names; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, MenuRemoved &v) { a.beginStructure(); a >> v.id >> v.names; a.endStructure(); return a; }
QDBusArgument &operator<<(QDBusArgument &a, const MenuEvent &v) { a.beginStructure(); a << v.id << v.event << v.data << v.timestamp; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, MenuEvent &v) { a.beginStructure(); a >> v.id >> v.event >> v.data >> v.timestamp; a.endStructure(); return a; }
void registerTrayTypes()
{
    qRegisterMetaType<MenuIds>("Aerial::Settings::MenuIds");
    qDBusRegisterMetaType<TrayImage>(); qDBusRegisterMetaType<TrayImages>(); qDBusRegisterMetaType<TrayTooltip>();
    qDBusRegisterMetaType<MenuLayout>(); qDBusRegisterMetaType<MenuProperties>(); qDBusRegisterMetaType<MenuPropertiesList>();
    qDBusRegisterMetaType<MenuRemoved>(); qDBusRegisterMetaType<MenuRemovedList>();
    qDBusRegisterMetaType<MenuEvent>(); qDBusRegisterMetaType<MenuEvents>(); qDBusRegisterMetaType<MenuIds>();
}
}
