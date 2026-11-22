// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PaletteSampleWriter.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>

qint64 PaletteSampleWriter::writeBatch(QFile &file, const QByteArray &batch)
{
    return file.write(batch);
}

bool PaletteSampleWriter::flush(QFile &file)
{
    return file.flush();
}

bool PaletteSampleWriter::close(QFile &file, QString *error)
{
    // close() returns void and can clear a previously reported error.
    const bool before = file.error() == QFileDevice::NoError;
    const QString detail = before ? QString() : file.errorString();
    file.close();
    const bool after = file.error() == QFileDevice::NoError;
    if (!before || !after)
        *error = before ? file.errorString() : detail;
    return before && after;
}

bool PaletteSampleWriter::readBack(QFile &file, qint64 offset, const QByteArray &batch, QString *error)
{
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    if (file.size() != offset + batch.size() || !file.seek(offset)
        || file.read(batch.size()) != batch || file.error() != QFileDevice::NoError) {
        *error = QStringLiteral("Appended size or suffix did not match disk readback: %1")
                     .arg(file.errorString());
        file.close();
        return false;
    }
    return close(file, error);
}

PaletteSampleWriter::Result PaletteSampleWriter::append(const QByteArray &line)
{
    const auto failure = [](const QString &error, bool uncertain = false) {
        return Result{false, uncertain, error};
    };
    if (m_filename.isEmpty() || !QDir::isAbsolutePath(m_filename))
        return failure(QStringLiteral("An absolute sample destination is required."));
    if (line.isEmpty() || line.size() > 4096 || !line.endsWith('\n')
        || line.left(line.size() - 1).contains('\n')
        || !QJsonDocument::fromJson(line).isObject())
        return failure(QStringLiteral("Sample must be one JSON object plus LF, at most 4096 bytes."));
    const auto regularOrAbsent = [this] {
        const QFileInfo info(m_filename);
        // A dangling symlink is neither an absent destination nor a regular file.
        return (!info.exists() && !info.isSymLink()) || info.isFile();
    };
    if (!regularOrAbsent())
        return failure(QStringLiteral("Sample destination is not a regular file."));
    const QFileInfo info(m_filename);
    if (!QDir().mkpath(info.absolutePath()))
        return failure(QStringLiteral("Cannot create the sample parent directory."));
    QLockFile lock(m_filename + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return failure(QStringLiteral("Cannot acquire sample lock (busy or inaccessible; error %1).")
                           .arg(int(lock.error())));
    if (!regularOrAbsent())
        return failure(QStringLiteral("Sample destination is not a regular file."));
    QFile file(m_filename);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Append | QIODevice::Unbuffered))
        return failure(QStringLiteral("Cannot open sample file: %1").arg(file.errorString()));
    const qint64 before = file.size();
    if (before < 0)
        return failure(QStringLiteral("Cannot inspect sample length: %1").arg(file.errorString()));
    QByteArray batch;
    if (before > 0) {
        if (!file.seek(before - 1))
            return failure(QStringLiteral("Cannot inspect sample tail: %1").arg(file.errorString()));
        const QByteArray tail = file.read(1);
        if (tail.size() != 1 || file.error() != QFileDevice::NoError)
            return failure(QStringLiteral("Cannot read sample tail: %1").arg(file.errorString()));
        if (tail != "\n")
            batch.append('\n');
    }
    batch.append(line);
    const qint64 written = writeBatch(file, batch);
    if (written != batch.size()) {
        const QString detail = file.errorString();
        file.close();
        return failure(QStringLiteral("Incomplete append (%1/%2 bytes): %3")
                           .arg(written).arg(batch.size()).arg(detail), true);
    }
    if (!flush(file)) {
        const QString detail = file.errorString();
        file.close();
        return failure(QStringLiteral("Sample flush failed: %1").arg(detail), true);
    }
    QString error;
    if (!close(file, &error))
        return failure(QStringLiteral("Sample close failed: %1").arg(error), true);
    if (!readBack(file, before, batch, &error))
        return failure(QStringLiteral("Sample readback failed: %1").arg(error), true);
    return {true, false, {}};
}
