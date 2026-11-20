#include "systemd-controller.h"
#include "async-call.h"
#include "common/client/client-support.h"
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QTimer>

namespace Aerial::Settings {
namespace {
constexpr auto Manager = "org.freedesktop.systemd1";
constexpr auto ManagerPath = "/org/freedesktop/systemd1";
constexpr auto ManagerInterface = "org.freedesktop.systemd1.Manager";
const QStringList allowed{"aerial.service", "aerial-idle.service", "aerial-tray.service"};
bool enabled(const QString &state) { return state == "enabled" || state == "enabled-runtime"; }
// Startup that survives logins. Runtime enablement ends with this boot.
bool persistent(const QString &state) { return state == "enabled"; }
}
SystemdController::SystemdController(QDBusConnection bus, QObject *parent, int operationTimeoutMs) : QObject(parent), bus(std::move(bus)), operationTimeoutMs(operationTimeoutMs)
{
    auto *watcher = new QDBusServiceWatcher(Manager, this->bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &SystemdController::ownerChanged);
    // Qt can cache a failed well-known-owner lookup permanently. Subscribe by
    // path/interface and authenticate each signal against our discovered owner.
    this->bus.connect({}, {}, "org.freedesktop.DBus.Properties", "PropertiesChanged", this, SLOT(propertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
    this->bus.connect({}, ManagerPath, ManagerInterface, "UnitFilesChanged", this, SLOT(filesChanged(QDBusMessage)));
    this->bus.connect({}, ManagerPath, ManagerInterface, "JobRemoved", this, SLOT(jobRemoved(uint,QDBusObjectPath,QString,QString,QDBusMessage)));
    for (const auto &signal : {"UnitNew", "UnitRemoved"})
        this->bus.connect({}, ManagerPath, ManagerInterface, signal, this, SLOT(unitChanged(QString,QDBusObjectPath,QDBusMessage)));
    discover();
}
void SystemdController::discover()
{
    const auto epoch = generation;
    discoverOwner(this, bus, QString(Manager), [this, epoch] { return epoch == generation; }, [this](const QDBusMessage &message) {
        QDBusPendingReply<QString> reply(message);
        if (reply.isError()) {
            problem = "No systemd user manager. Start aerial manually; use compositor autostart for aerial-idle and aerial-settings --tray."; emit changed();
        }
        else adopt(reply.value());
    });
}
void SystemdController::ownerChanged(const QString &, const QString &, const QString &next) { adopt(next); }
void SystemdController::adopt(const QString &next)
{
    ++generation; owner = next; connected = false; states.clear(); samples.clear();
    const auto previous = operations; operations.clear();
    for (auto it = previous.begin(); it != previous.end(); ++it) emit operationFinished(it.key(), it->kind, false);
    if (owner.isEmpty()) { problem = "The systemd user manager is unavailable. Settings still work; start the programs manually or from compositor autostart."; emit changed(); return; }
    subscribe();
}
void SystemdController::subscribe()
{
    const auto epoch = generation;
    call("Subscribe", {}, [this, epoch](const QDBusMessage &message) {
        if (message.type() == QDBusMessage::ErrorMessage && message.errorName() != "org.freedesktop.systemd1.AlreadySubscribed") {
            problem = "Couldn't observe the user manager: " + message.errorName().section('.', -1); emit changed();
            QTimer::singleShot(1000, this, [this, epoch] { if (epoch == generation && !connected && !owner.isEmpty()) subscribe(); });
            return;
        }
        connected = true; problem.clear(); refresh();
    });
}
void SystemdController::call(const QString &method, const QVariantList &arguments, std::function<void(const QDBusMessage &)> complete)
{
    const auto epoch = generation;
    auto request = QDBusMessage::createMethodCall(owner, ManagerPath, ManagerInterface, method); request.setAutoStartService(false); request.setArguments(arguments);
    watch(this, bus.asyncCall(request, 15000), [this, epoch, complete = std::move(complete)](const QDBusMessage &message) { if (epoch == generation) complete(message); });
}
void SystemdController::refresh() { if (connected) for (const auto &unit : allowed) sample(unit); }
void SystemdController::callOperation(const QString &unit, const QString &method, const QVariantList &arguments, std::function<void(const QDBusMessage &)> complete)
{
    if (!operations.contains(unit)) return;
    const auto serial = operations.value(unit).serial;
    call(method, arguments, [this, unit, serial, complete = std::move(complete)](const QDBusMessage &message) {
        if (operations.contains(unit) && operations.value(unit).serial == serial) complete(message);
    });
}
void SystemdController::sample(const QString &unit)
{
    const auto serial = ++samples[unit];
    call("GetUnitFileState", {unit}, [this, unit, serial](const QDBusMessage &message) {
        if (samples.value(unit) != serial) return;
        QDBusPendingReply<QString> file(message);
        QVariantMap state = states.value(unit).toMap();
        const bool missing = file.isError() && file.error().name() == "org.freedesktop.DBus.Error.FileNotFound";
        state["fileState"] = missing ? "not-found" : file.isError() ? "unknown" : file.value();
        state["error"] = file.isError() ? file.error().name().section('.', -1) : QString();
        state["fileError"] = state.value("error");
        if (file.isError() && !missing) retrySample(unit, serial);
        states[unit] = state;
        if (operations.contains(unit) && operations.value(unit).kind == "disable") checkOperation(unit);
        call("GetUnit", {unit}, [this, unit, serial](const QDBusMessage &message) {
            if (samples.value(unit) != serial) return;
            const QDBusPendingReply<QDBusObjectPath> reply(message);
            if (reply.isError()) {
                auto state = states.value(unit).toMap();
                if (reply.error().name() == "org.freedesktop.systemd1.NoSuchUnit") {
                    state["active"] = "inactive"; state["invocation"] = ""; state["path"] = "";
                } else { state["active"] = "unknown"; state["invocation"] = ""; state["error"] = reply.error().name().section('.', -1); retrySample(unit, serial); }
                states[unit] = state; checkOperation(unit); emit changed(); return;
            }
            const auto path = reply.value().path(); const auto epoch = generation;
            auto request = Client::propertyCall(owner, path, "org.freedesktop.systemd1.Unit");
            watch(this, bus.asyncCall(request, 5000), [this, unit, serial, epoch, path](const QDBusMessage &message) {
                if (epoch != generation || samples.value(unit) != serial) return;
                const QDBusPendingReply<QVariantMap> reply(message);
                auto state = states.value(unit).toMap();
                if (reply.isError()) { state["active"] = "unknown"; state["invocation"] = ""; state["error"] = reply.error().name().section('.', -1); retrySample(unit, serial); }
                else {
                    const auto p = Client::unpack(reply.value()).toMap();
                    state["active"] = p.value("ActiveState"); state["load"] = p.value("LoadState");
                    state["invocation"] = QString::fromLatin1(p.value("InvocationID").toByteArray().toHex());
                    state["started"] = p.value("ActiveEnterTimestampMonotonic"); state["path"] = path;
                }
                states[unit] = state; checkOperation(unit); emit changed();
            });
        });
    });
}
void SystemdController::retrySample(const QString &unit, quint64 serial)
{
    const auto epoch = generation;
    QTimer::singleShot(1000, this, [this, unit, serial, epoch] { if (epoch == generation && samples.value(unit) == serial && connected) sample(unit); });
}
void SystemdController::propertiesChanged(const QString &interface, const QVariantMap &, const QStringList &, const QDBusMessage &message)
{
    if (message.service() != owner || interface != "org.freedesktop.systemd1.Unit") return;
    for (const auto &unit : allowed) if (states.value(unit).toMap().value("path").toString() == message.path()) sample(unit);
}
void SystemdController::filesChanged(const QDBusMessage &message) { if (message.service() == owner) refresh(); }
void SystemdController::unitChanged(const QString &unit, const QDBusObjectPath &, const QDBusMessage &message)
{ if (message.service() == owner && allowed.contains(unit)) sample(unit); }
QVariantMap SystemdController::idle() const
{
    const auto state = states.value("aerial-idle.service").toMap();
    const bool on = persistent(state.value("fileState").toString()); const bool active = state.value("active") == "active";
    const auto file = state.value("fileState").toString();
    const bool unknown = file.isEmpty() || file == "unknown" || state.value("active").toString().isEmpty() || state.value("active") == "unknown";
    const bool blocked = file == "masked" || file == "masked-runtime" || file == "not-found";
    QString message = !connected ? "Automatic locking needs aerial-idle. Start it manually or add aerial-idle to compositor autostart."
        : file == "not-found" ? "The aerial-idle user unit is not installed. Install it or start aerial-idle manually."
        : blocked ? "aerial-idle is " + file + ". Check its user unit; Aerial will never unmask it."
        : unknown ? "The current aerial-idle state is unknown. Retrying the user manager read."
        : idleWritesPending ? "Saving idle settings. Wait for the save before starting or restarting aerial-idle."
        : !on ? "Automatic locking needs aerial-idle. Turn it on only if no other idle daemon owns automatic locking."
        : !active ? "aerial-idle is stopped. Start it to use automatic locking."
        : "aerial-idle is running. Its applied settings cannot be confirmed here. Restart uses the profile configured in its user unit.";
    return {{"message", message}, {"action", !connected || blocked || unknown || idleWritesPending || operations.contains("aerial-idle.service") ? QString() : !on ? "enableStart" : !active ? "start" : "restart"},
        {"actionLabel", !on ? "Turn on" : !active ? "Start aerial-idle" : "Restart aerial-idle"}, {"pending", operations.contains("aerial-idle.service")},
        {"observation", unknown ? "aerial-idle activity is unknown." : active ? "aerial-idle is running." : "aerial-idle is not running."}, {"fresh", false}};
}
bool SystemdController::persistentlyEnabled(const QString &unit) const
{ return persistent(states.value(unit).toMap().value("fileState").toString()); }
void SystemdController::setIdleWritesPending(bool pending)
{
    if (idleWritesPending == pending) return;
    idleWritesPending = pending; emit changed();
}
void SystemdController::operate(const QString &unit, const QString &operation)
{
    if (!allowed.contains(unit) || !QStringList{"start", "restart", "enableStart", "disable"}.contains(operation) || operations.contains(unit)) return;
    if (unit == "aerial-idle.service" && idleWritesPending && operation != "disable") return;
    if (!connected) { problem = "No systemd user manager. Run " + (unit == "aerial-tray.service" ? QString("aerial-settings --tray") : unit.section('.', 0, 0)) + " manually."; emit changed(); emit operationFinished(unit, operation, false); return; }
    if (states.value(unit).toMap().value("fileState").toString().startsWith("masked")) { problem = unit + " is masked. Unmask it yourself only if intended, then try again."; emit changed(); emit operationFinished(unit, operation, false); return; }
    const auto serial = ++operationSerial, epoch = generation;
    operations[unit] = {operation, {}, states.value(unit).toMap().value("invocation").toString(), false, {}, serial};
    QTimer::singleShot(operationTimeoutMs, this, [this, unit, serial, epoch] {
        if (epoch != generation || !operations.contains(unit) || operations.value(unit).serial != serial) return;
        finish(unit, false, "Completion was not confirmed before the deadline. Check the unit state before trying again.");
        sample(unit);
    });
    problem.clear(); emit changed();
    if (operation == "start" || operation == "restart") { startJob(unit); return; }
    const bool enable = operation == "enableStart";
    const auto changedFiles = [this, unit, enable](const QDBusMessage &message) {
        if (message.type() == QDBusMessage::ErrorMessage) { finish(unit, false, message.errorName().section('.', -1)); return; }
        callOperation(unit, "Reload", {}, [this, unit, enable](const QDBusMessage &message) {
            if (message.type() == QDBusMessage::ErrorMessage) { finish(unit, false, message.errorName().section('.', -1)); return; }
            if (enable) startJob(unit);
            else { operations[unit].jobDone = true; sample(unit); }
        });
    };
    if (enable) callOperation(unit, "EnableUnitFiles", {QStringList{unit}, false, false}, changedFiles);
    else callOperation(unit, "DisableUnitFiles", {QStringList{unit}, false}, [this, unit, changedFiles](const QDBusMessage &message) {
        if (message.type() == QDBusMessage::ErrorMessage) { changedFiles(message); return; }
        // Both directories can contain links while GetUnitFileState reports
        // only the higher-priority one. Remove both, then resample after Reload.
        callOperation(unit, "DisableUnitFiles", {QStringList{unit}, true}, changedFiles);
    });
}
void SystemdController::startJob(const QString &unit)
{
    if (!operations.contains(unit)) return;
    callOperation(unit, operations.value(unit).kind == "restart" ? "RestartUnit" : "StartUnit", {unit, "replace"}, [this, unit](const QDBusMessage &message) {
        const QDBusPendingReply<QDBusObjectPath> reply(message);
        if (reply.isError()) { finish(unit, false, reply.error().name().section('.', -1)); return; }
        if (!operations.contains(unit)) return;
        operations[unit].job = reply.value().path();
        if (operations[unit].earlyJobs.contains(reply.value().path())) {
            const auto result = operations[unit].earlyJobs.take(reply.value().path());
            if (result != "done") { finish(unit, false, "Job " + result); return; }
            operations[unit].jobDone = true; sample(unit);
        }
    });
}
void SystemdController::jobRemoved(uint, const QDBusObjectPath &job, const QString &unit, const QString &result, const QDBusMessage &message)
{
    if (message.service() != owner || !operations.contains(unit)) return;
    if (operations.value(unit).job.isEmpty()) { operations[unit].earlyJobs[job.path()] = result; return; }
    if (operations.value(unit).job != job.path()) return;
    if (result != "done") { finish(unit, false, "Job " + result); return; }
    operations[unit].jobDone = true; sample(unit);
}
void SystemdController::checkOperation(const QString &unit)
{
    if (!operations.contains(unit) || !operations.value(unit).jobDone) return;
    const auto op = operations.value(unit); const auto state = states.value(unit).toMap();
    if (!state.value("fileError").toString().isEmpty()) { finish(unit, false, state.value("fileError").toString()); return; }
    if (op.kind == "disable") { finish(unit, !state.value("fileState").toString().isEmpty() && state.value("fileState") != "unknown" && !enabled(state.value("fileState").toString()), "The unit is still enabled or its file state is unknown."); return; }
    if (!state.value("error").toString().isEmpty()) { finish(unit, false, state.value("error").toString()); return; }
    if (op.kind == "enableStart" && !persistent(state.value("fileState").toString())) { finish(unit, false, "The unit is not persistently enabled."); return; }
    const auto invocation = state.value("invocation").toString();
    if (state.value("active") != "active") { finish(unit, false, "The job finished but the unit is not active."); return; }
    if (unit == "aerial-idle.service") {
        if (invocation.isEmpty() || (op.kind == "restart" && invocation == op.beforeInvocation)) { finish(unit, false, "No new idle invocation was observed."); return; }
        // A job proves a lifecycle transition, not which profile/configuration
        // the running daemon consumed. Never infer configuration acknowledgement.
    }
    finish(unit, true);
}
void SystemdController::finish(const QString &unit, bool success, const QString &error)
{
    if (!operations.contains(unit)) return;
    const auto op = operations.take(unit); problem = success ? QString() : "Couldn't " + op.kind + " " + unit + ": " + error;
    emit changed(); emit operationFinished(unit, op.kind, success);
}
}
