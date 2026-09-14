/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "hal/switch_driver.h"

#include "definitions.h"
#include "edgetx_constants.h"
#include "myeeprom.h"
#include "hal/adc_driver.h"
#include "timers_driver.h"

#include <stdlib.h>

#define NB4_SW3_MAX        299
#define NB4_SW_BOTH_MIN   1701
#define NB4_SW_BOTH_MAX   2299
#define NB4_SW2_MIN       3601

static const char _switch_names[][4] = { "SW2", "SW3" };

#define NB4_SW_SETTLE  4

static SwitchHwPos nb4SwitchDecode(uint8_t idx)
{

  if (getAnalogValue(8) == 0) return SWITCH_HW_UP;

  const uint16_t value = getAnalogValue(5);   // RAW2 = channel 10 = PC0
  const bool both = (value >= NB4_SW_BOTH_MIN && value <= NB4_SW_BOTH_MAX);

  if (idx == 0 && (value >= NB4_SW2_MIN || both)) {
    return SWITCH_HW_DOWN;
  }
  if (idx == 1 && (value <= NB4_SW3_MAX || both)) {
    return SWITCH_HW_DOWN;
  }
  return SWITCH_HW_UP;
}

SwitchHwPos boardSwitchGetPosition(uint8_t idx)
{
  if (idx > 1) return SWITCH_HW_UP;

  static SwitchHwPos reported[2] = { SWITCH_HW_UP, SWITCH_HW_UP };
  static SwitchHwPos pending[2]  = { SWITCH_HW_UP, SWITCH_HW_UP };
  static tmr10ms_t since[2]      = { 0, 0 };

  const SwitchHwPos raw = nb4SwitchDecode(idx);
  const tmr10ms_t now = get_tmr10ms();

  if (raw != pending[idx]) {
    pending[idx] = raw;
    since[idx] = now;
  } else if (raw != reported[idx] &&
             (tmr10ms_t)(now - since[idx]) >= NB4_SW_SETTLE) {
    reported[idx] = raw;
  }

  return reported[idx];
}

const char* boardSwitchGetName(uint8_t idx)
{
  return _switch_names[idx];
}
