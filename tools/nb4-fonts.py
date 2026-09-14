#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Rebuild the embedded NB4 Barlow font set (lv_font_conv 1.5.3)."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import urllib.request

root = Path(__file__).resolve().parents[1]
src = root / "radio/src"
assets = src / "fonts/Barlow"
out = src / "fonts/lvgl/nb4"
work = root / "build/font-generation"
for directory in (assets, out, work):
    directory.mkdir(parents=True, exist_ok=True)
files = {
    "Barlow-Regular.ttf": "barlow/Barlow-Regular.ttf",
    "Barlow-SemiBold.ttf": "barlow/Barlow-SemiBold.ttf",
    "BarlowCondensed-SemiBold.ttf": "barlowcondensed/BarlowCondensed-SemiBold.ttf",
    "OFL.txt": "barlow/OFL.txt",
    "OFL-Condensed.txt": "barlowcondensed/OFL.txt",
}
for name, remote in files.items():
    path = assets / name
    if not path.exists():
        path.write_bytes(urllib.request.urlopen("https://raw.githubusercontent.com/google/fonts/main/ofl/" + remote).read())
(assets / "sources.json").write_text(json.dumps({name: {"source": "https://github.com/google/fonts/tree/main/ofl/" + remote,
    "sha256": hashlib.sha256((assets / name).read_bytes()).hexdigest()} for name, remote in files.items()}, indent=2) + "\n")
original = (src / "fonts/lvgl/make_fonts.sh").read_text()
symbols = re.search(r'^SYMBOLS="([^"]+)"', original, re.M).group(1)
symbols += ",61476,61881,61463"
latin = "0x20-0x7F,0xA0-0xFF,0x100-0x17F,0x2022,0x2026,0x2014,0x0394,0x2265,0x2212"
roboto = src / "fonts/Roboto"
for suffix, size, font in [("XXS", 12, roboto / "Roboto-Regular.ttf"),
    ("XS", 14, roboto / "Roboto-Regular.ttf"),
    ("STD", 17, roboto / "Roboto-Regular.ttf"),
    ("bold_STD", 17, roboto / "Roboto-Bold.ttf"),
    ("L", 26, assets / "BarlowCondensed-SemiBold.ttf"),
    ("bold_XL", 34, assets / "BarlowCondensed-SemiBold.ttf"),
    ("bold_LXL", 52, assets / "BarlowCondensed-SemiBold.ttf"),
    ("bold_XXL", 66, assets / "BarlowCondensed-SemiBold.ttf")]:
    stem = "lv_font_en_" + suffix
    target = out / (stem + ".c") if suffix == "STD" else work / "lv_font.inc"
    command = ["npx", "--yes", "--package=lv_font_conv@1.5.3", "lv_font_conv", "--no-prefilter", "--bpp", "4", "--size", str(size),
        "--font", str(font), "-r", ("0x20-0x7F,0xB0,0x2014,0x2212" if size >= 48 else latin),
        "--format", "lvgl", "--force-fast-kern-format", "--no-compress", "-o", str(target)]
    if size <= 17:
        command += ["--font", str(src / "fonts/lvgl/EdgeTX/extra.ttf"), "-r", "0x88-0x96",
            "--font", str(src / "fonts/lvgl/EdgeTX/OpenArrow-Regular.woff"), "-r", "0x21E8=>0x80,0x21E6=>0x81,0x21E7=>0x82,0x21E9=>0x83",
            "--font", str(src / "thirdparty/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"), "-r", symbols]
    subprocess.run(command, check=True, cwd=root)
    if suffix != "STD":
        exe = work / "compress-font"
        subprocess.run(["clang++", "-Wno-deprecated", "-I" + str(src / "thirdparty"), "-I" + str(work),
            str(src / "fonts/lvgl/lz4_font.cpp"), str(src / "thirdparty/lz4/lz4hc.c"), str(src / "thirdparty/lz4/lz4.c"),
            "-o", str(exe)], check=True)
        subprocess.run([str(exe), "nb4/" + stem], check=True, cwd=out.parent)
    # The generated comment must not depend on a checkout path.
    generated = out / (stem + ".c")
    generated_text = generated.read_text().replace(str(root) + "/", "")
    if size <= 17:
        license_id = "GPL-2.0-only AND Apache-2.0 AND OFL-1.1"
    else:
        license_id = "OFL-1.1"
    generated.write_text(
        f"/* SPDX-License-Identifier: {license_id} */\n" + generated_text)
print("Embedded NB4 fonts generated.")
