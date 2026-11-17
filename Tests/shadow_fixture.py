"""Guest-only account fixture restoration; never product authentication."""
from contextlib import contextmanager
import ctypes
import os
from pathlib import Path
import stat
import tempfile


@contextmanager
def account_lock():
    # shadow's normal account writers take this libc lock before commonio locks.
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.lckpwdf() != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error))
    try:
        yield
    finally:
        if libc.ulckpwdf() != 0:
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error))


def account_line(content, user):
    lines = [line for line in content.splitlines(keepends=True) if line.startswith(user.encode() + b':')]
    if len(lines) != 1:
        raise ValueError('expected exactly one fixture account')
    return lines[0]


def restore_account(user, original, database=Path('/etc/shadow')):
    if account_line(original, user) != original or not original.endswith(b'\n') or original.count(b':') != 8:
        raise ValueError('invalid fixture account backup')
    with account_lock():
        metadata = database.stat()
        current = database.read_bytes()
        line = account_line(current, user)
        replacement = current.replace(line, original)
        # Same-filesystem publication, with no truncation of the live database.
        with tempfile.NamedTemporaryFile(dir=database.parent, prefix='.' + database.name + '.aerial-',
                                         mode='wb', delete=False) as stream:
            temporary = Path(stream.name)
            try:
                os.fchown(stream.fileno(), metadata.st_uid, metadata.st_gid)
                os.fchmod(stream.fileno(), stat.S_IMODE(metadata.st_mode))
                stream.write(replacement)
                stream.flush()
                os.fsync(stream.fileno())
                stream.close()
                if temporary.read_bytes() != replacement:
                    raise OSError('incomplete account database replacement')
                os.replace(temporary, database)
                directory = os.open(database.parent, os.O_RDONLY | os.O_DIRECTORY)
                try:
                    os.fsync(directory)
                finally:
                    os.close(directory)
                if account_line(database.read_bytes(), user) != original:
                    raise OSError('fixture account restoration did not persist')
            finally:
                temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('user')
    parser.add_argument('backup', type=Path)
    args = parser.parse_args()
    metadata = args.backup.stat()
    if os.geteuid() != 0 or metadata.st_uid != 0 or stat.S_IMODE(metadata.st_mode) != 0o600:
        raise PermissionError('root-owned private guest account backup required')
    restore_account(args.user, account_line(args.backup.read_bytes(), args.user))
