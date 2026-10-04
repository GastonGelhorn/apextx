/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <stdint.h>

enum Nb4PhysicalTrim : uint8_t {
  NB4_ST_DOWN = 0,
  NB4_ST_UP,
  NB4_TH_DOWN = 2,
  NB4_TH_UP,
  NB4_T3_DOWN = 4,
  NB4_T3_UP,
  NB4_T4_DOWN = 6,
  NB4_T4_UP,
};

// Decode both four-way resistor ladders into EdgeTX's native trim-bit order.
// On production NB4 hardware TR1's front/back contacts are electrically the
// opposite way around to the original EdgeTX labels: W1 reduces steering and
// W3 increases it.  Keep the correction here so both normal trim handling and
// remapped physical-button actions see the same direction.
constexpr uint32_t nb4TrimLadderBits(uint16_t tr1, uint16_t tr2)
{
  uint32_t result = 0;

  if (tr1 <= 299)
    result |= 1u << NB4_ST_DOWN;
  else if (tr1 >= 801 && tr1 <= 1199)
    result |= 1u << NB4_T3_DOWN;
  else if (tr1 >= 1801 && tr1 <= 2199)
    result |= 1u << NB4_ST_UP;
  else if (tr1 >= 2801 && tr1 <= 3199)
    result |= 1u << NB4_T3_UP;

  if (tr2 <= 299)
    result |= 1u << NB4_TH_UP;
  else if (tr2 >= 801 && tr2 <= 1199)
    result |= 1u << NB4_T4_DOWN;
  else if (tr2 >= 1801 && tr2 <= 2199)
    result |= 1u << NB4_TH_DOWN;
  else if (tr2 >= 2801 && tr2 <= 3199)
    result |= 1u << NB4_T4_UP;

  return result;
}
