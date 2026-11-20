"""Best-effort retirement for task-owned settings fixtures, not desktop state."""
from pathlib import Path
import subprocess
import sys
import threading


class HostLease:
    """Renew a task host during caller work; no renewal permits natural expiry.

    The callback has a 10-second IPC timeout. A 20-second renewal period keeps
    the maximum normal gap below the host's 90-second inactivity watchdog,
    regardless of the number of native waits or captures in a test path.
    """
    def __init__(self, process, renew, interval=20):
        self.process, self.renew, self.interval = process, renew, interval
        self.stop = threading.Event(); self.failure = None
        renew()
        self.thread = threading.Thread(target=self.run, daemon=True); self.thread.start()

    def run(self):
        while not self.stop.wait(self.interval):
            if self.process.poll() is not None: return
            try: self.renew()
            except Exception as error:
                if self.process.poll() is None: self.failure = error
                return

    def finish(self):
        self.stop.set(); self.thread.join(timeout=11)
        if self.thread.is_alive(): raise RuntimeError("host lease did not retire")
        if self.failure is not None: raise RuntimeError("host lease renewal failed: " + str(self.failure))


class Cleanup:
    def __init__(self):
        self.failures = []

    def __call__(self, label, action):
        try:
            return action()
        except Exception as error:
            self.failures.append(label + ": " + str(error))

    def finish(self):
        if not self.failures:
            return
        message = "cleanup failed: " + "; ".join(self.failures)
        original = sys.exc_info()[1]
        if original is not None:
            note = getattr(original, "add_note", None)
            if callable(note): note(message)
            else:
                original.__notes__ = [*getattr(original, "__notes__", []), message]
                print(message, file=sys.stderr)
        else:
            raise RuntimeError(message)


def stop_process(process):
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)


def access_paths(output, runtime, socket, *executables):
    """Every private ancestor is searchable before the disposable UID starts."""
    home = Path.home()
    if not output.is_relative_to(home):
        raise RuntimeError("task output must be inside the guest home")
    paths = []
    for leaf in (output, *map(Path, executables)):
        if not leaf.is_relative_to(home):
            raise RuntimeError("task access path must be inside the guest home")
        paths.extend(path for path in reversed(leaf.parents) if path.is_relative_to(home))
        paths.append(leaf)
    return list(dict.fromkeys([*paths, Path(runtime), Path(socket)]))
