/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_widget_kit.h"

#if defined(RADIO_NB4_FAMILY)

std::string getSensorCustomValue(uint8_t sensor, int32_t value, LcdFlags flags);

using namespace nb4w;

class Nb4SensorWidget : public Widget
{
 public:
  Nb4SensorWidget(const WidgetFactory* factory, Window* parent,
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
    const bool compact = compactZone(h);

    title = new DynamicText(this, {PAD, 6, (coord_t)(w - PAD * 2), 18},
                            [this]() { return name(); },
                            COLOR_THEME_PRIMARY3_INDEX, FONT(XS));

    auto current = [this]() { return text(getValue(source())); };

    if (compact) {
      const coord_t valueW = w * 2 / 5;
      const LcdFlags f = fontForHeight(h - 34);
      value = new DynamicText(this, {PAD, (coord_t)((h - fontHeight(f)) / 2 + 6), valueW, fontHeight(f)},
                              current, COLOR_THEME_PRIMARY1_INDEX, f | CENTERED);
      rectangle(lvobj, valueW + PAD * 2, 30, 1, h - 40, trackColor());
      const coord_t rx = valueW + PAD * 3, rw = w - rx - PAD;
      bar.build(lvobj, rx, 44, rw, 10, orange());
      caption(this, {rx, 62, (coord_t)(rw / 2), 18}, STR_NB4_MIN);
      lo = new DynamicText(this, {(coord_t)(rx + rw / 2), 62, (coord_t)(rw / 2), 18},
                           [this]() { return text(seenLo); }, COLOR_THEME_PRIMARY1_INDEX, FONT(XS) | RIGHT);
      caption(this, {rx, 82, (coord_t)(rw / 2), 18}, STR_NB4_MAX);
      hi = new DynamicText(this, {(coord_t)(rx + rw / 2), 82, (coord_t)(rw / 2), 18},
                           [this]() { return text(seenHi); }, COLOR_THEME_PRIMARY1_INDEX, FONT(XS) | RIGHT);
    } else {
      const LcdFlags f = fontForWidth(w);
      value = new DynamicText(this, {0, 34, w, fontHeight(f)}, current,
                              COLOR_THEME_PRIMARY1_INDEX, f | CENTERED);
      const coord_t by = h - 66;
      bar.build(lvobj, PAD, by, w - PAD * 2, 10, orange());
      caption(this, {PAD, (coord_t)(by + 16), 50, 18}, STR_NB4_MIN);
      lo = new DynamicText(this, {(coord_t)(PAD + 50), (coord_t)(by + 16), (coord_t)(w - PAD * 2 - 50), 18},
                           [this]() { return text(seenLo); }, COLOR_THEME_PRIMARY1_INDEX, FONT(XS) | RIGHT);
      caption(this, {PAD, (coord_t)(by + 36), 50, 18}, STR_NB4_MAX);
      hi = new DynamicText(this, {(coord_t)(PAD + 50), (coord_t)(by + 36), (coord_t)(w - PAD * 2 - 50), 18},
                           [this]() { return text(seenHi); }, COLOR_THEME_PRIMARY1_INDEX, FONT(XS) | RIGHT);
    }
    update();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();

    const mixsrc_t src = source();
    if (src != lastSource) {
      lastSource = src;
      seenLo = INT32_MAX;
      seenHi = INT32_MIN;
    }
    if (!available(src)) {
      bar.set(bar.last == INT32_MIN ? 0 : INT32_MIN, 0, 1);
      return;
    }
    const int32_t v = getValue(src);
    if (v < seenLo) seenLo = v;
    if (v > seenHi) seenHi = v;

    const int lo = getPersistentData()->options[1].value.signedValue;
    const int hi = getPersistentData()->options[2].value.signedValue;
    bar.set(v, lo, hi);
    const bool alarm = hi > lo && v >= hi;
    if (alarm != lastAlarm) {
      lastAlarm = alarm;
      recolor(bar.fill, alarm ? red() : orange());
      if (value) ink(value, alarm ? red() : textColor());
    }
  }

 protected:
  RangeBar bar;
  StaticText* title = nullptr;
  StaticText* value = nullptr;
  StaticText* lo = nullptr;
  StaticText* hi = nullptr;
  mixsrc_t lastSource = 0;
  int32_t seenLo = INT32_MAX, seenHi = INT32_MIN;
  bool lastAlarm = false;

  mixsrc_t source() { return getPersistentData()->options[0].value.unsignedValue; }

  static bool configured(mixsrc_t src)
  {
    if (src >= MIXSRC_FIRST_TELEM && src <= MIXSRC_LAST_TELEM)
      return g_model.telemetrySensors[(src - MIXSRC_FIRST_TELEM) / 3].label[0] != '\0';
    return src != 0;
  }

  static bool available(mixsrc_t src)
  {
    if (!configured(src)) return false;
    if (src >= MIXSRC_FIRST_TELEM && src <= MIXSRC_LAST_TELEM)
      return telemetryItems[(src - MIXSRC_FIRST_TELEM) / 3].isAvailable();
    return true;
  }

  std::string name()
  {
    const mixsrc_t src = source();
    if (!configured(src)) return STR_NB4_SENSOR;
    return getSourceString(src);
  }

  std::string text(int32_t v)
  {
    const mixsrc_t src = source();
    if (v == INT32_MAX || v == INT32_MIN || !available(src)) return "--";
    if (src >= MIXSRC_FIRST_TELEM && src <= MIXSRC_LAST_TELEM)
      return getSensorCustomValue((src - MIXSRC_FIRST_TELEM) / 3, v, 0);
    return getSourceCustomValueString(src, v, 0);
  }
};

const WidgetOption Nb4SensorWidget::options[] = {
    {STR_SOURCE, WidgetOption::Source, MIXSRC_FIRST_TELEM},
    {STR_MIN, WidgetOption::Integer, WIDGET_OPTION_VALUE_SIGNED(0),
     WIDGET_OPTION_VALUE_SIGNED(-30000), WIDGET_OPTION_VALUE_SIGNED(30000)},
    {STR_MAX, WidgetOption::Integer, WIDGET_OPTION_VALUE_SIGNED(100),
     WIDGET_OPTION_VALUE_SIGNED(-30000), WIDGET_OPTION_VALUE_SIGNED(30000)},
    {nullptr, WidgetOption::Bool}};

BaseWidgetFactory<Nb4SensorWidget> nb4SensorWidget("NB4Sensor",
                                                   Nb4SensorWidget::options,
                                                   "Sensor");

#endif
