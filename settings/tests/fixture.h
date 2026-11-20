#pragma once
#include "settings-controller.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
namespace Aerial::Settings::Test {
struct Fixture {
    QTemporaryDir root;
    QProcess daemon;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    ConfigPaths paths;
    std::unique_ptr<Client::ServiceConnection> service;
    std::unique_ptr<SettingsController> controller;
    bool start()
    {
        const QString source = AERIAL_SETTINGS_TEST_SOURCE;
        environment.insert("AERIAL_LOCK_SHELL", source);
        for (const auto &key : {"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME", "XDG_RUNTIME_DIR"}) environment.insert(key, root.path() + "/" + key);
        paths = {source, environment.value("XDG_CONFIG_HOME") + "/aerial-lock/config.json", environment.value("XDG_CACHE_HOME") + "/aerial-lock/managed-v1",
            environment.value("XDG_STATE_HOME") + "/aerial-lock", environment.value("XDG_RUNTIME_DIR") + "/aerial-lock"};
        if (!QDir().mkpath(QFileInfo(paths.config).absolutePath())) return false;
        QFile config(paths.config);
        if (!config.open(QIODevice::WriteOnly)) return false;
        config.write("{\"sources\":[],\"acquisition\":{\"automatic\":{\"enabled\":false}}}"); config.close();
        daemon.setProcessEnvironment(environment);
        daemon.start(source + "/companion/aerial", {});
        if (!daemon.waitForStarted()) return false;
        service = std::make_unique<Client::ServiceConnection>(QDBusConnection::sessionBus(), paths, source);
        controller = std::make_unique<SettingsController>(*service);
        service->start();
        return QTest::qWaitFor([this] { return service->ready(); }, 5000);
    }
    ~Fixture()
    {
        controller.reset(); service.reset();
        if (daemon.state() != QProcess::NotRunning) { daemon.terminate(); if (!daemon.waitForFinished(5000)) { daemon.kill(); daemon.waitForFinished(); } }
    }
    QByteArray cli(const QStringList &args)
    {
        QProcess process; process.setProcessEnvironment(environment);
        process.start(QString(AERIAL_SETTINGS_TEST_SOURCE) + "/cli/aerialctl", args);
        if (!process.waitForFinished(10000) || process.exitCode()) return {};
        return process.readAllStandardOutput();
    }
    QVariantMap control(const QString &key) const { return controller->controls().value(key).toMap(); }
};
}
