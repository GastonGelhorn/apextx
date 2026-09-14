/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_widget_kit.h"
#include "pulses/afhds3.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

class Nb4BatteryWidget : public Widget
{
 public:
  Nb4BatteryWidget(const WidgetFactory* factory, Window* parent,
                   const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  static const WidgetOption options[];

  void delayedInit() override
  {
    Nb4Ui::card(lvobj);
    const coord_t w = width(), h = height();

    if (compactZone(h)) {
      const coord_t colW = (w - PAD * 3) / 2;
      block(PAD, 10, colW, STR_NB4_TX_RADIO, orange(), tx,
            []() { return tenthsText(g_vbat100mV); });
      rectangle(lvobj, PAD + colW + PAD / 2, 14, 1, h - 28, trackColor());
      block(PAD * 2 + colW, 10, colW, STR_NB4_RX_RECEIVER, green(), rx,
            []() { const auto t = afhds3::getReceiverTelemetry(INTERNAL_MODULE); return voltsText(t.voltageMv, t.voltageAvailable); });
    } else {
      caption(this, {PAD, 6, 100, 18}, STR_NB4_BATTERIES);
      const coord_t y2 = block(PAD, 30, w - PAD * 2, "TX", orange(), tx,
                               []() { return tenthsText(g_vbat100mV); });
      block(PAD, y2 + 14, w - PAD * 2, "RX", green(), rx,
            []() { const auto t = afhds3::getReceiverTelemetry(INTERNAL_MODULE); return voltsText(t.voltageMv, t.voltageAvailable); });
    }
    update();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();

    // Stored with fixed offsets: vBatMin from 9.0 V, vBatMax from 12.0 V.
    const int txLo = 90 + g_eeGeneral.vBatMin;
    const int txHi = 120 + g_eeGeneral.vBatMax;
    tx.bar.threshold(g_eeGeneral.vBatWarn, txLo, txHi);
    tx.bar.set(g_vbat100mV, txLo, txHi);
    recolor(tx.bar.fill, g_vbat100mV <= g_eeGeneral.vBatWarn ? red() : orange());
    tx.range(txLo, txHi);

    const int rxLo = getPersistentData()->options[0].value.signedValue;
    const int rxHi = getPersistentData()->options[1].value.signedValue;
    const auto t = afhds3::getReceiverTelemetry(INTERNAL_MODULE);
    const int rxTenths = t.voltageAvailable ? (int)(t.voltageMv / 100) : rxLo;
    rx.bar.threshold(rxLo + (rxHi - rxLo) / 10, rxLo, rxHi);
    rx.bar.set(rxTenths, rxLo, rxHi);
    recolor(rx.bar.fill, rxTenths <= rxLo + (rxHi - rxLo) / 10 ? red() : green());
    rx.range(rxLo, rxHi);
  }

 protected:
  struct Block {
    RangeBar bar;
    StaticText* lo = nullptr;
    StaticText* hi = nullptr;
    int lastLo = INT32_MIN, lastHi = INT32_MIN;

    void range(int l, int h)
    {
      if (l == lastLo && h == lastHi) return;
      lastLo = l;
      lastHi = h;
      if (lo) lo->setText(tenthsText(l));
      if (hi) hi->setText(tenthsText(h));
    }
  } tx, rx;

  coord_t block(coord_t x, coord_t y, coord_t w, const char* label,
                lv_color_t color, Block& b, std::function<std::string()> value)
  {
    auto name = nb4Label(this, {x, y + 6, (coord_t)(w / 2), 22}, label, FONT(BOLD));
    ink(name, color);
    new DynamicText(this, {(coord_t)(x + w / 2), y, (coord_t)(w - w / 2), 34},
                    value, COLOR_THEME_PRIMARY1_INDEX, FONT(L) | RIGHT);
    const coord_t by = y + 40;
    b.bar.build(lvobj, x, by, w, 10, color);
    b.lo = tiny(this, {x, (coord_t)(by + 14), 50, 16}, "");
    b.hi = tiny(this, {(coord_t)(x + w - 50), (coord_t)(by + 14), 50, 16}, "", RIGHT);
    return by + 30;
  }
};

// Receiver pack range in tenths of a volt. The radio's own range comes from
// its battery settings, so only the receiver needs configuring here.
const WidgetOption Nb4BatteryWidget::options[] = {
    {STR_MIN, WidgetOption::Integer, WIDGET_OPTION_VALUE_SIGNED(46),
     WIDGET_OPTION_VALUE_SIGNED(30), WIDGET_OPTION_VALUE_SIGNED(300)},
    {STR_MAX, WidgetOption::Integer, WIDGET_OPTION_VALUE_SIGNED(84),
     WIDGET_OPTION_VALUE_SIGNED(30), WIDGET_OPTION_VALUE_SIGNED(300)},
    {nullptr, WidgetOption::Bool}};

BaseWidgetFactory<Nb4BatteryWidget> nb4BatteryWidget("NB4Battery",
                                                     Nb4BatteryWidget::options,
                                                     "Battery");

#endif
