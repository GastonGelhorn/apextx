/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include "window.h"
#include "colors.h"
#include "nb4_car_state.h"

#if defined(RADIO_NB4_FAMILY)

#include <string>
#include <vector>

class StaticText;

enum Nb4Metric : uint8_t {
  NB4_METRIC_DEFAULT = 0,
  NB4_METRIC_RACE_TIMER,
  NB4_METRIC_CURRENT_LAP,
  NB4_METRIC_LAST_LAP,
  NB4_METRIC_BEST_LAP,
  NB4_METRIC_DELTA,
  NB4_METRIC_LAP_COUNT,
  NB4_METRIC_PIT,
  NB4_METRIC_LAPS_LEFT,
  NB4_METRIC_TX_BATTERY,
  NB4_METRIC_RX_BATTERY,
  NB4_METRIC_PACK,
  NB4_METRIC_RSSI,
  NB4_METRIC_TEMPERATURE,
  NB4_METRIC_RPM,
  NB4_METRIC_CLOCK,
  NB4_METRIC_STEER_TRIM,
  NB4_METRIC_THR_TRIM,
  NB4_METRIC_COUNT
};

#define NB4_TEMPLATE_SLOTS 4

const char* nb4TemplateName(uint8_t design);
const char* nb4TemplateInfo(uint8_t design);
unsigned nb4TemplateSlots(uint8_t design);
Nb4Metric nb4TemplateDefaultMetric(uint8_t design, unsigned slot);

Nb4Metric nb4SlotMetric(uint8_t design, unsigned slot);
void nb4SetSlotMetric(uint8_t design, unsigned slot, uint8_t metric);

const char* nb4MetricName(uint8_t metric);

struct Nb4Value {
  int32_t value = 0;
  bool valid = false;
  bool stale = false;
};

std::string nb4FormatReading(const Nb4Reading& r);
StaticText* nb4Label(Window* parent, rect_t rect, const char* text,
                     LcdFlags font = FONT(STD), LcdColorIndex color = COLOR_THEME_PRIMARY1_INDEX);

StaticText* nb4Caption(Window* parent, rect_t rect, const char* text,
                       LcdFlags align = 0);
/* One-pixel separator between regions. */
Window* nb4Hairline(Window* parent, rect_t rect);
Window* nb4Card(Window* parent, rect_t rect);

uint32_t nb4ThemeKey();

#endif  // RADIO_NB4_FAMILY
