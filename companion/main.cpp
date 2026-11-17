#include "config-repository.h"
#include "service.h"
#include "transfer-queue.h"
#include "../native/shell-dir.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QRandomGenerator>
#include <QLockFile>
#include <QTimer>
#include <sys/stat.h>
#include <memory>

#ifndef AERIAL_COMPANION_DATADIR
#define AERIAL_COMPANION_DATADIR "/usr/share/aerial-lock"
#endif
#ifndef AERIAL_COMPANION_PROBE
#define AERIAL_COMPANION_PROBE "/usr/libexec/aerial-lock/aerial-media-probe"
#endif

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ::umask(0077);
    app.setApplicationName("aerial-companion");
    if (app.arguments().size() != 1) { qWarning("usage: aerial-companion"); return 2; }
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
    QNetworkAccessManager transport;
    const auto clock = [] { return QDateTime::currentSecsSinceEpoch(); };
    Aerial::PlannerState state(paths.state, clock, [] { return QRandomGenerator::global()->generate(); });
    std::unique_ptr<Aerial::CacheStore> cache;
    std::unique_ptr<Aerial::TransferQueue> queue;
    const auto initialize = [&] {
        if (queue) return;
        const auto maximum = config.document().effective.value("cache").toObject().value("maxBytes").toInteger();
        // A fresh zero-cap profile creates no managed metadata or media. A later
        // settings transaction can initialize it without restarting the owner.
        if (!maximum) return;
        cache = std::make_unique<Aerial::CacheStore>(paths.cache, quint64(maximum));
        if (!cache->valid() || !state.valid()) { qWarning("companion storage unavailable"); return; }
        QFile root(paths.bundle + "/Config/apple-root.pem");
        const auto appleRoot = root.open(QIODevice::ReadOnly) ? root.readAll() : QByteArray{};
        queue = std::make_unique<Aerial::TransferQueue>(config, *cache, state, transport,
            QStringLiteral(AERIAL_COMPANION_PROBE), appleRoot, clock);
        queue->start();
    };
    QObject::connect(&config, &Aerial::ConfigRepository::changed, &app, initialize);
    // Migration and initial planning run only after exported methods/name exist.
    QTimer::singleShot(0, &config, [&] { config.migrate(); initialize(); });
    return app.exec();
}
