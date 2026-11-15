#include "../native/auth-context.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>
#include <functional>
#include <iostream>
#include <sys/wait.h>

static void require(bool condition, const char *what) {
    if (!condition) qFatal("auth-context: %s", what);
}
static bool until(const std::function<bool()> &predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 5000) QTest::qWait(5);
    return predicate();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    require(argc == 2, "fixture path required");
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    for (const QString &mode : {"success", "failed", "max-tries", "error", "crash", "truncated",
                                "success-crash", "extra", "malformed", "oversize"}) {
        AuthContext context(fixture, nullptr);
        context.service = mode;
        context.username = "unused-fixture-user";
        QSignalSpy completed(&context, &AuthContext::completed);
        QSignalSpy errors(&context, &AuthContext::error);
        int messages = 0;
        QObject::connect(&context, &AuthContext::pamMessage, &context, [&] {
            ++messages;
            if (messages == 1) require(!context.responseRequired() && context.message() == "Touch sensor", "information");
            if (messages == 2) require(context.messageIsError(), "error message");
            if (context.responseRequired()) {
                require(context.responseVisible() == (messages == 4), "echo mode");
                context.respond(messages == 3 ? "fixture-answer" : "");
            }
        });
        require(context.start(), "start");
        require(!context.start(), "duplicate start");
        require(until([&] { return !completed.isEmpty() || !errors.isEmpty(); }), "completion deadline");
        require(!context.active() && !context.responseRequired(), "terminal state");
        if (mode == "success" || mode == "failed" || mode == "max-tries") {
            int expected = mode == "success" ? AuthContext::Success : mode == "failed" ? AuthContext::Failed : AuthContext::MaxTries;
            require(completed.size() == 1 && completed[0][0].toInt() == expected && errors.isEmpty(), "completion result");
            require(messages == 4, "all conversation messages");
        } else require(completed.isEmpty() && errors.size() == 1, "failure cannot authenticate");
    }
    for (const QString &mode : {"hang", "success", "success-hang"}) {
        AuthContext context(fixture, nullptr);
        context.username = "unused-fixture-user";
        context.service = mode;
        QSignalSpy completed(&context, &AuthContext::completed), errors(&context, &AuthContext::error);
        require(context.start(), "abort start");
        if (mode == "success") require(until([&] { return context.responseRequired(); }), "abort prompt");
        else QTest::qWait(100);
        require(completed.isEmpty() && errors.isEmpty(), "no completion before clean worker exit");
        context.abort();
        context.abort();
        require(!context.active() && !context.responseRequired(), "aborted state");
        QTest::qWait(100);
        require(completed.isEmpty() && errors.isEmpty(), "no stale abort completion");
        context.service = "crash";
        require(context.start() && until([&] { return errors.size() == 1; }), "restart after abort");
    }
    for (const QString &response : {QString(512, 'a'), QString::fromUtf8("a\0b", 3), QString(256, QChar(0x044f))}) {
        AuthContext context(fixture, nullptr);
        context.service = "success";
        context.username = "unused-fixture-user";
        QSignalSpy errors(&context, &AuthContext::error), completed(&context, &AuthContext::completed);
        require(context.start() && until([&] { return context.responseRequired(); }), "oversize prompt");
        context.respond(response);
        require(errors.size() == 1 && completed.isEmpty() && !context.active(), "response cap includes UTF-8 bytes and rejects NUL");
    }
    for (const QString &response : {QString(511, 'a'), QString(255, QChar(0x044f))}) {
        AuthContext context(fixture, nullptr);
        context.service = "success";
        context.username = "unused-fixture-user";
        QSignalSpy completed(&context, &AuthContext::completed), errors(&context, &AuthContext::error);
        int prompts = 0;
        QObject::connect(&context, &AuthContext::pamMessage, &context, [&] {
            if (context.responseRequired()) context.respond(++prompts == 1 ? response : "");
        });
        require(context.start() && until([&] { return !completed.isEmpty() || !errors.isEmpty(); }), "exact-fit completion");
        require(errors.isEmpty() && completed.size() == 1 && completed[0][0].toInt() == AuthContext::Success, "exact-fit UTF-8 response accepted");
    }
    AuthContext missing("/nonexistent/aerial-auth-helper", nullptr);
    require(!missing.start() && !missing.active(), "exec failure");
    QTest::qWait(100);
    int status;
    require(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD, "no orphan/zombie worker");
    std::cout << "AUTH_CONTEXT_PASS protocol prompts info echo outcomes abort restart crash malformed oversize reaping\n";
}
