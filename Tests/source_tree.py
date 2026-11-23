"""Copy authored source, never dereference development links into build output."""
from pathlib import Path
import shutil


def copy_source_tree(source, destination, *extra_ignores):
    source = Path(source).resolve()
    ignored = shutil.ignore_patterns(".git", ".envrc", "build", "__pycache__", *extra_ignores)

    def inputs(directory, names):
        directory = Path(directory)
        excluded = set(ignored(str(directory), names))
        if directory == source / "settings":
            excluded.add("interactive")  # Make-owned module-root link, not source
        for name in names:
            path = directory / name
            if path.is_dir() and (path / "CMakeCache.txt").is_file() and (path / "CMakeFiles").is_dir():
                excluded.add(name)
        return excluded

    return shutil.copytree(source, destination, ignore=inputs, symlinks=True)
