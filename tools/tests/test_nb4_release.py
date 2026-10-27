# SPDX-License-Identifier: GPL-2.0-only
"""Public-release metadata and license-policy tests for ApexTX."""

from __future__ import annotations

import re
import ast
import unittest
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")


class Nb4ReleasePolicyTest(unittest.TestCase):
    def test_project_version_is_consistent(self) -> None:
        version = (ROOT / "APEXTX_VERSION").read_text(encoding="utf-8").strip()
        self.assertRegex(version, VERSION_RE)
        self.assertIn(version, (ROOT / "README.md").read_text(encoding="utf-8"))
        self.assertIn(version, (ROOT / "CHANGELOG.md").read_text(encoding="utf-8"))

    def test_release_workflow_builds_the_shipped_rf_route(self) -> None:
        workflow = (ROOT / ".github/workflows/release.yml").read_text(
            encoding="utf-8")
        self.assertIn("-DNB4_RF_PROFILE=RECOVERED_USART6", workflow)
        self.assertIn("-DAPEXTX_PUBLIC_RELEASE=ON", workflow)
        self.assertIn("SHA256SUMS", workflow)
        self.assertIn("actions/attest-build-provenance", workflow)
        self.assertIn("tools/nb4-audio-packs.py", workflow)
        self.assertIn("v2.12.3", workflow)
        self.assertIn("4e34b6cb40ab53c6dcd293e2b1344b54d912d41c", workflow)
        self.assertLess(
            workflow.index("apt\\.kitware\\.com"),
            workflow.index("apt-get update"),
        )

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

    def test_publication_requires_both_radios_and_all_four_audio_packs(self) -> None:
        workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        required = workflow.split(
            "- name: Verify all radio, desktop and audio downloads are present", 1
        )[1].split("- name: Publish release", 1)[0]
        for model in ("nb4", "nb4plus"):
            for extension in ("bin", "zip", "tar.gz", "update.json"):
                self.assertIn(f'"apextx-{model}-$version.{extension}"', required)
        for language in ("en", "es", "de", "cn"):
            self.assertIn(f'"apextx-audio-{language}-$version.zip"', required)
        self.assertIn('test -s "dist/$file"', required)

    def test_package_metadata_includes_chinese_and_its_font_license(self) -> None:
        source = (ROOT / "tools/nb4-package.py").read_text(encoding="utf-8")
        found = {}
        for node in ast.walk(ast.parse(source)):
            if isinstance(node, ast.Dict):
                for key, value in zip(node.keys, node.values):
                    if isinstance(key, ast.Constant) and key.value in (
                        "languages", "audioLanguages", "fonts"
                    ):
                        found[key.value] = ast.literal_eval(value)
        self.assertEqual(found["languages"], ["en", "es", "de", "cn"])
        self.assertEqual(found["audioLanguages"], found["languages"])
        self.assertIn("Noto Sans CJK SC", found["fonts"])
        self.assertIn('"licenses/NotoSansCJK-OFL.txt": ROOT / '
                      '"radio/src/fonts/Noto/LICENSE_OFL.txt"', source)
        self.assertTrue((ROOT / "radio/src/fonts/Noto/LICENSE_OFL.txt").is_file())

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

    def test_apextx_welcome_prompts_are_radio_compatible(self) -> None:
        phrases = {
            "en": '(\"Welcome to Apex T X!\", \"hello\")',
            "es": '(\"Bienvenido a Apex T X\", \"hello\")',
            "de": '(\"Willkommen bei Apex T X\", \"hello\")',
        }
        for language, phrase in phrases.items():
            with self.subTest(language=language):
                prompt = ROOT / f"sdcard/SOUNDS/{language}/SYSTEM/hello.wav"
                self.assertTrue(prompt.is_file())
                with wave.open(str(prompt), "rb") as sound:
                    self.assertEqual(sound.getnchannels(), 1)
                    self.assertEqual(sound.getsampwidth(), 2)
                    self.assertEqual(sound.getframerate(), 16000)
                    self.assertGreater(sound.getnframes(), 8000)
                mapping = (ROOT / f"radio/util/tts_{language}.py").read_text(
                    encoding="utf-8")
                self.assertIn(phrase, mapping)


if __name__ == "__main__":
    unittest.main()
