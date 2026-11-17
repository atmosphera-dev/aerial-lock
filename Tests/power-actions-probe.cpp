#include "../native/power-actions.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>
#include <QTest>
#include <functional>
#include <iostream>

static void require(bool value, const char *message) { if (!value) qFatal("power-actions: %s", message); }
static bool until(const std::function<bool()> &predicate) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 5000) QTest::qWait(5);
    return predicate();
}
static QJsonArray control(const QString &method, const QJsonObject &input = {}) {
    auto message = QDBusMessage::createMethodCall("org.aeriallock.PowerFixture", "/fixture", "org.aeriallock.PowerFixture", method);
    message << QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));
    auto reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 3000);
    require(reply.type() == QDBusMessage::ReplyMessage, "fixture control reply");
    return QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8()).array();
}
static int calls(const QString &method) {
    int count = 0;
    for (const auto &entry : control("Events")) if (entry.toObject()["method"] == method) ++count;
    return count;
}
static void ready(PowerActions &power) {
    power.setEnabled(true); power.setSecure(true);
    require(until([&] { return power.canReboot() && power.canSuspend() && power.canHibernate() && power.canPowerOff(); }), "capabilities ready");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    require(argc == 2, "fixture path");
    QProcess peer; peer.start(QString::fromLocal8Bit(argv[1]), {});
    require(peer.waitForStarted(3000) && peer.waitForReadyRead(3000) && peer.readLine() == "READY\n", "peer ready");
    const auto bus = QDBusConnection::sessionBus();
    for (const QString &value : {"yes", "no", "na", "challenge", "inhibited", "inhibitor-blocked", "challenge-inhibitor-blocked", "inhibited-blocked", "unknown", ""}) {
        control("Configure", {{"caps", QJsonObject{{"CanSuspend", value}}}});
        PowerActions power(bus, nullptr);
        require(!power.canSuspend(), "initial unknown");
        power.setEnabled(true); power.setSecure(true);
        require(until([&] { return power.canReboot() && power.suspendReason() != "unknown"; }), "capability snapshot");
        require(power.canSuspend() == (value == "yes"), "only yes permits");
        require(power.canReboot(), "independent capabilities");
    }
    for (const auto &caps : {QJsonObject{{"CanSuspend", false}}, QJsonObject{}}) {
        control("Configure", {{"caps", caps}, {"faults", caps.isEmpty() ? QJsonObject{{"CanSuspend", "org.freedesktop.DBus.Error.Failed"}} : QJsonObject{}}});
        PowerActions power(bus, nullptr); power.setEnabled(true); power.setSecure(true);
        require(until([&] { return power.canReboot(); }), "malformed snapshot settles");
        require(!power.canSuspend(), "malformed/error cannot enable");
    }
    for (const QString &action : {"suspend", "hibernate", "reboot", "powerOff"}) {
        control("Configure"); PowerActions power(bus, nullptr); ready(power);
        power.request(action);
        if (action == "reboot" || action == "powerOff") {
            require(power.confirmationRequired() && power.pendingAction() == action, "destructive confirmation");
            require(calls(action == "reboot" ? "RebootWithFlags" : "PowerOffWithFlags") == 0, "no premature dispatch");
            power.cancel(); power.confirm();
            require(power.state() == "idle", "cancel stale confirm");
            power.request(action); power.confirm(); power.confirm();
        } else require(!power.confirmationRequired(), "sleep no confirmation");
        power.request(action);
        require(until([&] { return power.state() == "waiting"; }), "accepted waiting");
        const QString method = action == "suspend" ? "SuspendWithFlags" : action == "hibernate" ? "HibernateWithFlags" : action == "reboot" ? "RebootWithFlags" : "PowerOffWithFlags";
        require(calls(method) == 1, "exactly once");
        for (const auto &value : control("Events")) {
            const auto event = value.toObject();
            require(!event["interactive"].toBool(), "no interactive authorization");
            require(event["sender"].toString().startsWith(':'), "private-bus unique sender recorded");
            if (event["method"] == method) require(event["signature"] == "t" && event["flags"].toInt() == 1, "strict flags/signature");
        }
        power.cancel(); require(power.state() == "waiting", "cannot cancel sent request");
        control("Signal", {{"name", action == "suspend" || action == "hibernate" ? "PrepareForSleep" : "PrepareForShutdown"}, {"value", true}});
        control("Signal", {{"name", action == "suspend" || action == "hibernate" ? "PrepareForSleep" : "PrepareForShutdown"}, {"value", false}});
        require(until([&] { return power.state() == "idle"; }), "resume/cancellation clears pending");
    }
    for (const QString &mode : {"block", "block-weak", "delay"}) {
        control("Configure", {{"inhibitors", QJsonArray{QJsonObject{{"what", "idle:sleep"}, {"mode", mode}}}}});
        PowerActions power(bus, nullptr); ready(power); power.request("suspend");
        require(until([&] { return !power.errorCode().isEmpty() || power.state() == "waiting"; }), "inhibitor outcome");
        require(calls("SuspendWithFlags") == (mode == "delay" ? 1 : 0), "inhibitors honored");
        if (mode != "delay") require(power.errorCode() == "inhibited", "inhibitor reason");
    }
    control("Configure", {{"inhibitors", QJsonArray{QJsonObject{{"what", "idle:handle-lid-switch"}, {"mode", "block"}}}}}); {
        PowerActions power(bus, nullptr); ready(power); power.request("suspend");
        require(until([&] { return power.state() == "waiting"; }), "unrelated inhibitors never block a power action");
        require(calls("SuspendWithFlags") == 1, "exact inhibitor what-token matching");
    }
    control("Configure"); {
        PowerActions power(bus, nullptr);
        power.request("suspend"); power.confirm();
        require(calls("SuspendWithFlags") == 0, "disabled/insecure service never dispatches");
        ready(power); power.request("invalid-action");
        require(power.errorCode() == "unavailable" && power.state() == "idle", "unknown IDs rejected");
        require(calls("SuspendWithFlags") == 0, "unknown IDs cannot choose an action");
    }
    control("Configure", {{"hold", "CanReboot"}}); {
        PowerActions power(bus, nullptr); power.setEnabled(true); power.setSecure(true);
        require(until([&] { return calls("CanReboot") == 1; }), "snapshot held for refresh burst");
        for (int i = 0; i < 100; ++i) power.refresh();
        require(calls("CanReboot") == 1, "refresh batch coalesces while in flight");
        power.setEnabled(false);
        control("Release"); QTest::qWait(50);
        require(!power.canReboot() && !power.canSuspend(), "retired capability replies cannot re-enable controls");
    }
    for (const QJsonObject &config : {QJsonObject{{"malformedInhibitors", true}},
                                    QJsonObject{{"faults", QJsonObject{{"ListInhibitors", "org.freedesktop.DBus.Error.AccessDenied"}}}},
                                    QJsonObject{{"faults", QJsonObject{{"SuspendWithFlags", "org.freedesktop.login1.BlockedByInhibitorLock"}}}}}) {
        control("Configure", config); PowerActions power(bus, nullptr); ready(power); power.request("suspend");
        require(until([&] { return !power.errorCode().isEmpty(); }), "error shown");
        require(power.state() == "idle", "definite error restores input");
    }
    for (const QString &error : {"org.freedesktop.DBus.Error.AccessDenied", "org.freedesktop.DBus.Error.InteractiveAuthorizationRequired",
                                "org.freedesktop.login1.OperationInProgress", "org.freedesktop.login1.SleepVerbNotSupported",
                                "org.freedesktop.DBus.Error.NoReply", "org.freedesktop.DBus.Error.ServiceUnknown"}) {
        control("Configure", {{"faults", QJsonObject{{"SuspendWithFlags", error}}}});
        PowerActions power(bus, nullptr); ready(power); power.request("suspend");
        require(until([&] { return !power.errorCode().isEmpty(); }), "dispatch error reaches caller");
        require(power.activeAction() == "suspend", "failure retains selected control");
        if (error.endsWith("NoReply") || error.endsWith("ServiceUnknown")) {
            require(power.state() == "uncertain" && !power.interactionActive(), "uncertain request never strands authentication");
            power.clearError(); power.request("suspend");
            require(!power.canSuspend() && calls("SuspendWithFlags") == 1, "uncertain request cannot resend or offer inert controls");
        } else {
            require(power.pendingAction().isEmpty(), "definite failure clears pending intent");
            power.clearError(); require(power.activeAction().isEmpty(), "dismiss returns normal set");
        }
    }
    for (bool longText : {false, true}) {
        const QString who = longText ? QString(50, 'n') : QStringLiteral("<b>Firefox</b> %3");
        const QString why = longText ? QString(90, 'r') : QStringLiteral("<img src='https://invalid.test'> %2");
        control("Configure", {{"inhibitors", QJsonArray{QJsonObject{{"what", "shutdown"}, {"mode", "block"}, {"who", who}, {"why", why}}}}});
        PowerActions power(bus, nullptr); ready(power); power.request("reboot"); power.confirm();
        require(until([&] { return power.errorCode() == "inhibited"; }), "blocker data outcome");
        require(power.activeAction() == "reboot" && calls("RebootWithFlags") == 0, "blocked action retained without dispatch");
        require(power.blockerWho() == (longText ? QString(37, 'n') + "..." : who), "name literal and bounded to 40 codepoints");
        require(power.blockerWhy() == (longText ? QString(77, 'r') + "..." : why), "reason literal and bounded to 80 codepoints");
    }
    for (int index = 0; index < 4; ++index) {
        const QString action = QStringList{"suspend", "hibernate", "reboot", "powerOff"}[index];
        const QString method = QStringList{"CanSuspend", "CanHibernate", "CanReboot", "CanPowerOff"}[index];
        control("Configure", {{"caps", QJsonObject{{method, "inhibitor-blocked"}}},
            {"inhibitors", QJsonArray{QJsonObject{{"what", index < 2 ? "sleep" : "shutdown"},
                {"mode", "block"}, {"who", "Firefox"}, {"why", "Downloading file"}}}}});
        PowerActions power(bus, nullptr); power.setEnabled(true); power.setSecure(true);
        require(until([&] { return power.property(qPrintable(action == "powerOff" ? "powerOffReason" : action + "Reason")).toString() == "inhibited"; }),
                "initial inhibited capability settles");
        power.request(action);
        require(until([&] { return power.errorCode() == "inhibited" && power.state() == "idle" && !power.blockerWho().isEmpty(); }), "initial inhibited attempt outcome");
        require(power.blockerWho() == "Firefox" && power.blockerWhy() == "Downloading file", "initial inhibition retains blocker detail");
        require(power.activeAction() == action && !power.confirmationRequired(), "initial inhibition never asks to execute");
        for (const auto &value : control("Events"))
            require(!value.toObject()["method"].toString().endsWith("WithFlags"), "initial inhibition never dispatches");
    }
    control("Configure", {{"caps", QJsonObject{{"CanSuspend", "inhibitor-blocked"}}}, {"hold", "ListInhibitors"}}); {
        PowerActions power(bus, nullptr); power.setEnabled(true); power.setSecure(true);
        require(until([&] { return power.suspendReason() == "inhibited"; }), "inhibited snapshot before detail hold");
        power.request("suspend");
        require(until([&] { return calls("ListInhibitors") == 1; }), "inhibitor detail held");
        power.clearError(); control("Release"); QTest::qWait(50);
        require(power.errorCode().isEmpty() && power.activeAction().isEmpty(), "dismiss retires late inhibitor detail");
    }
    control("Configure"); {
        PowerActions power(bus, nullptr); ready(power); power.request("reboot");
        control("Configure", {{"caps", QJsonObject{{"CanReboot", "challenge"}}}});
        power.confirm(); require(until([&] { return power.errorCode() == "authorization"; }), "recheck before dispatch");
        require(calls("RebootWithFlags") == 0, "changed capability never dispatches");
    }
    for (int retire = 0; retire < 3; ++retire) {
        control("Configure"); PowerActions power(bus, nullptr); ready(power);
        control("Configure", {{"hold", "ListInhibitors"}}); power.request("suspend");
        require(until([&] { return calls("ListInhibitors") == 1; }), "held preflight");
        if (retire == 0) power.cancel(); else if (retire == 1) power.setEnabled(false); else power.setSecure(false);
        control("Release"); QTest::qWait(50);
        require(calls("SuspendWithFlags") == 0, "retired check cannot execute");
    }
    control("Configure", {{"signalBeforeReply", true}}); {
        PowerActions power(bus, nullptr); ready(power); power.request("hibernate");
        require(until([&] { return calls("HibernateWithFlags") == 1 && power.state() == "idle"; }), "signal-before-reply cannot resurrect waiting");
    }
    control("Configure"); {
        PowerActions power(bus, nullptr); ready(power); power.request("reboot");
        control("Drop"); require(until([&] { return !power.canReboot() && !power.confirmationRequired(); }), "owner loss retires confirmation");
        power.confirm(); control("Claim"); require(until([&] { return power.canReboot(); }), "new owner refresh");
        require(calls("RebootWithFlags") == 0, "no cross-owner confirmation");
    }
    control("Quit"); require(peer.waitForFinished(3000) && peer.exitCode() == 0, "peer clean exit");
    std::cout << "POWER_ACTIONS_PASS capabilities confirmation cancel recheck inhibitors flags errors retirement owner resume\n";
}
