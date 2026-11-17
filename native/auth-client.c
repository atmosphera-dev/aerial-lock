#define _GNU_SOURCE
#include "auth-client.h"
#include "auth-protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

struct auth_client {
    int fd;
    pid_t pid;
    int status;
    bool started, exited, eof, broken, stopped, locked;
    bool required, visible, message_error;
    int terminal;
    size_t input_size, output_size, output_offset;
    char input[AUTH_MESSAGE_SIZE];
    char output[AUTH_LINE_SIZE];
    char message[PAM_MAX_MSG_SIZE];
};

struct auth_client *auth_client_create(void) {
    struct auth_client *client = mmap(NULL, sizeof(*client), PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (client == MAP_FAILED) return NULL;
    client->fd = -1;
    client->terminal = -1;
    client->locked = mlock(client, sizeof(*client)) == 0;
    madvise(client, sizeof(*client), MADV_DONTDUMP);
    return client;
}

static void wipe_transport(struct auth_client *client) {
    if (client->fd >= 0) close(client->fd);
    client->fd = -1;
    auth_wipe(client->input, sizeof(client->input));
    auth_wipe(client->output, sizeof(client->output));
    auth_wipe(client->message, sizeof(client->message));
    client->input_size = client->output_size = client->output_offset = 0;
    client->required = client->visible = client->message_error = false;
}

void auth_client_abort(struct auth_client *client) {
    if (!client || client->stopped) return;
    client->stopped = true;
    wipe_transport(client);
    if (client->pid > 0) kill(client->pid, SIGKILL);
}

int auth_client_reap(struct auth_client *client) {
    if (!client || client->pid <= 0) return 1;
    int status;
    pid_t result = waitpid(client->pid, &status, WNOHANG);
    if (result == 0 || (result < 0 && errno == EINTR)) return 0;
    client->pid = 0; /* No subsequent path may signal a reused PID. */
    client->exited = true;
    if (result < 0) client->broken = true;
    else client->status = status;
    return 1;
}

int auth_client_destroy(struct auth_client *client) {
    if (!client) return 1;
    if (client->pid > 0) return 0;
    wipe_transport(client);
    const bool locked = client->locked;
    auth_wipe(client, sizeof(*client));
    if (locked) munlock(client, sizeof(*client));
    return munmap(client, sizeof(*client)) == 0;
}

int auth_client_start(struct auth_client *client, const char *helper, const char *service,
                      const char *user, const char *directory) {
    if (!client || client->started || client->stopped || !helper || !*helper ||
        !service || !*service || !user || !*user || !directory || !*directory) return 0;
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets)) return 0;
    for (int i = 0; i < 2; ++i) {
        if (sockets[i] < 3) {
            int copy = fcntl(sockets[i], F_DUPFD_CLOEXEC, 3);
            close(sockets[i]);
            sockets[i] = copy;
        }
    }
    if (sockets[0] < 0 || sockets[1] < 0) {
        if (sockets[0] >= 0) close(sockets[0]);
        if (sockets[1] >= 0) close(sockets[1]);
        return 0;
    }
    if (fcntl(sockets[0], F_SETFL, O_NONBLOCK)) {
        close(sockets[0]); close(sockets[1]); return 0;
    }
    posix_spawn_file_actions_t actions;
    int rc = posix_spawn_file_actions_init(&actions);
    if (!rc) {
        rc = posix_spawn_file_actions_adddup2(&actions, sockets[1], STDIN_FILENO);
        if (!rc) rc = posix_spawn_file_actions_adddup2(&actions, sockets[1], STDOUT_FILENO);
        if (!rc) rc = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        if (!rc) rc = posix_spawn_file_actions_addclosefrom_np(&actions, 3);
        char *argv[] = {(char *)helper, (char *)service, (char *)user, (char *)directory, NULL};
        if (!rc) rc = posix_spawn(&client->pid, helper, &actions, NULL, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
    }
    close(sockets[1]);
    if (rc) { close(sockets[0]); client->pid = 0; return 0; }
    client->fd = sockets[0];
    client->started = true;
    return 1;
}

