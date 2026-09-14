/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>

#if defined(RADIO_NB4_FAMILY)

#define NB4_PIT_TIMER 1
#define NB4_PIT_MAX_MINUTES 120

bool nb4PitEnabled();
unsigned nb4PitMinutes();

void nb4PitConfigure(unsigned minutes);

int32_t nb4PitRemaining();
uint8_t nb4PitPercent();     // Remaining amount, 0..100
bool nb4PitRunning();

int nb4PitLapsLeft();

/* Refueling or replacing the battery restarts the count. */
void nb4PitRefuel();

void nb4PitPerMain();

#endif  // RADIO_NB4_FAMILY
