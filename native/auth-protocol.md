# Authentication boundary

The existing native generic plugin registers `AerialLock.Native.AuthContext`.
Its Qt adapter uses `native/auth-client.c`, the shared C transport/worker
owner. The C layer returns events rather than calling consumer callbacks,
so a Qt signal can synchronously abort, replace or destroy its owner safely.
It owns one `posix_spawn`ed `/usr/libexec/aerial-lock/aerial-lock-auth` per
conversation. PAM runs only after exec, in a single-threaded program linked to
libpam, not Qt. The supervisor does not participate. No PAM module or libc work
runs in a fork-only copy of the multithreaded locker.

`make dev` instead compiles the checkout's absolute `native/aerial-lock-auth`
path into its plugin. The native-config fingerprint tracks that selection, so
switching back to an installed build rebuilds the consumer with LIBEXECDIR.

The helper receives service, username and PAM configuration directory in argv.
It runs `pam_start_confdir`, `pam_authenticate`, then `pam_end`; it does not add
account management. The existing `/etc/pam.d/aerial-lock` remains unchanged.

## Protocol v1

Each frame ends with LF. `HEX` is lowercase hex-encoded UTF-8, including empty
strings. NUL is not representable in PAM responses and is rejected. Lengths are
bounded by the PAM headers, including the terminating NUL.

| # | Direction | Frame | Meaning |
|---|---|---|---|
| 1 | Helper to owner | `P0 HEX` | Echo-off prompt |
| 2 | Helper to owner | `P1 HEX` | Echo-on prompt |
| 3 | Helper to owner | `I HEX` | Information, including fingerprint instructions |
| 4 | Helper to owner | `E HEX` | PAM error message |
| 5 | Owner to helper | `R HEX` | Response to the current prompt |
| 6 | Owner to helper | `A` or EOF | Abort at the conversation boundary |
| 7 | Helper to owner | `D success`, `D failed`, `D max-tries`, `D error` | Terminal result |

The owner uses a private socket pair duplicated onto helper stdin/stdout. Other
file descriptors are closed at spawn; helper stderr goes to `/dev/null`.
Responses never enter argv, environment, logs or Qt's queued process buffers.
Native UTF-8/hex copies are explicitly wiped immediately after transmission or
decoding. PAM takes ownership of accepted response allocations and wipes them
after delivery; failed conversations wipe their partial responses themselves.
The existing QML draft/accepted-response lifetime limitation still applies.

A success frame is insufficient: the owner also requires clean helper exit,
drained EOF and no extra/truncated output before emitting completion. Malformed frames, crashes,
exec errors and oversized responses cannot authenticate. Abort closes transport,
kills the helper and asynchronously reaps it, independently of PAM cooperation.
The worker disables core dumps and requests parent-death SIGKILL. A stale worker
has no callbacks into a replacement conversation. C transport mappings and
outbound frames are explicitly wiped, excluded from core dumps, and mlocked
where permitted. Partial nonblocking sends retain only their unsent tail;
bytes already transmitted are wiped immediately. Qt only schedules readiness
and the asynchronous reaper; it does not implement a second frame parser.

The Lock controller continues to own prompting, user-visible state, `lock-state:`
logs and the inactivity watchdog. A helper stalled outside its conversation is
still cancellable; the watchdog never runs while waiting for user input.
