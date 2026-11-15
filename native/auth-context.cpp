#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "auth-context.h"
#include "auth-protocol.h"
#include <QCoreApplication>
#include <QPointer>
#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <pwd.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef AERIAL_LOCK_AUTH_HELPER
#define AERIAL_LOCK_AUTH_HELPER "/usr/libexec/aerial-lock/aerial-lock-auth"
#endif
extern char **environ;

namespace {
// Abort never waits for a stuck PAM module on the GUI thread. A separate
// app-owned reaper prevents zombies without allowing late events into a new
// conversation. The exec worker also dies when its locker parent disappears.
void retire(int pid) {
    if (pid <= 0) return;
    kill(pid, SIGKILL);
    auto *timer = new QTimer(qApp);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, pid] {
        int status;
        auto result = waitpid(pid, &status, WNOHANG);
        if (result == pid || (result < 0 && errno == ECHILD)) timer->deleteLater();
    });
    timer->start(20);
}
}

AuthContext::AuthContext(QObject *parent)
    : AuthContext(QStringLiteral(AERIAL_LOCK_AUTH_HELPER), parent) {}

AuthContext::AuthContext(const QString &helper, QObject *parent)
    : QObject(parent), executable(helper) {
    timer.setInterval(20);
    connect(&timer, &QTimer::timeout, this, &AuthContext::reap);
}

AuthContext::~AuthContext() { reset(); }

void AuthContext::reset() {
    ++generation;
    timer.stop();
    if (notifier) {
        notifier->setEnabled(false);
        notifier->deleteLater();
        notifier = nullptr;
    }
    if (fd >= 0) close(fd);
    fd = -1;
    int oldPid = pid;
    pid = 0;
    retire(oldPid);
    input.clear();
    terminal = -1;
    required = visible = isError = false;
    text.clear();
}

void AuthContext::abort() {
    bool wasActive = active();
    reset();
    emit messageChanged();
    if (wasActive) emit activeChanged();
}

bool AuthContext::start() {
    if (active()) return false;
    reset();
    QString selectedUser = username;
    if (selectedUser.isEmpty()) {
        auto *entry = getpwuid(getuid());
        if (!entry) return false;
        selectedUser = QString::fromLocal8Bit(entry->pw_name);
    }
    std::array<QByteArray, 4> args = {executable.toLocal8Bit(), service.toUtf8(),
                                    selectedUser.toUtf8(), directory.toLocal8Bit()};
    for (const auto &arg : args)
        if (arg.isEmpty() || arg.contains('\0')) return false;
    char *argv[] = {args[0].data(), args[1].data(), args[2].data(), args[3].data(), nullptr};
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets)) return false;
    // Keep file actions correct even when the caller has a closed stdio fd.
    for (int &socket : sockets) {
        if (socket < 3) {
            int copy = fcntl(socket, F_DUPFD_CLOEXEC, 3);
            close(socket);
            socket = copy;
        }
    }
    if (sockets[0] < 0 || sockets[1] < 0) {
        if (sockets[0] >= 0) close(sockets[0]);
        if (sockets[1] >= 0) close(sockets[1]);
        return false;
    }
    posix_spawn_file_actions_t actions;
    int rc = posix_spawn_file_actions_init(&actions);
    if (!rc) {
        rc = posix_spawn_file_actions_adddup2(&actions, sockets[1], STDIN_FILENO);
        if (!rc) rc = posix_spawn_file_actions_adddup2(&actions, sockets[1], STDOUT_FILENO);
        if (!rc) rc = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        if (!rc) rc = posix_spawn_file_actions_addclosefrom_np(&actions, 3);
        pid_t child = 0;
        // No application callback or PAM code runs in the pre-exec child.
        if (!rc) rc = posix_spawn(&child, argv[0], &actions, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
        if (!rc) pid = child;
    }
    close(sockets[1]);
    if (rc) { close(sockets[0]); return false; }
    fd = sockets[0];
    if (fcntl(fd, F_SETFL, O_NONBLOCK)) { reset(); return false; }
    notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, &AuthContext::receive);
    timer.start();
    emit messageChanged();
    emit activeChanged();
    return true;
}

