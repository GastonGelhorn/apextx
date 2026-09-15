/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_fault_screen.h"

#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)

#include <lvgl/lvgl.h>

#include <stdio.h>
#include <string.h>

#include "edgetx.h"

#include "colors.h"
#include "fonts.h"
#include "lcd.h"

namespace {

// Fixed, not read from the theme. The theme table is ordinary memory that a
// fault may well have been caused by, and a fault screen that depends on the
// state it is reporting on is no use.
constexpr uint16_t Ground = RGB(176, 0, 0);
constexpr uint16_t Ink = RGB(255, 255, 255);
constexpr uint16_t Muted = RGB(255, 200, 200);

// The painter's state. Written and read only inside one nb4FaultScreenShow
// call, on whichever task called it.
uint16_t* canvas = nullptr;
unsigned panelW = 0, panelH = 0;
unsigned pageW = 0, pageH = 0;
bool rotated = false;
uint32_t faultCode = 0;

void put(int x, int y, uint16_t colour)
{
  if (x < 0 || y < 0 || (unsigned)x >= pageW || (unsigned)y >= pageH) return;
  // The panel always scans portrait. This is the mapping LVGL's ninety degree
  // rotation produces, so a landscape page reads the right way up.
  const unsigned col = rotated ? (unsigned)y : (unsigned)x;
  const unsigned row = rotated ? panelH - 1 - (unsigned)x : (unsigned)y;
  canvas[row * panelW + col] = colour;
}

uint16_t blend(uint16_t fg, uint16_t bg, unsigned alpha)
{
  if (alpha >= 250) return fg;
  if (alpha <= 5) return bg;
  const unsigned inverse = 255 - alpha;
  const unsigned r = (((fg >> 11) & 0x1f) * alpha + ((bg >> 11) & 0x1f) * inverse) / 255;
  const unsigned g = (((fg >> 5) & 0x3f) * alpha + ((bg >> 5) & 0x3f) * inverse) / 255;
  const unsigned b = ((fg & 0x1f) * alpha + (bg & 0x1f) * inverse) / 255;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// The fonts are decompressed once at startup and stay resident, and a plain
// format glyph bitmap is a pointer straight into that buffer, so reading one
// allocates nothing. That is what makes real, translated text usable here.
//
// The bits of a glyph run contiguously: rows are not padded to a byte
// boundary, so the only way through the bitmap is a bit cursor.
unsigned glyphAlpha(const uint8_t* bitmap, unsigned bit, unsigned bpp)
{
  const unsigned shift = bit & 7;
  const unsigned value = (bitmap[bit >> 3] >> (8 - bpp - shift)) & ((1u << bpp) - 1);
  switch (bpp) {
    case 8: return value;
    case 4: return value * 17;
    case 2: return value * 85;
    default: return value ? 255 : 0;
  }
}

unsigned drawGlyph(const lv_font_t* font, uint32_t letter, uint32_t next, int x,
                   int y, uint16_t ink)
{
  lv_font_glyph_dsc_t g;
  if (!lv_font_get_glyph_dsc(font, &g, letter, next)) return 0;

  const uint8_t* bitmap = lv_font_get_glyph_bitmap(font, letter);
  if (bitmap && g.box_w && g.box_h) {
    const int top = y + font->line_height - font->base_line - g.box_h - g.ofs_y;
    const int left = x + g.ofs_x;
    const unsigned bpp = g.bpp == 3 ? 4 : g.bpp;  // as LVGL treats it
    unsigned bit = 0;
    for (unsigned row = 0; row < g.box_h; ++row) {
      for (unsigned col = 0; col < g.box_w; ++col, bit += bpp) {
        const unsigned alpha = glyphAlpha(bitmap, bit, bpp);
        if (alpha) put(left + (int)col, top + (int)row, blend(ink, Ground, alpha));
      }
    }
  }
  return g.adv_w;
}

// Width of `length` bytes of UTF-8 starting at `text`.
unsigned runWidth(const lv_font_t* font, const char* text, unsigned length)
{
  unsigned width = 0, i = 0;
  while (i < length) {
    uint32_t at = i;
    const uint32_t letter = _lv_txt_encoded_next(text, &at);
    if (!letter) break;
    const uint32_t peek = at < length ? _lv_txt_encoded_next(text + at, nullptr) : 0;
    width += lv_font_get_glyph_width(font, letter, peek);
    i = at;
  }
  return width;
}

int drawRun(const lv_font_t* font, const char* text, unsigned length, int x,
            int y, uint16_t ink)
{
  unsigned i = 0;
  while (i < length) {
    uint32_t at = i;
    const uint32_t letter = _lv_txt_encoded_next(text, &at);
    if (!letter) break;
    const uint32_t peek = at < length ? _lv_txt_encoded_next(text + at, nullptr) : 0;
    x += (int)drawGlyph(font, letter, peek, x, y, ink);
    i = at;
  }
  return x;
}

// Draws `text` wrapped to `width`, breaking on spaces, and returns the y below
// the last line.
int drawParagraph(const lv_font_t* font, const char* text, int x, int y,
                  unsigned width, uint16_t ink)
{
  if (!text) return y;
  const unsigned total = (unsigned)strlen(text);
  unsigned start = 0;
  while (start < total) {
    unsigned end = total, lastSpace = 0;
    for (unsigned i = start; i <= total; ++i) {
      if (i == total || text[i] == ' ') {
        if (runWidth(font, text + start, i - start) > width && lastSpace) {
          end = lastSpace;
          break;
        }
        lastSpace = i;
        if (i == total) { end = total; break; }
      }
    }
    drawRun(font, text + start, end - start, x, y, ink);
    y += font->line_height;
    start = end;
    while (start < total && text[start] == ' ') ++start;
  }
  return y;
}

// Rows of ground painted per Step. At 320 pixels a row this is a few thousand
// writes, comfortably inside one mixer period with room to spare.
constexpr unsigned GroundRowsPerStep = 32;

unsigned groundRow = 0;
bool pending = false;

void attach()
{
  canvas = lcdSpareCanvas(&panelW, &panelH, &rotated);
  pageW = rotated ? panelH : panelW;
  pageH = rotated ? panelW : panelH;
}

// Fills up to `rows` panel rows and returns whether the ground is complete.
bool fillGround(unsigned rows)
{
  const unsigned end = rows ? groundRow + rows : panelH;
  const unsigned last = end < panelH ? end : panelH;
  for (unsigned row = groundRow; row < last; ++row)
    for (unsigned col = 0; col < panelW; ++col) canvas[row * panelW + col] = Ground;
  groundRow = last;
  return groundRow >= panelH;
}

void drawMessage()
{
  const auto eyebrowFont = getFont(FONT(BOLD));
  const auto titleFont = getFont(FONT(XL));
  const auto bodyFont = getFont(FONT(STD));
  const auto codeFont = getFont(FONT(XXS));

  const int margin = 18;
  const unsigned column = pageW - 2 * margin;
  int y = (int)(pageH / 6);

  drawRun(eyebrowFont, STR_WARNING, (unsigned)strlen(STR_WARNING), margin, y, Muted);
  y += eyebrowFont->line_height + 6;

  y = drawParagraph(titleFont, STR_NB4_FAULT_TITLE, margin, y, column, Ink);
  y += 10;

  // A rule, so the instruction reads as separate from the headline.
  for (unsigned i = 0; i < column; ++i) put(margin + (int)i, y, Muted);
  y += 12;

  drawParagraph(bodyFont, STR_NB4_FAULT_ADVICE, margin, y, column, Ink);

  char code[24];
  snprintf(code, sizeof(code), "FAULT %u", (unsigned)faultCode);
  drawRun(codeFont, code, (unsigned)strlen(code), margin,
          (int)pageH - codeFont->line_height - margin, Muted);
}

}  // namespace

void nb4FaultScreenShow(uint32_t fault)
{
  faultCode = fault;
  attach();
  groundRow = 0;
  fillGround(0);
  drawMessage();
  pending = false;
  lcdPresentSpare();
}

void nb4FaultScreenRequest(uint32_t fault)
{
  faultCode = fault;
  groundRow = 0;
  pending = true;
}

bool nb4FaultScreenStep()
{
  if (!pending) return false;
  attach();
  if (!fillGround(GroundRowsPerStep)) return true;
  drawMessage();
  pending = false;
  lcdPresentSpare();
  return false;
}

#endif
