# SPDX-License-Identifier: GPL-2.0-only
"""Keep every NB4 interface text in the shared translation tables."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "radio/src"
I18N = SRC / "translations/i18n"

# A comparison of the stored UI language with a literal language code, for
# example `uiLanguage[1] == 's'` or `strncmp(g_eeGeneral.uiLanguage, "es", 2)`.
LANGUAGE_BRANCH = re.compile(
    r"uiLanguage\s*\[\s*[01]\s*\]\s*[!=]=\s*'[a-z]'"
    r"|\b(?:strn?cmp|memcmp)\s*\([^;]*uiLanguage[^;]*\"[a-z]{2}\""
    r"|\b(?:strn?cmp|memcmp)\s*\([^;]*\"[a-z]{2}\"[^;]*uiLanguage")
DEFINE = re.compile(r"^#define (TR_NB4_\w+)\s", re.MULTILINE)


def nb4_names(path: Path) -> list[str]:
    return DEFINE.findall(path.read_text(encoding="utf-8"))


class Nb4I18nTest(unittest.TestCase):
    def test_code_never_chooses_text_by_language_code(self) -> None:
        offenders = []
        for path in sorted(SRC.rglob("*")):
            if path.suffix not in {".c", ".cpp", ".h"}:
                continue
            relative = path.relative_to(SRC)
            if relative.parts[0] in {"tests", "translations", "thirdparty"}:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for match in LANGUAGE_BRANCH.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                offenders.append(f"{relative}:{line}")
        self.assertEqual(
            offenders, [],
            "Use STR_NB4_* or NB4_STR() instead of comparing the UI "
            "language; see CONTRIBUTING.md")

    def test_every_language_table_defines_the_same_nb4_strings(self) -> None:
        english = nb4_names(I18N / "en.h")
        self.assertEqual(len(english), len(set(english)), "duplicate TR_NB4_*")
        for header in sorted(I18N.glob("*.h")):
            with self.subTest(header=header.name):
                self.assertEqual(sorted(nb4_names(header)), sorted(english))

    def test_every_nb4_string_is_listed_for_both_lookups(self) -> None:
        english = set(nb4_names(I18N / "en.h"))
        listed = set(re.findall(
            r"^STR(?:ARRAY)?\((NB4_\w+)\)$",
            (SRC / "translations/string_list.h").read_text(encoding="utf-8"),
            re.MULTILINE))
        simulated = set(re.findall(
            r"^#define STR_(NB4_\w+) currentLangStrings->STR_\1$",
            (SRC / "translations/sim_string_list.h").read_text(
                encoding="utf-8"),
            re.MULTILINE))
        expected = {name[len("TR_"):] for name in english}
        self.assertEqual(listed, expected)
        self.assertEqual(simulated, expected)


if __name__ == "__main__":
    unittest.main()
