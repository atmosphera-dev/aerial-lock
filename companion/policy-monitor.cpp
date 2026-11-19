#include "policy-monitor.h"
#include "policy-monitor.moc"
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QJsonArray>
#include <QSet>
#include <QTimer>
#include <memory>
#include <algorithm>

namespace Aerial {
namespace {
const QString NM = QStringLiteral("org.freedesktop.NetworkManager");
const QString NMPath = QStringLiteral("/org/freedesktop/NetworkManager");
const QString Active = NM + ".Connection.Active";
const QString Device = NM + ".Device";
const QString UP = QStringLiteral("org.freedesktop.UPower");
const QString UPPath = QStringLiteral("/org/freedesktop/UPower");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");
QString objectKey(const QString &service, const QString &path, const QString &interface)
{
    return service + '\n' + path + '\n' + interface;
}
QString path(const QVariant &value) { return qvariant_cast<QDBusObjectPath>(value).path(); }
QList<QDBusObjectPath> paths(const QVariant &value) { return qdbus_cast<QList<QDBusObjectPath>>(value); }
QString connectionType(const QString &type)
{
    if (type.isEmpty()) return "unknown";
    if (type == "802-3-ethernet") return "ethernet";
    if (type == "802-11-wireless") return "wifi";
    if (type == "gsm" || type == "cdma") return "cellular";
    if (type == "bluetooth") return "bluetooth";
    if (type == "vpn" || type == "wireguard") return "vpn";
    return "other";
}
QString metering(const QVariant &value)
{
    // NMMetered: YES/GUESS_YES and NO/GUESS_NO; unknown stays unknown.
    switch (value.toUInt()) {
    case 1: case 3: return "metered";
    case 2: case 4: return "unmetered";
    default: return "unknown";
    }
}
}

PolicyMonitor::PolicyMonitor(QDBusConnection bus, QObject *parent) : QObject(parent), bus(std::move(bus)) {}

void PolicyMonitor::start()
{
    if (started) return;
    started = true;
    auto *watcher = new QDBusServiceWatcher(NM, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    watcher->addWatchedService(UP);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
        [this](const QString &service, const QString &, const QString &) { ownerChanged(service); });
    // Install all name/property subscriptions before taking any snapshot.
    watch(NM, NMPath, NM);
    watch(UP, UPPath, UP);
}

QString PolicyMonitor::watch(const QString &service, const QString &path, const QString &interface)
{
    if (path.isEmpty() || path == "/") return {};
    const auto key = objectKey(service, path, interface);
    if (objects.contains(key)) return key;
    Object object; object.service = service; object.path = path; object.interface = interface; object.serial = ++serial;
    objects.insert(key, object);
    bus.connect(service, path, Properties, "PropertiesChanged", this,
        SLOT(propertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
    sample(key);
    return key;
}

void PolicyMonitor::sample(const QString &key)
{
    auto &object = objects[key];
    if (object.pending) { object.dirty = true; return; }
    object.pending = true; object.dirty = false;
    const auto epoch = object.serial;
    auto request = QDBusMessage::createMethodCall(object.service, object.path, Properties, "GetAll");
    request.setAutoStartService(false);
    request << object.interface;
    auto *call = new QDBusPendingCallWatcher(bus.asyncCall(request, 1000), this);
    const auto handled = std::make_shared<bool>(false);
    const auto complete = [this, call, key, epoch, handled] {
        if (*handled) return;
        *handled = true;
        QDBusPendingReply<QVariantMap> reply = *call;
        call->deleteLater();
        auto it = objects.find(key);
        if (it == objects.end() || it->serial != epoch) return;
        it->pending = false;
        // A signal received during GetAll makes that reply potentially stale.
        if (it->dirty) { sample(key); return; }
        if (!reply.isError()) { it->values = reply.value(); it->invalidated.clear(); }
        else {
            // A failed read says nothing about properties already observed
            // from this owner. Owner replacement, not a transport error,
            // invalidates that cache. Retry without discarding its topology.
            QTimer::singleShot(1000, this, [this, key, epoch] {
                const auto object = objects.constFind(key);
                if (object != objects.cend() && object->serial == epoch && !object->pending) sample(key);
            });
        }
        it->sampled = true;
        topology();
        emit changed();
    };
    connect(call, &QDBusPendingCallWatcher::finished, this, complete);
    // A disconnected optional bus can return an already-completed error with
    // no later finished signal. Settle both forms through the same once-only
    // path, after startup has installed every policy subscription.
    if (call->isFinished()) QTimer::singleShot(0, this, complete);
}

void PolicyMonitor::propertiesChanged(const QString &interface, const QVariantMap &updated,
    const QStringList &invalidated, const QDBusMessage &message)
{
    // Qt resolves the subscribed well-known sender; only watched paths/interfaces
    // enter this cache. Re-snapshot on invalidation without synchronous proxies.
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        if (it->path != message.path() || it->interface != interface) continue;
        for (auto value = updated.begin(); value != updated.end(); ++value) {
            it->values[value.key()] = value.value(); it->invalidated.remove(value.key());
        }
        // Keep the previous restrictive observation until a fresh sample, and
        // mark the invalidated topology/property unresolved. Ordinary refreshes
        // do not revoke readiness or interrupt a healthy transfer.
        for (const auto &name : invalidated) it->invalidated.insert(name);
        sample(it.key());
        break;
    }
    topology();
    emit changed();
}

QVariantMap PolicyMonitor::values(const QString &service, const QString &path, const QString &interface) const
{
    return objects.value(objectKey(service, path, interface)).values;
}

void PolicyMonitor::topology()
{
    const auto manager = values(NM, NMPath, NM);
    QSet<QString> connections;
    const auto primary = path(manager.value("PrimaryConnection"));
    if (!primary.isEmpty() && primary != "/") connections.insert(primary);
    for (const auto &active : paths(manager.value("ActiveConnections"))) connections.insert(active.path());
    QSet<QString> wanted{objectKey(NM, NMPath, NM), objectKey(UP, UPPath, UP)};
    for (const auto &connection : connections) {
        const auto key = watch(NM, connection, Active);
        if (key.isEmpty()) continue;
        wanted.insert(key);
        for (const auto &device : paths(values(NM, connection, Active).value("Devices"))) {
            const auto deviceKey = watch(NM, device.path(), Device);
            if (!deviceKey.isEmpty()) wanted.insert(deviceKey);
        }
    }
    for (auto it = objects.begin(); it != objects.end();) {
        if (wanted.contains(it.key())) { ++it; continue; }
        bus.disconnect(it->service, it->path, Properties, "PropertiesChanged", this,
            SLOT(propertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
        it = objects.erase(it);
    }
}

void PolicyMonitor::ownerChanged(const QString &service)
{
    for (auto it = objects.begin(); it != objects.end();) {
        if (it->service != service) { ++it; continue; }
        bus.disconnect(it->service, it->path, Properties, "PropertiesChanged", this,
            SLOT(propertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
        it = objects.erase(it);
    }
    if (service == NM) watch(NM, NMPath, NM);
    else watch(UP, UPPath, UP);
    emit changed();
}

bool PolicyMonitor::ready() const
{
    if (!started || objects.isEmpty()) return false;
    // Background refresh does not revoke a settled observation. Only a new
    // owner/object that has never been sampled is still initializing.
    for (const auto &object : objects) if (!object.sampled || !object.invalidated.isEmpty()) return false;
    return true;
}

QJsonObject PolicyMonitor::observations() const
{
    const auto manager = values(NM, NMPath, NM);
    const auto state = manager.value("State").toUInt(), connectivity = manager.value("Connectivity").toUInt();
    QString online = "unknown";
    if ((state >= 10 && state < 70) || (connectivity >= 1 && connectivity <= 3)) online = "offline";
    else if (state == 70 || connectivity == 4) online = "online";
    const auto primaryPath = path(manager.value("PrimaryConnection"));
    const auto primary = values(NM, primaryPath, Active);
    auto type = connectionType(primary.value("Type", manager.value("PrimaryConnectionType")).toString());
    QSet<QString> types;
    auto metered = metering(manager.value("Metered"));
    QSet<QString> routes;
    if (!primaryPath.isEmpty() && primaryPath != "/") { routes.insert(primaryPath); types.insert(type); }
    for (const auto &active : paths(manager.value("ActiveConnections"))) {
        const auto connection = values(NM, active.path(), Active);
        if (connection.value("State").toUInt() == 2
            && (connection.value("Default").toBool() || connection.value("Default6").toBool())) {
            routes.insert(active.path());
            const auto routeType = connection.value("Vpn").toBool() ? QString("vpn") : connectionType(connection.value("Type").toString());
            types.insert(routeType);
            if (routeType == "vpn") type = "vpn";
        }
    }
    // PrimaryConnection remains the underlying route for default-route VPNs.
    // A positive metering observation there cannot be hidden by the tunnel.
    for (const auto &route : routes)
        for (const auto &device : paths(values(NM, route, Active).value("Devices"))) {
            const auto observed = metering(values(NM, device.path(), Device).value("Metered"));
            if (observed == "metered" || (metered == "unknown" && observed == "unmetered")) metered = observed;
        }
    const auto power = values(UP, UPPath, UP).value("OnBattery");
    const auto powerState = power.metaType().id() == QMetaType::Bool ? power.toBool() ? "battery" : "ac" : "unknown";
    if (types.isEmpty()) types.insert(type);
    auto orderedTypes = types.values(); std::sort(orderedTypes.begin(), orderedTypes.end());
    return {{"network", online}, {"connectionType", type}, {"connectionTypes", QJsonArray::fromStringList(orderedTypes)},
        {"metering", metered}, {"power", powerState}};
}

QString PolicyMonitor::deferReason(const QJsonObject &settings) const
{
    if (!ready()) return "Initializing";
    const auto automatic = settings.value("acquisition").toObject().value("automatic").toObject();
    const auto observed = observations();
    if (observed.value("network") == "offline") return "Offline";
    for (const auto &type : observed.value("connectionTypes").toArray())
        if (!automatic.value("connectionTypes").toObject().value(type.toString()).toBool()) return "ConnectionTypeDisallowed";
    if (observed.value("metering") == "metered" && !automatic.value("allowMetered").toBool()) return "Metered";
    if (!automatic.value("power").toObject().value(observed.value("power").toString()).toBool()) return "PowerDisallowed";
    return {};
}
}
