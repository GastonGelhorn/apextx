/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_widget_kit.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

class Nb4ThrColumnWidget : public Widget
{
 public:
  Nb4ThrColumnWidget(const WidgetFactory* factory, Window* parent,
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

    auto t1 = nb4Label(this, {PAD, 6, 44, 20}, STR_NB4_THR, FONT(BOLD));
    ink(t1, gasInk);
    nb4Label(this, {(coord_t)(PAD + 44), 6, (coord_t)(w - PAD * 2 - 44), 20},
             STR_NB4_BRAKE, FONT(BOLD));

    const coord_t top = 32, bottom = h - PAD;
    colH = bottom - top;
    axisY = top + colH / 2;
    colW = w / 8;
    if (colW < 14) colW = 14;
    if (colW > 30) colW = 30;
    colX = PAD + 6;
    track(lvobj, colX, top, colW, colH, 4);
    fill = rectangle(lvobj, colX + 2, axisY, colW - 4, 0, gasInk, 2);
    rectangle(lvobj, colX - 4, axisY - 1, colW + 8, 2, muted());

    const coord_t rx = colX + colW + PAD + 6;
    const coord_t rw = w - rx - PAD;
    tiny(this, {rx, (coord_t)(top - 2), rw, 16}, "+100");
    tiny(this, {rx, (coord_t)(bottom - 14), rw, 16}, "-100");

    const LcdFlags f = fontForChars(rw, 5, colH / 2 > 60 ? 60 : colH / 2);
    pct = new DynamicNumber<int16_t>(
        this, {rx, (coord_t)(axisY - fontHeight(f) / 2), rw, fontHeight(f)},
        []() { return channelPct(g_model.nb4Racing.throttleChannel); },
        COLOR_THEME_PRIMARY1_INDEX, f | CENTERED, nullptr, "%");
    gasWord = caption(this, {rx, (coord_t)(axisY - fontHeight(f) / 2 - 18), rw, 16},
                      STR_NB4_THR, CENTERED);
    brakeWord = caption(this, {rx, (coord_t)(axisY + fontHeight(f) / 2 + 2), rw, 16},
                        STR_NB4_BRAKE_B80D, CENTERED);
    update();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();
    const int16_t v = channelPct(g_model.nb4Racing.throttleChannel);
    if (v == last) return;
    last = v;
    const coord_t len = (coord_t)((abs((int32_t)v) * (colH / 2 - 2)) / 100);
    if (v >= 0) {
      place(fill, colX + 2, axisY - len, colW - 4, len);
      recolor(fill, gasInk);
    } else {
      place(fill, colX + 2, axisY, colW - 4, len);
      recolor(fill, brakeInk);
    }
    const lv_color_t c = v < 0 ? brakeInk : gasInk;
    if (pct) ink(pct, c);
    if (gasWord) ink(gasWord, v > 0 ? gasInk : muted());
    if (brakeWord) ink(brakeWord, v < 0 ? brakeInk : muted());
  }

 protected:
  lv_obj_t* fill = nullptr;
  StaticText* pct = nullptr;
  StaticText* gasWord = nullptr;
  StaticText* brakeWord = nullptr;
  lv_color_t gasInk{}, brakeInk{};
  coord_t colX = 0, colW = 0, colH = 0, axisY = 0;
  int16_t last = INT16_MIN;
};

BaseWidgetFactory<Nb4ThrColumnWidget> nb4ThrColumnWidget("NB4ThrColumn", nullptr,
                                                         "Thr column");

#endif
