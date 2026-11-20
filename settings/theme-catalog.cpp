#include "theme-catalog.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QBuffer>
#include <QCryptographicHash>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Aerial::Settings {
ThemeCatalog::ThemeCatalog(Client::ServiceConnection &service, QObject *parent) : QObject(parent), service(service)
{
    connect(&service, &Client::ServiceConnection::changed, this, &ThemeCatalog::sample);
    const auto invalidated = [this] { user.clear(); sample(); };
    connect(&watcher, &QFileSystemWatcher::directoryChanged, this, invalidated);
    connect(&watcher, &QFileSystemWatcher::fileChanged, this, invalidated);
    sample();
}
void ThemeCatalog::sample()
{
    if (!service.ready()) return;
    const auto paths = service.snapshot().properties.value("Paths").toMap();
    const auto bundleRoot = paths.value("bundle").toString(), config = paths.value("config").toString();
    if (bundleRoot.isEmpty() || config.isEmpty()) return;
    if (bundle == bundleRoot + "/Themes" && user == QFileInfo(config).absolutePath() + "/themes") return;
    bundle = bundleRoot + "/Themes"; user = QFileInfo(config).absolutePath() + "/themes";
    QMap<QString, QString> files;
    for (const auto &root : {bundle, user}) for (const auto &name : QDir(root).entryList({"*.qml"}, QDir::Files, QDir::Name)) files[name] = root;
    QStringList order{"Macintosh.qml", "Windows7.qml", "MaterialYou.qml", "Enfield.qml", "Clockwork.qml"};
    for (const auto &name : files.keys()) if (!order.contains(name)) order << name;
    QVariantList next; QStringList watches;
    // QString URLs use UTF-16. Old and new catalogs can coexist during a
    // rescan, so each catalog owns at most 8 MiB of encoded preview text.
    qint64 remaining = 8 * 1024 * 1024;
    QMap<QByteArray, bool> nextValidated;
    for (const auto &root : {bundle, user, QFileInfo(user).absolutePath()}) if (QFileInfo::exists(root)) watches << root;
    QString ancestor = user;
    while (!QFileInfo(ancestor).isDir() && ancestor != "/") ancestor = QFileInfo(ancestor).absolutePath();
    watches << ancestor;
    for (const auto &name : order) {
        if (!files.contains(name)) continue;
        const auto root = files.value(name); const auto preview = root + "/previews/" + QFileInfo(name).completeBaseName() + ".png";
        QString title = QFileInfo(name).completeBaseName();
        if (name == "Windows7.qml") title = "Windows 7";
        if (name == "MaterialYou.qml") title = "Material You";
        // Opening a FIFO must not block; validate the opened inode, not a
        // pathname check that can race replacement. All rows remain selectable.
        QFile file;
        const auto fd = ::open(QFile::encodeName(preview).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        struct stat info{};
        const bool regular = fd >= 0 && ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode);
        if (fd >= 0 && (!regular || !file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle))) ::close(fd);
        const QString prefix = "data:image/png;base64,";
        const qint64 maxBytes = qMax<qint64>(0, (remaining / 2 - prefix.size()) / 4 * 3);
        QByteArray bytes;
        if (file.isOpen() && info.st_size > 0 && info.st_size <= maxBytes) bytes = file.read(maxBytes + 1);
        QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "png");
        const bool readable = !bytes.isEmpty() && bytes.size() <= maxBytes && reader.canRead();
        const auto size = readable ? reader.size() : QSize();
        // PNG does not support scaled decoding in Qt: setScaledSize alone
        // still allocates the full image. Bound pixels before any decode.
        const bool bounded = readable && size.width() > 0 && size.height() > 0
            && size.width() <= 8192 && size.height() <= 8192 && qint64(size.width()) * size.height() <= 1024 * 1024;
        const auto hash = bounded ? QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) : QByteArray();
        const bool valid = bounded && (validatedPreviews.contains(hash) ? validatedPreviews.value(hash) : !reader.read().isNull());
        if (bounded) nextValidated[hash] = valid;
        const auto url = valid ? prefix + QString::fromLatin1(bytes.toBase64()) : QString();
        remaining -= url.size() * 2;
        next << QVariantMap{{"name", name}, {"title", title}, {"preview", url}, {"user", root == user}};
        if (QFileInfo::exists(root + "/previews")) watches << root + "/previews";
        if (QFileInfo::exists(preview)) watches << preview;
    }
    watches.removeDuplicates();
    validatedPreviews = std::move(nextValidated);
    const auto old = watcher.directories() + watcher.files();
    for (const auto &path : old) if (!watches.contains(path)) watcher.removePath(path);
    for (const auto &path : watches) if (!old.contains(path)) watcher.addPath(path);
    if (next != themes) { themes = next; emit changed(); }
}
}
