#pragma once

// The supervisor publishes its confirmed child on a per-compositor control
// socket. Linux abstract sockets disappear with their owner, so there is no
// stale PID record to trust. Both ends check kernel-supplied sender credentials.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace RecoveryChannel {

constexpr char query = 'q';
constexpr char purge = 'p';

inline socklen_t address(const char *path, sockaddr_un &addr)
{
    struct stat st {};
    if (stat(path, &st) != 0 || !S_ISSOCK(st.st_mode)) return 0;
    addr = {};
    addr.sun_family = AF_UNIX;
    int n = snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1,
                     "aerial-lock-holder-%ju-%jx-%jx", uintmax_t(getuid()),
                     uintmax_t(st.st_dev), uintmax_t(st.st_ino));
    if (n < 0 || size_t(n) >= sizeof(addr.sun_path) - 1) return 0;
    return offsetof(sockaddr_un, sun_path) + 1 + n;
}

inline int openSocket()
{
    const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    const int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0) return fd;
    close(fd);
    return -1;
}

inline bool receive(int fd, void *data, size_t size, sockaddr_un *peer = nullptr,
                    socklen_t *peerLen = nullptr)
{
    iovec iov {data, size};
    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(ucred))] {};
    msghdr msg {};
    msg.msg_name = peer;
    msg.msg_namelen = peerLen ? *peerLen : 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    const ssize_t received = recvmsg(fd, &msg, MSG_CMSG_CLOEXEC);
    if (received < 0) return false;
    if (peerLen) *peerLen = msg.msg_namelen;
    bool ownUser = false;
    for (cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level != SOL_SOCKET) continue;
        if (c->cmsg_type == SCM_RIGHTS) {
            // Even a truncated or invalid packet can deliver descriptors.
            const size_t count = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            for (size_t i = 0; i < count; ++i) {
                int incoming;
                memcpy(&incoming, CMSG_DATA(c) + i * sizeof(int), sizeof(incoming));
                close(incoming);
            }
        } else if (c->cmsg_type == SCM_CREDENTIALS && c->cmsg_len == CMSG_LEN(sizeof(ucred))) {
            ucred sender {};
            memcpy(&sender, CMSG_DATA(c), sizeof(sender));
            ownUser = sender.uid == getuid();
        }
    }
    return ownUser && received == ssize_t(size) && !(msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC));
}

}
