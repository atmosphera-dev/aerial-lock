"""Race-tolerant process observations for test drivers, never product runtime.

An existence check cannot keep procfs entries alive. Read directly and treat
ENOENT/ESRCH as an exited process (or a finished thread/closed descriptor).
Other errors must still reach the caller.
"""
from contextlib import contextmanager
from pathlib import Path


@contextmanager
def require_process(pid):
    """A vanished required process is a failed outcome, not an empty sample."""
    try:
        yield
    except (FileNotFoundError, ProcessLookupError) as error:
        raise RuntimeError(f"process {pid} exited during observation") from error


def process_text(pid, entry):
    try:
        return (Path("/proc") / str(pid) / entry).read_text()
    except (FileNotFoundError, ProcessLookupError):
        return ""


def process_entries(pid, entry):
    try:
        return list((Path("/proc") / str(pid) / entry).iterdir())
    except (FileNotFoundError, ProcessLookupError):
        return []


def process_exited(pid):
    status = process_text(pid, "stat")
    return not status or status.rsplit(") ", 1)[1].startswith("Z ")


def descendants(pid):
    result = []
    for child in process_text(pid, f"task/{pid}/children").split():
        child = int(child)
        result.append(child)
        result.extend(descendants(child))
    return result


def thread_children(pid):
    return [int(child) for task in process_entries(pid, "task")
            for child in process_text(pid, f"task/{task.name}/children").split()]
