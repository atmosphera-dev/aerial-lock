#include "config-repository.h"
#include "config-repository.moc"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Aerial {
ConfigRepository::ConfigRepository(ConfigPaths value, QObject *parent)
    : QObject(parent), paths(std::move(value)), current(readConfig(paths))
{
    connect(&watcher, &QFileSystemWatcher::directoryChanged, this, [this] { reload(); });
    connect(&watcher, &QFileSystemWatcher::fileChanged, this, [this] { reload(); });
    watch();
}

void ConfigRepository::watch()
{
    // Watch an existing ancestor too: a missing config directory may be created
    // or replaced by an editor. Re-arm after every rename/removal notification.
    QStringList desired;
    QString parent = QFileInfo(paths.config).absolutePath();
    while (!QFileInfo(parent).isDir() && parent != "/") parent = QFileInfo(parent).absolutePath();
    desired << parent;
    if (QFileInfo(paths.config).exists()) desired << paths.config;
    const auto active = watcher.files() + watcher.directories();
    for (const auto &path : active) if (!desired.contains(path)) watcher.removePath(path);
    for (const auto &path : desired) if (!active.contains(path)) watcher.addPath(path);
}

void ConfigRepository::reload()
{
    auto next = readConfig(paths);
    const bool different = next.revision != current.revision;
    const bool diagnosticsDifferent = next.settingsDiagnostics != current.settingsDiagnostics;
    current = std::move(next);
    watch();
    if (different) emit changed(current.revision, {});
    if (diagnosticsDifferent) emit diagnosticsChanged();
}

bool ConfigRepository::prepareDirectory()
{
    const auto parent = QFileInfo(paths.config).absolutePath();
    const QFileInfo directory(parent), target(paths.config);
    if (directory.isSymLink() || target.isSymLink()) return false;
    if (!directory.exists() && !QDir().mkpath(parent)) return false;
    struct stat state;
    const auto encoded = QFile::encodeName(parent);
    if (::lstat(encoded.constData(), &state) || !S_ISDIR(state.st_mode) || state.st_uid != ::geteuid()) return false;
    if (target.exists() && (!target.isFile() || target.ownerId() != ::geteuid())) return false;
    if (!directory.exists()) return QFile::setPermissions(parent, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return true;
}

ConfigTransaction ConfigRepository::update(const QString &revision, const QJsonObject &patch)
{
    return transact(revision, patch, false);
}

ConfigTransaction ConfigRepository::transact(const QString &revision, const QJsonObject &patch, bool migration)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!prepareDirectory()) return {{}, "StorageError", {"unsafe or unwritable configuration directory"}};
    const auto lockPath = QFile::encodeName(paths.config + ".edit.lock");
    const int fd = ::open(lockPath.constData(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    struct Lock { int fd; ~Lock() { if (fd >= 0) ::close(fd); } } lock{fd};
    struct stat state;
    if (fd < 0 || ::fstat(fd, &state) || !S_ISREG(state.st_mode) || state.st_uid != ::geteuid()
        || state.st_nlink != 1 || ::flock(fd, LOCK_EX | LOCK_NB))
        return {{}, "Busy", {"configuration edit lock unavailable"}};
    const auto latest = readConfig(paths);
    if (latest.revision != revision) { reload(); return {{}, "Conflict", {"settings changed; reread before editing"}}; }
    if (!latest.bundleValid || !latest.userValid) return {{}, "InvalidSetting", {"malformed configuration preserved"}};
    const auto errors = validatePatch(patch, latest.schema);
    if (!errors.isEmpty()) return {{}, "InvalidSetting", errors};
    auto user = mergePatch(migration ? latest.projectedUser : latest.user, patch);
    // Persist deterministic IDs on a requested edit of an ID-less source. The
    // effective ID is already derived from type/source, never its display name.
    if ((patch.contains("sources") || migration) && user.value("sources").isArray()) {
        auto list = user.value("sources").toArray();
        // IDs for newly requested entries are derived by the same reader path.
        for (qsizetype i = 0; i < list.size(); ++i) {
            auto source = list.at(i).toObject();
            if (!source.contains("id")) {
                const auto previous = latest.effective.value("sources").toArray();
                // Match an existing unambiguous source by type and unchanged
                // name or URL. Ambiguous edits must supply their explicit ID.
                QString id;
                for (const auto &old : previous) {
                    const auto entry = old.toObject();
                    if (entry.value("type") == source.value("type") && (entry.value("name") == source.value("name") || entry.value("source") == source.value("source"))) {
                        if (!id.isEmpty()) return {{}, "InvalidSetting", {"ambiguous source edit; supply its id"}};
                        id = entry.value("id").toString();
                    }
                }
                source["id"] = id.isEmpty() ? sourceIdentity(source) : id;
            }
            source.remove("safeName");
            list[i] = source;
        }
        user["sources"] = list;
        const auto identityErrors = validatePatch({{"sources", list}}, latest.schema);
        if (!identityErrors.isEmpty()) return {{}, "InvalidSetting", identityErrors};
    }
    QSaveFile output(paths.config);
    output.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(user).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || output.write(bytes) != bytes.size() || !output.commit())
        return {{}, "StorageError", {"atomic settings write failed"}};
    const auto diagnostics = current.settingsDiagnostics;
    current = readConfig(paths);
    watch();
    emit changed(current.revision, changedPaths(patch));
    if (diagnostics != current.settingsDiagnostics) emit diagnosticsChanged();
    return {current.revision, {}, {}};
}

ConfigTransaction ConfigRepository::observeHeight(int height)
{
    reload();
    if (height <= 0 || current.user.contains("preferredResolution")) return {current.revision, {}, {}};
    return update(current.revision, {{"preferredResolution", height >= 2160 ? 2160 : 1080}});
}

ConfigTransaction ConfigRepository::migrate()
{
    reload();
    if (!current.migrated) return {current.revision, {}, {}};
    return transact(current.revision, {}, true);
}
}
