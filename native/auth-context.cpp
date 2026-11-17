#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "auth-context.h"
#include "auth-client.h"
#include "auth-protocol.h"
#include <QCoreApplication>
#include <QPointer>
#include <array>
#include <pwd.h>
#include <unistd.h>

#ifndef AERIAL_LOCK_AUTH_HELPER
#define AERIAL_LOCK_AUTH_HELPER "/usr/libexec/aerial-lock/aerial-lock-auth"
#endif

namespace {
// Only the scheduler is Qt-specific. The retired transport owns/reaps its own
// worker, independently of this QObject or a replacement conversation.
void retire(auth_client *client) {
    if (!client) return;
    auth_client_abort(client);
    if (auth_client_reap(client)) { auth_client_destroy(client); return; }
    auto *timer = new QTimer(qApp);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, client] {
        if (auth_client_reap(client)) {
            auth_client_destroy(client);
            timer->deleteLater();
        }
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
    for (auto **watch : {&notifier, &writer}) {
        if (*watch) {
            (*watch)->setEnabled(false);
            (*watch)->deleteLater();
            *watch = nullptr;
        }
    }
    auto *old = client;
    client = nullptr;
    retire(old);
    required = visible = isError = false;
    text.clear();
}

void AuthContext::abort() {
    bool wasActive = active();
    reset();
    QPointer<AuthContext> self(this);
    const quint64 current = generation;
    emit messageChanged();
    if (self && current == generation && wasActive) emit activeChanged();
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
    client = auth_client_create();
    if (!client) return false;
    if (!auth_client_start(client, args[0].constData(), args[1].constData(),
                           args[2].constData(), args[3].constData())) {
        reset();
        return false;
    }
    notifier = new QSocketNotifier(auth_client_fd(client), QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, &AuthContext::receive);
    timer.start();
    const quint64 current = generation;
    QPointer<AuthContext> self(this);
    emit messageChanged();
    if (self && current == generation) emit activeChanged();
    return true;
}

void AuthContext::updateWriteNotifier() {
    if (!client || !auth_client_wants_write(client)) {
        if (writer) writer->setEnabled(false);
        return;
    }
    if (!writer) {
        writer = new QSocketNotifier(auth_client_fd(client), QSocketNotifier::Write, this);
        connect(writer, &QSocketNotifier::activated, this, &AuthContext::receive);
    }
    writer->setEnabled(true);
}

void AuthContext::respond(const QString &response) {
    if (!client || !required) return;
    QByteArray bytes = response.toUtf8();
    const bool sent = auth_client_respond(client, bytes.constData(), size_t(bytes.size()));
    auth_wipe(bytes.data(), size_t(bytes.size()));
    if (!sent) { fail(); return; }
    required = false;
    updateWriteNotifier();
    emit messageChanged();
}

void AuthContext::fail() {
    reset();
    QPointer<AuthContext> self(this);
    const quint64 current = generation;
    emit messageChanged();
    if (!self || current != generation) return;
    emit activeChanged();
    if (self && current == generation) emit error(Error);
}

void AuthContext::receive() {
    const quint64 current = generation;
    QPointer<AuthContext> self(this);
    while (client) {
        const auto event = auth_client_step(client);
        if (notifier && !auth_client_wants_read(client)) notifier->setEnabled(false);
        updateWriteNotifier();
        if (event == AUTH_WAIT) return;
        if (event == AUTH_BROKEN) { fail(); return; }
        if (event == AUTH_COMPLETE) {
            const int outcome = int(auth_client_result(client));
            reset();
            const quint64 completedGeneration = generation;
            emit messageChanged();
            if (!self || completedGeneration != generation) return;
            emit activeChanged();
            if (!self || completedGeneration != generation) return;
            if (outcome == Error) emit error(Error);
            else emit completed(outcome);
            return;
        }
        text = QString::fromUtf8(auth_client_message(client));
        isError = auth_client_message_is_error(client);
        required = auth_client_response_required(client);
        visible = auth_client_response_visible(client);
        emit messageChanged();
        if (!self || current != generation) return;
        emit pamMessage();
        if (!self || current != generation) return;
    }
}

void AuthContext::reap() { receive(); }

#include "auth-context.moc"
