// SPDX-License-Identifier: LGPL-3.0-or-later
#include "TestTraceSetup.h"
#include <QGuiApplication>
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>
#ifdef IA_TEST_PLAYGROUND
#include "PlaygroundState.h"
#include <QTemporaryDir>
#endif
#include <cstdio>
#include <cstring>

class Setup : public QObject
{
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
#ifdef IA_TEST_PLAYGROUND
        // DocumentsLocation is NOT sandboxed by QStandardPaths test mode.
        // Inject before any fixture can create or activate its Save button.
        if (!m_samples.isValid())
            qFatal("Cannot create sandbox for playground palette samples");
        auto *state = engine->singletonInstance<PlaygroundState *>("InterActive.Playground", "PlaygroundState");
        if (!state)
            qFatal("Cannot configure playground sample sandbox");
        state->setPaletteSamplePath(m_samples.filePath("samples.txt"));
#else
        Q_UNUSED(engine);
#endif
    }
private:
#ifdef IA_TEST_PLAYGROUND
    QTemporaryDir m_samples;
#endif
};

int main(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--qt-version") == 0) {
        QGuiApplication application(argc, argv);
        const bool matches = std::strcmp(QT_VERSION_STR, IA_EXPECTED_QT_VERSION) == 0
            && std::strcmp(qVersion(), IA_EXPECTED_QT_VERSION) == 0;
        std::printf("Qt compiled=%s runtime=%s expected=%s\n", QT_VERSION_STR, qVersion(), IA_EXPECTED_QT_VERSION);
        return matches ? 0 : 1;
    }
    // Shared test-only trace activation: inert unless IA_TEST_TRACE_MODE is set.
    const bool traced = InterActive::TestTrace::install(argv[0]);
    Setup setup;
    const int result = quick_test_main_with_setup(argc, argv, "interactive", nullptr, &setup);
    if (traced && !InterActive::TestTrace::finalize()) {
        std::fprintf(stderr, "Test trace session failed.\n");
        return 1;
    }
    return result;
}

#include "runner.moc"
