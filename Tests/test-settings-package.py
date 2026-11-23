#!/usr/bin/env python3
"""Acceptance recipe adaptation must retain non-settings package declarations."""
import unittest
from settings_package import with_settings_dependencies


class SettingsPackage(unittest.TestCase):
    def test_full_app_dependencies_and_idempotence(self):
        source = """pkgname=aerial-lock
depends=('quickshell>=0.3' 'qt6-base'
         'qt6-declarative' 'libarchive' 'ffmpeg')
makedepends=('wayland-protocols')
package() { make install; }
"""
        adapted = with_settings_dependencies(source)
        for dependency in ("qt6-base>=6.12.0", "qt6-declarative>=6.12.0", "qt6-svg>=6.12.0",
                           "qt6-shadertools>=6.12.0", "cmake", "ninja", "wayland-protocols",
                           "quickshell>=0.3", "libarchive", "ffmpeg"):
            self.assertEqual(adapted.count(dependency), 1, dependency)
        self.assertTrue(adapted.endswith("package() { make install; }\n"))
        self.assertEqual(with_settings_dependencies(adapted), adapted)
        self.assertNotIn("qt6-svg", source)


if __name__ == "__main__":
    unittest.main()
