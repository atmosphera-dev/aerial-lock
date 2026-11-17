#include "local-probe.h"
#include <QJsonDocument>
#include <fcntl.h>
#include <unistd.h>

namespace Aerial {
LocalProbe::LocalProbe(const QString &executable, QObject *parent) : QObject(parent), executable(executable)
{
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, this, &LocalProbe::cancel);
    child.setStandardInputFile(QProcess::nullDevice());
    child.setStandardErrorFile(QProcess::nullDevice());
    connect(&child, &QProcess::readyReadStandardOutput, this, [this] {
        bytes += child.readAllStandardOutput();
        if (bytes.size() > 4096) cancel();
    });
    connect(&child, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) settle(false);
    });
    connect(&child, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        bytes += child.readAllStandardOutput();
        settle(code == 0 && status == QProcess::NormalExit && bytes.size() <= 4096);
    });
}

LocalProbe::~LocalProbe()
{
    disconnect(&child, nullptr, this, nullptr);
    if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(1000); }
    if (file >= 0) close(file);
}

void LocalProbe::start(int ownedReadOnlyFd)
{
    file = ownedReadOnlyFd;
    if (file < 0) { settle(false); return; }
    child.setChildProcessModifier([fd = file] {
        if (dup2(fd, 3) < 0 || fcntl(3, F_SETFD, 0) < 0) _exit(1);
    });
    deadline.start(15000);
    child.start(executable, {});
}

void LocalProbe::cancel()
{
    if (terminal) return;
    if (child.state() == QProcess::NotRunning) settle(false);
    else child.kill(); // Only our isolated media worker, never the locker.
}

void LocalProbe::settle(bool success)
{
    if (terminal) return;
    terminal = true; deadline.stop();
    if (file >= 0) { close(file); file = -1; }
    if (success) output = QJsonDocument::fromJson(bytes).object();
    if (!output.value("initialFrameDecoded").toBool()) output = {};
    emit finished();
}
}

#include "local-probe.moc"
