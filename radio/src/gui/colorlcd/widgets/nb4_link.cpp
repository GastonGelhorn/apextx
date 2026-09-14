/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_widget_kit.h"
#include "pulses/afhds3.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

class Nb4LinkWidget : public Widget
{
 public:
  Nb4LinkWidget(const WidgetFactory* factory, Window* parent,
                const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  void delayedInit() override
  {
    Nb4Ui::card(lvobj);
    const coord_t w = width(), h = height();
    const bool compact = compactZone(h);

    caption(this, {PAD, 6, 80, 18}, STR_NB4_LINK);
    state = pill(this, {(coord_t)(w - PAD - 84), 4, 84, 20}, "", green());

    auto rx = []() {
      const auto t = afhds3::getReceiverTelemetry(INTERNAL_MODULE);
      return voltsText(t.voltageMv, t.voltageAvailable);
    };

    if (compact) {
      const coord_t colW = (w - PAD * 3) / 2;
      rf(PAD, 30, colW, h - 38);
      rectangle(lvobj, PAD + colW + PAD / 2, 30, 1, h - 40, trackColor());
      caption(this, {(coord_t)(PAD * 2 + colW), 30, 40, 16}, "RX");
      new DynamicText(this, {(coord_t)(PAD * 2 + colW), 46, colW, 44}, rx,
                      COLOR_THEME_PRIMARY1_INDEX, FONT(XL) | RIGHT);
    } else {
      rf(PAD, 32, w - PAD * 2, 60);
      caption(this, {PAD, (coord_t)(h - 62), 40, 16}, "RX");
      new DynamicText(this, {PAD, (coord_t)(h - 46), (coord_t)(w - PAD * 2), 38}, rx,
                      COLOR_THEME_PRIMARY1_INDEX, FONT(L) | RIGHT);
    }
    update();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();
    const int q = quality();
    if (q == lastQuality) return;
    lastQuality = q;

    const int lit = q < 0 ? 0 : q >= 90 ? 5 : q >= 75 ? 4 : q >= 55 ? 3 : q >= 35 ? 2 : 1;
    lv_color_t color = q < 0 || q < g_model.rfAlarms.critical ? red()
                     : q < g_model.rfAlarms.warning ? orange() : green();
    for (int i = 0; i < 5; ++i) recolor(bars[i], i < lit ? color : trackColor());
    if (pct) ink(pct, color);
    if (state) {
      state->setText(q < 0 ? STR_NB4_NO_LINK
                   : q < g_model.rfAlarms.critical ? STR_NB4_CRITICAL
                   : q < g_model.rfAlarms.warning ? STR_NB4_WEAK
                   : "OK");
      recolorPill(state, color);
    }
  }

 protected:
  lv_obj_t* bars[5] = {};
  StaticText* pct = nullptr;
  StaticText* state = nullptr;
  int lastQuality = INT32_MIN;

  static int quality() { return TELEMETRY_STREAMING() ? (int)TELEMETRY_RSSI() : -1; }

  void rf(coord_t x, coord_t y, coord_t w, coord_t h)
  {
    caption(this, {x, y, 40, 16}, "RF");
    const coord_t bottom = y + h - 6;
    for (int i = 0; i < 5; ++i) {
      const coord_t bh = 8 + i * 6;
      bars[i] = rectangle(lvobj, x + i * 8, bottom - bh, 6, bh, trackColor(), 1);
    }
    pct = new DynamicNumber<int16_t>(
        this, {(coord_t)(x + 46), (coord_t)(y + h - 46), (coord_t)(w - 46), 40},
        []() { const int q = quality(); return q < 0 ? 0 : q; },
        COLOR_THEME_PRIMARY1_INDEX, FONT(L) | RIGHT, nullptr, "%");
  }
};

BaseWidgetFactory<Nb4LinkWidget> nb4LinkWidget("NB4Link", nullptr, "Link");

#endif
