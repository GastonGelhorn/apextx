/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
/*
 * Copyright (C) EdgeTX
 *
 * See nb4_home_templates.h.
 *
 * Compatibility readers for legacy visual fields and shared formatting.
 * ApexTX owns the single native Home.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "edgetx.h"

#if defined(RADIO_NB4_FAMILY)

#include "nb4_home_templates.h"
#include "nb4_home.h"
#include "nb4_model_compat.h"
#include "nb4_pit.h"
#include "nb4_racing.h"

#include "button.h"
#include "dialog.h"
#include "etx_lv_theme.h"
#include "output_edit.h"
#include "quick_menu.h"
#include "static.h"
#include "timer_setup.h"

#if defined(RTCLOCK)
#include "rtc.h"
#endif

namespace {

struct TemplateInfo {
  const char* es;
  const char* en;
  const char* infoEs;
  const char* infoEn;
  uint8_t slots;
  Nb4Metric defaults[NB4_TEMPLATE_SLOTS];
};

const TemplateInfo templates[NB4_HOME_COUNT] = {

    {"Cluster", "Cluster", "Reloj de gas con trim, escala de dirección y crono",
     "Throttle gauge with trim, steering scale and timer", 1, {NB4_METRIC_RACE_TIMER}},
    {"Esencial", "Essential", "Dos escalas y el crono, nada más",
     "Two scales and the timer, nothing else", 1, {NB4_METRIC_RACE_TIMER}},
    {"Home anterior", "Previous home", "La pantalla clásica",
     "The classic screen", 0, {}},
    {"Crono", "Chrono", "Vuelta en curso, última, mejor y delta",
     "Current lap, last, best and delta", 4,
     {NB4_METRIC_CURRENT_LAP, NB4_METRIC_LAST_LAP, NB4_METRIC_BEST_LAP,
      NB4_METRIC_DELTA}},
    {"Boxes", "Pit", "Depósito o pack, crono y vueltas que quedan",
     "Fuel or pack, timer and laps left", 4,
     {NB4_METRIC_PIT, NB4_METRIC_RACE_TIMER, NB4_METRIC_CURRENT_LAP,
      NB4_METRIC_LAPS_LEFT}},
    {"Telemetría", "Telemetry", "Pack, señal, receptor y temperatura",
     "Pack, signal, receiver and temperature", 4,
     {NB4_METRIC_PACK, NB4_METRIC_RSSI, NB4_METRIC_RX_BATTERY,
      NB4_METRIC_TEMPERATURE}},
    {"Mesa", "Bench", "Los ocho canales con su salida real, para ajustar parado",
     "All eight channels with their real output, for setting up", 0, {}},
};

struct MetricInfo {
  const char* es;
  const char* en;
  const char* titleEs;
  const char* titleEn;
};

const MetricInfo metrics[NB4_METRIC_COUNT] = {
    {"Por defecto", "Default", "", ""},
    {"Cronómetro", "Race timer", "CRONÓMETRO", "RACE TIMER"},
    {"Vuelta en curso", "Current lap", "VUELTA", "LAP"},
    {"Última vuelta", "Last lap", "ÚLTIMA", "LAST"},
    {"Mejor vuelta", "Best lap", "MEJOR", "BEST"},
    {"Delta", "Delta", "DELTA", "DELTA"},
    {"Vueltas", "Lap count", "VUELTAS", "LAPS"},
    {"Boxes", "Pit countdown", "BOXES", "PIT"},
    {"Vueltas restantes", "Laps left", "QUEDAN", "LAPS LEFT"},
    {"Batería TX", "TX battery", "BATERÍA TX", "TX BATTERY"},
    {"Batería RX", "RX battery", "RECEPTOR", "RECEIVER"},
    {"Pack", "Pack voltage", "PACK", "PACK"},
    {"Señal", "Signal", "SEÑAL", "SIGNAL"},
    {"Temperatura", "Temperature", "TEMPERATURA", "TEMPERATURE"},
    {"RPM", "RPM", "RPM", "RPM"},
    {"Hora", "Clock", "HORA", "TIME"},
    {"Trim dirección", "Steering trim", "TRIM ST", "TRIM ST"},
    {"Trim gas", "Throttle trim", "TRIM TH", "TRIM TH"},
};

}  // namespace

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

const char* nb4TemplateName(uint8_t design)
{
  if (design >= NB4_HOME_COUNT) design = NB4_HOME_INSTRUMENTS;
  return nb4Text(templates[design].es, templates[design].en);
}

const char* nb4TemplateInfo(uint8_t design)
{
  if (design >= NB4_HOME_COUNT) design = NB4_HOME_INSTRUMENTS;
  return nb4Text(templates[design].infoEs, templates[design].infoEn);
}

unsigned nb4TemplateSlots(uint8_t design)
{
  return design < NB4_HOME_COUNT ? templates[design].slots : 0;
}

Nb4Metric nb4TemplateDefaultMetric(uint8_t design, unsigned slot)
{
  if (design >= NB4_HOME_COUNT || slot >= templates[design].slots)
    return NB4_METRIC_RACE_TIMER;
  return templates[design].defaults[slot];
}

Nb4Metric nb4SlotMetric(uint8_t design, unsigned slot)
{
  if (design >= NB4_HOME_COUNT || slot >= NB4_TEMPLATE_SLOTS)
    return nb4TemplateDefaultMetric(design, slot);
  uint8_t chosen = g_eeGeneral.nb4Cards[design * NB4_TEMPLATE_SLOTS + slot];
  if (chosen == NB4_METRIC_DEFAULT || chosen >= NB4_METRIC_COUNT)
    return nb4TemplateDefaultMetric(design, slot);
  return (Nb4Metric)chosen;
}

void nb4SetSlotMetric(uint8_t design, unsigned slot, uint8_t metric)
{
  if (design >= NB4_HOME_COUNT || slot >= NB4_TEMPLATE_SLOTS) return;
  if (metric >= NB4_METRIC_COUNT) metric = NB4_METRIC_DEFAULT;
  g_eeGeneral.nb4Cards[design * NB4_TEMPLATE_SLOTS + slot] = metric;
  storageDirty(EE_GENERAL);
}

const char* nb4MetricName(uint8_t metric)
{
  if (metric >= NB4_METRIC_COUNT) metric = NB4_METRIC_DEFAULT;
  return nb4Text(metrics[metric].es, metrics[metric].en);
}

uint32_t nb4ThemeKey()
{
  return ((uint32_t)lcdColorTable[COLOR_THEME_FOCUS_INDEX] << 16) ^
         ((uint32_t)lcdColorTable[COLOR_THEME_ACTIVE_INDEX] << 8) ^
         (uint32_t)lcdColorTable[COLOR_THEME_PRIMARY2_INDEX] ^
         ((uint32_t)lcdColorTable[COLOR_THEME_PRIMARY3_INDEX] << 4);
}

// ---------------------------------------------------------------------------
// Additional readings
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

std::string nb4FormatReading(const Nb4Reading& r)
{
  if (r.validity == Nb4Validity::Absent) return "--";
  char buf[32];
  switch (r.unit) {
    case Nb4Unit::Millivolts:
      snprintf(buf, sizeof(buf), "%ld.%ld V", long(r.value / 1000),
               long(abs(r.value / 100) % 10));
      break;
    case Nb4Unit::Seconds:
      snprintf(buf, sizeof(buf), "%s%02ld:%02ld", r.value < 0 ? "-" : "",
               long(abs(r.value) / 60), long(abs(r.value) % 60));
      break;
    case Nb4Unit::Centiseconds:
      if (r.value == 0)
        strcpy(buf, "0.00");
      else
        nb4RacingFormatTime(buf, r.value);
      break;
    case Nb4Unit::Percent:
      snprintf(buf, sizeof(buf), "%ld%%", long(r.value));
      break;
    default:
      snprintf(buf, sizeof(buf), "%+ld", long(r.value));
      break;
  }
  std::string text = buf;
  if (r.validity == Nb4Validity::Stale) text += " ~";
  if (r.validity == Nb4Validity::Alarm) text += " !";
  return text;
}

StaticText* nb4Label(Window* parent, rect_t rect, const char* text,
                     LcdFlags font, LcdColorIndex color)
{
  auto t = new StaticText(parent, rect, text, color, font);
  lv_label_set_long_mode(t->getLvObj(), LV_LABEL_LONG_DOT);

  const coord_t line = getFontHeight(font);
  if (rect.h > line && rect.h <= 58)
    lv_obj_set_style_pad_top(t->getLvObj(), (rect.h - line) / 2, LV_PART_MAIN);
  return t;
}

StaticText* nb4Caption(Window* parent, rect_t rect, const char* text,
                       LcdFlags align)
{
  auto t = nb4Label(parent, rect, text, FONT(XS) | align,
                    COLOR_THEME_PRIMARY3_INDEX);
  lv_obj_set_style_text_letter_space(t->getLvObj(), 1, LV_PART_MAIN);
  return t;
}

Window* nb4Hairline(Window* parent, rect_t rect)
{
  auto w = new Window(parent, rect);
  etx_solid_bg(w->getLvObj(), COLOR_THEME_SECONDARY2_INDEX);
  return w;
}

Window* nb4Card(Window* parent, rect_t rect)
{
  auto w = new Window(parent, rect);
  auto o = w->getLvObj();
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(o, 9, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  etx_border_color(o, COLOR_THEME_SECONDARY2_INDEX);
  etx_solid_bg(o, COLOR_THEME_SECONDARY3_INDEX);
  return w;
}

#endif  // RADIO_NB4_FAMILY
