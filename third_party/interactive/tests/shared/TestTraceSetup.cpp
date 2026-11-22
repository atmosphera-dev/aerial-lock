// SPDX-License-Identifier: LGPL-3.0-or-later
#include "TestTraceSetup.h"
#include "AppearanceTrace_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <cstdio>
#include <unistd.h>

namespace InterActive::TestTrace {

namespace {
bool g_active = false;
QString g_dir;
QString g_binary = QStringLiteral("test");
TraceMode g_mode = TraceMode::Off;
}

bool install(const char *argv0)
{
    const QByteArray mode = qgetenv("IA_TEST_TRACE_MODE");
    if (mode.isEmpty())
        return false;
    if (mode != "summary" && mode != "trace") {
        std::fprintf(stderr, "IA_TEST_TRACE_MODE must be summary or trace.\n");
        return false;
    }
    const QByteArray dir = qgetenv("IA_TEST_TRACE_DIR");
    if (dir.isEmpty() || !QFileInfo::exists(QString::fromLocal8Bit(dir))) {
        std::fprintf(stderr, "IA_TEST_TRACE_DIR must name an existing fresh directory.\n");
        return false;
    }
    g_dir = QString::fromLocal8Bit(dir);
    g_mode = mode == "trace" ? TraceMode::Trace : TraceMode::Summary;
    if (argv0 && *argv0)
        g_binary = QString::fromLocal8Bit(argv0).section(QLatin1Char('/'), -1);
    AppearanceTrace::Config config;
    config.capacityMiB = 32;            // test sessions are short
    if (!AppearanceTrace::start(g_mode, config)) {
        std::fprintf(stderr, "Test trace session could not start.\n");
        return false;
    }
    g_active = true;
    return true;
}

bool finalize()
{
    if (!g_active)
        return true;
    g_active = false;
    AppearanceTrace::stop();
    if (!AppearanceTrace::quiesce(1000)) {
        std::fprintf(stderr, "Test trace session did not quiesce.\n");
        return false;
    }
    if (AppearanceTrace::recordsWritten() == 0) {
        std::fprintf(stderr, "Test trace session recorded no records.\n");
        return false;
    }
    const QString base = g_dir + QLatin1Char('/') + g_binary
        + QLatin1Char('-') + QString::number(::getpid());
    const QString tracePath = base + QStringLiteral(".trace.json");
    QString error;
    // Both modes export: summary sessions contain only summary-class records.
    if (!AppearanceTrace::exportTo(tracePath, base + QStringLiteral(".sidecar.json"), &error)) {
        std::fprintf(stderr, "Test trace export failed: %s\n", qPrintable(error));
        return false;
    }
    QJsonObject manifest;
    manifest.insert(QStringLiteral("mode"), g_mode == TraceMode::Trace
        ? QStringLiteral("trace") : QStringLiteral("summary"));
    manifest.insert(QStringLiteral("recordsWritten"), QString::number(AppearanceTrace::recordsWritten()));
    manifest.insert(QStringLiteral("incomplete"), AppearanceTrace::sessionIncomplete());
    manifest.insert(QStringLiteral("trace"), tracePath);
    QFile file(base + QStringLiteral(".manifest.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        std::fprintf(stderr, "Test trace manifest path is not fresh.\n");
        return false;
    }
    file.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    file.write("\n");
    file.flush();
    if (AppearanceTrace::sessionIncomplete()) {
        std::fprintf(stderr, "Test trace session is incomplete (overflow).\n");
        return false;
    }
    return true;
}

} // namespace InterActive::TestTrace
