#include "../supervisor/emergency-settings.h"
#include <QCoreApplication>
#include <QJSEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QTemporaryDir>
#include "test-check.h"
#include <memory>

static QByteArray read(const QString &path) { QFile f(path); CHECK(f.open(QIODevice::ReadOnly)); return f.readAll(); }
static void write(const QString &path, const QByteArray &bytes) { QFile f(path); CHECK(f.open(QIODevice::WriteOnly)); CHECK(f.write(bytes)==bytes.size()); }
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv); CHECK(argc==2);
    QString source=QString::fromLocal8Bit(argv[1]);
    QString qml=QString::fromUtf8(read(source+"/Services/ConfigStore.qml"));
    QRegularExpression functions("^    function .*?^    }",QRegularExpression::MultilineOption|QRegularExpression::DotMatchesEverythingOption);
    auto matches=functions.globalMatch(qml); QString methods;
    while (matches.hasNext()) methods+=matches.next().captured()+"\n";
    QString schema=qml.section("    readonly property var schema: (",1).section("\n    })",0,0)+"\n    }";
    QJSEngine js;
    auto value=js.evaluate("var Logger={w:function(){}};\n"+methods+"\nvar schema=("+schema+");"); CHECK(!value.isError());
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData("import QtQml\nQtObject { property real source: 0; property int value: source }",QUrl());
    std::unique_ptr<QObject> conversion(component.create()); CHECK(conversion);
    QTemporaryDir tmp; CHECK(tmp.isValid());
    QDir().mkpath(tmp.path()+"/Config");
    QByteArray defaults=read(source+"/Config/defaults.json");
    write(tmp.path()+"/Config/defaults.json",defaults);
    QString user=tmp.path()+"/user.json";
    const QList<QByteArray> users={"{}","{\"pamService\":\"custom\",\"pamWatchdogTimeoutMs\":890}",
        "{\"pamService\":null,\"pamWatchdogTimeoutMs\":\"wrong\"}","{\"pamService\":\"\"}",
        "{\"pamService\":12,\"pamWatchdogTimeoutMs\":false}","{\"pamWatchdogTimeoutMs\":0}",
        "{\"pamWatchdogTimeoutMs\":10.8}","{\"pamWatchdogTimeoutMs\":2147483647}","broken", "null", "[]"};
    unsigned count=0;
    for (const auto &bytes:users) for (const QString &override: {QString(),QString("env-custom")}) {
        write(user,bytes);
        auto settings=emergencySettings(tmp.path(),user,override);
        js.globalObject().setProperty("bundleText",QString::fromUtf8(defaults));
        js.globalObject().setProperty("userText",QString::fromUtf8(bytes));
        auto merged=js.evaluate("var defaults=JSON.parse(bundleText); var merged=defaults; try { var user=JSON.parse(userText); migrateUserConfig(user); merged=deepMerge(defaults,user); sanitise(merged,defaults,schema,''); } catch(e) { merged=defaults; } merged;");
        CHECK(!merged.isError());
        QString service=override.isEmpty()?merged.property("pamService").toString():override;
        if (service.isEmpty()) service="aerial-lock";
        CHECK(settings.service==service);
        conversion->setProperty("source",merged.property("pamWatchdogTimeoutMs").toNumber());
        if (settings.watchdog!=conversion->property("value").toInt()) qFatal("watchdog conversion mismatch: %s native=%d qml=%d",bytes.constData(),settings.watchdog,conversion->property("value").toInt());
        CHECK(read(user)==bytes); ++count;
    }
    for (const QByteArray &bytes: {QByteArray("{\"pamWatchdogTimeoutMs\":-1}"),QByteArray("{\"pamWatchdogTimeoutMs\":1e30}")}) {
        write(user,bytes); auto settings=emergencySettings(tmp.path(),user,QString());
        CHECK(settings.watchdog==60000 && settings.warnings.contains("auth-watchdog-invalid"));
    }
    write(tmp.path()+"/Config/defaults.json","broken"); write(user,"{}");
    CHECK(emergencySettings(tmp.path(),user,QString()).service.isEmpty());
    CHECK(emergencySettings(tmp.path(),user,"explicit").service=="explicit");
    write(user,"{\"pamService\":\"user-explicit\"}");
    CHECK(emergencySettings(tmp.path(),user,QString()).service=="user-explicit");
    write(user,"{\"pamService\":\"bad\\u0000service\"}");
    CHECK(emergencySettings(tmp.path(),user,QString()).service.isEmpty());
    printf("EMERGENCY_SETTINGS_PASS ConfigStore-conformance=%u invalid-watchdog=2 broken-bundle explicit-override no-writes\n",count);
}
