/* SPDX-License-Identifier: GPL-2.0-only */

#include "nb4_widget_kit.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

// Outputs CH3 and CH4 (lights, winch, 2-speed, ...) as centred bars.
class Nb4AuxWidget : public Widget
{
 public:
  Nb4AuxWidget(const WidgetFactory* factory, Window* parent,
               const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  void delayedInit() override
  {
    Nb4Ui::card(lvobj);
    const coord_t w = width(), h = height();
    const lv_color_t color = Nb4Ui::auxColor();
    // Side by side in a short zone, stacked otherwise.
    const bool row = h < 2 * 44;
    for (unsigned i = 0; i < 2; ++i) {
      const coord_t x = row ? (coord_t)(i * w / 2) : 0;
      const coord_t y = row ? 0 : (coord_t)(i * h / 2);
      const coord_t cw = row ? w / 2 : w, ch = row ? h : h / 2;
      auto t = nb4Label(this, {(coord_t)(x + PAD), (coord_t)(y + (ch - 20) / 2), 40, 20},
                        i ? "CH4" : "CH3", FONT(BOLD));
      ink(t, color);
      const coord_t bx = x + PAD + 44, bw = cw - PAD * 2 - 44 - 46;
      bars[i].build(lvobj, bx, y + ch / 2 - 5, bw < 20 ? 20 : bw, 10, color);
      const uint8_t channel = 2 + i;
      new DynamicNumber<int16_t>(this, {(coord_t)(x + cw - PAD - 44), (coord_t)(y + (ch - 21) / 2), 44, 21},
                                 [channel]() { return channelPct(channel); },
                                 COLOR_THEME_PRIMARY1_INDEX, FONT(STD) | RIGHT);
    }
    if (row) nb4Hairline(this, {(coord_t)(w / 2), 8, 1, (coord_t)(h - 16)});
    update();
    checkEvents();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();
    bars[0].set(channelPct(2));
    bars[1].set(channelPct(3));
  }

 protected:
  CentreBar bars[2];
};

Nb4WidgetFactory<Nb4AuxWidget> nb4AuxWidget(
    "NB4Aux", nullptr, NB4_STR(WIDGET_AUX));

#endif
