/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>

#if defined(RADIO_NB4_FAMILY)

#define NB4_PALETTE_COLORS 13

struct Nb4Palette {
  const char* name;
  const char* infoEs;
  const char* infoEn;

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

#endif  // RADIO_NB4_FAMILY
