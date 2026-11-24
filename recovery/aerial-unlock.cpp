/*
 * aerial-unlock - native takeover-release for a stuck session lock
 *
 * Manual recovery from a TTY. Native C++/wayland-client only: a
 * Quickshell-side crash cannot take it down too.
 *
 * Exit codes:
 *   0  recovered (took over the lock and released it)
 *   1  no Wayland socket / environment (fatal, with guidance)
 *   2  inconclusive (neither locked nor finished within the timeout)
 *   3  refused (a live client holds the lock)
 *
 * After refusal, --purge asks the supervisor to stop its confirmed locker
 * for this compositor, then retries takeover.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <string>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/stat.h>
#include <wayland-client.h>
#include "ext-session-lock-v1-client.h"
#include "../native/recovery-channel.h"

static struct ext_session_lock_manager_v1 *lock_manager = nullptr;
static struct ext_session_lock_v1 *lock_obj = nullptr;
static int got_locked = 0;
static int got_finished = 0;

/* ---- listeners ---- */

static void lock_locked(void *, struct ext_session_lock_v1 *)
{
    got_locked = 1;
}

static void lock_finished(void *, struct ext_session_lock_v1 *)
{
    got_finished = 1;
}

static const struct ext_session_lock_v1_listener lock_listener = {
    .locked = lock_locked,
    .finished = lock_finished,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version)
{
    (void)data; (void)version;
    if (strcmp(interface, "ext_session_lock_manager_v1") == 0) {
        lock_manager = static_cast<struct ext_session_lock_manager_v1 *>(
            wl_registry_bind(registry, name,
                             &ext_session_lock_manager_v1_interface, 1));
    }
}

static void registry_global_remove(void *, struct wl_registry *, uint32_t) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* ---- socket discovery ---- */

static std::string find_socket()
{
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    const char *explicit_display = getenv("WAYLAND_DISPLAY");
    if (explicit_display && *explicit_display) {
        if (explicit_display[0] == '/') return explicit_display;
        if (xdg && *xdg) return std::string(xdg) + "/" + explicit_display;
    }
    if (!xdg || !*xdg) {
        fprintf(stderr,
                "aerial-unlock: XDG_RUNTIME_DIR is not set.\n"
                "  From a TTY: export XDG_RUNTIME_DIR=/run/user/$(id -u)\n");
        return "";
    }

    std::vector<std::string> sockets;
    DIR *dir = opendir(xdg);
    if (!dir) {
        fprintf(stderr, "aerial-unlock: cannot open %s: %s\n", xdg, strerror(errno));
        return "";
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        const char *n = ent->d_name;
        if (strncmp(n, "wayland-", 8) != 0) continue;
        const char *suffix = n + 8;
        if (!*suffix || *suffix < '0' || *suffix > '9') continue;   // skip .lock
        std::string path = std::string(xdg) + "/" + n;
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode))
            sockets.push_back(path);
    }
    closedir(dir);

    if (sockets.empty()) {
        fprintf(stderr,
                "aerial-unlock: no Wayland socket found under %s\n"
                "  Expected wayland-N entries; check ls -la %s/wayland-*\n",
                xdg, xdg);
        return "";
    }

    return sockets.front();
}

// Pin the socket inode, not just the name: a stale path must never redirect a
// purge or its takeover to a replacement compositor. Linux connects through
// the O_PATH handle even if the filesystem entry is renamed after checking.
struct Endpoint {
    const std::string path;
    const int handle;
    struct stat identity {};

    explicit Endpoint(const std::string &socket)
        : path(socket), handle(open(socket.c_str(), O_PATH | O_CLOEXEC))
    {
        if (handle >= 0) fstat(handle, &identity);
    }
    ~Endpoint() { if (handle >= 0) close(handle); }
    Endpoint(const Endpoint &) = delete;
    Endpoint &operator=(const Endpoint &) = delete;

    bool current() const
    {
        struct stat st {};
        return S_ISSOCK(identity.st_mode) && stat(path.c_str(), &st) == 0 &&
            st.st_dev == identity.st_dev && st.st_ino == identity.st_ino;
    }
};

struct HolderReply {
    bool requested = false;
    pid_t holder = 0;
};

static HolderReply confirmedHolder(const Endpoint &socket, bool purge)
{
    sockaddr_un addr {};
    const socklen_t len = RecoveryChannel::address(socket.identity, addr);
    if (!len || !socket.current()) return {};
    const int fd = RecoveryChannel::openSocket();
    if (fd < 0) return {};
    // SO_PASSCRED autobinds a private abstract reply address on connect.
    HolderReply result;
    const char request = purge ? RecoveryChannel::purge : RecoveryChannel::query;
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), len) == 0 && send(fd, &request, 1, 0) == 1) {
        result.requested = true;
        pollfd reply {fd, POLLIN, 0};
        if (poll(&reply, 1, RecoveryChannel::replyTimeoutMs) <= 0 ||
            !RecoveryChannel::receive(fd, &result.holder, sizeof(result.holder))) result.holder = 0;
    }
    close(fd);
    return result;
}

/* ---- main ---- */

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--purge]\n"
            "  Take over a stuck session lock and release it.\n"
            "  --purge   stop only this session's confirmed locker if takeover is refused\n",
            argv0);
}

using Clock = std::chrono::steady_clock;

