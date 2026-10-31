/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_leds.h"
#include "nb4_led_diagnostic.h"

#if defined(LED_STRIP_GPIO)
#include "boards/generic_stm32/rgb_leds.h"

#if defined(SIMU)

extern uint8_t nb4SimuChargeSource;
static bool nb4LedChargerActive() { return nb4SimuChargeSource != 0; }
#elif defined(RADIO_NB4)
// Match the home screen's live charger pins instead of a boot-time latch.
static bool nb4LedChargerActive() { return nb4ChargeSource() != 0; }
#else
extern bool isChargerActive();
static bool nb4LedChargerActive() { return isChargerActive(); }
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

uint8_t nb4LedBrightnessOrDefault(uint8_t stored)
{
  if (stored < NB4_LED_BRIGHTNESS_MIN || stored > NB4_LED_BRIGHTNESS_MAX)
    return NB4_LED_BRIGHTNESS_DEFAULT;
  return stored;
}

static Nb4LedRgb nb4LedColorFull(uint8_t mode, uint8_t color, bool charging,
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

Nb4LedRgb nb4LedColorNow(uint8_t mode, uint8_t color, bool charging,
                         uint8_t batteryPercent, uint32_t ms,
                         uint8_t brightness)
{
  const Nb4LedRgb c = nb4LedColorFull(mode, color, charging, batteryPercent, ms);
  return scale(c, nb4LedBrightnessOrDefault(brightness), 100);
}

enum Nb4BootStage : uint8_t { NB4_BOOT_BLINK, NB4_BOOT_SPLASH, NB4_BOOT_DONE };
static Nb4BootStage _bootStage = NB4_BOOT_BLINK;

static const Nb4LedRgb kBootBlue = {0, 0, 50};

Nb4LedRgb nb4LedBootColor(uint32_t ms)
{
  // 4 Hz: 125 ms blue, 125 ms off.
  if ((ms / 125) & 1) return {0, 0, 0};
  return kBootBlue;
}

void nb4LedBootSplash()
{
  if (_bootStage == NB4_BOOT_BLINK) _bootStage = NB4_BOOT_SPLASH;
}

void nb4LedBootComplete() { _bootStage = NB4_BOOT_DONE; }

bool nb4LedBootIsComplete() { return _bootStage == NB4_BOOT_DONE; }

void nb4LedBootRefresh(uint32_t ms)
{
#if defined(LED_STRIP_GPIO)
  const Nb4LedRgb c =
      _bootStage == NB4_BOOT_SPLASH ? kBootBlue : nb4LedBootColor(ms);
  for (uint8_t i = 0; i < LED_STRIP_LENGTH; i += 1)
    rgbSetLedColor(i, c.r, c.g, c.b);
  rgbLedColorApply();
#else
  (void)ms;
#endif
}

#if defined(LED_STRIP_GPIO)

void rgbLedOnUpdate() { nb4LedRefresh(); }
#endif

void nb4LedRefresh()
{
#if defined(LED_STRIP_GPIO)

#if defined(NB4_LED_DIAGNOSTIC) && !defined(BOOT)
  if (nb4LedDiagWritePattern()) return;
#endif

  if (_bootStage != NB4_BOOT_DONE) {
    nb4LedBootRefresh(timersGetMsTick());
    return;
  }

  extern uint8_t nb4BatteryPercent(uint16_t cellMv);
  // The battery curve expects mV; the measured voltage is in 100 mV units.
  const uint8_t percent = nb4BatteryPercent((uint16_t)(g_vbat100mV * 100));

  const Nb4LedRgb c =
      nb4LedColorNow(g_eeGeneral.nb4LedMode, g_eeGeneral.nb4LedColor,
                     nb4LedChargerActive(), percent, timersGetMsTick(),
                     g_eeGeneral.nb4LedBrightness);

  for (uint8_t i = 0; i < LED_STRIP_LENGTH; i += 1)
    rgbSetLedColor(i, c.r, c.g, c.b);
  rgbLedColorApply();
#endif
}

#endif  // RADIO_NB4_FAMILY
