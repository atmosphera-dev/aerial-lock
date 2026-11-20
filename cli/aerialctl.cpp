#include "common/client/blocking-client.h"
#include "native/shell-dir.h"
#include <QCoreApplication>
#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <memory>

#ifndef AERIAL_COMPANION_DATADIR
#define AERIAL_COMPANION_DATADIR "/usr/share/aerial-lock"
#endif

namespace {
using namespace Aerial::Client;
int failure(const QDBusError &error)
{
    // Error codes are stable; never echo remote prose that might contain a URL.
    QTextStream(stderr) << "aerialctl: " << error.name() << '\n';
    if (error.name().endsWith(".Conflict")) return 4;
    if (error.name().endsWith(".PolicyDeferred")) return 3;
    return 1;
}
template<class... T> bool completed(QDBusPendingReply<T...> &reply)
{
    reply.waitForFinished(); return !reply.isError();
}
void print(const QJsonValue &value, bool compact)
{
    const QJsonDocument document = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    QTextStream(stdout) << document.toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented) << (compact ? "\n" : "");
}
int waitJob(CompanionClient &client, const QDBusObjectPath &job, bool fetch, bool json)
{
    if (job.path() == "/") { QTextStream(stderr) << "aerialctl: job was not accepted\n"; return 1; }
    Observer observer(client.connection());
    const auto profile = client.profile();
    std::unique_ptr<CompanionClient> replacement;
    auto *owner = &client;
    for (;;) {
        QDBusError error;
        const auto values = properties(owner->connection(), job.path(), JobInterface, error, owner->service());
        if (error.isValid()) {
            // Durable paths survive service restarts. Resnapshot the owner once
            // instead of treating an unknown job or a disconnected bus as success.
            QDBusError connectionError;
            replacement = currentOwner(client.connection(), profile, connectionError);
            if (!replacement) return failure(connectionError);
            owner = replacement.get();
            auto saved = owner->ListJobs();
            if (!completed(saved)) return failure(saved.error());
            if (!saved.value().contains(job)) return failure(error);
            observer.wait(); continue;
        }
        if (!values.value("Cancellable").toBool()) {
            print(QJsonObject::fromVariantMap(values), json);
            const auto state = values.value("State").toString();
            if (fetch ? values.value("ReadyClips").toUInt() > 0 : state == "succeeded") return 0;
            return !fetch && state == "deferred" ? 3 : 1;
        }
        observer.wait();
    }
}
int usage()
{
    QTextStream(stderr) << "usage: aerialctl status [--json]\n"
        "       aerialctl config show [--effective] [--json]\n"
        "       aerialctl config set <dot.path> <json> | reset <dot.path>\n"
        "       aerialctl fetch [source-id] [--limit N] [--wait]\n"
        "       aerialctl content ensure | refresh [source-id] | pause | resume\n"
        "       aerialctl jobs [--watch] | cancel <job-path>\n"
        "       aerialctl cache status | prune [--dry-run]\n";
    return 2;
}
bool option(QStringList &args, const QString &flag) { return args.removeAll(flag) != 0; }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Aerial::registerBusTypes();
    auto args = app.arguments(); args.removeFirst();
    if (args.contains("--help") || args.contains("-h")) { usage(); return 0; }
    if (args == QStringList{"--version"}) { QTextStream(stdout) << "aerialctl (Companion API 1)\n"; return 0; }
    const bool json = option(args, "--json");
    if (args.isEmpty()) return usage();
    if (args.size() >= 2 && args[0] == "content" && args[1] == "fetch") args.removeFirst();
    const auto command = args.takeFirst();
    // Parse the complete command before connecting, so malformed invocations
    // cannot unexpectedly activate/download through a production service.
    enum Action { Status, Show, Set, Reset, Fetch, Ensure, Refresh, Pause, Resume, Jobs, Cancel, Prune } action;
    bool effective = false, wait = false, watch = false, dry = false;
    int limit = 0;
    QString key, encoded;
    QStringList sources;
    if (command == "status" && args.isEmpty()) action = Status;
    else if (command == "config" && !args.isEmpty()) {
        const auto verb = args.takeFirst();
        if (verb == "show") { action = Show; effective = option(args, "--effective"); if (!args.isEmpty()) return usage(); }
        else if (verb == "set" && args.size() == 2) { action = Set; key = args[0]; encoded = args[1]; }
        else if (verb == "reset" && args.size() == 1) { action = Reset; key = args[0]; }
        else return usage();
    } else if (command == "fetch") {
        action = Fetch; wait = option(args, "--wait");
        const auto index = args.indexOf("--limit");
        if (index >= 0) {
            if (index + 1 >= args.size()) return usage();
            bool ok; limit = args[index + 1].toInt(&ok);
            if (!ok || limit < 0) return usage();
            args.removeAt(index + 1); args.removeAt(index);
        }
        if (args.size() > 1 || (!args.isEmpty() && args[0].startsWith('-'))) return usage();
        sources = args;
    } else if (command == "content" && !args.isEmpty()) {
        const auto verb = args.takeFirst();
        if (verb == "ensure" && args.isEmpty()) action = Ensure;
        else if (verb == "refresh" && args.size() <= 1 && (args.isEmpty() || !args[0].startsWith('-'))) { action = Refresh; sources = args; }
        else if (verb == "pause" && args.isEmpty()) action = Pause;
        else if (verb == "resume" && args.isEmpty()) action = Resume;
        else return usage();
    } else if (command == "jobs") { action = Jobs; watch = option(args, "--watch"); if (!args.isEmpty()) return usage(); }
    else if (command == "cancel" && args.size() == 1 && args[0].startsWith("/org/aeriallock/Aerial1/jobs/j")) { action = Cancel; key = args[0]; }
    else if (command == "cache" && !args.isEmpty()) {
        const auto verb = args.takeFirst();
        if (verb == "status" && args.isEmpty()) action = Status;
        else if (verb == "prune") { action = Prune; dry = option(args, "--dry-run"); if (!args.isEmpty()) return usage(); }
        else return usage();
    } else return usage();

    QJsonObject patch;
    if (action == Set || action == Reset) {
        QJsonValue value = QJsonValue::Null;
        if (action == Set) {
            QJsonParseError parse;
            const auto parsed = QJsonDocument::fromJson(("[" + encoded + "]").toUtf8(), &parse);
            if (parse.error != QJsonParseError::NoError || parsed.array().size() != 1) return usage();
            value = parsed.array().first();
        }
        bool valid;
        patch = settingPatch(key, value, &valid);
        if (!valid) return usage();
    }
    const auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return failure(bus.lastError());
    const auto local = Aerial::readConfig(Aerial::ConfigPaths::environment(shellDir(QStringLiteral(AERIAL_COMPANION_DATADIR))));
    if (mayActivate(local.paths, QStringLiteral(AERIAL_COMPANION_DATADIR))) {
        // Only the ordinary installed profile may activate. After that, all
        // calls are pinned to a checked unique owner, including observations.
        auto activate = QDBusMessage::createMethodCall(Service, Path, Service, "GetSettings");
        const auto reply = bus.call(activate, QDBus::Block, 5000);
        if (reply.type() == QDBusMessage::ErrorMessage) return failure(QDBusError(reply));
    }
    QDBusError connectionError;
    auto connection = currentOwner(bus, local.profile, connectionError);
    if (!connection) return failure(connectionError);
    auto &client = *connection;
    auto settings = client.GetSettings();
    if (!completed(settings)) return failure(settings.error());
    if (client.profile() != local.profile) { QTextStream(stderr) << "aerialctl: ProfileMismatch (service uses different XDG roots or bundle)\n"; return 1; }

    if (action == Show) { print(QJsonDocument::fromJson((effective ? settings.argumentAt<1>() : settings.argumentAt<0>()).toUtf8()).object(), json); return 0; }
    if (action == Set || action == Reset) {
        auto reply = client.UpdateSettings(settings.argumentAt<2>(), QString::fromUtf8(QJsonDocument(patch).toJson(QJsonDocument::Compact)));
        if (!completed(reply)) return failure(reply.error());
        print(QJsonObject{{"revision", reply.value()}}, json); return 0;
    }
    if (action == Pause || action == Resume) {
        auto reply = client.SetPaused(action == Pause); return completed(reply) ? 0 : failure(reply.error());
    }
    if (action == Ensure) {
        auto reply = client.EnsureContent({{"profile", local.profile}}); return completed(reply) ? 0 : failure(reply.error());
    }
    if (action == Cancel) {
        const auto valid = QDBusObjectPath(key);
        if (valid.path().isEmpty()) return usage();
        auto reply = client.Cancel(valid); return completed(reply) ? 0 : failure(reply.error());
    }
    if (action == Fetch || action == Refresh || action == Prune) {
        auto reply = action == Fetch ? client.Acquire(sources, {{"limit", limit}}) : action == Refresh ? client.Refresh(sources) : client.Prune(dry);
        if (!completed(reply)) return failure(reply.error());
        if (wait) return waitJob(client, reply.value(), action == Fetch, json);
        if (reply.value().path() == "/") return 1;
        print(QJsonObject{{"job", reply.value().path()}, {"accepted", true}}, json); return 0;
    }
    Observer observer(bus);
    for (;;) {
        QDBusError error;
        auto rows = QJsonArray::fromVariantList(jobs(*connection, error));
        if (error.isValid()) {
            connection = currentOwner(bus, local.profile, connectionError);
            if (!connection) return failure(connectionError);
            error = QDBusError(); rows = QJsonArray::fromVariantList(jobs(*connection, error));
            if (error.isValid()) return failure(error);
        }
        if (action == Jobs) print(rows, json);
        else {
            auto values = properties(bus, Path, Service, error, connection->service());
            if (error.isValid()) return failure(error);
            auto result = QJsonObject::fromVariantMap(values); result["jobs"] = rows; print(result, json); return 0;
        }
        bool active = false;
        for (const auto &row : rows) active |= row.toObject().value("Cancellable").toBool();
        if (!watch || !active) return 0;
        observer.wait();
    }
}
