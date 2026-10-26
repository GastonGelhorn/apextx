/* SPDX-License-Identifier: GPL-2.0-only */

#include "nb4_trim_row.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

// Full ring around the percentage: throttle fills clockwise from 12 o'clock,
// brake anticlockwise.
class Nb4ThrRingWidget : public Nb4TrimWidget
{
 public:
  Nb4ThrRingWidget(const WidgetFactory* factory, Window* parent,
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

    coord_t r = (w - PAD * 2) / 2;
    if (r > (h - T - 30) / 2) r = (h - T - 30) / 2;
    if (r < 20) r = 20;
    const coord_t cx = w / 2, cy = T + 2 + r;
    const coord_t width = r / 5 < 6 ? 6 : r / 5;
    ring(lvobj, cx, cy, r, width, trackColor());
    fill = ring(lvobj, cx, cy, r, width, gasInk);
    lv_arc_set_bg_angles(fill, 270, 270);

    const LcdFlags f = fontForChars(r * 2 - width * 2 - 8, 4, r);
    pct = new DynamicNumber<int16_t>(
        this, {(coord_t)(cx - r), (coord_t)(cy - fontHeight(f) / 2), (coord_t)(2 * r), fontHeight(f)},
        []() { return (int16_t)abs(channelPct(g_model.nb4Racing.throttleChannel)); },
        COLOR_THEME_PRIMARY1_INDEX, f | CENTERED);
    word = caption(this, {0, (coord_t)(cy + r + 4), w, 16}, STR_NB4_THR, CENTERED);
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
    const int sweep = abs(v) * 342 / 100;  // leave a small gap at full scale
    if (sweep == 0) lv_arc_set_bg_angles(fill, 270, 270);
    else if (v > 0) lv_arc_set_bg_angles(fill, 270, (270 + sweep) % 360);
    else lv_arc_set_bg_angles(fill, (270 - sweep + 360) % 360, 270);
    const lv_color_t c = v < 0 ? brakeInk : gasInk;
    lv_obj_set_style_arc_color(fill, c, LV_PART_MAIN);
    if (pct) ink(pct, c);
    if (word) {
      word->setText(v < 0 ? STR_NB4_BRAKE_B80D : STR_NB4_THR);
      ink(word, c);
    }
  }

 protected:
  lv_obj_t* fill = nullptr;
  StaticText* pct = nullptr;
  StaticText* word = nullptr;
  lv_color_t gasInk{}, brakeInk{};
  int16_t last = INT16_MIN;
};

Nb4WidgetFactory<Nb4ThrRingWidget> nb4ThrRingWidget(
    "NB4ThrRing", trimOptions, NB4_STR(WIDGET_THR_RING));

#endif
