/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>

#include "nb4_i18n.h"

#if defined(RADIO_NB4_FAMILY)

#define NB4_PALETTE_COLORS 13

struct Nb4Palette {
  const char* name;  // Stored in the settings; never translated.
  Nb4Str label;      // What the Appearance page shows.
  Nb4Str info;

  bool dark;
  uint32_t colors[NB4_PALETTE_COLORS];  // 0xRRGGBB
};

unsigned nb4PaletteCount();
const Nb4Palette& nb4Palette(unsigned index);

int nb4PaletteIndexByName(const char* name);

unsigned nb4AccentCount();
const char* nb4AccentName(unsigned index);
uint32_t nb4AccentRgb(unsigned index);

void nb4ApplyAccent();

// Optional "nb4:" section of an external theme.yml. Instrument colours are
// 0x01RRGGBB when the theme sets them and 0 when the built-in colour applies.
#define NB4_THEME_COLOR_SET 0x01000000u

struct Nb4ThemeExt {
  uint32_t steering = 0;
  uint32_t throttle = 0;
  uint32_t brake = 0;
  uint32_t aux = 0;
  uint8_t cardOpacity = 100;  // percent, only used over a background image
};

void nb4SetThemeExt(const Nb4ThemeExt& ext);
const Nb4ThemeExt& nb4ThemeExt();

#endif  // RADIO_NB4_FAMILY
