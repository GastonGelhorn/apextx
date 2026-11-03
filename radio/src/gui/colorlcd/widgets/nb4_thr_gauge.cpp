/* SPDX-License-Identifier: GPL-2.0-only */

#include "nb4_trim_row.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

// Half-circle gauge: throttle sweeps right from 12 o'clock, brake left, with
// a pointer on the scale and the value in the middle.
class Nb4ThrGaugeWidget : public Nb4TrimWidget
{
 public:
  Nb4ThrGaugeWidget(const WidgetFactory* factory, Window* parent,
                    const rect_t& rect, int screenNum, int zoneNum) :
      Nb4TrimWidget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  void delayedInit() override
  {
    Nb4Ui::card(lvobj);
    // Everything below lays out in the height the trim row leaves.
    const coord_t w = width(), h = height() - trim.build(this, ADC_MAIN_TH, Nb4Ui::throttleColor());
    gasInk = Nb4Ui::throttleColor();
    brakeInk = Nb4Ui::brakeColor();
    const coord_t T = titleRow(STR_NB4_THR_BRAKE, textColor());

    cx = w / 2;
    cy = h - PAD - 6;
    r = (w - PAD * 2) / 2;
    if (r > cy - T - 4) r = cy - T - 4;
    if (r < 24) r = 24;
    arcW = r / 7 < 5 ? 5 : r / 7;
    auto scale = ring(lvobj, cx, cy, r, arcW, trackColor());
    lv_arc_set_bg_angles(scale, 180, 0);
    fill = ring(lvobj, cx, cy, r, arcW, gasInk);
    lv_arc_set_bg_angles(fill, 270, 270);
    // Ticks every 50 % on both sides.
    for (unsigned i = 0; i < 5; ++i) {
      auto& p = tickPts[i];
      p[0] = polar(cx, cy, 180 + i * 45, r + 2);
      p[1] = polar(cx, cy, 180 + i * 45, r + 2 - arcW - 6);
      lv_line_set_points(stroke(lvobj, 2, muted()), p, 2);
    }
    pointer = stroke(lvobj, 4, textColor());

    const LcdFlags f = fontForChars(r * 6 / 5, 4, r / 2 + 8);
    const coord_t fh = fontHeight(f);
    pct = new DynamicNumber<int16_t>(
        this, {(coord_t)(cx - r * 3 / 5), (coord_t)(cy - fh), (coord_t)(r * 6 / 5), fh},
        []() { return (int16_t)abs(channelPct(g_model.nb4Racing.throttleChannel)); },
        COLOR_THEME_PRIMARY1_INDEX, f | CENTERED);
    last = INT16_MIN;
    update();
    checkEvents();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();
    trim.update();
    const int16_t v = channelPct(g_model.nb4Racing.throttleChannel);
    if (v == last) return;
    last = v;
    const int sweep = v * 90 / 100;
    if (v >= 0) lv_arc_set_bg_angles(fill, 270, (270 + sweep) % 360);
    else lv_arc_set_bg_angles(fill, 270 + sweep, 270);
    const lv_color_t c = v < 0 ? brakeInk : gasInk;
    lv_obj_set_style_arc_color(fill, c, LV_PART_MAIN);
    // The pointer stays on the scale, clear of the value in the middle.
    pointerPts[0] = polar(cx, cy, 270 + sweep, r * 11 / 20);
    pointerPts[1] = polar(cx, cy, 270 + sweep, r - arcW - 3);
    lv_line_set_points(pointer, pointerPts, 2);
    if (pct) ink(pct, c);
  }

 protected:
  lv_obj_t* fill = nullptr;
  lv_obj_t* pointer = nullptr;
  lv_point_t tickPts[5][2] = {};
  lv_point_t pointerPts[2] = {};
  StaticText* pct = nullptr;
  lv_color_t gasInk{}, brakeInk{};
  coord_t cx = 0, cy = 0, r = 0, arcW = 0;
  int16_t last = INT16_MIN;
};

Nb4WidgetFactory<Nb4ThrGaugeWidget> nb4ThrGaugeWidget(
    "NB4ThrGauge", trimOptions, NB4_STR(WIDGET_THR_GAUGE));

#endif
