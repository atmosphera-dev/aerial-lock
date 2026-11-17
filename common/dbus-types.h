#pragma once
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QVariantMap>

namespace Aerial {
using SourceList = QList<QVariantMap>;
inline void registerBusTypes()
{
    qDBusRegisterMetaType<SourceList>();
    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
}
}
