# SPDX-License-Identifier: GPL-2.0-only
"""Public-release metadata and license-policy tests for ApexTX."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")


class Nb4ReleasePolicyTest(unittest.TestCase):
    def test_project_version_is_consistent(self) -> None:
        version = (ROOT / "APEXTX_VERSION").read_text(encoding="utf-8").strip()
        self.assertRegex(version, VERSION_RE)
        self.assertIn(version, (ROOT / "README.md").read_text(encoding="utf-8"))
        self.assertIn(version, (ROOT / "CHANGELOG.md").read_text(encoding="utf-8"))

    def test_release_workflow_uses_qualified_public_build(self) -> None:
        workflow = (ROOT / ".github/workflows/release.yml").read_text(
            encoding="utf-8")
        self.assertIn("-DNB4_RF_PROFILE=QUALIFIED", workflow)
        self.assertIn("-DAPEXTX_PUBLIC_RELEASE=ON", workflow)
        self.assertIn("SHA256SUMS", workflow)
        self.assertIn("actions/attest-build-provenance", workflow)

    def test_project_owned_sources_have_spdx_headers(self) -> None:
        patterns = (
            "radio/src/nb4*.[ch]", "radio/src/nb4*.cpp",
            "radio/src/gui/**/nb4*.[ch]", "radio/src/gui/**/nb4*.cpp",
            "radio/src/targets/pl18/nb4*.[ch]",
            "radio/src/targets/pl18/nb4*.cpp",
            "radio/src/targets/pl18/nb4*.cmake",
            "radio/src/tests/nb4*.cpp", "radio/src/fonts/lvgl/nb4/*.c",
            "tools/nb4*.py", "tools/nb4*.sh",
            "sdcard/SCRIPTS/TOOLS/NB4*.lua", "sdcard/THEMES/ApexTX*/*.yml",
            ".github/workflows/ci.yml", ".github/workflows/release.yml",
            ".github/dependabot.yml",
        )
        files = {
            path for pattern in patterns for path in ROOT.glob(pattern)
            if not path.name.startswith("nb4p_")
        }
        self.assertTrue(files)
        missing = [
            str(path.relative_to(ROOT)) for path in sorted(files)
            if "SPDX-License-Identifier:" not in
            "\n".join(path.read_text(encoding="utf-8").splitlines()[:25])
        ]
        self.assertEqual(missing, [])

    def test_apextx_brand_assets_and_names_are_consistent(self) -> None:
        for relative in (
            "docs/branding/apextx-logo-source.jpg",
            "docs/branding/apextx-logo.png",
            "radio/src/bitmaps/480x272/splash_logo.png",
            "radio/src/bitmaps/480x272/default_theme/mask_top_logo.png",
            "radio/src/bitmaps/480x272/default_theme/mask_edgetx.png",
        ):
            self.assertTrue((ROOT / relative).is_file(), relative)

        self.assertTrue((ROOT / "sdcard/THEMES/ApexTXDark/theme.yml").is_file())
        self.assertTrue((ROOT / "sdcard/THEMES/ApexTXLight/theme.yml").is_file())
        self.assertFalse((ROOT / "NB4_VERSION").exists())


if __name__ == "__main__":
    unittest.main()
