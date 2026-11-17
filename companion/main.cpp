#include "config-repository.h"
#include "service.h"
#include "../native/shell-dir.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QLockFile>
#include <QTimer>
#include <sys/stat.h>

#ifndef AERIAL_COMPANION_DATADIR
#define AERIAL_COMPANION_DATADIR "/usr/share/aerial-lock"
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
    // Migration is queued only after configuration and exported methods exist.
    QTimer::singleShot(0, &config, [&config] { config.migrate(); });
    return app.exec();
}
