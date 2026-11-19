#include "config-repository.h"
#include "service.h"
#include "transfer-queue.h"
#include "policy-monitor.h"
#include "../native/shell-dir.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QDateTime>
#include <QRandomGenerator>
#include <QLockFile>
#include <QTimer>
#include <sys/stat.h>
#include <memory>

#ifndef AERIAL_COMPANION_DATADIR
#define AERIAL_COMPANION_DATADIR "/usr/share/aerial-lock"
#endif
#ifndef AERIAL_MEDIA_PROBE_PATH
#define AERIAL_MEDIA_PROBE_PATH "/usr/libexec/aerial-lock/aerial-media-probe"
#endif
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ::umask(0077);
    app.setApplicationName("aerial");
    if (app.arguments().size() != 1) { qWarning("usage: aerial"); return 2; }
    const auto paths = Aerial::ConfigPaths::environment(shellDir(QStringLiteral(AERIAL_COMPANION_DATADIR)));
    Aerial::ConfigRepository config(paths);
    if (!config.document().bundleValid) {
        qWarning().noquote() << config.document().diagnostics.join('\n');
        return 1;
    }
    // A profile owner lock also protects independent private buses. The content
    // cache gets its own ownership/reader locks when its manager is integrated.
    const auto directory = QFileInfo(paths.config).absolutePath();
    if (QFileInfo(directory).isSymLink() || !QDir().mkpath(directory)) return 1;
    QLockFile owner(directory + "/companion.owner.lock");
    owner.setStaleLockTime(0);
    if (!owner.tryLock(0)) { qWarning("companion profile already owned"); return 1; }
    Aerial::CompanionService service(config, QDBusConnection::sessionBus());
    if (!service.start()) { qWarning("companion bus ownership failed"); return 1; }
    Aerial::HttpTransport transport;
    const auto clock = [] { return QDateTime::currentSecsSinceEpoch(); };
    std::unique_ptr<Aerial::PlannerState> state;
    std::unique_ptr<Aerial::CacheStore> cache;
    std::unique_ptr<Aerial::TransferQueue> queue;
    Aerial::PolicyMonitor policy(QDBusConnection::systemBus());
    QTimer storageRetry;
    storageRetry.setSingleShot(true);
    storageRetry.setInterval(1000);
    const auto initialize = [&] {
        if (queue) return;
        if (!state || !state->valid()) {
            service.bind(nullptr, cache.get(), nullptr, &policy);
            state.reset();
            state = std::make_unique<Aerial::PlannerState>(paths.state, clock, [] { return QRandomGenerator::global()->generate(); });
            service.bind(state.get(), cache.get(), nullptr, &policy);
            if (!state->valid()) { storageRetry.start(); return; }
        }
        const auto maximum = config.document().effective.value("cache").toObject().value("maxBytes").toInteger();
        // A fresh zero-cap profile creates no managed metadata or media. A later
        // settings transaction can initialize it without restarting the owner.
        if (!maximum && !QFileInfo::exists(paths.cache)) return;
        service.bind(state.get(), nullptr, nullptr, &policy);
        cache.reset();
        cache = std::make_unique<Aerial::CacheStore>(paths.cache, quint64(maximum));
        service.bind(state.get(), cache.get(), nullptr, &policy);
        if (!cache->valid()) { storageRetry.start(); return; }
        storageRetry.stop();
        QFile root(paths.bundle + "/Config/apple-root.pem");
        const auto appleRoot = root.open(QIODevice::ReadOnly) ? root.readAll() : QByteArray{};
        // Build-tree binaries are paired siblings; installed aerial is in BINDIR
        // while the private decoder remains under the configured LIBEXECDIR.
        const auto sibling = QCoreApplication::applicationDirPath() + "/aerial-media-probe";
        const auto probe = QFileInfo(sibling).isExecutable() ? sibling : QStringLiteral(AERIAL_MEDIA_PROBE_PATH);
        queue = std::make_unique<Aerial::TransferQueue>(config, *cache, *state, transport,
            probe, appleRoot, clock);
        queue->setPolicyReason(policy.ready() ? policy.deferReason(config.document().effective) : QStringLiteral("Initializing"));
        service.bind(state.get(), cache.get(), queue.get(), &policy);
        queue->start();
    };
    QObject::connect(&storageRetry, &QTimer::timeout, &app, initialize);
    const auto updatePolicy = [&] {
        if (queue) queue->setPolicyReason(policy.ready() ? policy.deferReason(config.document().effective) : QStringLiteral("Initializing"));
        else initialize();
    };
    QObject::connect(&policy, &Aerial::PolicyMonitor::changed, &app, updatePolicy);
    QObject::connect(&config, &Aerial::ConfigRepository::changed, &app, updatePolicy);
    QObject::connect(&config, &Aerial::ConfigRepository::changed, &app, initialize);
    initialize();
    policy.start();
    // A disconnected owner cannot keep accepting/performing unobservable work.
    // Destruction retains durable jobs/partials for the next exclusive owner.
    QDBusConnection::sessionBus().connect({}, {}, "org.freedesktop.DBus.Local", "Disconnected", &app, SLOT(quit()));
    // Migration and initial planning run only after exported methods/name exist.
    QTimer::singleShot(0, &config, [&] { config.migrate(); initialize(); });
    return app.exec();
}
