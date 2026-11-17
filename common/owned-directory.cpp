#include "owned-directory.h"

#include <QDir>
#include <QFile>
#include <QUuid>
#include <dirent.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace Aerial {
namespace {
bool ownedFile(int fd)
{
    struct stat st{};
    return fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == getuid() && st.st_nlink == 1;
}

bool walkFiles(int fd, const QString &prefix, QStringList &out)
{
    // Independent open description: dup() would share the directory offset.
    DIR *dir = fdopendir(openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!dir) return false;
    bool ok = true;
    errno = 0;
    while (auto *entry = readdir(dir)) {
        const QByteArray name(entry->d_name);
        if (name == "." || name == "..") continue;
        struct stat st{};
        if (fstatat(fd, name.constData(), &st, AT_SYMLINK_NOFOLLOW) || st.st_uid != getuid()) { ok = false; break; }
        const auto relative = prefix + QFile::decodeName(name);
        if (S_ISDIR(st.st_mode)) {
            const int child = openat(fd, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (child < 0) { ok = false; break; }
            ok = walkFiles(child, relative + '/', out);
            close(child);
            if (!ok) break;
        } else if (S_ISREG(st.st_mode) && st.st_nlink == 1) out.append(relative);
        else { ok = false; break; }
        errno = 0;
    }
    if (errno) ok = false;
    closedir(dir);
    return ok;
}
}

OwnedDirectory::OwnedDirectory(const QString &path, bool create) : root(QDir::cleanPath(path))
{
    if (!path.startsWith('/') || path.contains(QChar(0))) return;
    int current = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const auto parts = root.split('/', Qt::SkipEmptyParts);
    for (const auto &part : parts) {
        if (current < 0 || part == "." || part == "..") { if (current >= 0) close(current); return; }
        const auto name = QFile::encodeName(part);
        if (create && mkdirat(current, name.constData(), 0700) && errno != EEXIST) { close(current); return; }
        const int next = openat(current, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(current);
        current = next;
    }
    struct stat st{};
    if (current < 0) return;
    if (fstat(current, &st) || st.st_uid != getuid() || (st.st_mode & 0022)) { close(current); return; }
    fd = current;
}

OwnedDirectory::~OwnedDirectory() { if (fd >= 0) close(fd); }

int OwnedDirectory::parent(const QString &relative, bool create, QByteArray &name) const
{
    if (fd < 0 || relative.isEmpty() || relative.startsWith('/') || relative.contains(QChar(0))) return -1;
    const auto parts = relative.split('/');
    for (const auto &part : parts) if (part.isEmpty() || part == "." || part == "..") return -1;
    int current = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
        const auto component = QFile::encodeName(parts[i]);
        if (create && mkdirat(current, component.constData(), 0700) && errno != EEXIST) { close(current); return -1; }
        const int next = openat(current, component.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(current);
        if (next < 0) return -1;
        struct stat st{};
        if (fstat(next, &st) || st.st_uid != getuid() || (st.st_mode & 0022)) { close(next); return -1; }
        current = next;
    }
    name = QFile::encodeName(parts.last());
    return current;
}

int OwnedDirectory::open(const QString &relative, int flags, bool createParents) const
{
    QByteArray name;
    const int dir = parent(relative, createParents, name);
    if (dir < 0) return -1;
    // O_TRUNC is delayed until after link/ownership checks.
    const int file = openat(dir, name.constData(), (flags & ~O_TRUNC) | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    close(dir);
    if (file < 0) return -1;
    if (!ownedFile(file) || ((flags & O_TRUNC) && ftruncate(file, 0))) { close(file); return -1; }
    return file;
}

QByteArray OwnedDirectory::read(const QString &relative, qint64 maximum, bool *ok) const
{
    if (ok) *ok = false;
    const int file = open(relative, O_RDONLY);
    if (file < 0) return {};
    QFile input;
    if (!input.open(file, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) { close(file); return {}; }
    if (input.size() > maximum) return {};
    const auto bytes = input.read(maximum + 1);
    if (bytes.size() > maximum || input.error() != QFileDevice::NoError) return {};
    if (ok) *ok = true;
    return bytes;
}

bool OwnedDirectory::replace(const QString &relative, const QByteArray &bytes) const
{
    QByteArray name;
    const int dir = parent(relative, true, name);
    if (dir < 0) return false;
    struct stat before{};
    const bool exists = fstatat(dir, name.constData(), &before, AT_SYMLINK_NOFOLLOW) == 0;
    if (exists && (!S_ISREG(before.st_mode) || before.st_uid != getuid() || before.st_nlink != 1)) { close(dir); return false; }
    const auto temporary = ".write-" + QUuid::createUuid().toByteArray(QUuid::Id128);
    const int file = openat(dir, temporary.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file < 0) { close(dir); return false; }
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::write(file, bytes.constData() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += count;
    }
    const bool synced = offset == bytes.size() && fsync(file) == 0;
    close(file);
    const bool success = synced && renameat(dir, temporary.constData(), dir, name.constData()) == 0;
    if (!success) unlinkat(dir, temporary.constData(), 0);
    else fsync(dir);
    close(dir);
    return success;
}

bool OwnedDirectory::same(const struct stat &a, const struct stat &b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_uid == b.st_uid
        && a.st_size == b.st_size && a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec;
}

bool OwnedDirectory::identity(const QString &relative, struct stat &out) const
{
    const int file = open(relative, O_RDONLY);
    if (file < 0) return false;
    const bool ok = fstat(file, &out) == 0;
    close(file);
    return ok;
}

bool OwnedDirectory::remove(const QString &relative, const struct stat &expected) const
{
    QByteArray name;
    const int dir = parent(relative, false, name);
    if (dir < 0) return false;
    struct stat current{};
    const bool ok = fstatat(dir, name.constData(), &current, AT_SYMLINK_NOFOLLOW) == 0
        && S_ISREG(current.st_mode) && current.st_uid == getuid() && current.st_nlink == 1
        && same(current, expected) && unlinkat(dir, name.constData(), 0) == 0;
    close(dir);
    return ok;
}

bool OwnedDirectory::rename(const QString &from, const QString &to) const
{
    QByteArray source, destination;
    const int a = parent(from, false, source), b = parent(to, true, destination);
    struct stat st{};
    const bool ok = a >= 0 && b >= 0 && fstatat(a, source.constData(), &st, AT_SYMLINK_NOFOLLOW) == 0
        && S_ISREG(st.st_mode) && st.st_uid == getuid() && st.st_nlink == 1
        && renameat2(a, source.constData(), b, destination.constData(), RENAME_NOREPLACE) == 0;
    if (ok) { fsync(a); fsync(b); }
    if (a >= 0) close(a);
    if (b >= 0) close(b);
    return ok;
}

QStringList OwnedDirectory::files(bool *ok) const
{
    QStringList out;
    const bool success = fd >= 0 && walkFiles(fd, {}, out);
    if (ok) *ok = success;
    out.sort();
    return out;
}

quint64 OwnedDirectory::available() const
{
    struct statvfs st{};
    if (fd < 0 || fstatvfs(fd, &st)) return 0;
    return quint64(st.f_bavail) * st.f_frsize;
}
}
