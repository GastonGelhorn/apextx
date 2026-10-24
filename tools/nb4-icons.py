#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Draw the NB4 route glyphs as 30 x 30 masks.

Every glyph is a line drawing with the same stroke, drawn at 8x and scaled
down, so the set has one optical weight and size (docs/nb4/DESIGN.md). Masks
follow the firmware's convention: white background, black ink.

    tools/nb4-icons.py            # write radio/src/bitmaps/480x272/default_theme
    tools/nb4-icons.py --sheet x.png
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "radio/src/bitmaps/480x272/default_theme"
SIZE = 30
SCALE = 8
STROKE = 2.4  # in 30 px units


class Pen:
    def __init__(self) -> None:
        self.image = Image.new("L", (SIZE * SCALE, SIZE * SCALE), 0)
        self.draw = ImageDraw.Draw(self.image)

    @staticmethod
    def p(x: float, y: float) -> tuple[float, float]:
        return (x * SCALE, y * SCALE)

    def line(self, *points: tuple[float, float], w: float = STROKE) -> None:
        pts = [self.p(*q) for q in points]
        self.draw.line(pts, fill=255, width=round(w * SCALE), joint="curve")
        for q in (pts[0], pts[-1]):
            r = w * SCALE / 2
            self.draw.ellipse([q[0] - r, q[1] - r, q[0] + r, q[1] + r], fill=255)

    def circle(self, cx: float, cy: float, r: float, w: float = STROKE,
               fill: bool = False) -> None:
        # The stroke is centred on the radius; Pillow draws it inwards.
        if fill:
            self.draw.ellipse([self.p(cx - r, cy - r), self.p(cx + r, cy + r)], fill=255)
            return
        o = r + w / 2
        self.draw.ellipse([self.p(cx - o, cy - o), self.p(cx + o, cy + o)], outline=255,
                          width=round(w * SCALE))

    def arc(self, cx: float, cy: float, r: float, start: float, end: float,
            w: float = STROKE) -> None:
        o = r + w / 2
        self.draw.arc([self.p(cx - o, cy - o), self.p(cx + o, cy + o)], start, end,
                      fill=255, width=round(w * SCALE))
        for a in (start, end):
            self.dot(cx + r * math.cos(math.radians(a)),
                     cy + r * math.sin(math.radians(a)), w / 2)

    def rect(self, x0: float, y0: float, x1: float, y1: float, r: float = 2,
             w: float = STROKE, fill: bool = False) -> None:
        box = [self.p(x0, y0), self.p(x1, y1)]
        if fill:
            self.draw.rounded_rectangle(box, radius=r * SCALE, fill=255)
        else:
            self.draw.rounded_rectangle(box, radius=r * SCALE, outline=255,
                                        width=round(w * SCALE))

    def poly(self, *points: tuple[float, float], fill: bool = True,
             w: float = STROKE) -> None:
        pts = [self.p(*q) for q in points]
        if fill:
            self.draw.polygon(pts, fill=255)
        else:
            self.line(*points, points[0], w=w)

    def dot(self, x: float, y: float, r: float) -> None:
        if r > 0:
            self.circle(x, y, r, fill=True)

    def erase(self, x0: float, y0: float, x1: float, y1: float) -> None:
        self.draw.rectangle([self.p(x0, y0), self.p(x1, y1)], fill=0)

    def mask(self) -> Image.Image:
        small = self.image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
        return small.point(lambda v: 255 - v)


def rays(pen: Pen, cx: float, cy: float, r0: float, r1: float, n: int,
         offset: float = 0) -> None:
    for i in range(n):
        a = math.radians(offset + i * 360 / n)
        pen.line((cx + r0 * math.cos(a), cy + r0 * math.sin(a)),
                 (cx + r1 * math.cos(a), cy + r1 * math.sin(a)))


def brightness(pen: Pen) -> None:
    pen.circle(15, 15, 5.5)
    rays(pen, 15, 15, 9.5, 12.5, 8)


