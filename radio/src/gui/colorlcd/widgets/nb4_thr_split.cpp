/* SPDX-License-Identifier: GPL-2.0-only */

#include "nb4_trim_row.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

// Two columns side by side: throttle on the left, brake on the right.
class Nb4ThrSplitWidget : public Nb4TrimWidget
{
 public:
  Nb4ThrSplitWidget(const WidgetFactory* factory, Window* parent,
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

    const LcdFlags f = fontForChars(w - PAD * 2, 4, 43);
    const coord_t fh = fontHeight(f);
    titleShown = titled();
    top = titleShown ? 30 : 6;
    bottom = h - PAD - fh - 4;
    colW = (w - PAD * 3) / 2;
    const coord_t x[2] = {PAD, (coord_t)(PAD * 2 + colW)};
    const char* titles[2] = {STR_NB4_THR, STR_NB4_BRAKE_B80D};
    const lv_color_t inks[2] = {gasInk, brakeInk};
    for (unsigned i = 0; i < 2; ++i) {
      if (titleShown) {
        auto t = nb4Label(this, {x[i], 6, colW, 20}, titles[i], FONT(BOLD));
        ink(t, inks[i]);
      }
      track(lvobj, x[i], top, colW, bottom - top, 4);
      fills[i] = rectangle(lvobj, x[i] + 2, bottom, colW - 4, 0, inks[i], 2);
      colX[i] = x[i];
    }
    pct = new DynamicNumber<int16_t>(
        this, {PAD, (coord_t)(h - PAD - fh), (coord_t)(w - PAD * 2), fh},
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
    trim.update();
    const int16_t v = channelPct(g_model.nb4Racing.throttleChannel);
    if (v == last) return;
    last = v;
    const coord_t span = bottom - top - 4;
    const coord_t gas = v > 0 ? (coord_t)(v * span / 100) : 0;
    const coord_t brake = v < 0 ? (coord_t)(-v * span / 100) : 0;
    place(fills[0], colX[0] + 2, bottom - 2 - gas, colW - 4, gas);
    place(fills[1], colX[1] + 2, bottom - 2 - brake, colW - 4, brake);
    if (pct) ink(pct, v < 0 ? brakeInk : gasInk);
  }

 protected:
  lv_obj_t* fills[2] = {};
  StaticText* pct = nullptr;
  lv_color_t gasInk{}, brakeInk{};
  coord_t top = 0, bottom = 0, colW = 0;
  coord_t colX[2] = {};
  int16_t last = INT16_MIN;
};

Nb4WidgetFactory<Nb4ThrSplitWidget> nb4ThrSplitWidget(
    "NB4ThrSplit", trimOptions, NB4_STR(WIDGET_THR_SPLIT));

#endif
