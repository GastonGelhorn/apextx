/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_leds.h"

#if defined(LED_STRIP_GPIO)
#include "boards/generic_stm32/rgb_leds.h"

#if defined(SIMU)

extern uint8_t nb4SimuChargeSource;
static bool isChargerActive() { return nb4SimuChargeSource != 0; }
#else
extern bool isChargerActive();
#endif
#endif

namespace {

const Nb4LedRgb kPalette[NB4_LED_COLOR_COUNT] = {
    {255, 255, 255},  // White
    {255, 0, 0},      // Red
    {255, 90, 0},     // Orange
    {255, 200, 0},    // Yellow
    {0, 255, 0},      // Green
    {0, 255, 255},    // Cyan
    {0, 0, 255},      // Blue
    {255, 0, 255},    // Magenta
};

Nb4LedRgb scale(Nb4LedRgb c, uint32_t numerator, uint32_t denominator)
{
  if (!denominator) return {0, 0, 0};
  return {(uint8_t)((uint32_t)c.r * numerator / denominator),
          (uint8_t)((uint32_t)c.g * numerator / denominator),
          (uint8_t)((uint32_t)c.b * numerator / denominator)};
}

uint32_t breathLevel(uint32_t ms)
{
  const uint32_t period = 2400;
  const uint32_t half = period / 2;
  const uint32_t t = ms % period;
  const uint32_t up = t < half ? t : period - t;   // 0..half
  return 15 + up * 85 / half;                      // 15..100
}

}  // namespace

Nb4LedRgb nb4LedPalette(uint8_t color)
{
  if (color >= NB4_LED_COLOR_COUNT) return kPalette[NB4_LED_WHITE];
  return kPalette[color];
}

Nb4LedRgb nb4LedColorNow(uint8_t mode, uint8_t color, bool charging,
                         uint8_t batteryPercent, uint32_t ms)
{

  if (mode == NB4_LED_OFF) return {0, 0, 0};

  if (charging) {
    const Nb4LedRgb green = kPalette[NB4_LED_GREEN];
    if (batteryPercent >= 99) return green;
    return scale(green, breathLevel(ms), 100);
  }

  if (mode == NB4_LED_BATTERY) {

    const Nb4LedRgb base = batteryPercent <= 15  ? kPalette[NB4_LED_RED]
                           : batteryPercent <= 35 ? kPalette[NB4_LED_ORANGE]
                                                  : kPalette[NB4_LED_GREEN];

    if (batteryPercent <= 15) return scale(base, breathLevel(ms), 100);
    return base;
  }

  const Nb4LedRgb base = nb4LedPalette(color);
  if (mode == NB4_LED_BREATHE) return scale(base, breathLevel(ms), 100);
  return base;
}

#if defined(LED_STRIP_GPIO)

void rgbLedOnUpdate() { nb4LedRefresh(); }
#endif

void nb4LedRefresh()
{
#if defined(LED_STRIP_GPIO)

  extern uint8_t nb4BatteryPercent(uint16_t cellMv);
  const uint8_t percent = nb4BatteryPercent((uint16_t)(g_vbat100mV * 10));

  const Nb4LedRgb c =
      nb4LedColorNow(g_eeGeneral.nb4LedMode, g_eeGeneral.nb4LedColor,
                     isChargerActive(), percent, timersGetMsTick());

  for (uint8_t i = 0; i < LED_STRIP_LENGTH; i += 1)
    rgbSetLedColor(i, c.r, c.g, c.b);
  rgbLedColorApply();
#endif
}

#endif  // RADIO_NB4_FAMILY
