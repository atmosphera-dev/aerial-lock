#pragma once

// Executable-local syscall seam. Product code still calls the real fsync API;
// tests can fail a precise file/directory boundary without product backdoors.
#include <cerrno>
#include <functional>
#include <sys/syscall.h>
#include <unistd.h>

inline std::function<bool(int)> failSync;
struct SyncFault {
    explicit SyncFault(std::function<bool(int)> predicate) { failSync = std::move(predicate); }
    ~SyncFault() { failSync = {}; }
};

extern "C" int fsync(int fd)
{
    if (failSync && failSync(fd)) { errno = EIO; return -1; }
    return int(syscall(SYS_fsync, fd));
}
