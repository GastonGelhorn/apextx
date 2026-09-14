/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "nb4_widget_kit.h"

#if defined(RADIO_NB4_FAMILY)

// Half-circle gauge with a needle, growing symmetrically from the top centre.
// Derived widgets pick the channel, the colours and the labels.
class Nb4DialWidget : public Widget
{
 public:
  Nb4DialWidget(const WidgetFactory* factory, Window* parent,
                const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  void delayedInit() override
  {
    using namespace nb4w;
    Nb4Ui::card(lvobj);
    const coord_t w = width(), h = height();
    const bool compact = compactZone(h);

    coord_t r;
    if (compact) {
      r = h - 44;
      if (r > (w / 2 - PAD)) r = w / 2 - PAD;
      cx = PAD + r;
      cy = 24 + r;
    } else {
      r = (w - PAD * 2) / 2;
      if (r > h - 78) r = h - 78;
      cx = w / 2;
      cy = 30 + r;
    }
    radius = r;
    needleLen = r - r / 4;

    const bool trimTopRight = w >= 220;
    auto title = nb4Label(this, {PAD, 6, (coord_t)(trimTopRight ? w - PAD * 2 - 78 : w - PAD * 2), 20},
                          titleText(), FONT(BOLD));
    ink(title, accent(0));
    if (trimTopRight)
      trim = new DynamicText(this, {(coord_t)(w - PAD - 74), 5, 74, 18},
                             [this]() { return trimText(); },
                             COLOR_THEME_PRIMARY3_INDEX, FONT(XS) | RIGHT);

    const coord_t trackW = r < 50 ? 6 : r / 7;
    arc = lv_arc_create(lvobj);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(arc, cx - r, cy - r);
    lv_obj_set_size(arc, 2 * r, 2 * r);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    lv_obj_set_style_pad_all(arc, 0, 0);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, trackW, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, trackColor(), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, trackW, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, accent(0), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 180, 360);
    lv_arc_set_mode(arc, LV_ARC_MODE_SYMMETRICAL);
    lv_arc_set_range(arc, -100, 100);
    lv_arc_set_value(arc, 0);

    needle = lv_line_create(lvobj);
    lv_obj_clear_flag(needle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(needle, 0, 0);
    lv_obj_set_style_line_width(needle, r < 50 ? 2 : 3, 0);
    lv_obj_set_style_line_rounded(needle, true, 0);
    lv_obj_set_style_line_color(needle, accent(0), 0);
    rectangle(lvobj, cx - 5, cy - 5, 10, 10, accent(0), LV_RADIUS_CIRCLE);

    LcdFlags f;
    rect_t pr;
    if (compact) {
      const coord_t px = cx + r + PAD;
      f = fontForChars(w - px - PAD, 5, h - 40);
      pr = {px, (coord_t)((h - fontHeight(f)) / 2 + 8), (coord_t)(w - px - PAD), fontHeight(f)};
    } else {
      f = fontForChars(w - PAD * 2, 5, h - cy - 24);
      pr = {0, (coord_t)(cy + 4), w, fontHeight(f)};
    }
    pct = new DynamicNumber<int16_t>(this, pr, [this]() { return channelPct(channel()); },
                                     COLOR_THEME_PRIMARY1_INDEX, f | CENTERED, nullptr, "%");
    ink(pct, accent(0));
    if (!trimTopRight)
      trim = new DynamicText(this, {0, (coord_t)(pr.y + pr.h + 2), w, 18},
                             [this]() { return trimText(); },
                             COLOR_THEME_PRIMARY3_INDEX, FONT(XS) | CENTERED);

    tiny(this, {(coord_t)(cx - r - 2), (coord_t)(cy + 2), 44, 16}, "-100");
    tiny(this, {(coord_t)(cx + r - 42), (coord_t)(cy + 2), 44, 16}, "+100", RIGHT);

    lastPct = INT16_MIN;
    update();
    checkEvents();
  }

  void checkEvents() override
  {
    using namespace nb4w;
    if (!loaded) return;
    Widget::checkEvents();
    const int16_t v = channelPct(channel());
    if (v == lastPct) return;
    lastPct = v;
    lv_arc_set_value(arc, v);
    const double a = (270.0 + v * 0.9) * M_PI / 180.0;
    pts[0] = {cx, cy};
    pts[1] = {(lv_coord_t)(cx + cos(a) * needleLen), (lv_coord_t)(cy + sin(a) * needleLen)};
    lv_line_set_points(needle, pts, 2);
    const lv_color_t c = accent(v);
    lv_obj_set_style_arc_color(arc, c, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(needle, c, 0);
    if (pct) ink(pct, c);
  }

 protected:
  virtual uint8_t channel() const = 0;
  virtual uint8_t trimIndex() const = 0;
  virtual const char* titleText() const = 0;
  virtual lv_color_t accent(int16_t pct) const = 0;

  std::string trimText() const
  {
    using namespace nb4w;
    if (!trimOk(trimIndex())) return "";
    return std::string("TRIM ") + signedText(trimValue(trimIndex()));
  }

  lv_obj_t* arc = nullptr;
  lv_obj_t* needle = nullptr;
  lv_point_t pts[2] = {};
  StaticText* pct = nullptr;
  StaticText* trim = nullptr;
  coord_t cx = 0, cy = 0, radius = 0, needleLen = 0;
  int16_t lastPct = INT16_MIN;
};

#endif
