// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PaletteSampleHistory.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
bool decodeColor(const QJsonValue &value, QColor *color)
{
    if (!value.isObject())
        return false;
    const auto object = value.toObject();
    const auto rgbaValue = object.value(QStringLiteral("rgba16"));
    const auto hexValue = object.value(QStringLiteral("hexArgb"));
    if (!rgbaValue.isArray() || !hexValue.isString())
        return false;
    const auto rgba = rgbaValue.toArray();
    if (rgba.size() != 4)
        return false;
    quint16 channels[4];
    for (int i = 0; i < 4; ++i) {
        if (!rgba[i].isDouble())
            return false;
        const double number = rgba[i].toDouble();
        if (!std::isfinite(number) || number < 0 || number > 65535 || std::floor(number) != number)
            return false;
        channels[i] = quint16(number);
    }
    const QColor decoded = QColor::fromRgba64(channels[0], channels[1], channels[2], channels[3]);
    static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{8}$"));
    const QString display = hexValue.toString();
    if (!hex.match(display).hasMatch()
        || display.compare(decoded.name(QColor::HexArgb), Qt::CaseInsensitive) != 0)
        return false;
    *color = decoded;
    return true;
}

bool decodeRecord(const QByteArray &line, QColor *accent)
{
    const auto document = QJsonDocument::fromJson(line);
    if (!document.isObject())
        return false;
    const auto record = document.object();
    const auto version = record.value(QStringLiteral("version"));
    const auto utc = record.value(QStringLiteral("utc"));
    const auto scheme = record.value(QStringLiteral("scheme"));
    const auto contrast = record.value(QStringLiteral("contrast"));
    const auto corrections = record.value(QStringLiteral("corrections"));
    if (!version.isDouble() || version.toDouble() != 1 || !utc.isString()
        || !utc.toString().endsWith(QLatin1Char('Z'))
        || !QDateTime::fromString(utc.toString(), Qt::ISODateWithMs).isValid()
        || !scheme.isString() || (scheme.toString() != QLatin1String("light") && scheme.toString() != QLatin1String("dark"))
        || !contrast.isString() || (contrast.toString() != QLatin1String("normal") && contrast.toString() != QLatin1String("high"))
        || !corrections.isObject())
        return false;
    const auto flags = corrections.toObject();
    if (!flags.value(QStringLiteral("amber")).isBool() || !flags.value(QStringLiteral("danger")).isBool())
        return false;
    QColor amber, danger;
    return decodeColor(record.value(QStringLiteral("accent")), accent)
        && decodeColor(record.value(QStringLiteral("amber")), &amber)
        && decodeColor(record.value(QStringLiteral("danger")), &danger);
}
} // namespace

QString PaletteSampleHistory::opaqueAccentKey(const QColor &color)
{
    if (!color.isValid() || color.toRgb().rgba64().alpha() != 65535)
        return {};
    return color.toRgb().name(QColor::HexRgb);
}

QByteArray PaletteSampleHistory::readChunk(QFile &file, qint64 maximum) const
{
    return file.read(maximum);
}

PaletteSampleHistory::Result PaletteSampleHistory::read() const
{
    const auto unavailable = [](const QString &reason) {
        return Result{{}, false, 0, QStringLiteral("Saved-accent history unavailable: %1").arg(reason)};
    };
    if (m_filename.isEmpty() || !QDir::isAbsolutePath(m_filename))
        return unavailable(QStringLiteral("an absolute sample destination is required."));
    const QFileInfo info(m_filename);
    if (info.isSymLink() && !info.exists())
        return unavailable(QStringLiteral("the destination is a dangling link."));
    // Nonblocking open + descriptor stat avoids a FIFO/device replacement race
    // between a pathname check and opening. Only regular descriptors are read.
    const QByteArray encoded = QFile::encodeName(m_filename);
    const int descriptor = ::open(encoded.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0) {
        const int error = errno;
        if (error == ENOENT)
            return {{}, true, 0, QStringLiteral("Saved accents: 0 (no sample file yet).")};
        return unavailable(QString::fromLocal8Bit(std::strerror(error)));
    }
    struct stat metadata;
    if (::fstat(descriptor, &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
        ::close(descriptor);
        return unavailable(QStringLiteral("the destination is not a readable regular file."));
    }
    QFile file;
    if (!file.open(descriptor, QIODevice::ReadOnly | QIODevice::Unbuffered, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor);
        return unavailable(file.errorString());
    }
    // Fixed starting boundary: concurrent appends are observed on a later refresh.
    qint64 remaining = metadata.st_size;
    QSet<QString> keys;
    quint64 skipped = 0;
    QByteArray line;
    line.reserve(4096);
    bool oversized = false;
    while (remaining > 0) {
        const auto chunk = readChunk(file, std::min<qint64>(remaining, 1024));
        if (chunk.isEmpty() || file.error() != QFileDevice::NoError)
            return unavailable(QStringLiteral("cannot complete the bounded read: %1").arg(file.errorString()));
        remaining -= chunk.size();
        for (const char character : chunk) {
            if (character == '\n') {
                QColor accent;
                if (oversized || !decodeRecord(line, &accent)) {
                    ++skipped;
                } else {
                    const QString key = opaqueAccentKey(accent);
                    if (!key.isEmpty())
                        keys.insert(key);
                }
                line.clear();
                oversized = false;
            } else if (!oversized) {
                // The writer's 4096-byte bound includes the terminating LF.
                if (line.size() == 4095) {
                    line.clear();
                    oversized = true;
                } else {
                    line.append(character);
                }
            }
        }
    }
    if (oversized || !line.isEmpty())
        ++skipped;
    QStringList sorted(keys.begin(), keys.end());
    std::sort(sorted.begin(), sorted.end());
    const QString status = skipped
        ? QStringLiteral("Saved accents: %1; skipped %2 invalid or incomplete records.").arg(sorted.size()).arg(skipped)
        : QStringLiteral("Saved accents: %1.").arg(sorted.size());
    return {sorted, true, skipped, status};
}
