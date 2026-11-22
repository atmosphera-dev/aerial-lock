// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "interactive_playground_scene_export.h"
#include <QFile>
#include <QString>

// Playground persistence only. The caller supplies a destination and a bounded
// serialized snapshot; this class never reads or replaces sample history.
class INTERACTIVE_PLAYGROUND_SCENE_EXPORT PaletteSampleWriter
{
public:
    struct Result {
        bool success = false;
        bool uncertain = false;
        QString error;
    };
    explicit PaletteSampleWriter(const QString &filename) : m_filename(filename) {}
    virtual ~PaletteSampleWriter() = default;
    QString filename() const { return m_filename; }
    Result append(const QByteArray &line);

protected:
    // Narrow I/O seam for deterministic failure proofs against the real writer.
    virtual qint64 writeBatch(QFile &file, const QByteArray &batch);
    virtual bool flush(QFile &file);
    virtual bool close(QFile &file, QString *error);
    virtual bool readBack(QFile &file, qint64 offset, const QByteArray &batch, QString *error);

private:
    const QString m_filename;
};
