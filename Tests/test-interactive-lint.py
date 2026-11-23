#!/usr/bin/env python3
"""The source-only IA imports resolve real types and still reject bad properties."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="aerial-ia-lint-") as temporary:
    root = Path(temporary)
    script = root / "Scripts/dev/qmllint.sh"
    script.parent.mkdir(parents=True)
    shutil.copy2(source / "Scripts/dev/qmllint.sh", script)
    shutil.copytree(source / "settings/lint", root / "settings/lint")
    (root / "third_party").symlink_to(source / "third_party", target_is_directory=True)
    fixture = root / "Consumer.qml"
    qml = """import QtQuick
import InterActive
import InterActive.Foundation as IA
import InterActive.Components
IAStatusBanner {
    title: "Resolved"
    actionText: "Check"
    property color observed: IA.Appearance.colors.canvas
}
"""
    fixture.write_text(qml)
    result = subprocess.run(["bash", str(script), str(fixture)], cwd=root,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    fixture.write_text(qml.replace('title: "Resolved"', 'unresolvedAerialProperty: "Rejected"'))
    result = subprocess.run(["bash", str(script), str(fixture)], cwd=root,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode != 0 and "unresolvedAerialProperty" in result.stderr, result.stdout + result.stderr
print("INTERACTIVE_LINT_PASS original QML and native types; unresolved property rejected")
