/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
/*
 * Copyright (C) EdgeTX
 *
 * See nb4_palettes.h.
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

#include "nb4_palettes.h"
#include "colors.h"

static const Nb4Palette palettes[] = {

    {"ApexTX Dark", NB4_STR(NIGHT_AND_GARAGE), true,
     {0xF5F5F5, 0x080808, 0xA8A8A8, 0xE0E0E0, 0x181818, 0x000000, 0xFF8C1A,
      0x35D07A, 0x9A5410, 0xFF5A4E, 0x808080, 0x000000, 0xF5F5F5}},
    {"ApexTX Light", NB4_STR(DAYLIGHT), false,
     {0x101A22, 0xFFFFFF, 0x5A6B78, 0x0B4F82, 0xBACFE0, 0xEDF2F5, 0x0069C0,
      0x00792E, 0xFFC400, 0xC81E1E, 0x76838D, 0x101A22, 0xFFFFFF}},
    {"ApexTX Sun", NB4_STR(DIRECT_SUN_MAXIMUM_CONTRAST), false,
     {0x000000, 0xFFFFFF, 0x3A3A3A, 0x000000, 0xAFAFAF, 0xF0F0F0, 0x0033CC,
      0x006B00, 0xE8A000, 0xC00000, 0x5E5E5E, 0x000000, 0xFFFFFF}},
    {"ApexTX Red", NB4_STR(NIGHT_RED_PRESERVES_NIGHT_VISION), true,
     {0xFF7A6B, 0x1F1011, 0xD26D5E, 0xFFA79B, 0x502923, 0x060202, 0xFF4A3D,
      0xFF9A8D, 0x5A1611, 0xFFB0A4, 0x8A4A42, 0x000000, 0xFF7A6B}},
    {"ApexTX Green", NB4_STR(TRACK_GREEN), true,
     {0xE4F3E8, 0x0E1A13, 0x86B295, 0xC9E6D3, 0x274532, 0x040A07, 0x2ECC71,
      0x5FE39A, 0x18703E, 0xFF6B5E, 0x6E8F7A, 0x000000, 0xE4F3E8}},
    {"ApexTX Orange", NB4_STR(NOBLE_ORANGE), true,
     {0xF5F2EF, 0x1A1510, 0xA79C93, 0xE6DED6, 0x3E362E, 0x060505, 0xFF7A00,
      0x4FD37A, 0xA04E00, 0xFF6B4A, 0x8C837B, 0x000000, 0xF5F2EF}},
    {"ApexTX Mono", NB4_STR(BLACK_AND_WHITE), true,
     {0xF5F5F5, 0x121212, 0x9A9A9A, 0xE0E0E0, 0x333333, 0x000000, 0xFFFFFF,
      0xBEBEBE, 0x555555, 0xFF6060, 0x777777, 0x000000, 0xF5F5F5}},
};

unsigned nb4PaletteCount() { return DIM(palettes); }

const Nb4Palette& nb4Palette(unsigned index)
{
  return palettes[index < DIM(palettes) ? index : 0];
}

int nb4PaletteIndexByName(const char* name)
{
  if (!name) return -1;
  for (unsigned i = 0; i < DIM(palettes); i++)
    if (strcmp(palettes[i].name, name) == 0) return (int)i;

  // Resolve names written by development builds before palette identifiers
  // were standardized in English. The caller can then store the canonical
  // name from nb4Palette(index).name without changing the selected colors.
  static const struct {
    const char* legacyName;
    unsigned paletteIndex;
  } legacyNames[] = {
      {"NB4 Dark", 0},   {"NB4 Light", 1}, {"NB4 Sun", 2},
      {"NB4 Red", 3},    {"NB4 Green", 4}, {"NB4 Orange", 5},
      {"NB4 Mono", 6},
      {"NB4 Noche", 0},  {"NB4 Día", 1},     {"NB4 Dia", 1},
      {"NB4 Sol", 2},    {"NB4 Rojo", 3},    {"NB4 Verde", 4},
      {"NB4 Naranja", 5},
  };
  for (const auto& legacy : legacyNames)
    if (strcmp(legacy.legacyName, name) == 0) return (int)legacy.paletteIndex;
  return -1;
}

static const struct {
  Nb4Str name;
  uint32_t rgb;
} accents[] = {
    {NB4_STR(PALETTE_ACCENT), 0},
    {NB4_STR(BLUE), 0x2E9BD8},
    {NB4_STR(ORANGE), 0xFF7A00},
    {NB4_STR(GREEN), 0x2ECC71},
    {NB4_STR(RED), 0xE5393B},
    {NB4_STR(YELLOW), 0xFFC400},
    {NB4_STR(CYAN), 0x00BCD4},
    {NB4_STR(MAGENTA), 0xD81B60},
    {NB4_STR(WHITE), 0xF0F0F0},
};

unsigned nb4AccentCount() { return DIM(accents); }

const char* nb4AccentName(unsigned index)
{
  if (index >= DIM(accents)) index = 0;
  return accents[index].name();
}

uint32_t nb4AccentRgb(unsigned index)
{
  return index < DIM(accents) ? accents[index].rgb : 0;
}

void nb4ApplyAccent()
{
  unsigned index = g_eeGeneral.nb4Accent;
  if (index == 0 || index >= DIM(accents)) return;
  uint32_t c = accents[index].rgb;
  lcdColorTable[COLOR_THEME_FOCUS_INDEX] =
      RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

#endif  // RADIO_NB4_FAMILY
