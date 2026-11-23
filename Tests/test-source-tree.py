#!/usr/bin/env python3
"""A source copy retains authored inputs, not links into generated trees."""
from pathlib import Path
import tempfile
import unittest
from source_tree import copy_source_tree


class SourceTree(unittest.TestCase):
    def test_source_ownership_and_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); source = root / "source"; source.mkdir()
            (source / "settings").mkdir()
            (source / "third_party/interactive").mkdir(parents=True)
            (source / "third_party/interactive/keep.cpp").write_text("vendor input")
            (source / "Owned.qml").write_text("authored input")
            (source / "source-link").symlink_to("Owned.qml")
            output = source / "custom output"; output.mkdir()
            (output / "CMakeFiles").mkdir()
            (output / "CMakeCache.txt").write_text("CMAKE_HOME_DIRECTORY:INTERNAL=" + str(source / "settings"))
            (output / "generated.qml").write_text("derived input")
            (source / "settings/interactive").symlink_to(output, target_is_directory=True)
            (source / ".envrc").write_text("do not transfer")
            destination = root / "copied"
            copy_source_tree(source, destination)
            self.assertEqual((destination / "Owned.qml").read_text(), "authored input")
            self.assertEqual((destination / "third_party/interactive/keep.cpp").read_text(), "vendor input")
            self.assertTrue((destination / "source-link").is_symlink())
            self.assertEqual((destination / "source-link").readlink(), Path("Owned.qml"))
            for name in (".envrc", "custom output", "settings/interactive"):
                self.assertFalse((destination / name).exists(), name)


if __name__ == "__main__":
    unittest.main()
