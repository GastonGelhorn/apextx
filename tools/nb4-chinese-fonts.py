#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Regenerate Simplified Chinese LVGL fonts from the complete translation table.

Uses the repository's OFL-licensed Noto Sans CJK SC fonts and lv_font_conv 1.5.3.
One-bit CJK bitmaps keep the complete character set within the NB4 image budget.
Latin letters and UI symbols fall back to the existing matching English font.
Run from the repository root; requires Node/npx and clang++.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "radio/src"
WORK = ROOT / "build/chinese-font-generation"
OUT = SRC / "fonts/lvgl/std"


def main() -> None:
    WORK.mkdir(parents=True, exist_ok=True)
    text = (SRC / "translations/i18n/cn.h").read_text(encoding="utf-8")
    symbols = ",".join(hex(code) for code in sorted({ord(c) for c in text
        if 0x2000 <= ord(c) <= 0x9FFF or 0xFF00 <= ord(c) <= 0xFFEF}))
    for suffix, size, weight in (("XXS", 9, "Regular"), ("XS", 13, "Regular"),
                                ("STD", 16, "Regular"), ("bold_STD", 16, "Bold"),
                                ("L", 24, "Regular"), ("bold_XL", 32, "Bold")):
        stem = "lv_font_cn_" + suffix
        target = OUT / (stem + ".c")
        generated = target if suffix == "STD" else WORK / "lv_font.inc"
        command = ["npx", "--yes", "--package=lv_font_conv@1.5.3", "lv_font_conv",
                   "--no-prefilter", "--bpp", "1", "--size", str(size),
                   "--font", f"radio/src/fonts/Noto/NotoSansCJKsc-{weight}.otf",
                   "-r", symbols, "--format", "lvgl", "--force-fast-kern-format",
                   "--no-compress", "-o", str(generated.relative_to(ROOT))]
        if suffix == "STD":
            command += ["--lv-fallback", "lv_font_en_STD"]
        subprocess.run(command, cwd=ROOT, check=True)
        if suffix != "STD":
            exe = WORK / "compress-font"
            subprocess.run(["clang++", "-Wno-deprecated", "-DNO_KERN",
                            "-I" + str(SRC / "thirdparty"), "-I" + str(WORK),
                            str(SRC / "fonts/lvgl/lz4_font.cpp"),
                            str(SRC / "thirdparty/lz4/lz4hc.c"),
                            str(SRC / "thirdparty/lz4/lz4.c"), "-o", str(exe)],
                           cwd=ROOT, check=True)
            subprocess.run([str(exe), "std/" + stem],
                           cwd=OUT.parent, check=True)
        target.write_text("/* SPDX-License-Identifier: OFL-1.1 */\n" +
                          target.read_text(encoding="utf-8"), encoding="utf-8")
    print("Complete Simplified Chinese fonts generated.")


if __name__ == "__main__":
    main()
