/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "edgetx.h"
#include "widget.h"

#if defined(RADIO_NB4_FAMILY)

#include "nb4_racing.h"
#include "telemetry/telemetry.h"

class Nb4RacingWidget : public Widget
{
 public:
  Nb4RacingWidget(const WidgetFactory* factory, Window* parent,
                  const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  void delayedInit() override
  {
    coord_t w = width();
    coord_t h = height();

    coord_t rowH = EdgeTxStyles::STD_FONT_HEIGHT;
    barH = h / 8;
    if (barH < MIN_BAR_H) barH = MIN_BAR_H;
    if (barH > MAX_BAR_H) barH = MAX_BAR_H;

    compact = (h < rowH * 3 + barH * 2 + PAD_TINY * 3);
    if (compact) barH = (h - PAD_TINY * 3) / 2 - rowH;
    if (barH < MIN_BAR_H / 2) barH = MIN_BAR_H / 2;

    coord_t y = 0;

    if (!compact) {
    auto nameText = new DynamicText(
        this, {PAD_SMALL, y, w / 2, rowH},
        []() {
          return std::string(g_model.header.name,
                             strnlen(g_model.header.name, LEN_MODEL_NAME));
        },
        COLOR_THEME_PRIMARY1_INDEX, FONT(STD));

    lv_label_set_long_mode(nameText->getLvObj(), LV_LABEL_LONG_DOT);

    new DynamicText(
        this, {w / 2, y, w / 2 - PAD_SMALL, rowH},
        []() {
          char buf[24];
          char* p = buf;
          if (TELEMETRY_STREAMING()) {
            p = strAppendUnsigned(p, TELEMETRY_RSSI());
          } else {
            *p++ = '-';
          }
          *p++ = ' ';
          *p++ = ' ';
          p = strAppendUnsigned(p, g_vbat100mV / 10);
          *p++ = '.';
          p = strAppendUnsigned(p, g_vbat100mV % 10);
          *p++ = 'V';
          *p = '\0';
          return std::string(buf);
        },
        COLOR_THEME_PRIMARY1_INDEX, FONT(STD) | RIGHT);

    y += rowH + PAD_TINY;
    }

    y = addBar(y, w, rowH, "ST", &steerBox, &steerBar, &steerTick,
               &steerValue, g_model.nb4Racing.steeringChannel);
    y = addBar(y, w, rowH, "TH", &thrBox, &thrBar, &thrTick,
               &thrValue, g_model.nb4Racing.throttleChannel);

    coord_t lapH = 0;
    if (g_model.nb4Racing.lapSw != 0) lapH = rowH;

    if (h - y - lapH > rowH) {
      timerText = new DynamicText(
          this, {0, y, w, h - y},
          []() {
            char str[LEN_TIMER_STRING];
            TimerOptions timerOptions;
            timerOptions.options = SHOW_TIMER;
            int32_t val = timersStates[0].val;
            getTimerString(str, abs(val), timerOptions);
            return std::string(str);
          },
          COLOR_THEME_PRIMARY1_INDEX, FONT(XXL) | CENTERED);
      lv_obj_set_height(timerText->getLvObj(), h - y - lapH);
    }

    if (lapH > 0) {
      new DynamicText(
          this, {PAD_SMALL, h - lapH, w - PAD_SMALL * 2, lapH},
          []() {
            char buf[40];
            char* p = buf;
            *p++ = 'V';
            *p++ = ' ';
            p = strAppendUnsigned(p, nb4RacingLaps());
            p = appendLapTime(p, " U ", nb4RacingLastLap());
            p = appendLapTime(p, " M ", nb4RacingBestLap());
            *p = '\0';
            return std::string(buf);
          },
          COLOR_THEME_PRIMARY1_INDEX, FONT(XS) | CENTERED);
    }

    update();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();

    setBar(steerBox, steerBar, g_model.nb4Racing.steeringChannel, &lastSteer);
    setBar(thrBox, thrBar, g_model.nb4Racing.throttleChannel, &lastThr);
  }

 protected:
  coord_t barH = 0;
  coord_t barW = 0;
  bool compact = false;
  lv_obj_t* steerBox = nullptr;
  lv_obj_t* steerBar = nullptr;
  lv_obj_t* steerTick = nullptr;
  lv_obj_t* thrBox = nullptr;
  lv_obj_t* thrBar = nullptr;
  lv_obj_t* thrTick = nullptr;
  DynamicNumber<int16_t>* steerValue = nullptr;
  DynamicNumber<int16_t>* thrValue = nullptr;
  DynamicText* timerText = nullptr;
  int16_t lastSteer = INT16_MIN;
  int16_t lastThr = INT16_MIN;

  static char* appendLapTime(char* p, const char* label, uint32_t cs)
  {
    while (*label) *p++ = *label++;
    char t[16];
    nb4RacingFormatTime(t, cs);
    for (char* q = t; *q; q++) *p++ = *q;
    return p;
  }

  static int16_t channelPercent(uint8_t ch)
  {
    return (int16_t)divRoundClosest(channelOutputs[ch] * 100, RESX);
  }

  coord_t addBar(coord_t y, coord_t w, coord_t rowH, const char* label,
                 lv_obj_t** box, lv_obj_t** bar, lv_obj_t** tick,
                 DynamicNumber<int16_t>** value, uint8_t ch)
  {
    new StaticText(this, {PAD_SMALL, y, w / 2, rowH}, label,
                   COLOR_THEME_PRIMARY1_INDEX, FONT(XS));

    *value = new DynamicNumber<int16_t>(
        this, {w / 2, y, w / 2 - PAD_SMALL, rowH},
        [=]() { return channelPercent(ch); }, COLOR_THEME_PRIMARY1_INDEX,
        FONT(XS) | RIGHT, nullptr, "%");

    y += rowH;

    barW = w - PAD_SMALL * 2;
    *box = lv_obj_create(lvobj);
    lv_obj_set_pos(*box, PAD_SMALL, y);
    lv_obj_set_size(*box, barW, barH);
    lv_obj_clear_flag(*box, LV_OBJ_FLAG_CLICKABLE);
    etx_solid_bg(*box, COLOR_THEME_PRIMARY2_INDEX);

    *bar = lv_obj_create(*box);
    lv_obj_set_pos(*bar, barW / 2, 0);
    lv_obj_set_size(*bar, 0, barH);
    lv_obj_clear_flag(*bar, LV_OBJ_FLAG_CLICKABLE);
    etx_obj_add_style(*bar, styles->bg_opacity_cover, LV_PART_MAIN);
    etx_solid_bg(*bar, COLOR_THEME_FOCUS_INDEX);

    *tick = lv_obj_create(*box);
    lv_obj_set_pos(*tick, barW / 2, 0);
    lv_obj_set_size(*tick, TICK_W, barH);
    lv_obj_clear_flag(*tick, LV_OBJ_FLAG_CLICKABLE);
    etx_solid_bg(*tick, COLOR_THEME_PRIMARY1_INDEX);

    return y + barH + PAD_TINY;
  }

  void setBar(lv_obj_t* box, lv_obj_t* bar, uint8_t ch, int16_t* last)
  {
    if (!box || !bar || barW <= 0) return;

    int16_t v = channelOutputs[ch];
    if (v == *last) return;
    *last = v;

    coord_t half = barW / 2;
    if (v > RESX) v = RESX;
    if (v < -RESX) v = -RESX;

    coord_t len = (coord_t)((abs((int32_t)v) * half) / RESX);
    if (v >= 0) {
      lv_obj_set_pos(bar, half, 0);
    } else {
      lv_obj_set_pos(bar, half - len, 0);
    }
    lv_obj_set_size(bar, len, barH);
  }

  static LAYOUT_VAL_SCALED(MIN_BAR_H, 18)
  static LAYOUT_VAL_SCALED(MAX_BAR_H, 44)
  static LAYOUT_VAL_SCALED(TICK_W, 2)
};

BaseWidgetFactory<Nb4RacingWidget> nb4RacingWidget("NB4Racing", nullptr,
                                                   "Racing");

#endif  // RADIO_NB4_FAMILY
