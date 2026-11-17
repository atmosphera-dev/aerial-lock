#include "service.h"
#include "companion_adaptor.h"
#include "service.moc"
#include <QDBusMessage>
#include <QJsonDocument>

namespace Aerial {
CompanionService::CompanionService(ConfigRepository &repository, QDBusConnection connection, QObject *parent)
    : QObject(parent), config(repository), bus(std::move(connection))
{
    new Companion1Adaptor(this);
    connect(&config, &ConfigRepository::changed, this, [this](const QString &revision, const QStringList &paths) {
        emit SettingsChanged(revision, paths);
        auto signal = QDBusMessage::createSignal("/org/aeriallock/Companion1", "org.freedesktop.DBus.Properties", "PropertiesChanged");
        signal << QString("org.aeriallock.Companion1") << QVariantMap{{"ConfigRevision", revision}} << QStringList{};
        bus.send(signal);
    });
}

bool CompanionService::start()
{
    // Methods are available before the non-replacing bus name is claimed.
    if (!bus.registerObject("/org/aeriallock/Companion1", this, QDBusConnection::ExportAdaptors)) return false;
    if (!bus.registerService("org.aeriallock.Companion1")) {
        bus.unregisterObject("/org/aeriallock/Companion1");
        return false;
    }
    return true;
}

QString CompanionService::GetSettings(QString &effective, QString &revision)
{
    config.reload();
    const auto &doc = config.document();
    effective = QString::fromUtf8(QJsonDocument(doc.effective).toJson(QJsonDocument::Compact));
    revision = doc.revision;
    return QString::fromUtf8(QJsonDocument(doc.user).toJson(QJsonDocument::Compact));
}

void CompanionService::error(const ConfigTransaction &result)
{
    if (!result.ok() && calledFromDBus())
        sendErrorReply("org.aeriallock.Companion1.Error." + result.error, result.diagnostics.join("; "));
}

QString CompanionService::UpdateSettings(const QString &expected, const QString &patch)
{
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(patch.toUtf8(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        error({{}, "InvalidSetting", {"merge patch must be a JSON object"}});
        return {};
    }
    const auto result = config.update(expected, document.object());
    error(result);
    return result.revision;
}

void CompanionService::SetPaused(bool paused)
{
    config.reload();
    error(config.update(config.document().revision, {{"acquisition", QJsonObject{{"automatic", QJsonObject{{"enabled", !paused}}}}}}));
}
}
