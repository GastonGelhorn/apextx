/* SPDX-License-Identifier: GPL-2.0-only */

#include "nb4_widget_kit.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

// Horizontal bar with neutral at 30 %: brake fills left, throttle right.
class Nb4ThrBarWidget : public Widget
{
 public:
  Nb4ThrBarWidget(const WidgetFactory* factory, Window* parent,
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

    const coord_t barH = h < 110 ? 18 : 30;
    barX = PAD;
    barW = w - PAD * 2;
    const coord_t barY = h - PAD - 16 - barH;
    zero = barW * 3 / 10;
    track(lvobj, barX, barY, barW, barH, 4);
    fill = rectangle(lvobj, barX + zero, barY + 3, 0, barH - 6, gasInk, 2);
    rectangle(lvobj, barX + zero - 1, barY - 4, 2, barH + 8, textColor());
    fillY = barY + 3;
    fillH = barH - 6;
    auto b = caption(this, {barX, (coord_t)(barY + barH + 2), 60, 16}, STR_NB4_BRAKE);
    ink(b, brakeInk);
    auto g = caption(this, {(coord_t)(barX + barW - 60), (coord_t)(barY + barH + 2), 60, 16}, STR_NB4_THR, RIGHT);
    ink(g, gasInk);

    const LcdFlags f = fontForChars(w - PAD * 2, 4, barY - 30);
    pct = new DynamicNumber<int16_t>(
        this, {PAD, (coord_t)(30 + (barY - 30 - fontHeight(f)) / 2), (coord_t)(w - PAD * 2), fontHeight(f)},
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
    if (v >= 0) {
      const coord_t len = (coord_t)(v * (barW - zero - 3) / 100);
      place(fill, barX + zero, fillY, len, fillH);
      recolor(fill, gasInk);
    } else {
      const coord_t len = (coord_t)(-v * (zero - 3) / 100);
      place(fill, barX + zero - len, fillY, len, fillH);
      recolor(fill, brakeInk);
    }
    if (pct) ink(pct, v < 0 ? brakeInk : gasInk);
  }

 protected:
  lv_obj_t* fill = nullptr;
  StaticText* pct = nullptr;
  lv_color_t gasInk{}, brakeInk{};
  coord_t barX = 0, barW = 0, zero = 0, fillY = 0, fillH = 0;
  int16_t last = INT16_MIN;
};

Nb4WidgetFactory<Nb4ThrBarWidget> nb4ThrBarWidget(
    "NB4ThrBar", nullptr, NB4_STR(WIDGET_THR_BAR));

#endif
