#define _GNU_SOURCE
#include "../native/auth-client.h"
#include "../native/auth-protocol.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>

/* Exercise the actual production send queue, without product fault switches. */
ssize_t __real_send(int, const void *, size_t, int);
ssize_t __wrap_send(int fd, const void *bytes, size_t size, int flags) {
    static unsigned call;
    ++call;
    if (call % 5 == 1) { errno = EINTR; return -1; }
    if (call % 5 == 2) { errno = EAGAIN; return -1; }
    return __real_send(fd, bytes, size > 7 ? 7 : size, flags);
}

static void require(int condition, const char *what) {
    if (!condition) { fprintf(stderr, "auth-client: %s\n", what); exit(1); }
}
static long long now_ms(void) {
    struct timespec t;
    require(clock_gettime(CLOCK_MONOTONIC, &t) == 0, "clock");
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void retire(struct auth_client *client) {
    auth_client_abort(client);
    long long deadline = now_ms() + 5000;
    while (!auth_client_reap(client) && now_ms() < deadline) poll(NULL, 0, 1);
    require(auth_client_destroy(client), "retired worker reaped before free");
}
static struct auth_client *start(const char *fixture, const char *mode) {
    struct auth_client *client = auth_client_create();
    require(client != NULL, "allocate");
    require(auth_client_start(client, fixture, mode, "unused-fixture-user", "/nonexistent-fixture"), "spawn");
    require(!auth_client_start(client, fixture, mode, "unused-fixture-user", "/nonexistent-fixture"), "one conversation per transport");
    return client;
}
static enum auth_event run(struct auth_client *client, const char *answer, size_t length, int *messages) {
    long long deadline = now_ms() + 5000;
    while (now_ms() < deadline) {
        enum auth_event event = auth_client_step(client);
        if (event == AUTH_BROKEN || event == AUTH_COMPLETE) return event;
        if (event == AUTH_MESSAGE) {
            ++*messages;
            if (auth_client_response_required(client)) {
                if (!auth_client_respond(client, answer, length)) return AUTH_BROKEN;
            }
            continue;
        }
        struct pollfd fd = {.fd = auth_client_fd(client),
                            .events = (auth_client_wants_read(client) ? POLLIN : 0) |
                                      (auth_client_wants_write(client) ? POLLOUT : 0)};
        if (!fd.events) poll(NULL, 0, 5);
        else poll(&fd, 1, 5);
    }
    require(0, "completion deadline");
    return AUTH_BROKEN;
}
int main(int argc, char **argv) {
    require(argc == 2, "fixture path");
    const char *modes[] = {"success", "failed", "max-tries", "error", "crash", "truncated",
                          "success-crash", "extra", "malformed", "oversize", "nul-prefix",
                          "pending-success", "terminal-prompt", "duplicate-prompt", "eof-no-terminal"};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        struct auth_client *client = start(argv[1], modes[i]);
        int messages = 0;
        enum auth_event event;
        if (!strcmp(modes[i], "pending-success") || !strcmp(modes[i], "duplicate-prompt")) {
            const long long deadline = now_ms() + 5000;
            do { event = auth_client_step(client); } while (event == AUTH_WAIT && now_ms() < deadline);
            require(event == AUTH_MESSAGE && auth_client_response_required(client), "pending prompt");
            do { event = auth_client_step(client); } while (event == AUTH_WAIT && now_ms() < deadline);
        } else event = run(client, "fixture-answer", 14, &messages);
        if (i < 4) {
            require(event == AUTH_COMPLETE, "valid terminal protocol");
            const enum auth_result expected[] = {AUTH_SUCCESS, AUTH_FAILED, AUTH_MAX_TRIES, AUTH_ERROR};
            require(auth_client_result(client) == expected[i] && messages == 4, "terminal result and prompts");
        } else require(event == AUTH_BROKEN, "invalid conversation cannot complete");
        retire(client);
    }
    char exact[PAM_MAX_RESP_SIZE];
    memset(exact, 'a', sizeof(exact));
    for (size_t size = PAM_MAX_RESP_SIZE - 1; size <= PAM_MAX_RESP_SIZE; ++size) {
        struct auth_client *client = start(argv[1], "success");
        int messages = 0;
        enum auth_event result = run(client, exact, size, &messages);
        require(result == (size < PAM_MAX_RESP_SIZE ? AUTH_COMPLETE : AUTH_BROKEN), "exact byte boundary");
        retire(client);
    }
    struct auth_client *client = start(argv[1], "success");
    int messages = 0;
    require(run(client, "a\0b", 3, &messages) == AUTH_BROKEN, "reject NUL");
    retire(client);
    for (int i = 0; i < 20; ++i) {
        client = start(argv[1], "hang");
        require(auth_client_step(client) == AUTH_WAIT, "hung worker is not success");
        auth_client_abort(client);
        require(auth_client_step(client) == AUTH_BROKEN, "aborted worker cannot complete");
        retire(client);
    }
    client = auth_client_create();
    require(!auth_client_start(client, "/nonexistent/aerial-auth-helper", "service", "user", "/etc/pam.d"), "exec failure");
    retire(client);
    int status;
    require(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD, "no worker zombies");
    puts("AUTH_CLIENT_PASS shared C protocol prompts bounds cancellation and reaping");
    return 0;
}
