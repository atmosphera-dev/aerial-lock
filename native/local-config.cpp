#include "local-config.h"
#include "local-config.moc"

bool LocalConfig::load(const QString &bundle, const QString &userPath)
{
    auto paths = Aerial::ConfigPaths::environment(bundle);
    paths.config = userPath;
    document = Aerial::readConfig(paths);
    emit changed();
    return ready();
}
