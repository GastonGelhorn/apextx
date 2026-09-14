/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

// Shared building blocks for the NB4 car widgets. Header-only so the widget
// glob in CMake picks up each widget without further wiring.

#include "edgetx.h"
#include "widget.h"
#include "nb4_ui.h"
#include "nb4_car_state.h"
#include "nb4_home_templates.h"
#include "telemetry/telemetry.h"
#include "hal/adc_driver.h"

#include <math.h>
#include <stdio.h>

namespace nb4w {

constexpr coord_t PAD = 8;

// The wide landscape zone is about 125 px tall; anything under 150 lays out
// side by side instead of stacked.
inline bool compactZone(coord_t h) { return h < 150; }

inline LcdFlags fontForHeight(coord_t h)
{
  if (h >= 66) return FONT(XXL);
  if (h >= 52) return FONT(LXL);
  if (h >= 42) return FONT(XL);
  if (h >= 31) return FONT(L);
  return FONT(STD);
}

// Six characters of time or a signed percentage have to fit the width.
inline LcdFlags fontForWidth(coord_t w)
{
  if (w >= 420) return FONT(XXL);
  if (w >= 300) return FONT(LXL);
  if (w >= 230) return FONT(XL);
  if (w >= 130) return FONT(L);
  return FONT(STD);
}

// Largest font whose n characters fit in w pixels (Barlow digits run about
// 0.58 of the line height), capped by the height available.
inline LcdFlags fontForChars(coord_t w, unsigned n, coord_t maxH = 67)
{
  static const LcdFlags order[] = {FONT(XXL), FONT(LXL), FONT(XL), FONT(L), FONT(STD)};
  static const coord_t heights[] = {67, 53, 43, 32, 21};
  for (unsigned i = 0; i < 5; ++i)
    if (heights[i] <= maxH && (coord_t)(n * heights[i] * 58 / 100) <= w) return order[i];
  return FONT(STD);
}

inline coord_t fontHeight(LcdFlags f)
{
  switch (FONT_INDEX(f)) {
    case FONT_XXL_INDEX: return 67;
    case FONT_LXL_INDEX: return 53;
    case FONT_XL_INDEX: return 43;
    case FONT_L_INDEX: return 32;
    case FONT_XS_INDEX: return 18;
    case FONT_XXS_INDEX: return 17;
    default: return 21;
  }
}

inline lv_color_t green() { return Nb4Ui::color(COLOR_THEME_EDIT_INDEX); }
inline lv_color_t orange() { return Nb4Ui::color(COLOR_THEME_FOCUS_INDEX); }
inline lv_color_t red() { return Nb4Ui::color(COLOR_THEME_WARNING_INDEX); }
inline lv_color_t muted() { return Nb4Ui::color(COLOR_THEME_PRIMARY3_INDEX); }
inline lv_color_t textColor() { return Nb4Ui::color(COLOR_THEME_PRIMARY1_INDEX); }
inline lv_color_t trackColor() { return Nb4Ui::color(COLOR_THEME_SECONDARY2_INDEX); }

inline void ink(Window* w, lv_color_t c)
{
  lv_obj_set_style_text_color(w->getLvObj(), c, 0);
}

inline StaticText* caption(Window* p, rect_t r, const char* text,
                           LcdFlags extra = 0)
{
  return new StaticText(p, r, text, COLOR_THEME_PRIMARY3_INDEX, FONT(XS) | extra);
}

inline StaticText* tiny(Window* p, rect_t r, const char* text, LcdFlags extra = 0)
{
  return new StaticText(p, r, text, COLOR_THEME_PRIMARY3_INDEX, FONT(XXS) | extra);
}

inline lv_obj_t* rectangle(lv_obj_t* parent, coord_t x, coord_t y, coord_t w,
                           coord_t h, lv_color_t color, coord_t radius = 0)
{
  auto o = lv_obj_create(parent);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w < 0 ? 0 : w, h < 0 ? 0 : h);
  lv_obj_set_style_bg_color(o, color, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_radius(o, radius, 0);
  return o;
}

inline lv_obj_t* track(lv_obj_t* parent, coord_t x, coord_t y, coord_t w,
                       coord_t h, coord_t radius = 0)
{
  return rectangle(parent, x, y, w, h, trackColor(), radius);
}

inline void place(lv_obj_t* o, coord_t x, coord_t y, coord_t w, coord_t h)
{
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w < 0 ? 0 : w, h < 0 ? 0 : h);
}

inline void recolor(lv_obj_t* o, lv_color_t c)
{
  lv_obj_set_style_bg_color(o, c, 0);
}