int auth_client_fd(const struct auth_client *client) { return client ? client->fd : -1; }
int auth_client_wants_read(const struct auth_client *client) {
    return client && client->fd >= 0 && !client->eof && !client->stopped;
}
int auth_client_wants_write(const struct auth_client *client) {
    return client && client->output_offset < client->output_size;
}
const char *auth_client_message(const struct auth_client *client) { return client->message; }
int auth_client_response_required(const struct auth_client *client) { return client->required; }
int auth_client_response_visible(const struct auth_client *client) { return client->visible; }
int auth_client_message_is_error(const struct auth_client *client) { return client->message_error; }
enum auth_result auth_client_result(const struct auth_client *client) {
    return client->terminal >= 0 ? (enum auth_result)client->terminal : AUTH_ERROR;
}
int auth_client_memory_locked(const struct auth_client *client) { return client->locked; }

static int flush_response(struct auth_client *client) {
    while (client->output_offset < client->output_size) {
        ssize_t n = send(client->fd, client->output + client->output_offset,
                         client->output_size - client->output_offset, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 1;
        if (n <= 0) { client->broken = true; return 0; }
        auth_wipe(client->output + client->output_offset, (size_t)n);
        client->output_offset += (size_t)n;
    }
    client->output_size = client->output_offset = 0;
    return 1;
}

int auth_client_respond(struct auth_client *client, const char *bytes, size_t length) {
    if (!client || client->broken || client->stopped || !client->required ||
        client->terminal >= 0 || auth_client_wants_write(client)) return 0;
    if (!bytes || length >= PAM_MAX_RESP_SIZE || memchr(bytes, 0, length)) {
        client->broken = true;
        return 0;
    }
    client->output[0] = 'R'; client->output[1] = ' ';
    auth_hex(bytes, length, client->output + 2);
    client->output_size = length * 2 + 3;
    client->output[client->output_size - 1] = '\n';
    client->required = false;
    return flush_response(client);
}

static enum auth_event frame(struct auth_client *client) {
    const char *line = client->input;
    if (client->terminal >= 0 || client->required || auth_client_wants_write(client)) return AUTH_BROKEN;
    if (!strncmp(line, "D ", 2)) {
        if (!strcmp(line, "D success")) client->terminal = AUTH_SUCCESS;
        else if (!strcmp(line, "D failed")) client->terminal = AUTH_FAILED;
        else if (!strcmp(line, "D max-tries")) client->terminal = AUTH_MAX_TRIES;
        else if (!strcmp(line, "D error")) client->terminal = AUTH_ERROR;
        else return AUTH_BROKEN;
        return AUTH_WAIT;
    }
    size_t prefix;
    if (!strncmp(line, "P0 ", 3) || !strncmp(line, "P1 ", 3)) prefix = 3;
    else if (!strncmp(line, "I ", 2) || !strncmp(line, "E ", 2)) prefix = 2;
    else return AUTH_BROKEN;
    auth_wipe(client->message, sizeof(client->message));
    if (auth_unhex(line + prefix, client->input_size - prefix,
                   client->message, sizeof(client->message)) < 0) return AUTH_BROKEN;
    client->required = line[0] == 'P';
    client->visible = !client->required || line[1] == '1';
    client->message_error = line[0] == 'E';
    return AUTH_MESSAGE;
}

enum auth_event auth_client_step(struct auth_client *client) {
    if (!client || !client->started || client->stopped || client->broken) return AUTH_BROKEN;
    if (!flush_response(client)) return AUTH_BROKEN;
    while (!client->eof) {
        char byte = 0;
        ssize_t n = recv(client->fd, &byte, 1, MSG_DONTWAIT);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n < 0) { client->broken = true; return AUTH_BROKEN; }
        if (n == 0) { client->eof = true; break; }
        if (byte == '\n') {
            client->input[client->input_size] = 0;
            enum auth_event event = frame(client);
            auth_wipe(client->input, sizeof(client->input));
            client->input_size = 0;
            if (event == AUTH_BROKEN) client->broken = true;
            if (event != AUTH_WAIT) return event;
        } else {
            if (!byte || client->input_size >= sizeof(client->input) - 1) {
                client->broken = true; return AUTH_BROKEN;
            }
            client->input[client->input_size++] = byte;
        }
    }
    auth_client_reap(client);
    if (client->broken) return AUTH_BROKEN;
    if (!client->exited || !client->eof) return AUTH_WAIT;
    if (!WIFEXITED(client->status) || WEXITSTATUS(client->status) || client->input_size ||
        client->required || auth_client_wants_write(client) || client->terminal < 0) {
        client->broken = true;
        return AUTH_BROKEN;
    }
    return AUTH_COMPLETE;
}
