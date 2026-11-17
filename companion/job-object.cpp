#include "job-object.h"
#include "job_adaptor.h"
#include "job-object.moc"
#include <QDBusMessage>

namespace Aerial {
JobObject::JobObject(QString path, QDBusConnection bus, QObject *parent)
    : QObject(parent), path(std::move(path)), bus(std::move(bus))
{
    new Job1Adaptor(this);
}
void JobObject::update(const QJsonObject &value)
{
    if (record == value) return;
    const bool finished = value.contains("finishedAt") && !record.contains("finishedAt");
    record = value;
    auto signal = QDBusMessage::createSignal(path, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    signal << QString("org.aeriallock.Companion1.Job1") << QVariantMap{
        {"State", state()}, {"Kind", kind()}, {"SourceId", sourceId()}, {"AssetId", assetId()}, {"AssetKey", assetKey()},
        {"ReceivedBytes", QVariant::fromValue(received())}, {"TotalBytes", QVariant::fromValue(total())},
        {"TotalKnown", totalKnown()}, {"ReadyClips", ready()}, {"ErrorCode", errorCode()},
        {"Cancellable", cancellable()}, {"Details", details()}} << QStringList{};
    bus.send(signal);
    if (finished) emit Finished(state(), errorCode());
}
}
