/* SPDX-License-Identifier: GPL-2.0-only */

#include "nb4_trim_row.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

// Car seen from above; the front wheels follow the steering channel.
class Nb4SteerCarWidget : public Nb4TrimWidget
{
 public:
  Nb4SteerCarWidget(const WidgetFactory* factory, Window* parent,
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

    // Body proportions of a 1/10 touring car: about twice as long as wide.
    coord_t bodyH = compact ? h - T - 12 : (h - T - 68 < (w - 60) * 2 ? h - T - 68 : (w - 60) * 2);
    if (bodyH < 40) bodyH = 40;
    bodyW = bodyH / 2;
    cx = compact ? PAD + bodyW / 2 + 16 : w / 2;
    cy = compact ? T + bodyH / 2 : T + 10 + bodyH / 2;
    wheelLen = bodyH / 5;
    const coord_t wheelW = bodyW / 4 < 5 ? 5 : bodyW / 4;

    // Wheels first so the body covers their inner half.
    for (auto& wheel : wheels) wheel = stroke(lvobj, wheelW, textColor());
    axleY[0] = cy - bodyH * 3 / 10;
    axleY[1] = cy + bodyH * 3 / 10;
    auto body = rectangle(lvobj, cx - bodyW / 2, cy - bodyH / 2, bodyW, bodyH, trackColor(), bodyW / 3);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_border_color(body, color, 0);
    rectangle(lvobj, cx - bodyW / 2 + bodyW / 6, cy - bodyH / 8, bodyW - bodyW / 3, bodyH / 4,
              Nb4Ui::color(COLOR_THEME_SECONDARY3_INDEX), 4);
    heading = stroke(lvobj, 3, color);

    rect_t pr;
    LcdFlags f;
    if (compact) {
      const coord_t px = cx + bodyW / 2 + 16 + PAD;
      f = fontForChars(w - px - PAD, 5, h - T - 12);
      pr = {px, (coord_t)((h + T - 12 - fontHeight(f)) / 2), (coord_t)(w - px - PAD), fontHeight(f)};
    } else {
      f = fontForChars(w - PAD * 2, 5, h - (cy + bodyH / 2) - 10);
      pr = {0, (coord_t)(cy + bodyH / 2 + 6), w, fontHeight(f)};
    }
    new DynamicNumber<int16_t>(this, pr, []() { return channelPct(g_model.nb4Racing.steeringChannel); },
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
    // Front wheels lock up to 30 deg; rear wheels stay straight.
    const double lock = v * 0.3;
    for (unsigned i = 0; i < 4; ++i) {
      const coord_t x = cx + ((i & 1) ? bodyW / 2 + 2 : -bodyW / 2 - 2);
      const coord_t y = axleY[i / 2];
      const double a = 270.0 + (i < 2 ? lock : 0.0);
      wheelPts[i][0] = polar(x, y, a, wheelLen / 2);
      wheelPts[i][1] = polar(x, y, a + 180.0, wheelLen / 2);
      lv_line_set_points(wheels[i], wheelPts[i], 2);
    }
    headPts[0] = {(lv_coord_t)cx, (lv_coord_t)(axleY[0] - wheelLen / 2)};
    headPts[1] = polar(cx, axleY[0] - wheelLen / 2, 270.0 + lock, wheelLen);
    lv_line_set_points(heading, headPts, 2);
  }

 protected:
  lv_obj_t* wheels[4] = {};
  lv_obj_t* heading = nullptr;
  lv_point_t wheelPts[4][2] = {};
  lv_point_t headPts[2] = {};
  coord_t cx = 0, cy = 0, bodyW = 0, wheelLen = 0;
  coord_t axleY[2] = {};
  int16_t last = INT16_MIN;
};

Nb4WidgetFactory<Nb4SteerCarWidget> nb4SteerCarWidget(
    "NB4SteerCar", trimOptions, NB4_STR(WIDGET_STEER_CAR));

#endif
