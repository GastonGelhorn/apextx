#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Turn the native tests' NB4 screen captures into PNG contact sheets.

The NB4 UX tests write one PPM per view when NB4_SCREENSHOT_DIR is set:

    NB4_SCREENSHOT_DIR=/tmp/after ./gtests-radio --gtest_filter='Nb4*'

This script groups those captures by prefix (for example
``route-en-portrait``) and writes one sheet per group, so every view can be
reviewed at a glance. Given a second directory it pairs captures of the same
name side by side, labelled before and after, and marks the pairs that differ.

    tools/nb4-contact-sheet.py /tmp/after -o /tmp/sheets
    tools/nb4-contact-sheet.py /tmp/before /tmp/after -o /tmp/sheets
"""

from __future__ import annotations

import argparse
import fnmatch
import re
from collections import defaultdict
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

GROUP = re.compile(r"^(.*?-(?:portrait|landscape))(?:-|$)")
LABEL_H = 18
GAP = 8
BACKGROUND = (32, 32, 36)
INK = (230, 230, 230)
CHANGED = (255, 170, 40)


def group_of(stem: str) -> str:
    match = GROUP.match(stem)
    return match.group(1) if match else stem.split("-", 1)[0]


def label_font(size: int = 12) -> ImageFont.ImageFont:
    for name in ("DejaVuSans.ttf", "Arial.ttf", "Helvetica.ttc",
                 "/System/Library/Fonts/Helvetica.ttc"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def scaled(image: Image.Image, scale: float) -> Image.Image:
    if scale == 1:
        return image
    return image.resize((round(image.width * scale), round(image.height * scale)),
                        Image.Resampling.LANCZOS)


def cell(name: str, images: list[Image.Image | None], scale: float,
         font: ImageFont.ImageFont) -> Image.Image:
    shown = [scaled(i, scale) if i is not None else None for i in images]
    w = max(i.width for i in shown if i is not None)
    h = max(i.height for i in shown if i is not None)
    changed = len(images) == 2 and None not in images and (
        images[0].size != images[1].size or
        ImageChops.difference(images[0], images[1]).getbbox() is not None)
    out = Image.new("RGB", (len(shown) * w + (len(shown) - 1) * 4, h + LABEL_H),
                    BACKGROUND)
    draw = ImageDraw.Draw(out)
    title = name + ("  [changed]" if changed else "")
    draw.text((2, 2), title, fill=CHANGED if changed else INK, font=font)
    for index, image in enumerate(shown):
        x = index * (w + 4)
        if image is None:
            draw.rectangle([x, LABEL_H, x + w - 1, LABEL_H + h - 1], outline=INK)
            draw.text((x + 6, LABEL_H + 6), "missing", fill=INK, font=font)
        else:
            out.paste(image, (x, LABEL_H))
    return out


def sheet(cells: list[Image.Image], columns: int, heading: str,
          font: ImageFont.ImageFont) -> Image.Image:
    cw = max(c.width for c in cells)
    ch = max(c.height for c in cells)
    rows = (len(cells) + columns - 1) // columns
    head = 28
    out = Image.new("RGB", (GAP + columns * (cw + GAP), head + rows * (ch + GAP)),
                    BACKGROUND)
    ImageDraw.Draw(out).text((GAP, 6), heading, fill=INK, font=label_font(16))
    for index, c in enumerate(cells):
        out.paste(c, (GAP + (index % columns) * (cw + GAP),
                      head + (index // columns) * (ch + GAP)))
    return out


def load(path: Path | None) -> Image.Image | None:
    if path is None or not path.exists():
        return None
    with Image.open(path) as image:
        return image.convert("RGB")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("directories", nargs="+", type=Path,
                        help="capture directory, or BEFORE AFTER")
    parser.add_argument("-o", "--output", type=Path, required=True)
    parser.add_argument("--match", default="*",
                        help="glob on capture names, for example 'route-*'")
    parser.add_argument("--scale", type=float, default=0.5)
    parser.add_argument("--columns", type=int, default=0,
                        help="cells per row (default: by orientation)")
    args = parser.parse_args()
    if len(args.directories) > 2:
        parser.error("give one directory, or a before and an after directory")

    stems: set[str] = set()
    for directory in args.directories:
        stems |= {p.stem for p in directory.glob("*.ppm")
                  if fnmatch.fnmatch(p.stem, args.match)}
    groups: dict[str, list[str]] = defaultdict(list)
    for stem in sorted(stems):
        groups[group_of(stem)].append(stem)

    args.output.mkdir(parents=True, exist_ok=True)
    font = label_font()
    changed_total = 0
    for group, members in sorted(groups.items()):
        cells = []
        for stem in members:
            images = [load(d / f"{stem}.ppm") for d in args.directories]
            name = stem[len(group) + 1:] if stem != group else stem
            c = cell(name or stem, images, args.scale, font)
            cells.append(c)
            if len(images) == 2 and None not in images and \
               ImageChops.difference(images[0], images[1]).getbbox() is not None:
                changed_total += 1
        landscape = "landscape" in group
        columns = args.columns or (4 if len(args.directories) == 2 else 8) - (
            1 if landscape else 0)
        heading = group if len(args.directories) == 1 else \
            f"{group}   (left: {args.directories[0].name}, right: {args.directories[1].name})"
        target = args.output / f"{group}.png"
        sheet(cells, columns, heading, font).save(target, optimize=True)
        print(f"{target}  {len(members)} views")
    if len(args.directories) == 2:
        print(f"{changed_total} views differ")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
