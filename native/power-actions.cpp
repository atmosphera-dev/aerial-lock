#include "power-actions.h"
#include <QDBusArgument>
#include <QDBusError>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <algorithm>

namespace {
const QString serviceName = QStringLiteral("org.freedesktop.login1");
const QString objectPath = QStringLiteral("/org/freedesktop/login1");
const QString manager = QStringLiteral("org.freedesktop.login1.Manager");
const std::array<QString, 4> ids{{"suspend", "hibernate", "reboot", "powerOff"}};
const std::array<QString, 4> canMethods{{"CanSuspend", "CanHibernate", "CanReboot", "CanPowerOff"}};
const std::array<QString, 4> actionMethods{{"SuspendWithFlags", "HibernateWithFlags", "RebootWithFlags", "PowerOffWithFlags"}};
QString clipped(const QString &value, qsizetype limit) {
    const auto text = value.simplified();
    const auto characters = text.toStdU32String();
    if (qsizetype(characters.size()) <= limit) return text;
    return QString::fromUcs4(characters.data(), limit - 3) + "...";
}
}

PowerActions::PowerActions(QObject *parent) : PowerActions(QDBusConnection::systemBus(), parent) {}
PowerActions::PowerActions(const QDBusConnection &connection, QObject *parent)
    : QObject(parent), bus(connection), watcher(serviceName, connection, QDBusServiceWatcher::WatchForOwnerChange) {
    connect(&watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &PowerActions::ownerChanged);
    poll.setInterval(5000);
    connect(&poll, &QTimer::timeout, this, &PowerActions::refresh);
}

bool PowerActions::capable(int index) const {
    return active() && phase != "uncertain" && !owner.isEmpty() && capabilities[index] == "yes";
}
QString PowerActions::reason(int index) const { return active() ? capabilityReason(capabilities[index]) : QStringLiteral("unavailable"); }
QString PowerActions::pendingAction() const { return pending < 0 ? QString{} : ids[pending]; }
void PowerActions::setEnabled(bool value) { if (allowed != value) { allowed = value; activationChanged(); } }
void PowerActions::setSecure(bool value) { if (covered != value) { covered = value; activationChanged(); } }
void PowerActions::activationChanged() {
    if (active()) { poll.start(); refresh(); }
    else { poll.stop(); retire(); }
    emit changed();
}

void PowerActions::call(const QString &service, const QString &interface, const QString &method,
                        const QList<QVariant> &arguments, std::function<void(const QDBusMessage &)> complete) {
    auto message = QDBusMessage::createMethodCall(service, objectPath, interface, method);
    message.setArguments(arguments);
    message.setInteractiveAuthorizationAllowed(false);
    message.setAutoStartService(false);
    auto *pendingCall = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(pendingCall, &QDBusPendingCallWatcher::finished, this, [pendingCall, complete = std::move(complete)] {
        const auto reply = pendingCall->reply();
        pendingCall->deleteLater();
        complete(reply);
    });
}

void PowerActions::resolveOwner() {
    if (resolving || !active()) return;
    resolving = true;
    const auto epoch = ownerEpoch;
    auto message = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner");
    message << serviceName;
    message.setInteractiveAuthorizationAllowed(false);
    auto *query = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(query, &QDBusPendingCallWatcher::finished, this, [this, query, epoch] {
        QDBusPendingReply<QString> reply = *query;
        query->deleteLater();
        if (epoch != ownerEpoch) return;
        resolving = false;
        if (!active()) return;
        if (reply.isError()) {
            capabilities.fill("service"); emit changed();
        } else adoptOwner(reply.value());
    });
}

