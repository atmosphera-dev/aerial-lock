#pragma once
#include <QDBusConnection>
#include <QJsonObject>
#include <QObject>
#include <QVariantMap>

namespace Aerial {
// A wire projection of a durable queue record, never an independent job owner.
class JobObject : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString State READ state)
    Q_PROPERTY(QString Kind READ kind)
    Q_PROPERTY(QString SourceId READ sourceId)
    Q_PROPERTY(QString AssetId READ assetId)
    Q_PROPERTY(QString AssetKey READ assetKey)
    Q_PROPERTY(qulonglong ReceivedBytes READ received)
    Q_PROPERTY(qulonglong TotalBytes READ total)
    Q_PROPERTY(bool TotalKnown READ totalKnown)
    Q_PROPERTY(uint ReadyClips READ ready)
    Q_PROPERTY(QString ErrorCode READ errorCode)
    Q_PROPERTY(bool Cancellable READ cancellable)
    Q_PROPERTY(QVariantMap Details READ details)
public:
    JobObject(QString path, QDBusConnection bus, QObject *parent);
    void update(const QJsonObject &record);
    QString state() const { return record.value("state").toString(); }
    QString kind() const { return record.value("kind").toString(); }
    QString sourceId() const { return record.value("sourceId").toString(); }
    QString assetId() const { return record.value("assetId").toString(); }
    QString assetKey() const { return record.value("assetKey").toString(); }
    qulonglong received() const { return record.value("received").toInteger(); }
    qulonglong total() const { return record.value("total").toInteger(); }
    bool totalKnown() const { return record.value("totalKnown").toBool(); }
    uint ready() const { return record.value("ready").toInt(); }
    QString errorCode() const { return record.value("errorCode").toString(); }
    bool cancellable() const { return !record.contains("finishedAt"); }
    QVariantMap details() const { return record.toVariantMap(); }
signals:
    void Finished(const QString &result, const QString &errorCode);
private:
    QString path;
    QDBusConnection bus;
    QJsonObject record;
};
}
