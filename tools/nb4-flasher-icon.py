#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Render the ApexTX Flasher application icon from the brand logo.

The Dock and taskbar icon is the ApexTX mark on a dark rounded tile, so it
keeps its contrast on light and dark desktops from 16 px up to 1024 px. The
script writes the 1024 px master that ``build-flasher.py`` turns into the
platform icon formats, and the 256 px tile the in-app header shows.
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
LOGO = ROOT / 'docs/branding/apextx-logo.png'
MASTER = ROOT / 'tools/apextx-flasher-icon.png'
HEADER = ROOT / 'tools/apextx-updater-logo.png'

SCALE = 4            # supersampling factor for clean edges after downscale
SIZE = 1024
TILE = 832           # macOS icon grid: the tile leaves a transparent margin
RADIUS = 186         # ~22.4 % corner radius, as the system icons use
MARK_WIDTH = 600     # width of the brand mark inside the tile


def brand_mark():
    """The A-and-signal mark of the logo, without the wordmark, with alpha."""
    source = Image.open(LOGO).convert('RGBA')
    pixels = source.load()
    for y in range(source.height):
        for x in range(source.width):
            r, g, b, _ = pixels[x, y]
            level = max(r, g, b)
            alpha = 0 if level <= 26 else 255 if level >= 80 else int((level - 26) / 54 * 255)
            pixels[x, y] = (r, g, b, alpha)
    mark = source.crop((0, 0, source.width, 700))     # the wordmark starts below
    return mark.crop(mark.getbbox())


def vertical_gradient(size, top, bottom):
    ramp = Image.linear_gradient('L').resize((size, size), Image.Resampling.BILINEAR)
    return Image.composite(Image.new('RGBA', (size, size), bottom),
                           Image.new('RGBA', (size, size), top), ramp)


def render(size=SIZE):
    big = size * SCALE
    tile = TILE * size // SIZE * SCALE
    radius = RADIUS * size // SIZE * SCALE
    offset = (big - tile) // 2

    # Tile: charcoal-to-navy gradient inside a rounded square.
    body = vertical_gradient(tile, (31, 38, 50, 255), (12, 15, 20, 255))
    mask = Image.new('L', (tile, tile), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, tile - 1, tile - 1), radius=radius, fill=255)
    canvas = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    canvas.paste(body, (offset, offset), mask)

    # A faint inner highlight along the top edge separates the tile from a
    # dark Dock without reading as a border on a light one.
    highlight = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    stroke = max(2, 3 * size // SIZE * SCALE)
    ImageDraw.Draw(highlight).rounded_rectangle(
        (offset, offset, offset + tile - 1, offset + tile - 1), radius=radius,
        outline=(255, 255, 255, 34), width=stroke)
    fade = Image.linear_gradient('L').resize((big, big)).point(lambda v: 255 - v)
    highlight.putalpha(Image.composite(highlight.getchannel('A'), Image.new('L', (big, big), 0), fade))
    canvas.alpha_composite(highlight)

    # The mark, slightly above the optical centre, with a soft shadow so the
    # white A keeps its edge when the tile is scaled to 16 px.
    mark = brand_mark()
    width = MARK_WIDTH * size // SIZE * SCALE
    mark = mark.resize((width, mark.height * width // mark.width), Image.Resampling.LANCZOS)
    x = (big - mark.width) // 2
    y = (big - mark.height) // 2 - big // 64
    shadow = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    shadow.paste((0, 0, 0, 110), (x, y + big // 128), mark.getchannel('A'))
    shadow = shadow.filter(ImageFilter.GaussianBlur(big // 160))
    canvas.alpha_composite(shadow)
    canvas.alpha_composite(mark, (x, y))
    return canvas.resize((size, size), Image.Resampling.LANCZOS)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sizes-dir', type=Path, help='Also write a PNG set (16-1024 px) into this directory.')
    args = parser.parse_args()
    master = render()
    master.save(MASTER, optimize=True)
    master.resize((256, 256), Image.Resampling.LANCZOS).save(HEADER, optimize=True)
    if args.sizes_dir:
        args.sizes_dir.mkdir(parents=True, exist_ok=True)
        for size in (16, 32, 64, 128, 256, 512, 1024):
            master.resize((size, size), Image.Resampling.LANCZOS).save(args.sizes_dir / f'apextx-flasher-{size}.png', optimize=True)
    print(f'Wrote {MASTER.relative_to(ROOT)} and {HEADER.relative_to(ROOT)}')


if __name__ == '__main__':
    main()
