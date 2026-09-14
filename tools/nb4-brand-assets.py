#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generate ApexTX documentation and firmware artwork from the approved logo."""

from __future__ import annotations

import base64
from io import BytesIO
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "docs/branding/apextx-logo-source.jpg"
DOC_LOGO = ROOT / "docs/branding/apextx-logo.png"
SPLASH = ROOT / "radio/src/bitmaps/480x272/splash_logo.png"
TOP_LOGO = ROOT / "radio/src/bitmaps/480x272/default_theme/mask_top_logo.png"
APP_ICON = ROOT / "radio/src/bitmaps/480x272/default_theme/mask_edgetx.png"


def foreground(source: Image.Image, crop: tuple[int, int, int, int]) -> Image.Image:
    """Remove the source's nearly uniform dark background without a hard edge."""
    image = source.crop(crop).convert("RGB")
    background = (23, 23, 23)
    output = Image.new("RGBA", image.size)
    pixels = []
    for red, green, blue in image.get_flattened_data():
        distance = max(abs(red - background[0]), abs(green - background[1]),
                       abs(blue - background[2]))
        alpha = max(0, min(255, round((distance - 4) * 255 / 176)))
        if alpha:
            red = max(0, min(255, round(background[0] +
                      (red - background[0]) * 255 / alpha)))
            green = max(0, min(255, round(background[1] +
                        (green - background[1]) * 255 / alpha)))
            blue = max(0, min(255, round(background[2] +
                       (blue - background[2]) * 255 / alpha)))
        pixels.append((red, green, blue, alpha))
    output.putdata(pixels)
    return output


def fit(image: Image.Image, width: int, height: int) -> Image.Image:
    image = image.copy()
    image.thumbnail((width, height), Image.Resampling.LANCZOS)
    return image


def embedded_svg(path: Path, image: Image.Image) -> None:
    buffer = BytesIO()
    image.save(buffer, "PNG", optimize=True)
    encoded = base64.b64encode(buffer.getvalue()).decode("ascii")
    path.write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{image.width}" '
        f'height="{image.height}" viewBox="0 0 {image.width} {image.height}">\n'
        f'  <image width="{image.width}" height="{image.height}" '
        f'href="data:image/png;base64,{encoded}"/>\n'
        '</svg>\n',
        encoding="utf-8",
    )


def main() -> None:
    source = Image.open(SOURCE).convert("RGB")

    doc = source.crop((145, 180, 1535, 1270))
    doc.thumbnail((1200, 960), Image.Resampling.LANCZOS)
    DOC_LOGO.parent.mkdir(parents=True, exist_ok=True)
    doc.save(DOC_LOGO, optimize=True)

    complete = foreground(source, (145, 180, 1535, 1270))
    splash_art = fit(complete, 253, 229)
    splash = Image.new("RGBA", (271, 257), (0, 0, 0, 0))
    splash.alpha_composite(
        splash_art,
        ((splash.width - splash_art.width) // 2,
         (splash.height - splash_art.height) // 2),
    )
    splash.save(SPLASH, optimize=True)

    wordmark = fit(foreground(source, (145, 990, 1535, 1280)), 116, 21)
    mask = Image.new("L", (122, 25), 255)
    alpha = wordmark.getchannel("A")
    black = Image.new("L", wordmark.size, 0)
    mask.paste(black, ((122 - wordmark.width) // 2,
                       (25 - wordmark.height) // 2), alpha)
    mask_rgb = mask.convert("RGB")
    mask_rgb.save(TOP_LOGO, optimize=True)

    symbol = fit(foreground(source, (360, 175, 1295, 955)), 27, 27)
    icon = Image.new("L", (30, 30), 255)
    icon_alpha = symbol.getchannel("A")
    icon.paste(Image.new("L", symbol.size, 0),
               ((30 - symbol.width) // 2, (30 - symbol.height) // 2),
               icon_alpha)
    icon_rgb = icon.convert("RGB")
    icon_rgb.save(APP_ICON, optimize=True)

    embedded_svg(ROOT / "radio/src/bitmaps/img-src/splash_logo.svg", splash)
    embedded_svg(
        ROOT / "radio/src/bitmaps/img-src/default_theme/mask_top_logo.svg",
        mask_rgb,
    )
    embedded_svg(
        ROOT / "radio/src/bitmaps/img-src/default_theme/mask_edgetx.svg",
        icon_rgb,
    )


if __name__ == "__main__":
    main()