def lights(pen: Pen) -> None:
    # A bulb with its glow.
    pen.arc(15, 12, 7, 140, 400)
    pen.line((10.5, 17.5), (11.5, 21), (18.5, 21), (19.5, 17.5))
    pen.line((12, 24.5), (18, 24.5))
    pen.line((13, 27.5), (17, 27.5))
    pen.line((15, 9), (15, 13))


def throttle(pen: Pen) -> None:
    # An accelerator pedal, tilted on its hinge, with a ribbed pad.
    import math as m
    c, sn = m.cos(m.radians(-22)), m.sin(m.radians(-22))

    def t(x: float, y: float) -> tuple[float, float]:
        x, y = x - 15, y - 14
        return (15 + x * c - y * sn, 14 + x * sn + y * c)

    pen.poly(t(9, 3), t(21, 3), t(21, 22), t(9, 22), fill=False)
    for y in (8, 12.5, 17):
        pen.line(t(12, y), t(18, y))
    pen.line(t(15, 22), (17, 27))
    pen.line((8, 27), (24, 27))


def usb(pen: Pen) -> None:
    pen.line((15, 4), (15, 25))
    pen.poly((15, 2), (11.5, 7), (18.5, 7))
    pen.line((15, 18), (8, 13), (8, 10))
    pen.rect(6, 7, 10, 11, r=0.5, fill=True)
    pen.line((15, 15), (22, 11), (22, 9))
    pen.circle(22, 8, 2, fill=True)
    pen.circle(15, 25.5, 3, fill=True)


def keys(pen: Pen) -> None:
    pen.rect(3, 7, 27, 23, r=3)
    for x in (8, 13, 17, 22):
        pen.dot(x, 12, 1.4)
    for x in (8, 22):
        pen.dot(x, 17, 1.4)
    pen.line((12, 17.5), (18, 17.5))


def toggle(pen: Pen) -> None:
    pen.rect(3, 9, 27, 21, r=6)
    pen.circle(20.5, 15, 3.6, fill=True)
    pen.line((8, 15), (12, 15))


def filter_(pen: Pen) -> None:
    pen.line((4, 6), (26, 6), (17.5, 16), (17.5, 24), (12.5, 27), (12.5, 16), (4, 6))


def history(pen: Pen) -> None:
    for y in (8, 15, 22):
        pen.dot(6, y, 1.8)
        pen.line((11, y), (25, y))


def power(pen: Pen) -> None:
    pen.arc(15, 16, 10, 300, 600)
    pen.line((15, 3), (15, 14))


def bell(pen: Pen) -> None:
    pen.line((6, 22), (8, 19), (8, 13))
    pen.arc(15, 13, 7, 180, 360)
    pen.line((22, 13), (22, 19), (24, 22), (6, 22))
    pen.line((12.5, 25.5), (17.5, 25.5))
    pen.line((15, 4), (15, 6))


def voice(pen: Pen) -> None:
    pen.rect(4, 5, 26, 20, r=4)
    pen.line((9, 20), (8, 26), (15, 20))
    for x in (10, 15, 20):
        pen.dot(x, 12.5, 1.6)


def alert(pen: Pen) -> None:
    pen.poly((15, 3.5), (27.5, 25.5), (2.5, 25.5), fill=False)
    pen.line((15, 11), (15, 18))
    pen.dot(15, 21.8, 1.5)


def flag(pen: Pen) -> None:
    pen.line((6, 3), (6, 28))
    pen.rect(6, 4, 25, 17, r=0.5)
    for i in range(3):
        for j in range(2):
            if (i + j) % 2 == 0:
                pen.rect(7 + i * 6, 5 + j * 6, 12 + i * 6, 10 + j * 6, r=0, fill=True)


def stopwatch(pen: Pen) -> None:
    pen.circle(15, 17, 10)
    pen.line((15, 17), (15, 11))
    pen.line((15, 17), (19, 19))
    pen.line((12, 3), (18, 3))
    pen.line((15, 3), (15, 7))
    pen.line((24, 7), (26, 5))


def globe(pen: Pen) -> None:
    pen.circle(15, 15, 11.5)
    pen.line((3.5, 15), (26.5, 15))
    box_r = 5.2
    pen.draw.ellipse([pen.p(15 - box_r - STROKE / 2, 3.5 - STROKE / 2),
                      pen.p(15 + box_r + STROKE / 2, 26.5 + STROKE / 2)],
                     outline=255, width=round(STROKE * SCALE))
    pen.line((6, 8.5), (24, 8.5))
    pen.line((6, 21.5), (24, 21.5))


