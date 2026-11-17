#pragma once
#include "../common/config-document.h"
#include <QFileSystemWatcher>
#include <QObject>

namespace Aerial {
struct ConfigTransaction {
    QString revision, error;
    QStringList diagnostics;
    bool ok() const { return error.isEmpty(); }
};

// Sole application settings writer. Call on its owner thread; all cooperating
// writers additionally serialize using the same on-disk advisory edit lock.
class ConfigRepository : public QObject {
    Q_OBJECT
public:
    explicit ConfigRepository(ConfigPaths paths, QObject *parent = nullptr);
    const ConfigDocument &document() const { return current; }
    void reload();
    ConfigTransaction update(const QString &revision, const QJsonObject &patch);
    ConfigTransaction observeHeight(int height);
    ConfigTransaction migrate();
signals:
    void changed(const QString &revision, const QStringList &paths);
private:
    ConfigTransaction transact(const QString &revision, const QJsonObject &patch, bool migration);
    bool prepareDirectory();
    void watch();
    ConfigPaths paths;
    ConfigDocument current;
    QFileSystemWatcher watcher;
};
}
