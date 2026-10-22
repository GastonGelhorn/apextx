/* SPDX-License-Identifier: GPL-2.0-only */

#include "nb4_widget_kit.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

// LED-style column: brake segments below the neutral line, throttle above.
class Nb4ThrSegmentsWidget : public Widget
{
  static constexpr unsigned SEGMENTS = 14;
  static constexpr unsigned BRAKE_SEGMENTS = 4;

 public:
  Nb4ThrSegmentsWidget(const WidgetFactory* factory, Window* parent,
                       const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  void delayedInit() override
  {
    Nb4Ui::card(lvobj);
    const coord_t w = width(), h = height();
    gasInk = Nb4Ui::throttleColor();
    brakeInk = Nb4Ui::brakeColor();
    nb4Label(this, {PAD, 6, (coord_t)(w - PAD * 2), 20}, STR_NB4_THR_BRAKE, FONT(BOLD));

    const coord_t top = 32, bottom = h - PAD, gap = 3;
    const coord_t segH = (bottom - top - gap * (SEGMENTS - 1)) / SEGMENTS;
    const coord_t segW = w * 2 / 5;
    for (unsigned i = 0; i < SEGMENTS; ++i) {
      const coord_t y = bottom - (coord_t)(i + 1) * segH - (coord_t)i * gap;
      segs[i] = rectangle(lvobj, PAD + 4, y, segW, segH < 3 ? 3 : segH, trackColor(), 2);
    }
    const coord_t rx = PAD + 4 + segW + PAD, rw = w - rx - PAD;
    const LcdFlags f = fontForChars(rw, 4, (bottom - top) / 2);
    pct = new DynamicNumber<int16_t>(
        this, {rx, (coord_t)((top + bottom - fontHeight(f)) / 2), rw, fontHeight(f)},
        []() { return channelPct(g_model.nb4Racing.throttleChannel); },
        COLOR_THEME_PRIMARY1_INDEX, f | CENTERED, nullptr, "%");
    last = INT16_MIN;
    update();
    checkEvents();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();
    const int16_t v = channelPct(g_model.nb4Racing.throttleChannel);
    if (v == last) return;
    last = v;
    const unsigned gasLit = v > 0 ? (v * (SEGMENTS - BRAKE_SEGMENTS) + 99) / 100 : 0;
    const unsigned brakeLit = v < 0 ? (-v * BRAKE_SEGMENTS + 99) / 100 : 0;
    for (unsigned i = 0; i < SEGMENTS; ++i) {
      bool on;
      if (i < BRAKE_SEGMENTS) on = BRAKE_SEGMENTS - 1 - i < brakeLit;
      else on = i - BRAKE_SEGMENTS < gasLit;
      recolor(segs[i], on ? (i < BRAKE_SEGMENTS ? brakeInk : gasInk) : trackColor());
    }
    if (pct) ink(pct, v < 0 ? brakeInk : gasInk);
  }

 protected:
  lv_obj_t* segs[SEGMENTS] = {};
  StaticText* pct = nullptr;
  lv_color_t gasInk{}, brakeInk{};
  int16_t last = INT16_MIN;
};

Nb4WidgetFactory<Nb4ThrSegmentsWidget> nb4ThrSegmentsWidget(
    "NB4ThrSegments", nullptr, NB4_STR(WIDGET_THR_SEGMENTS));

#endif
