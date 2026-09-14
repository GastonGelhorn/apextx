/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stdint.h>

#if defined(RADIO_NB4_FAMILY)

enum Nb4LedMode : uint8_t {
  NB4_LED_OFF = 0,      // Off
  NB4_LED_FIXED,        // Fixed color
  NB4_LED_BREATHE,      // Slowly pulsing color
  NB4_LED_BATTERY,      // Battery state selects green, amber, or red
  NB4_LED_MODE_COUNT,
};

enum Nb4LedColor : uint8_t {
  NB4_LED_WHITE = 0,
  NB4_LED_RED,
  NB4_LED_ORANGE,
  NB4_LED_YELLOW,
  NB4_LED_GREEN,
  NB4_LED_CYAN,
  NB4_LED_BLUE,
  NB4_LED_MAGENTA,
  NB4_LED_COLOR_COUNT,
};

struct Nb4LedRgb {
  uint8_t r = 0, g = 0, b = 0;
  bool operator==(const Nb4LedRgb& o) const
  {
    return r == o.r && g == o.g && b == o.b;
  }
};

Nb4LedRgb nb4LedPalette(uint8_t color);

Nb4LedRgb nb4LedColorNow(uint8_t mode, uint8_t color, bool charging,
                         uint8_t batteryPercent, uint32_t ms);

void nb4LedRefresh();

#endif  // RADIO_NB4_FAMILY
