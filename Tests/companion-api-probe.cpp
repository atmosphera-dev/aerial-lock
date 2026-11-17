#include "companion_client.h"
#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 3) return 2;
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("AERIAL_LOCK_SHELL", QString::fromLocal8Bit(argv[2]));
    for (const auto &key : {"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME", "XDG_RUNTIME_DIR"})
        env.insert(key, root.path() + "/" + key);
    QProcess service;
    service.setProcessEnvironment(env);
    service.start(QString::fromLocal8Bit(argv[1]), {});
    if (!service.waitForStarted(5000)) return 1;
    struct Retire { QProcess &process; ~Retire() { process.terminate(); if (!process.waitForFinished(5000)) { process.kill(); process.waitForFinished(5000); } } } retire{service};
    auto bus = QDBusConnection::sessionBus();
    QElapsedTimer deadline; deadline.start();
    while (!bus.interface()->isServiceRegistered("org.aeriallock.Companion1").value() && deadline.elapsed() < 5000) {
        if (service.state() == QProcess::NotRunning) qFatal("service failed to start: %s", service.readAllStandardError().constData());
        QTest::qWait(10);
    }
    CompanionClient client("org.aeriallock.Companion1", "/org/aeriallock/Companion1", bus);
    auto first = client.GetSettings(); first.waitForFinished();
    if (first.isError()) qFatal("GetSettings: %s", qPrintable(first.error().message()));
    const auto revision = first.argumentAt<2>();
    auto accepted = client.UpdateSettings(revision, R"({"idle":{"timeoutSeconds":42}})"); accepted.waitForFinished();
    if (accepted.isError()) qFatal("UpdateSettings: %s", qPrintable(accepted.error().message()));
    auto conflict = client.UpdateSettings(revision, R"({"language":"fr"})"); conflict.waitForFinished();
    if (!conflict.isError() || !conflict.error().name().endsWith(".Conflict")) qFatal("stale API update did not conflict");
    auto after = client.GetSettings(); after.waitForFinished();
    auto user = QJsonDocument::fromJson(after.argumentAt<0>().toUtf8()).object();
    if (user.contains("language") || user.value("idle").toObject().value("timeoutSeconds").toInt() != 42) qFatal("transaction data lost");
    auto invalid = client.UpdateSettings(after.argumentAt<2>(), R"({"cache":{"maxBytes":-1},"language":"fr"})"); invalid.waitForFinished();
    if (!invalid.isError() || !invalid.error().name().endsWith(".InvalidSetting")) qFatal("invalid update accepted");
    auto paused = client.SetPaused(true); paused.waitForFinished();
    if (paused.isError()) qFatal("SetPaused failed");
    after = client.GetSettings(); after.waitForFinished();
    if (QJsonDocument::fromJson(after.argumentAt<1>().toUtf8()).object().value("acquisition").toObject().value("automatic").toObject().value("enabled").toBool()) qFatal("pause did not persist");
    // External exact-byte replacement is observed on the next transaction.
    const auto path = env.value("XDG_CONFIG_HOME") + "/aerial-lock/config.json";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
    file.write("{ invalid"); file.close();
    auto stale = client.UpdateSettings(after.argumentAt<2>(), R"({"language":"fr"})"); stale.waitForFinished();
    if (!stale.isError() || !stale.error().name().endsWith(".Conflict")) qFatal("external edit was overwritten");
    after = client.GetSettings(); after.waitForFinished();
    invalid = client.UpdateSettings(after.argumentAt<2>(), R"({"language":"fr"})"); invalid.waitForFinished();
    if (!invalid.isError() || !invalid.error().name().endsWith(".InvalidSetting")) qFatal("malformed config not protected");
    if (!file.open(QIODevice::ReadOnly) || file.readAll() != "{ invalid") qFatal("malformed bytes changed");
    puts("COMPANION_API_PASS: actual generated client/service, revision conflicts, sparse settings, pause and malformed-file preservation");
    return 0;
}
