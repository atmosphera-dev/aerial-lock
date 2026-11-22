// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "interactive_playground_scene_export.h"
#include <QColor>
#include <QFile>
#include <QStringList>
#include <utility>

// Read-only playground index, independent of appearance and the append writer.
class INTERACTIVE_PLAYGROUND_SCENE_EXPORT PaletteSampleHistory
{
public:
    struct Result {
        QStringList keys;
        bool available = false;
        quint64 skipped = 0;
        QString status;
    };
    explicit PaletteSampleHistory(QString filename) : m_filename(std::move(filename)) {}
    virtual ~PaletteSampleHistory() = default;
    Result read() const;
    static QString opaqueAccentKey(const QColor &color);

protected:
    // Deterministic I/O failure/race injection in the actual reader's unit tests.
    virtual QByteArray readChunk(QFile &file, qint64 maximum) const;

private:
    QString m_filename;
};
