/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include "window.h"
#include "nb4_car_state.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_home_templates.h"
#include "button.h"
#include "static.h"

namespace Nb4Ui {
constexpr coord_t Margin = 8, Gap = 6, Touch = 44;
lv_color_t color(LcdColorIndex index);
lv_color_t throttleColor();
lv_color_t steeringColor();

lv_color_t brakeColor();

void panel(lv_obj_t* object);

void card(lv_obj_t* object);

void well(lv_obj_t* object);
/* Outlined pill without a fill. */
void chip(lv_obj_t* object);

void passThrough(lv_obj_t* root);
void button(lv_obj_t* object, bool primary = false);
TextButton* action(Window*, rect_t, const char*, std::function<void()>, bool primary = false);
void header(Window*, const char*, std::function<void()>);

void inkIfChanged(StaticText* label, LcdColorIndex& last, LcdColorIndex want);
const char* phaseText();
std::string timeText(uint32_t cs, bool available = true);
std::string timerText(int32_t seconds, bool available = true);
}

class Nb4Dial : public Window
{
 public:
  Nb4Dial(Window* parent, rect_t rect, bool landscape);
  void checkEvents() override;

 private:
  coord_t cx = 0, cy = 0, radius = 0, needleLen = 0;
  coord_t needleHub = 0, needleOx = 0, needleOy = 0;
  coord_t digitsCx = 0, digitsTop = 0, ruleX0 = 0, ruleSpan = 0, wedgeY = 0;
  lv_obj_t *track = nullptr, *arc = nullptr, *needle = nullptr, *wedge = nullptr;

  lv_point_t needlePts[2] = {};
  lv_point_t tickPts[40][2] = {};
  StaticText *digits = nullptr, *pct = nullptr, *chipVal = nullptr;
  int16_t arcStart = -1, arcEnd = -1;
  int16_t tipX = INT16_MIN, tipY = INT16_MIN;
  int16_t lastPct = INT16_MIN;
  int16_t lastWedgeX = INT16_MIN;
  int32_t lastTrim = INT32_MIN;
  uint8_t lastDw = 0;
  bool lastTrimOk = false;
  bool wedgeHidden = false;
};

class Nb4Column : public Window
{
 public:
  Nb4Column(Window* parent, rect_t rect, bool landscape);
  void checkEvents() override;

 private:
  coord_t axisY = 0, semi = 0, fillX = 0, fillW = 0, trimAxisY = 0;
  coord_t markerX = 0, markerW = 0, triX = 0, digitsCx = 0, digitsTop = 0;
  lv_obj_t *gas = nullptr, *brake = nullptr, *marker = nullptr, *tri = nullptr;
  StaticText *digits = nullptr, *pct = nullptr, *chipVal = nullptr;
  lv_color_t gasInk, brakeInk;
  int16_t lastPx = INT16_MIN, lastPct = INT16_MIN, lastTriY = INT16_MIN;
  int32_t lastTrim = INT32_MIN;
  uint8_t lastDw = 0;
  int8_t lastSign = 2;
  bool lastTrimOk = false;
  bool triHidden = false;
};

class Nb4Telltale : public Window
{
 public:
  enum Kind : uint8_t { Link = 0, Transmitter = 1, Receiver = 2 };
  Nb4Telltale(Window* parent, rect_t rect, uint8_t kind, bool boxed);
  void refresh(const Nb4CarState& state);

 private:
  static void drawBatteryIcon(lv_event_t* event);
  static void drawSegmentStrip(lv_event_t* event);
  void setBatteryIcon(uint8_t level, LcdColorIndex tint);
  void setSegments(uint8_t count, LcdColorIndex tint);

  uint8_t kind;
  StaticText *label = nullptr, *value = nullptr;
  lv_obj_t *batteryIcon = nullptr, *segmentStrip = nullptr, *chargePill = nullptr;
  lv_obj_t* bars[4] = {};
  uint8_t segCount = 0xff;
  LcdColorIndex segInk = COLOR_THEME_EDIT_INDEX;
  LcdColorIndex valueInk = COLOR_THEME_PRIMARY1_INDEX;
  LcdColorIndex labelInk = COLOR_THEME_PRIMARY3_INDEX;
  LcdColorIndex barInk = COLOR_THEME_EDIT_INDEX;
  LcdColorIndex iconInk = COLOR_THEME_PRIMARY3_INDEX;
  uint8_t barCount = 0xff;
  uint8_t battLevel = 0xff;
  uint8_t chargeShown = 0xff;
};

class Nb4Chrono : public Window
{
 public:
  Nb4Chrono(Window* parent, rect_t rect, const Nb4CarState& state, bool landscape);
  void refresh(const Nb4CarState& state);

 private:
  bool raceMode = false;
  StaticText *timeLabel = nullptr, *stateLabel = nullptr;
  lv_obj_t* pill = nullptr;
  LcdColorIndex timeInk = COLOR_THEME_PRIMARY1_INDEX;
  LcdColorIndex stateInk = COLOR_THEME_EDIT_INDEX;
  LcdColorIndex pillInk = COLOR_THEME_EDIT_INDEX;
  uint8_t lastState = 0xff;
  int32_t lastTime = INT32_MIN;
};

class Nb4Stats : public Window
{
 public:
  Nb4Stats(Window* parent, rect_t rect, bool landscape);
  void refresh(const Nb4CarState& state);

 private:
  StaticText* values[4] = {};
  uint32_t last[4] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
};

class Nb4RacePanel : public Window {
 public:
  Nb4RacePanel(Window*, rect_t, bool race = true);
  void refresh(const Nb4CarState&);
 private:
  bool raceMode;
  StaticText *title = nullptr, *timer = nullptr, *phase = nullptr;
  StaticText *laps = nullptr, *last = nullptr, *best = nullptr, *delta = nullptr;
  StaticText *timerInfo = nullptr;
  LcdColorIndex deltaInk = COLOR_THEME_PRIMARY1_INDEX;
  LcdColorIndex timerInk = COLOR_THEME_PRIMARY1_INDEX;
  lv_obj_t* progress;
};
#endif
