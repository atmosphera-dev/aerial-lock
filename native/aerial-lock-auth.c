#define _GNU_SOURCE
#include "auth-protocol.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>

static int send_bytes(const char *p, size_t n) {
    while (n) {
        ssize_t sent = write(STDOUT_FILENO, p, n);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) return 0;
        p += sent;
        n -= (size_t)sent;
    }
    return 1;
}

static int send_message(const struct pam_message *message) {
    const char *text = message->msg ? message->msg : "";
    size_t n = strnlen(text, PAM_MAX_MSG_SIZE);
    if (n == PAM_MAX_MSG_SIZE) return 0;
    char frame[AUTH_MESSAGE_SIZE];
    const char *kind;
    switch (message->msg_style) {
    case PAM_PROMPT_ECHO_OFF: kind = "P0 "; break;
    case PAM_PROMPT_ECHO_ON: kind = "P1 "; break;
    case PAM_TEXT_INFO: kind = "I "; break;
    case PAM_ERROR_MSG: kind = "E "; break;
    default: return 0;
    }
    size_t prefix = strlen(kind);
    memcpy(frame, kind, prefix);
    auth_hex(text, n, frame + prefix);
    frame[prefix + n * 2] = '\n';
    return send_bytes(frame, prefix + n * 2 + 1);
}

static int read_response(char **response) {
    char frame[AUTH_LINE_SIZE] = {0};
    char decoded[PAM_MAX_RESP_SIZE] = {0};
    size_t n = 0;
    int ok = 0;
    /* Unbuffered input: no stdio copy can retain a password after wiping. */
    while (n < sizeof(frame)) {
        char c;
        ssize_t got = read(STDIN_FILENO, &c, 1);
        if (got < 0 && errno == EINTR) continue;
        if (got != 1) break;
        if (c == '\n') {
            if (n >= 2 && frame[0] == 'R' && frame[1] == ' ' &&
                auth_unhex(frame + 2, n - 2, decoded, sizeof(decoded)) >= 0) {
                *response = strdup(decoded);
                ok = *response != NULL;
            }
            break; /* A, malformed, oversized and EOF all abort the exchange. */
        }
        frame[n++] = c;
        auth_wipe(&c, sizeof(c));
    }
    auth_wipe(frame, sizeof(frame));
    auth_wipe(decoded, sizeof(decoded));
    return ok;
}

static int conversation(int count, const struct pam_message **messages,
                        struct pam_response **out, void *unused) {
    (void)unused;
    if (count <= 0 || count > PAM_MAX_NUM_MSG || !messages || !out) return PAM_CONV_ERR;
    struct pam_response *responses = calloc((size_t)count, sizeof(*responses));
    if (!responses) return PAM_BUF_ERR;
    for (int i = 0; i < count; ++i) {
        if (!messages[i] || !send_message(messages[i])) goto fail;
        if ((messages[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
             messages[i]->msg_style == PAM_PROMPT_ECHO_ON) && !read_response(&responses[i].resp)) goto fail;
    }
    /* Linux-PAM takes ownership and wipes these responses after delivery. */
    *out = responses;
    return PAM_SUCCESS;
fail:
    for (int i = 0; i < count; ++i) {
        if (responses[i].resp) {
            auth_wipe(responses[i].resp, strlen(responses[i].resp));
            free(responses[i].resp);
        }
    }
    free(responses);
    return PAM_CONV_ERR;
}

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    /* No core files or surviving worker if the locker exits unexpectedly. */
    struct rlimit no_core = {0, 0};
    if (setrlimit(RLIMIT_CORE, &no_core) || prctl(PR_SET_DUMPABLE, 0) ||
        prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() == 1) return 2;
    signal(SIGPIPE, SIG_IGN);
    struct pam_conv conv = {conversation, NULL};
    pam_handle_t *handle = NULL;
    int result = pam_start_confdir(argv[1], argv[2], &conv, argv[3], &handle);
    if (result == PAM_SUCCESS) result = pam_authenticate(handle, 0);
    const char *frame = "D error\n";
    if (result == PAM_SUCCESS) frame = "D success\n";
    else if (result == PAM_AUTH_ERR) frame = "D failed\n";
    else if (result == PAM_MAXTRIES) frame = "D max-tries\n";
    if (handle && pam_end(handle, result) != PAM_SUCCESS) frame = "D error\n";
    return send_bytes(frame, strlen(frame)) ? 0 : 1;
}