def backup(pen: Pen) -> None:
    # An archive box: lid, body and its label slot.
    pen.rect(3, 5, 27, 11, r=1.5)
    pen.rect(5, 11, 25, 26, r=1.5)
    pen.line((12, 16), (18, 16))


def template(pen: Pen) -> None:
    pen.rect(9, 3, 26, 21, r=2)
    pen.line((4, 9), (4, 27), (20, 27))
    pen.line((13, 9), (22, 9))
    pen.line((13, 14), (22, 14))


def channels(pen: Pen) -> None:
    # A servo connector: three pins over a housing.
    pen.rect(4, 15, 26, 25, r=2)
    for x in (9, 15, 21):
        pen.line((x, 4), (x, 15))
        pen.dot(x, 20, 1.6)


def button(pen: Pen) -> None:
    pen.circle(15, 15, 11)
    pen.circle(15, 15, 5, fill=True)


def update(pen: Pen) -> None:
    pen.line((15, 3), (15, 18))
    pen.line((9, 12), (15, 18), (21, 12))
    pen.line((4, 18), (4, 25), (26, 25), (26, 18))


def restart(pen: Pen) -> None:
    pen.arc(15, 15, 10, 200, 520)
    pen.poly((2.5, 9), (11, 8), (5, 15.5))


def speaker(pen: Pen) -> None:
    pen.poly((4, 11), (9, 11), (16, 5), (16, 25), (9, 19), (4, 19), fill=True)
    pen.arc(16, 15, 6, 300, 420)
    pen.arc(16, 15, 11, 300, 420)


def vibrate(pen: Pen) -> None:
    pen.rect(10, 4, 20, 26, r=2.5)
    pen.line((5, 10), (3, 12.5), (5, 15), (3, 17.5), (5, 20))
    pen.line((25, 10), (27, 12.5), (25, 15), (27, 17.5), (25, 20))


def bluetooth(pen: Pen) -> None:
    pen.line((8, 9), (21, 20), (15, 26), (15, 4), (21, 10), (8, 21))


def help_(pen: Pen) -> None:
    pen.circle(15, 15, 12)
    pen.arc(15, 11.5, 4.2, 180, 405)
    pen.line((17.8, 14.8), (15, 17), (15, 18.5))
    pen.dot(15, 22.5, 1.6)


def screens(pen: Pen) -> None:
    pen.rect(3, 5, 27, 25, r=2.5)
    pen.line((3, 11), (27, 11))
    pen.line((15, 11), (15, 25))
    pen.line((15, 18), (27, 18))


GLYPHS = {
    "brightness": brightness, "lights": lights, "throttle": throttle,
    "usb": usb, "keys": keys, "toggle": toggle, "filter": filter_,
    "history": history, "power": power, "bell": bell, "voice": voice,
    "alert": alert, "flag": flag, "stopwatch": stopwatch, "globe": globe,
    "backup": backup, "template": template, "channels": channels,
    "button": button, "update": update, "restart": restart,
    "speaker": speaker, "vibrate": vibrate, "bluetooth": bluetooth,
    "help": help_, "screens": screens,
}


def render(name: str) -> Image.Image:
    pen = Pen()
    GLYPHS[name](pen)
    return pen.mask()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--sheet", type=Path, help="also write a preview sheet")
    args = parser.parse_args()
    masks = {name: render(name) for name in GLYPHS}
    for name, mask in masks.items():
        mask.save(OUT / f"mask_nb4_{name}.png", optimize=True)
    if args.sheet:
        cell = SIZE * 3 + 8
        sheet = Image.new("L", (cell * 9, cell * ((len(masks) + 8) // 9)), 200)
        for i, mask in enumerate(masks.values()):
            sheet.paste(mask.resize((SIZE * 3, SIZE * 3), Image.Resampling.NEAREST),
                        ((i % 9) * cell + 4, (i // 9) * cell + 4))
        sheet.save(args.sheet)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
