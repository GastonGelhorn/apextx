#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generate the small Roboto subsets used by NB4 steering/throttle readouts."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
for name, size, symbols in (("gauge_22", 22, "+-0123456789"),
                            ("percent_15", 15, "%")):
    target = Path("radio/src/fonts/lvgl/nb4/lv_font_nb4_" + name + ".c")
    subprocess.run([
        "npx", "--yes", "--package=lv_font_conv@1.5.3", "lv_font_conv",
        "--no-prefilter", "--bpp", "4", "--size", str(size),
        "--font", "radio/src/fonts/Roboto/Roboto-Regular.ttf",
        "--symbols", symbols, "--format", "lvgl", "--no-compress",
        "--force-fast-kern-format", "-o", str(target),
    ], cwd=root, check=True)
    generated = root / target
    generated.write_text(
        "/* SPDX-License-Identifier: Apache-2.0 */\n" +
        generated.read_text().rstrip() + "\n")
