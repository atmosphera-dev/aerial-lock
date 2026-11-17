#pragma once
#include "../common/content-snapshot.h"
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

// Local episode data only. No content/config service call can delay locking.
class LocalContent : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString generation READ generation NOTIFY changed)
public:
    explicit LocalContent(QObject *parent = nullptr) : QObject(parent) {}
    QString generation() const { return snapshot ? snapshot->generation() : QString(); }
    Q_INVOKABLE void freeze(const QString &cacheRoot, const QString &stateRoot, const QVariantList &sources);
    Q_INVOKABLE QVariantList forSource(const QString &sourceId) const;
    Q_INVOKABLE QVariantMap lookup(const QString &path) const;
    Q_INVOKABLE void reportPlayback(const QString &path);
signals:
    void changed();
private:
    std::unique_ptr<Aerial::ContentSnapshot> snapshot;
    QJsonArray entries;
    QString stateRoot;
    bool frozen = false;
};
