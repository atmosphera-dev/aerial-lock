#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QQmlEngine>

namespace Aerial::Settings {
inline void configureInterActive(QQmlEngine &engine)
{
    const QDir executable(QCoreApplication::applicationDirPath());
    const auto development = executable.filePath("interactive/qml");
    engine.addImportPath(QDir(development).exists() ? development
        : executable.filePath(QStringLiteral(AERIAL_INTERACTIVE_INSTALLED_QML)));
}
}
