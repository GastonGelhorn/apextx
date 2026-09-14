/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_widget_kit.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

class Nb4SteerBarWidget : public Widget
{
 public:
  Nb4SteerBarWidget(const WidgetFactory* factory, Window* parent,
                    const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  void delayedInit() override
  {
    Nb4Ui::card(lvobj);
    const coord_t w = width(), h = height();
    const lv_color_t color = Nb4Ui::steeringColor();

    auto title = nb4Label(this, {PAD, 6, (coord_t)(compactZone(h) ? w / 2 : w - PAD * 2), 20}, STR_NB4_STEERING, FONT(BOLD));
    ink(title, color);

    const bool compact = compactZone(h);
    const LcdFlags f = compact ? fontForChars(w / 2 - PAD, 5, 34) : fontForChars(w - PAD * 2, 5, 53);
    const coord_t valueTop = compact ? 4 : 30;
    const coord_t valueX = compact ? w / 2 : PAD;
    auto value = new DynamicNumber<int16_t>(
        this, {valueX, valueTop, (coord_t)(w - valueX - PAD), fontHeight(f)},
        []() { return channelPct(g_model.nb4Racing.steeringChannel); },
        COLOR_THEME_PRIMARY1_INDEX, f | RIGHT, nullptr, "%");
    ink(value, color);

    const coord_t barH = h < 110 ? 22 : 34;
    const coord_t by = h - barH - 42;
    bar.build(lvobj, PAD, by, w - PAD * 2, barH, color);
    trimSpan = w - PAD * 2 - 6;
    trimMark = rectangle(lvobj, PAD + trimSpan / 2, by + barH + 3, 6, 6, muted(), 1);

    tiny(this, {PAD, (coord_t)(by + barH + 12), 44, 16}, "-100");
    tiny(this, {(coord_t)(w / 2 - 22), (coord_t)(by + barH + 12), 44, 16}, "0", CENTERED);
    tiny(this, {(coord_t)(w - PAD - 44), (coord_t)(by + barH + 12), 44, 16}, "+100", RIGHT);
    update();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();
    bar.set(channelPct(g_model.nb4Racing.steeringChannel));
    const bool ok = trimOk(ADC_MAIN_ST);
    const int t = ok ? trimPct(ADC_MAIN_ST) : 0;
    if (t != lastTrim || ok != lastOk) {
      lastTrim = t;
      lastOk = ok;
      lv_obj_set_x(trimMark, PAD + (coord_t)divRoundClosest((t + 100) * (int32_t)trimSpan, 200));
      nb4w::show(trimMark, ok);
    }
  }

 protected:
  CentreBar bar;
  lv_obj_t* trimMark = nullptr;
  coord_t trimSpan = 0;
  int lastTrim = INT32_MIN;
  bool lastOk = true;
};

BaseWidgetFactory<Nb4SteerBarWidget> nb4SteerBarWidget("NB4SteerBar", nullptr,
                                                       "Steer bar");

#endif
