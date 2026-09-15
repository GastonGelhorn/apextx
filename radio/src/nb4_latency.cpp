/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_latency.h"

#if defined(RADIO_NB4_FAMILY)

#include "timers_driver.h"

namespace {

// Written by the mixer task and read by the interface, both on this core, and
// every field is a single aligned word. The interface can read a set of values
// from either side of one update; for a latency figure that is not worth a
// lock in the control path.
volatile uint32_t sampledAt = 0;
volatile bool carriesChannels = false;
volatile bool sampled = false;

volatile uint16_t lastUs = 0;
volatile uint16_t minUs = 0;
volatile uint16_t maxUs = 0;
volatile uint32_t frames = 0;
volatile uint32_t ignored = 0;

// The mean is kept as a running sum, renormalised before it can overflow, so
// the figure keeps following the radio instead of freezing after a long run.
volatile uint32_t sumUs = 0;
volatile uint32_t counted = 0;
constexpr uint32_t kRenormaliseAfter = 1u << 20;

uint32_t (*clock)() = timersGetUsTick;

}  // namespace

void nb4LatencySampled()
{
  sampledAt = clock();
  sampled = true;
  carriesChannels = false;
}

void nb4LatencyCarriesChannels()
{
  if (sampled) carriesChannels = true;
}

void nb4LatencySent()
{
  if (!sampled || !carriesChannels) return;
  carriesChannels = false;
  sampled = false;

  // Unsigned arithmetic, so a tick that wrapped between the two reads still
  // yields the elapsed count rather than a huge number.
  const uint32_t elapsed = clock() - sampledAt;
  if (elapsed > Nb4LatencyPlausibleUs) {
    ignored = ignored + 1;
    return;
  }

  const uint16_t us = (uint16_t)elapsed;
  lastUs = us;
  if (!frames || us < minUs) minUs = us;
  if (!frames || us > maxUs) maxUs = us;
  frames = frames + 1;

  if (counted >= kRenormaliseAfter) {
    sumUs = sumUs / 2;
    counted = counted / 2;
  }
  sumUs = sumUs + us;
  counted = counted + 1;
}

Nb4ControlLatency nb4LatencyRead()
{
  Nb4ControlLatency result{};
  result.lastUs = lastUs;
  result.minUs = minUs;
  result.maxUs = maxUs;
  result.frames = frames;
  result.ignored = ignored;
  const uint32_t n = counted;
  result.averageUs = n ? (uint16_t)(sumUs / n) : 0;
  return result;
}

void nb4LatencyReset()
{
  sampled = false;
  carriesChannels = false;
  lastUs = minUs = maxUs = 0;
  frames = ignored = 0;
  sumUs = counted = 0;
}

void nb4LatencySetClock(uint32_t (*source)())
{
  clock = source ? source : timersGetUsTick;
}

#endif  // RADIO_NB4_FAMILY
