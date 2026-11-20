"""Best-effort retirement for task-owned settings fixtures, not desktop state."""
from pathlib import Path
import subprocess
import sys


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
