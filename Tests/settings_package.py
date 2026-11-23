"""Current full-app dependencies for disposable exact-tree acceptance packages.

The retained release recipe stays byte-identical. This adapter is not publishing.
"""
import re
import shlex


def with_settings_dependencies(text):
    def runtime(match):
        dependencies = shlex.split(match[1])
        for name in ("qt6-base", "qt6-declarative", "qt6-svg"):
            floor = name + ">=6.12.0"
            if name in dependencies:
                dependencies[dependencies.index(name)] = floor
            elif floor not in dependencies:
                assert not any(item.startswith(name + ">") or item.startswith(name + "=") for item in dependencies), name
                dependencies.append(floor)
        return "depends=(" + " ".join(map(shlex.quote, dependencies)) + ")"
    text, count = re.subn(r"^depends=\((.*?)\)", runtime, text, flags=re.M | re.S)
    assert count == 1

    def build(match):
        dependencies = shlex.split(match[1])
        for item in ("cmake", "ninja", "qt6-shadertools>=6.12.0"):
            if item not in dependencies:
                dependencies.append(item)
        return "makedepends=(" + " ".join(map(shlex.quote, dependencies)) + ")"
    text, count = re.subn(r"^makedepends=\((.*?)\)", build, text, flags=re.M | re.S)
    assert count == 1
    return text