inline void show(lv_obj_t* o, bool visible)
{
  if (visible) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

inline StaticText* pill(Window* p, rect_t r, const char* text, lv_color_t color)
{
  auto t = new StaticText(p, r, text, COLOR_THEME_PRIMARY1_INDEX,
                          FONT(XXS) | CENTERED);
  Nb4Ui::chip(t->getLvObj());
  ink(t, color);
  lv_obj_set_style_border_color(t->getLvObj(), color, 0);
  return t;
}

inline void recolorPill(StaticText* t, lv_color_t color)
{
  ink(t, color);
  lv_obj_set_style_border_color(t->getLvObj(), color, 0);
}

inline int16_t channelPct(uint8_t ch)
{
  if (ch >= MAX_OUTPUT_CHANNELS) return 0;
  int32_t v = channelOutputs[ch];
  if (v > RESX) v = RESX;
  if (v < -RESX) v = -RESX;
  return (int16_t)divRoundClosest(v * 100, RESX);
}

inline bool trimOk(uint8_t idx)
{
  const auto raw = getRawTrimValue(0, idx);
  return raw.mode != TRIM_MODE_NONE && raw.mode != TRIM_MODE_3POS;
}

inline int trimPct(uint8_t idx)
{
  return limit<int>(-100, divRoundClosest(getTrimValue(0, idx) * 100, TRIM_MAX), 100);
}

// The home screen shows the raw trim step count, so the widgets do too.
inline int trimValue(uint8_t idx) { return getTrimValue(0, idx); }

inline std::string signedText(int v)
{
  char b[12];
  if (v == 0) return "0";
  snprintf(b, sizeof(b), "%+d", v);
  return b;
}

inline std::string voltsText(uint32_t mv, bool available)
{
  if (!available) return "--";
  char b[16];
  snprintf(b, sizeof(b), "%lu.%lu V", (unsigned long)(mv / 1000),
           (unsigned long)((mv % 1000) / 100));
  return b;
}

inline std::string tenthsText(int tenths)
{
  char b[16];
  snprintf(b, sizeof(b), "%d.%d V", tenths / 10, abs(tenths) % 10);
  return b;
}

// Signed centiseconds as "+0.25" / "-1.03".
inline std::string deltaText(int32_t cs)
{
  char b[16];
  const uint32_t a = (uint32_t)abs(cs);
  snprintf(b, sizeof(b), "%c%lu.%02lu", cs < 0 ? '-' : '+',
           (unsigned long)(a / 100), (unsigned long)(a % 100));
  return b;
}

// Symmetric horizontal bar: fill grows from the centre towards the value.
struct CentreBar {
  lv_obj_t* box = nullptr;
  lv_obj_t* fill = nullptr;
  coord_t w = 0, h = 0;
  int16_t last = INT16_MIN;

  void build(lv_obj_t* parent, coord_t x, coord_t y, coord_t width,
             coord_t height, lv_color_t color)
  {
    w = width;
    h = height;
    box = track(parent, x, y, w, h, 3);
    fill = rectangle(box, w / 2, 0, 0, h, color, 2);
    rectangle(box, w / 2 - 1, 0, 2, h, muted());
  }

  void set(int16_t pct)
  {
    if (!box || pct == last) return;
    last = pct;
    const coord_t half = w / 2;
    const coord_t len = (coord_t)((abs((int32_t)pct) * half) / 100);
    place(fill, pct >= 0 ? half : half - len, 0, len, h);
  }
};

// Bar that fills from the left between a minimum and a maximum, with an
// optional threshold marker.
struct RangeBar {
  lv_obj_t* box = nullptr;
  lv_obj_t* fill = nullptr;
  lv_obj_t* marker = nullptr;
  coord_t w = 0, h = 0;
  int last = INT32_MIN;

  void build(lv_obj_t* parent, coord_t x, coord_t y, coord_t width,
             coord_t height, lv_color_t color)
  {
    w = width;
    h = height;
    box = track(parent, x, y, w, h, 3);
    fill = rectangle(box, 0, 0, 0, h, color, 3);
    marker = rectangle(box, 0, -2, 2, h + 4, red());
    show(marker, false);
  }

  void threshold(int value, int lo, int hi)
  {
    if (!marker || hi <= lo) return;
    const coord_t x = (coord_t)divRoundClosest((value - lo) * (int32_t)w, hi - lo);
    show(marker, x > 0 && x < w);
    lv_obj_set_x(marker, x - 1);
  }

  void set(int value, int lo, int hi)
  {
    if (!box || value == last) return;
    last = value;
    coord_t len = 0;
    if (hi > lo) len = (coord_t)divRoundClosest(limit<int>(0, value - lo, hi - lo) * (int32_t)w, hi - lo);
    place(fill, 0, 0, len, h);
  }
};

}  // namespace nb4w
