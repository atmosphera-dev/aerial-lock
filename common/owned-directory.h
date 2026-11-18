#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <sys/stat.h>

namespace Aerial {
// Directory-FD relative access. Never follow symlinks, including in parent
// components, and never adopt multiply-linked or foreign-owned files.
class OwnedDirectory {
public:
    enum class Mutation { NotApplied, Unsynchronized, Durable };
    explicit OwnedDirectory(const QString &path, bool create = false);
    ~OwnedDirectory();
    OwnedDirectory(const OwnedDirectory &) = delete;
    OwnedDirectory &operator=(const OwnedDirectory &) = delete;
    bool valid() const { return fd >= 0; }
    QString path() const { return root; }
    int open(const QString &relative, int flags, bool createParents = false) const;
    QByteArray read(const QString &relative, qint64 maximum, bool *ok = nullptr) const;
    bool replace(const QString &relative, const QByteArray &bytes) const;
    bool remove(const QString &relative, const struct stat &expected) const;
    bool rename(const QString &from, const QString &to) const;
    Mutation lastMutation() const { return mutation; }
    bool synchronizeParent(const QString &relative) const;
    bool identity(const QString &relative, struct stat &out) const;
    QStringList files(bool *ok = nullptr) const;
    quint64 available() const;
    static bool same(const struct stat &a, const struct stat &b);
private:
    int parent(const QString &relative, bool create, QByteArray &name) const;
    QString root;
    int fd = -1;
    mutable Mutation mutation = Mutation::NotApplied;
};
}
