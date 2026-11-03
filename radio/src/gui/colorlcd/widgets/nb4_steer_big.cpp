/* SPDX-License-Identifier: GPL-2.0-only */

#include "nb4_trim_row.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

// Steering as one large number with direction chevrons and a thin scale.
class Nb4SteerBigWidget : public Nb4TrimWidget
{
 public:
  Nb4SteerBigWidget(const WidgetFactory* factory, Window* parent,
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
    color = Nb4Ui::steeringColor();
    const coord_t T = titleRow(STR_NB4_STEERING, color);

    // Chevrons beside the number, or in a row below it when that leaves the
    // number clearly larger (narrow zones).
    const coord_t side = 3 * 12 + PAD;
    LcdFlags f = fontForChars(w - side * 2, 4, h - T - 36);
    const LcdFlags wide = fontForChars(w - PAD * 2, 4, h - T - 36 - 24);
    const bool stacked = fontHeight(wide) > fontHeight(f);
    if (stacked) f = wide;
    const coord_t fh = fontHeight(f);
    const coord_t block = fh + (stacked ? 24 : 0);
    const coord_t top = T + (h - T - 26 - block) / 2;
    const coord_t numX = stacked ? PAD : side;
    new DynamicNumber<int16_t>(this, {numX, top, (coord_t)(w - numX * 2), fh},
                               []() { return (int16_t)abs(channelPct(g_model.nb4Racing.steeringChannel)); },
                               COLOR_THEME_PRIMARY1_INDEX, f | CENTERED);
    const coord_t midY = stacked ? top + fh + 12 : top + fh / 2;
    const coord_t half = stacked ? 7 : (fh / 4 < 8 ? 8 : fh / 4);
    // Inner edges of the two chevron groups.
    const coord_t leftEdge = stacked ? w / 2 - 10 : side - 4;
    const coord_t rightEdge = stacked ? w / 2 + 10 : w - side + 4;
    for (unsigned i = 0; i < 3; ++i) {
      for (unsigned s = 0; s < 2; ++s) {
        // s = 0 points left, s = 1 points right; i = 0 is nearest the number.
        const coord_t x = s ? rightEdge + i * 12 : leftEdge - i * 12;
        const int dir = s ? 1 : -1;
        auto& p = chevronPts[s][i];
        p[0] = {(lv_coord_t)(x - dir * 5), (lv_coord_t)(midY - half)};
        p[1] = {(lv_coord_t)(x + dir * 3), (lv_coord_t)midY};
        p[2] = {(lv_coord_t)(x - dir * 5), (lv_coord_t)(midY + half)};
        chevrons[s][i] = stroke(lvobj, 4, trackColor());
        lv_line_set_points(chevrons[s][i], p, 3);
      }
    }
    bar.build(lvobj, PAD * 2, h - 20, w - PAD * 4, 6, color);
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
    bar.set(v);
    const unsigned lit = abs(v) > 75 ? 3 : abs(v) > 45 ? 2 : abs(v) > 15 ? 1 : 0;
    for (unsigned s = 0; s < 2; ++s) {
      const bool active = s ? v > 0 : v < 0;
      for (unsigned i = 0; i < 3; ++i)
        lv_obj_set_style_line_color(chevrons[s][i], active && i < lit ? color : trackColor(), 0);
    }
  }

 protected:
  CentreBar bar;
  lv_obj_t* chevrons[2][3] = {};
  lv_point_t chevronPts[2][3][3] = {};
  lv_color_t color{};
  int16_t last = INT16_MIN;
};

Nb4WidgetFactory<Nb4SteerBigWidget> nb4SteerBigWidget(
    "NB4SteerBig", trimOptions, NB4_STR(WIDGET_STEER_BIG));

#endif
