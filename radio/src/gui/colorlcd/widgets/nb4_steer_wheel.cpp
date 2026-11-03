/* SPDX-License-Identifier: GPL-2.0-only */

#include "nb4_trim_row.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

// Steering wheel that turns with the steering channel (+-100 % = +-135 deg).
class Nb4SteerWheelWidget : public Nb4TrimWidget
{
 public:
  Nb4SteerWheelWidget(const WidgetFactory* factory, Window* parent,
                      const rect_t& rect, int screenNum, int zoneNum) :
      Nb4TrimWidget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  void delayedInit() override
  {
    Nb4Ui::card(lvobj);
    // Everything below lays out in the height the trim row leaves.
    const coord_t w = width(), h = height() - trim.build(this, ADC_MAIN_ST, Nb4Ui::steeringColor());
    const lv_color_t color = Nb4Ui::steeringColor();
    const bool compact = compactZone(h);
    const coord_t T = titleRow(STR_NB4_STEERING, color);

    coord_t r;
    if (compact) {
      r = (h - T - 8) / 2;
      cx = PAD + r + 4;
      cy = T + 2 + r;
    } else {
      r = (w - PAD * 4) / 2;
      if (r > (h - T - 64) / 2) r = (h - T - 64) / 2;
      cx = w / 2;
      cy = T + 2 + r;
    }
    if (r < 16) r = 16;
    rimR = r;
    const coord_t rim = r / 6 < 4 ? 4 : r / 6;
    ring(lvobj, cx, cy, r, rim, trackColor());
    marker = ring(lvobj, cx, cy, r, rim, color);
    for (auto& spoke : spokes) spoke = stroke(lvobj, r < 40 ? 3 : 5, muted());
    hub = r / 4 < 6 ? 6 : r / 4;
    rectangle(lvobj, cx - hub, cy - hub, 2 * hub, 2 * hub, Nb4Ui::color(COLOR_THEME_PRIMARY2_INDEX),
              LV_RADIUS_CIRCLE);
    auto ringHub = rectangle(lvobj, cx - hub, cy - hub, 2 * hub, 2 * hub, color, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(ringHub, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ringHub, 2, 0);
    lv_obj_set_style_border_color(ringHub, color, 0);

    rect_t pr;
    LcdFlags f;
    if (compact) {
      const coord_t px = cx + r + PAD;
      f = fontForChars(w - px - PAD, 5, h - T - 12);
      pr = {px, (coord_t)((h + T - 12 - fontHeight(f)) / 2), (coord_t)(w - px - PAD), fontHeight(f)};
    } else {
      f = fontForChars(w - PAD * 2, 5, h - cy - r - 12);
      pr = {0, (coord_t)(cy + r + 6), w, fontHeight(f)};
    }
    pct = new DynamicNumber<int16_t>(this, pr, []() { return channelPct(g_model.nb4Racing.steeringChannel); },
                                     COLOR_THEME_PRIMARY1_INDEX, f | CENTERED, nullptr, "%");
    last = INT16_MIN;
    update();
    checkEvents();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();
    trim.update();
    const int16_t v = channelPct(g_model.nb4Racing.steeringChannel);
    if (v == last) return;
    last = v;
    const double turn = v * 1.35;
    // Spokes at 6, 10 and 2 o'clock when the wheel is straight.
    static const double base[3] = {90.0, 210.0, 330.0};
    for (unsigned i = 0; i < 3; ++i) {
      pts[i][0] = polar(cx, cy, base[i] + turn, hub);
      pts[i][1] = polar(cx, cy, base[i] + turn, rimR - 2);
      lv_line_set_points(spokes[i], pts[i], 2);
    }
    // The coloured top mark shows where "straight ahead" went.
    const int top = (int)lround(270 + turn);
    lv_arc_set_bg_angles(marker, (top - 14 + 720) % 360, (top + 14 + 720) % 360);
  }

 protected:
  lv_obj_t* marker = nullptr;
  lv_obj_t* spokes[3] = {};
  lv_point_t pts[3][2] = {};
  StaticText* pct = nullptr;
  coord_t cx = 0, cy = 0, rimR = 0, hub = 0;
  int16_t last = INT16_MIN;
};

Nb4WidgetFactory<Nb4SteerWheelWidget> nb4SteerWheelWidget(
    "NB4SteerWheel", trimOptions, NB4_STR(WIDGET_STEER_WHEEL));

#endif
