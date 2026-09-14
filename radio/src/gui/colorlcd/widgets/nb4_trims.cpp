/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_widget_kit.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

class Nb4TrimsWidget : public Widget
{
 public:
  Nb4TrimsWidget(const WidgetFactory* factory, Window* parent,
                 const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  void delayedInit() override
  {
    Nb4Ui::card(lvobj);
    const coord_t w = width(), h = height();

    caption(this, {PAD, 6, 80, 18}, STR_NB4_TRIMS);

    if (compactZone(h)) {
      const coord_t colW = (w - PAD * 3) / 2;
      block(PAD, 26, colW, STR_NB4_ST, Nb4Ui::steeringColor(), steer);
      block(PAD * 2 + colW, 26, colW, STR_NB4_TH, Nb4Ui::throttleColor(), thr);
    } else {
      const coord_t y2 = block(PAD, 32, w - PAD * 2, STR_NB4_ST,
                               Nb4Ui::steeringColor(), steer);
      block(PAD, y2 + 12, w - PAD * 2, STR_NB4_TH, Nb4Ui::throttleColor(), thr);
    }
    update();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();
    steer.update(ADC_MAIN_ST);
    thr.update(ADC_MAIN_TH);
  }

 protected:
  struct Row {
    lv_obj_t* marker = nullptr;
    coord_t x0 = 0, span = 0;
    int last = INT32_MIN;
    bool lastOk = true;

    void update(uint8_t idx)
    {
      if (!marker) return;
      const bool ok = trimOk(idx);
      const int pct = ok ? trimPct(idx) : 0;
      if (pct == last && ok == lastOk) return;
      last = pct;
      lastOk = ok;
      lv_obj_set_x(marker, x0 + (coord_t)divRoundClosest((pct + 100) * (int32_t)span, 200));
      nb4w::show(marker, ok);
    }
  } steer, thr;

  coord_t block(coord_t x, coord_t y, coord_t w, const char* label,
                lv_color_t color, Row& row)
  {
    auto name = nb4Label(this, {x, y + 4, 40, 22}, label, FONT(BOLD));
    ink(name, color);
    const uint8_t idx = &row == &steer ? ADC_MAIN_ST : ADC_MAIN_TH;
    auto value = new DynamicText(
        this, {(coord_t)(x + 40), y, (coord_t)(w - 40), 34},
        [idx]() { return trimOk(idx) ? signedText(trimValue(idx)) : std::string("--"); },
        COLOR_THEME_PRIMARY1_INDEX, FONT(L) | RIGHT);
    ink(value, color);

    const coord_t sy = y + 38;
    track(lvobj, x, sy, w, 10, 3);
    rectangle(lvobj, x + w / 2 - 1, sy - 3, 2, 16, muted());
    row.x0 = x;
    row.span = w - 4;
    row.marker = rectangle(lvobj, x + w / 2 - 2, sy - 4, 4, 18, color, 2);

    tiny(this, {x, (coord_t)(sy + 14), 40, 16}, "-100");
    tiny(this, {(coord_t)(x + w / 2 - 20), (coord_t)(sy + 14), 40, 16}, "0", CENTERED);
    tiny(this, {(coord_t)(x + w - 40), (coord_t)(sy + 14), 40, 16}, "+100", RIGHT);
    return sy + 30;
  }
};

BaseWidgetFactory<Nb4TrimsWidget> nb4TrimsWidget("NB4Trims", nullptr, "Trims");

#endif
