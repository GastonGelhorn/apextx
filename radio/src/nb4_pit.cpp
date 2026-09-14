/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
/*
 * Copyright (C) EdgeTX
 *
 * See nb4_pit.h.
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

#include "nb4_pit.h"
#include "nb4_racing.h"
#include "timers.h"

#if defined(HAPTIC)
#include "haptic.h"
#endif

bool nb4PitEnabled()
{
  return g_model.nb4Racing.pitEnabled &&
         g_model.timers[NB4_PIT_TIMER].start > 0;
}

unsigned nb4PitMinutes()
{
  if (!nb4PitEnabled()) return 0;
  return (g_model.timers[NB4_PIT_TIMER].start + 30) / 60;
}

void nb4PitConfigure(unsigned minutes)
{
  TimerData& t = g_model.timers[NB4_PIT_TIMER];

  if (minutes == 0) {

    if (g_model.nb4Racing.pitEnabled) {
      t.mode = TMRMODE_OFF;
      t.start = 0;
      t.countdownBeep = COUNTDOWN_SILENT;
      t.minuteBeep = 0;
    }
    g_model.nb4Racing.pitEnabled = 0;
  } else {
    if (minutes > NB4_PIT_MAX_MINUTES) minutes = NB4_PIT_MAX_MINUTES;
    const TimerData& race = g_model.timers[0];
    t.start = minutes * 60;

    t.mode = race.mode != TMRMODE_OFF ? race.mode : TMRMODE_THR_START;
    t.swtch = race.swtch;
    t.countdownBeep = COUNTDOWN_VOICE;  // Announce the final seconds
    t.minuteBeep = 1;
    t.persistent = race.persistent;     // Nitro timing survives a radio power cycle
    t.countdownStart = 0;
    t.showElapsed = 0;
    g_model.nb4Racing.pitEnabled = 1;
  }

  timerReset(NB4_PIT_TIMER);
  storageDirty(EE_MODEL);
}

int32_t nb4PitRemaining()
{
  return nb4PitEnabled() ? (int32_t)timersStates[NB4_PIT_TIMER].val : 0;
}

uint8_t nb4PitPercent()
{
  if (!nb4PitEnabled()) return 0;
  int32_t start = (int32_t)g_model.timers[NB4_PIT_TIMER].start;
  int32_t left = nb4PitRemaining();
  if (left <= 0) return 0;
  if (left >= start) return 100;
  return (uint8_t)((left * 100) / start);
}

bool nb4PitRunning()
{
  return nb4PitEnabled() && timersStates[NB4_PIT_TIMER].state == TMR_RUNNING;
}

int nb4PitLapsLeft()
{
  if (!nb4PitEnabled()) return -1;
  uint32_t average = nb4RacingRaceAverage();  // Hundredths of a second
  if (average == 0) return -1;
  int32_t left = nb4PitRemaining();
  if (left <= 0) return 0;
  return (int)(((uint32_t)left * 100) / average);
}

void nb4PitRefuel()
{
  if (!nb4PitEnabled()) return;
  timerReset(NB4_PIT_TIMER);
  AUDIO_KEY_PRESS();
#if defined(HAPTIC)
  haptic.play(15, 0, 0);
#endif
}

void nb4PitPerMain()
{

  static bool wasPositive = true;
  if (!nb4PitEnabled()) {
    wasPositive = true;
    return;
  }
  bool positive = nb4PitRemaining() > 0;
  if (wasPositive && !positive &&
      timersStates[NB4_PIT_TIMER].state == TMR_NEGATIVE) {
#if defined(HAPTIC)
    haptic.play(40, 10, 2);
#endif
  }
  wasPositive = positive;
}

#endif  // RADIO_NB4_FAMILY
