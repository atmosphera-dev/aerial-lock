#include "client-support.h"
#include <QDBusArgument>
#include <QDBusVariant>
#include <QDir>
#include <unistd.h>

namespace Aerial::Client {
QVariant unpack(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>()) return unpack(value.value<QDBusVariant>().variant());
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const auto argument = value.value<QDBusArgument>();
        // asVariant() duplicates a structure without decoding it. Recursing on
        // that unchanged value overflows the stack (e.g. systemd Unit.Job).
        // Preserve future/unknown structured properties as opaque wire values.
        if (argument.currentType() == QDBusArgument::StructureType) return value;
        if (argument.currentType() == QDBusArgument::MapType) return unpack(QVariant::fromValue(qdbus_cast<QVariantMap>(argument)));
        if (argument.currentType() == QDBusArgument::ArrayType) {
            QVariantList result; argument.beginArray();
            while (!argument.atEnd()) result.append(unpack(argument.asVariant()));
            argument.endArray(); return result;
        }
        return unpack(argument.asVariant());
    }
    if (value.metaType() == QMetaType::fromType<SourceList>()) {
        QVariantList result;
        for (const auto &row : value.value<SourceList>()) result.append(unpack(row));
        return result;
    }
    if (value.metaType().id() == QMetaType::QVariantMap) {
        auto map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) it.value() = unpack(it.value());
        return map;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        auto list = value.toList(); for (auto &item : list) item = unpack(item); return list;
    }
    return value;
}

QJsonObject settingPatch(const QString &key, QJsonValue value, bool *valid)
{
    const auto parts = key.split('.');
    for (const auto &part : parts) if (part.isEmpty()) { if (valid) *valid = false; return {}; }
    for (auto it = parts.crbegin(); it != parts.crend(); ++it) value = QJsonObject{{*it, value}};
    if (valid) *valid = true;
    return value.toObject();
}

bool mayActivate(const ConfigPaths &paths, const QString &installedBundle)
{
    return isDefaultProfile(paths, installedBundle, QDir::homePath(), "/run/user/" + QString::number(getuid()));
}

QDBusMessage propertyCall(const QString &owner, const QString &path, const QString &interface)
{
    auto call = QDBusMessage::createMethodCall(owner, path, Properties, "GetAll");
    call.setAutoStartService(false);
    call << interface;
    return call;
}
}