// Bound every sync/dispatch, including against an unresponsive compositor.
static bool dispatchUntil(wl_display *display, const int &ready, const int &refused,
                          Clock::time_point deadline)
{
    while (!ready && !refused) {
        if (wl_display_dispatch_pending(display) < 0) return false;
        if (ready || refused) return true;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (remaining.count() <= 0) return false;
        if (wl_display_prepare_read(display) != 0) continue;
        const int flushed = wl_display_flush(display);
        if (flushed < 0 && errno != EAGAIN) {
            wl_display_cancel_read(display);
            return false;
        }
        pollfd p {wl_display_get_fd(display), short(POLLIN | (flushed < 0 ? POLLOUT : 0)), 0};
        const int result = poll(&p, 1, int(remaining.count()));
        if (result > 0 && p.revents & POLLIN) {
            if (wl_display_read_events(display) < 0) return false;
        } else {
            wl_display_cancel_read(display);
            if (result <= 0 || p.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
        }
    }
    return true;
}

static void syncDone(void *data, wl_callback *cb, uint32_t)
{
    *static_cast<int *>(data) = 1;
    wl_callback_destroy(cb);
}

static bool sync(wl_display *display, Clock::time_point deadline)
{
    int done = 0;
    static const wl_callback_listener listener {.done = syncDone};
    wl_callback *callback = wl_display_sync(display);
    wl_callback_add_listener(callback, &listener, &done);
    const bool synced = dispatchUntil(display, done, 0, deadline);
    if (!done) wl_callback_destroy(callback);
    return synced;
}

static int takeover(const Endpoint &socket, Clock::time_point deadline)
{
    lock_manager = nullptr;
    got_locked = got_finished = 0;
    if (Clock::now() >= deadline) return 2;
    if (!socket.current()) {
        fprintf(stderr, "aerial-unlock: Wayland socket unavailable or changed: %s\n", socket.path.c_str());
        return 1;
    }
    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    const auto len = snprintf(addr.sun_path, sizeof(addr.sun_path), "/proc/self/fd/%d", socket.handle);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return 1;
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), offsetof(sockaddr_un, sun_path) + len + 1) != 0) {
        const int error = errno;
        close(fd);
        // Linux reports EAGAIN (not EINPROGRESS) when an AF_UNIX stream
        // listen queue is full. Do not block behind a wedged compositor.
        if (error == EAGAIN || error == EINTR) return 2;
        fprintf(stderr, "aerial-unlock: failed to connect to %s: %s\n", socket.path.c_str(), strerror(error));
        return 1;
    }
    // libwayland takes ownership of fd, even on failure.
    struct wl_display *display = wl_display_connect_to_fd(fd);
    if (!display) {
        fprintf(stderr, "aerial-unlock: failed to initialize Wayland connection\n");
        return 1;
    }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, nullptr);
    int rc = 2;
    if (!sync(display, deadline)) {
        // Inconclusive; we could not query the compositor's registry.
    } else if (!lock_manager) {
        fprintf(stderr,
                "aerial-unlock: ext_session_lock_manager_v1 not available on this "
                "compositor\n  (cannot take over a lock that isn't exposed)\n");
        rc = 1;
    } else {
        lock_obj = ext_session_lock_manager_v1_lock(lock_manager);
        ext_session_lock_v1_add_listener(lock_obj, &lock_listener, nullptr);
        if (dispatchUntil(display, got_locked, got_finished, deadline)) {
            if (got_locked) {
                ext_session_lock_v1_unlock_and_destroy(lock_obj);
                // Takeover may be confirmed just as its deadline expires.
                // Once we hold the lock, reserve a separate bounded flush
                // and sync window so the release is not left in a buffer.
                if (sync(display, Clock::now() + std::chrono::milliseconds(RecoveryChannel::releaseTimeoutMs))) rc = 0;
            } else {
                ext_session_lock_v1_destroy(lock_obj);
                rc = 3;
            }
        }
        ext_session_lock_manager_v1_destroy(lock_manager);
    }

    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return rc;
}

int main(int argc, char **argv)
{
    bool purge = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--purge") == 0) purge = true;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]); return 0;
        } else {
            usage(argv[0]); return 2;
        }
    }

    const std::string path = find_socket();
    if (path.empty()) return 1;
    const Endpoint socket(path);
    fprintf(stderr, "aerial-unlock: trying %s\n", path.c_str());
    const auto budget = std::chrono::milliseconds(RecoveryChannel::takeoverTimeoutMs);
    int rc = takeover(socket, Clock::now() + budget);
    HolderReply reply;
    if (rc == 3) {
        reply = confirmedHolder(socket, purge);
        if (purge && reply.requested) {
            if (reply.holder > 0) {
                fprintf(stderr, "aerial-unlock: supervisor is stopping confirmed holder pid %d\n", reply.holder);
            } else {
                fprintf(stderr, "aerial-unlock: no purge acknowledgement; stop outcome unknown, retrying takeover\n");
            }
            const auto deadline = Clock::now() + budget;
            do {
                rc = takeover(socket, deadline);
                if (rc != 3) break;
                usleep(50000);
            } while (Clock::now() < deadline);
        }
    }
    if (rc == 0) {
        fprintf(stderr, "aerial-unlock: recovered — took over the lock and released it\n");
    } else if (rc == 3) {
        fprintf(stderr, "aerial-unlock: compositor refused takeover.\n");
        if (reply.holder > 0 && !purge)
            fprintf(stderr, "  confirmed holder: pid %d\n  retry with: aerial-unlock --purge\n", reply.holder);
        else if (!purge || !reply.requested)
            fprintf(stderr, "  no confirmed Aerial holder for this session; no process was stopped.\n");
    } else if (rc == 2) {
        fprintf(stderr, "aerial-unlock: inconclusive — compositor did not confirm takeover and release.\n");
    }
    return rc;
}
