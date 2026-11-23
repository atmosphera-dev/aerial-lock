#include "notices-model.h"
#include "sources-controller.h"
#include "systemd-controller.h"
#include "status-model.h"

namespace Aerial::Settings {
NoticesModel::NoticesModel(Client::ServiceConnection &service, SourcesController &sources, SystemdController &systemd, StatusModel &status, QObject *parent)
    : QObject(parent), service(service), sources(sources), systemd(systemd), status(status)
{
    connect(&service, &Client::ServiceConnection::changed, this, &NoticesModel::sample);
    connect(&systemd, &SystemdController::changed, this, &NoticesModel::sample);
    connect(&sources, &SourcesController::changed, this, &NoticesModel::sample);
    connect(&status, &StatusModel::changed, this, &NoticesModel::sample);
    sample();
}
QString NoticesModel::connectionDiagnostic(const Client::ServiceConnection &service)
{
    const auto error = service.errorName().section('.', -1);
    if (error.isEmpty()) return "Connecting to aerial...";
    if (error == "NameHasNoOwner" || error == "ServiceUnknown") return "The aerial service isn't running. Try again or check its user service.";
    if (error == "NoReply" || error == "Timeout") return "The aerial service didn't answer. Try again.";
    if (error == "AccessDenied") return "Couldn't access the aerial service. Check your session permissions.";
    return "Couldn't connect to the aerial service. Try again or check its user service.";
}
QVariantMap NoticesModel::idle() const
{
    for (const auto &entry : current) if (entry.toMap().value("id") == "idle") return entry.toMap();
    return {};
}
void NoticesModel::setDownloadErrors(const QString &download, const QString &automatic)
{
    if (download == downloadError && automatic == automaticError) return;
    downloadError = download; automaticError = automatic; sample();
}
void NoticesModel::sample()
{
    QVariantList next;
    const auto add = [&](const QString &id, const QString &title, const QString &message, const QString &actionId = {}, const QString &sourceId = {}, const QString &actionLabel = {}, const QString &severity = "warning") {
        next << QVariantMap{{"id", id}, {"title", title}, {"message", message}, {"actionId", actionId}, {"sourceId", sourceId}, {"actionLabel", actionId.isEmpty() ? QString() : actionLabel}, {"severity", severity}};
    };
    const auto idle = systemd.idle();
    const auto operation = idle.value("action").toString();
    const auto idleAction = operation == "enableStart" ? QString("turn-on-idle") : operation == "start" ? QString("start-idle") : operation == "restart" ? QString("restart-idle") : QString();
    add("idle", operation == "enableStart" || !systemd.available() ? "The screen doesn't lock by itself" : "Automatic locking", idle.value("message").toString(), idleAction, {}, idle.value("actionLabel").toString(),
        systemd.available() && operation != "enableStart" ? "info" : "warning");
    if (!service.ready()) add("service", "The aerial service isn't available", connectionDiagnostic(service), "retry-service", {}, "Try again", "error");
    const auto properties = service.snapshot().properties;
    for (const auto &entry : properties.value("SettingsDiagnostics").toList()) {
        const auto item = entry.toMap();
        const auto id = "config:" + item.value("file").toString() + ':' + item.value("code").toString() + ':' + item.value("settingPath").toString() + ':' + item.value("line").toString();
        QString message = item.value("file").toString() + ": " + item.value("code").toString();
        if (item.contains("line")) message += " at line " + item.value("line").toString() + ", column " + item.value("column").toString();
        if (item.contains("settingPath")) message += " (" + item.value("settingPath").toString() + ")";
        add(id, "Settings need attention", message, "open-settings-folder", {}, "Open folder", item.value("severity").toString());
    }
    for (const auto &entry : sources.rows()) {
        const auto row = entry.toMap();
        if (!row.value("enabled", true).toBool() || row.value("errorCode").toString().isEmpty()) continue;
        const auto id = row.value("id").toString();
        const bool retry = !status.facts().value("primaryActionId").toString().isEmpty() && row.value("canRetry").toBool();
        add("source:" + id, row.value("name").toString() + " needs attention", row.value("description").toString(), retry ? "retry-source" : "open-sources", id, retry ? "Try again" : "Open Sources", "error");
    }
    if (!systemd.error().isEmpty()) add("systemd", "Service operation needs attention", systemd.error(), "open-about", {}, "Open About");
    if (!downloadError.isEmpty()) add("download-operation", "Content operation needs attention", downloadError, "retry-service", {}, "Check service state", "error");
    if (!automaticError.isEmpty()) add("automatic-operation", "Automatic downloads need attention", automaticError, "retry-service", {}, "Check service state", "error");
    if (next != current) { current = next; emit changed(); }
}
}
