/* SPDX-License-Identifier: GPL-2.0-only */

#include "nb4_widget_kit.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

// Steering as one large number with direction chevrons and a thin scale.
class Nb4SteerBigWidget : public Widget
{
 public:
  Nb4SteerBigWidget(const WidgetFactory* factory, Window* parent,
                    const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  void delayedInit() override
  {
    Nb4Ui::card(lvobj);
    const coord_t w = width(), h = height();
    color = Nb4Ui::steeringColor();
    auto title = nb4Label(this, {PAD, 6, (coord_t)(w - PAD * 2), 20}, STR_NB4_STEERING, FONT(BOLD));
    ink(title, color);

    const coord_t side = 3 * 12 + PAD;
    const LcdFlags f = fontForChars(w - side * 2, 4, h - 64);
    const coord_t fh = fontHeight(f);
    const coord_t top = 28 + (h - 28 - 26 - fh) / 2;
    new DynamicNumber<int16_t>(this, {side, top, (coord_t)(w - side * 2), fh},
                               []() { return (int16_t)abs(channelPct(g_model.nb4Racing.steeringChannel)); },
                               COLOR_THEME_PRIMARY1_INDEX, f | CENTERED);
    const coord_t midY = top + fh / 2;
    const coord_t half = fh / 4 < 8 ? 8 : fh / 4;
    for (unsigned i = 0; i < 3; ++i) {
      for (unsigned s = 0; s < 2; ++s) {
        // s = 0 points left, s = 1 points right; i = 0 is nearest the number.
        const coord_t x = s ? w - side + 4 + i * 12 : side - 4 - i * 12;
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
    "NB4SteerBig", nullptr, NB4_STR(WIDGET_STEER_BIG));

#endif
