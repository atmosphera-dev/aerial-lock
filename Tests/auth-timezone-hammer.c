#define _GNU_SOURCE
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Test-only: hammer libc's actual timezone lock in the multithreaded locker.
 * The preload is inherited across exec, but must never add threads to the
 * plain helper. Constructors do not rerun in Quickshell's fork-only PAM child. */
static void *hammer(void *unused) {
    (void)unused;
    for (;;) {
        time_t now = time(NULL);
        struct tm local;
        localtime_r(&now, &local);
    }
    return NULL;
}

__attribute__((constructor)) static void start_hammer(void) {
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n < 0) return;
    exe[n] = 0;
    const char *name = strrchr(exe, '/');
    if (!name || (strcmp(name + 1, "qs") && strcmp(name + 1, "quickshell"))) return;
    for (int i = 0; i < 4; ++i) {
        pthread_t thread;
        if (!pthread_create(&thread, NULL, hammer, NULL)) pthread_detach(thread);
    }
}
