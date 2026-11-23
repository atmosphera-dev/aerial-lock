#!/usr/bin/env python3
"""Generate/check source-only IA tooling against the real Qt module output.

Run --write only after a Qt 6.12 build. Runtime engines never use this tree.
"""
import argparse
import os
from pathlib import Path


def descriptors(source, build):
    inventory = (build / "interactive/runtime-files.txt").read_text().splitlines()
    assert len(inventory) == 12
    expected = {}
    for index, (uri, directory) in enumerate((("InterActive.Foundation", "foundation"),
                                             ("InterActive", "controls"),
                                             ("InterActive.Components", "components"))):
        module = source / "settings/lint" / uri.replace(".", "/")
        qmldir, typeinfo = map(Path, inventory[index * 4 + 2:index * 4 + 4])
        lines = []
        for line in qmldir.read_text().splitlines():
            words = line.split()
            # These descriptors are intentionally tooling-only, not a plugin.
            if any(word in words for word in ("plugin", "classname", "linktarget", "prefer")):
                continue
            if words and words[-1].endswith(".qml"):
                original = source / "third_party/interactive/src" / directory / words[-1]
                assert original.is_file(), original
                words[-1] = os.path.relpath(original, module)
                line = " ".join(words)
            lines.append(line)
        expected[module / "qmldir"] = "\n".join(lines).rstrip() + "\n"
        metadata = typeinfo.read_text()
        # Qt's generated source locations are relative; never pin a VM path.
        assert str(source) not in metadata and str(build) not in metadata
        expected[module / typeinfo.name] = metadata
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("build", type=Path)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    expected = descriptors(args.source.resolve(), args.build.resolve())
    for path, text in expected.items():
        if args.write:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        else:
            assert path.read_text() == text, "stale IA tooling descriptor: " + str(path)
    actual = {path for path in (args.source.resolve() / "settings/lint").rglob("*") if path.is_file()}
    assert actual == set(expected), actual ^ set(expected)
    print("INTERACTIVE_METADATA_PASS three modules, real native metadata, original QML")


if __name__ == "__main__":
    main()