void AuthContext::respond(const QString &response) {
    if (!active() || !required || terminal >= 0) return;
    QByteArray bytes = response.toUtf8();
    if (bytes.size() >= PAM_MAX_RESP_SIZE || bytes.contains('\0')) {
        auth_wipe(bytes.data(), size_t(bytes.size()));
        fail();
        return;
    }
    std::array<char, AUTH_LINE_SIZE> frame{};
    frame[0] = 'R'; frame[1] = ' ';
    auth_hex(bytes.constData(), size_t(bytes.size()), frame.data() + 2);
    size_t size = size_t(bytes.size()) * 2 + 3;
    frame[size - 1] = '\n';
    auth_wipe(bytes.data(), size_t(bytes.size()));
    ssize_t sent;
    do { sent = send(fd, frame.data(), size, MSG_NOSIGNAL | MSG_DONTWAIT); }
    while (sent < 0 && errno == EINTR);
    auth_wipe(frame.data(), frame.size());
    if (sent != ssize_t(size)) { fail(); return; }
    required = false;
    emit messageChanged();
}

void AuthContext::fail() {
    reset();
    emit messageChanged();
    emit activeChanged();
    emit error(Error);
    // Errors are distinct from authentication rejection. Do not follow with
    // completed(Error), which would overwrite Lock's auth-error projection.
}

bool AuthContext::frame(const QByteArray &line) {
    if (terminal >= 0) return false;
    if (line.startsWith("D ")) {
        if (required) return false;
        if (line == "D success") terminal = Success;
        else if (line == "D failed") terminal = Failed;
        else if (line == "D max-tries") terminal = MaxTries;
        else if (line == "D error") terminal = Error;
        else return false;
        return true;
    }
    if (required) return false;
    int prefix = 0;
    if (line.startsWith("P0 ") || line.startsWith("P1 ")) prefix = 3;
    else if (line.startsWith("I ") || line.startsWith("E ")) prefix = 2;
    else return false;
    std::array<char, PAM_MAX_MSG_SIZE> decoded{};
    int size = auth_unhex(line.constData() + prefix, size_t(line.size() - prefix), decoded.data(), decoded.size());
    if (size < 0) return false;
    text = QString::fromUtf8(decoded.data(), size);
    isError = line[0] == 'E';
    required = line[0] == 'P';
    visible = !required || line[1] == '1';
    emit messageChanged();
    emit pamMessage();
    return true;
}

void AuthContext::receive() {
    const quint64 current = generation;
    QPointer<AuthContext> self(this);
    while (fd >= 0) {
        char c;
        ssize_t n = recv(fd, &c, 1, MSG_DONTWAIT);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n <= 0) {
            if (notifier) notifier->setEnabled(false);
            return; // Completion is accepted only after waitpid observes exit.
        }
        if (c == '\n') {
            QByteArray line;
            line.swap(input);
            bool ok = frame(line);
            if (!self || current != generation) return;
            if (!ok) { fail(); return; }
        } else {
            input.append(c);
            if (input.size() >= AUTH_MESSAGE_SIZE) { fail(); return; }
        }
    }
}

void AuthContext::reap() {
    if (!active()) return;
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (!result || (result < 0 && errno == EINTR)) return;
    if (result < 0) { fail(); return; }
    pid = 0; // Already reaped: no failure path may signal a reused PID.
    const quint64 current = generation;
    QPointer<AuthContext> self(this);
    receive(); // Drain terminal data even if exit won the notifier race.
    if (!self || current != generation) return;
    int outcome = terminal;
    bool valid = WIFEXITED(status) && WEXITSTATUS(status) == 0 && input.isEmpty() && outcome >= 0;
    reset();
    emit messageChanged();
    emit activeChanged();
    if (!valid || outcome == Error) emit error(Error);
    else emit completed(outcome);
}

#include "auth-context.moc"