void PowerActions::subscribe(const QString &name, bool attach) {
    if (name.isEmpty()) return;
    const auto wire = [&](const QString &interface, const QString &signal, const char *slot) {
        if (attach) bus.connect(name, objectPath, interface, signal, this, slot);
        else bus.disconnect(name, objectPath, interface, signal, this, slot);
    };
    wire(manager, "PrepareForSleep", SLOT(prepareSleep(bool)));
    wire(manager, "PrepareForShutdown", SLOT(prepareShutdown(bool)));
    wire(manager, "SessionNew", SLOT(sessionsChanged(QString,QDBusObjectPath)));
    wire(manager, "SessionRemoved", SLOT(sessionsChanged(QString,QDBusObjectPath)));
    wire(manager, "UserNew", SLOT(usersChanged(uint,QDBusObjectPath)));
    wire(manager, "UserRemoved", SLOT(usersChanged(uint,QDBusObjectPath)));
    wire("org.freedesktop.DBus.Properties", "PropertiesChanged", SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
}

void PowerActions::ownerChanged(const QString &, const QString &, const QString &current) { adoptOwner(current); }
void PowerActions::adoptOwner(const QString &current) {
    if (owner != current) {
        subscribe(owner, false);
        ++ownerEpoch; ++snapshotEpoch; ++transactionEpoch;
        refreshing = dirty = resolving = false;
        capabilities.fill("?pending");
        if (pending >= 0) {
            if (sent) { phase = "uncertain"; failure = "uncertain"; }
            else { pending = -1; phase = "idle"; failure = "service"; }
        }
        owner = current;
        subscribe(owner, true);
    }
    emit changed();
    if (active() && !owner.isEmpty()) { snapshotPreparing(); refresh(); }
}

void PowerActions::snapshotPreparing() {
    const auto epoch = ownerEpoch;
    call(owner, "org.freedesktop.DBus.Properties", "GetAll", {manager}, [this, epoch](const QDBusMessage &message) {
        if (epoch != ownerEpoch || !active()) return;
        QDBusPendingReply<QVariantMap> reply(message);
        if (reply.isError()) return; // Optional lifecycle snapshot never grants permission.
        const auto values = reply.value();
        if (values.value("PreparingForSleep").metaType() == QMetaType::fromType<bool>())
            prepared(values.value("PreparingForSleep").toBool(), true);
        if (values.value("PreparingForShutdown").metaType() == QMetaType::fromType<bool>())
            prepared(values.value("PreparingForShutdown").toBool(), false);
    });
}

void PowerActions::refresh() {
    if (!active()) return;
    if (owner.isEmpty()) { resolveOwner(); return; }
    if (refreshing) { dirty = true; return; }
    refreshing = true; dirty = false; remaining = 4;
    const auto epoch = ++snapshotEpoch;
    for (int index = 0; index < 4; ++index) {
        call(owner, manager, canMethods[index], {}, [this, epoch, index](const QDBusMessage &message) {
            if (epoch != snapshotEpoch || !active()) return;
            QDBusPendingReply<QString> reply(message);
            capabilities[index] = reply.isError() ? QStringLiteral("service") : reply.value();
            emit changed();
            if (--remaining == 0) {
                refreshing = false;
                if (dirty) { dirty = false; QTimer::singleShot(0, this, &PowerActions::refresh); }
            }
        });
    }
}

void PowerActions::request(const QString &action) {
    if (!active() || pending >= 0) return;
    auto found = std::find(ids.begin(), ids.end(), action);
    if (found == ids.end()) { ++transactionEpoch; failure = "unavailable"; emit changed(); return; }
    const int index = int(found - ids.begin());
    lastAction = action; who.clear(); why.clear();
    ++transactionEpoch;
    if (!capable(index)) {
        failure = reason(index); emit changed();
        if (reason(index) == "inhibited" && !owner.isEmpty()) {
            // Already refused: enrich the failure without starting an operation
            // or disturbing independent authentication. There is no pending intent.
            inhibited(transactionEpoch, owner, index);
        } else refresh();
        return;
    }
    failure.clear(); pending = index; sent = preparationSeen = false;
    if (index >= 2) { phase = "confirming"; emit changed(); }
    else check();
}
void PowerActions::confirm() { if (phase == "confirming" && active()) check(); }
void PowerActions::cancel() { if (phase == "confirming" || phase == "checking") finish(); }
void PowerActions::clearError() {
    if (!failure.isEmpty()) { ++transactionEpoch; failure.clear(); lastAction.clear(); who.clear(); why.clear(); emit changed(); }
}
void PowerActions::finish(const QString &error) {
    ++transactionEpoch;
    phase = "idle"; pending = -1; sent = preparationSeen = false; failure = error;
    if (error.isEmpty()) { lastAction.clear(); who.clear(); why.clear(); }
    emit changed();
    refresh();
}
void PowerActions::retire() {
    ++ownerEpoch; ++snapshotEpoch; ++transactionEpoch;
    resolving = refreshing = dirty = false;
    capabilities.fill("?pending");
    if (sent) { phase = "uncertain"; failure = "uncertain"; }
    else { pending = -1; phase = "idle"; }
}

void PowerActions::check() {
    if (!active() || pending < 0 || owner.isEmpty()) { finish("service"); return; }
    phase = "checking"; emit changed();
    const auto transaction = transactionEpoch;
    const auto service = owner;
    call(service, manager, canMethods[pending], {}, [this, transaction, service](const QDBusMessage &message) {
        if (transaction != transactionEpoch || !active() || service != owner) return;
        QDBusPendingReply<QString> capability(message);
        if (capability.isError()) { finish(dbusReason(message)); return; }
        capabilities[pending] = capability.value();
        if (capability.value() != "yes") {
            const auto reason = capabilityReason(capability.value());
            if (reason == "inhibited") inhibited(transaction, service, pending);
            else finish(reason);
            return;
        }
        call(service, manager, "ListInhibitors", {}, [this, transaction, service](const QDBusMessage &reply) {
            if (transaction != transactionEpoch || !active() || service != owner) return;
            if (reply.type() == QDBusMessage::ErrorMessage) { finish(dbusReason(reply)); return; }
            if (reply.signature() != "a(ssssuu)" || reply.arguments().size() != 1) { finish("failed"); return; }
            if (readBlocker(reply, pending)) { finish("inhibited"); return; }
            dispatch(transaction, service);
        });
    });
}

void PowerActions::dispatch(quint64 transaction, const QString &service) {
    if (transaction != transactionEpoch || !active() || service != owner || pending < 0) return;
    sent = true; preparationSeen = false; phase = "executing"; emit changed();
    // ROOT_CHECK_INHIBITORS only. Never SKIP_INHIBITORS, force or interactive.
    call(service, manager, actionMethods[pending], {QVariant::fromValue(qulonglong(1))},
         [this, transaction, service](const QDBusMessage &message) {
        if (transaction != transactionEpoch || service != owner || !active()) return;
        if (message.type() == QDBusMessage::ErrorMessage) {
            const auto code = dbusReason(message);
            qWarning().noquote() << "power-action:" << message.errorName();
            if (code == "service" || code == "uncertain") { phase = "uncertain"; failure = "uncertain"; emit changed(); }
            else if (code == "inhibited") inhibited(transaction, service, pending);
            else finish(code);
        } else if (!message.arguments().isEmpty()) finish("failed");
        else { phase = "waiting"; emit changed(); }
    });
}

void PowerActions::inhibited(quint64 transaction, const QString &service, int action) {
    if (transaction != transactionEpoch || !active() || service != owner) return;
    // Definite refusal ends the operation before optional metadata is fetched.
    // Authentication must never depend on another application's detail reply.
    const auto detailEpoch = transactionEpoch + 1;
    finish("inhibited");
    if (detailEpoch != transactionEpoch || !active() || service != owner || failure != "inhibited" || lastAction != ids[action]) return;
    call(service, manager, "ListInhibitors", {}, [this, detailEpoch, service, action](const QDBusMessage &reply) {
        if (detailEpoch != transactionEpoch || !active() || service != owner || failure != "inhibited") return;
        readBlocker(reply, action);
        emit changed();
    });
}

void PowerActions::prepared(bool preparing, bool sleep) {
    if (pending >= 0 && sent && ((pending < 2) == sleep)) {
        if (preparing) preparationSeen = true;
        else if (preparationSeen) finish(sleep ? QString{} : QStringLiteral("cancelled"));
    }
    if (!preparing) refresh();
}
bool PowerActions::readBlocker(const QDBusMessage &message, int action) {
    who.clear(); why.clear();
    if (action < 0 || message.signature() != "a(ssssuu)" || message.arguments().size() != 1) return false;
    const auto list = qvariant_cast<QDBusArgument>(message.arguments()[0]);
    bool found = false;
    list.beginArray();
    while (!list.atEnd()) {
        QString what, application, reason, mode;
        uint uid, pid;
        list.beginStructure(); list >> what >> application >> reason >> mode >> uid >> pid; list.endStructure();
        if (!found && (mode == "block" || mode == "block-weak") && what.split(':').contains(action < 2 ? "sleep" : "shutdown")) {
            found = true; who = clipped(application, 40); why = clipped(reason, 80);
        }
    }
    list.endArray();
    return found;
}
void PowerActions::prepareSleep(bool preparing) { prepared(preparing, true); }
void PowerActions::prepareShutdown(bool preparing) { prepared(preparing, false); }
void PowerActions::sessionsChanged(const QString &, const QDBusObjectPath &) { refresh(); }
void PowerActions::usersChanged(uint, const QDBusObjectPath &) { refresh(); }
void PowerActions::propertiesChanged(const QString &interface, const QVariantMap &, const QStringList &) {
    if (interface == manager) refresh();
}

QString PowerActions::capabilityReason(const QString &value) {
    if (value == "yes") return {};
    if (value == "?pending") return "unknown";
    if (value == "na") return "unsupported";
    if (value == "challenge") return "authorization";
    if (value == "no") return "denied";
    if (value == "inhibited" || value == "inhibitor-blocked" || value == "challenge-inhibitor-blocked" || value == "inhibited-blocked") return "inhibited";
    return value == "service" ? QStringLiteral("service") : QStringLiteral("unavailable");
}
QString PowerActions::dbusReason(const QDBusMessage &message) {
    const auto name = message.errorName();
    if (name.endsWith("BlockedByInhibitorLock")) return "inhibited";
    if (name.endsWith("InteractiveAuthorizationRequired")) return "authorization";
    if (name.endsWith("AccessDenied") || name.endsWith("NotAuthorized")) return "denied";
    if (name.endsWith("OperationInProgress")) return "in-progress";
    if (name.endsWith("SleepVerbNotSupported") || name.endsWith("UnknownMethod")) return "unsupported";
    if (name.endsWith("NoReply") || name.endsWith("Timeout")) return "uncertain";
    if (name.endsWith("ServiceUnknown") || name.endsWith("NameHasNoOwner") || name.endsWith("Disconnected")) return "service";
    return "failed";
}

#include "power-actions.moc"
