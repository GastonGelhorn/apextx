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
volatile uint32_t rawMaxUs = 0;
volatile uint32_t frames = 0;
volatile uint32_t ignored = 0;

// The mean is kept as a running sum, renormalised before it can overflow, so
// the figure keeps following the radio instead of freezing after a long run.
volatile uint32_t sumUs = 0;
volatile uint32_t counted = 0;
constexpr uint32_t kRenormaliseAfter = 1u << 20;

volatile bool touchPending = false;
volatile bool touchFramePending = false;
volatile uint32_t touchStartedAt = 0;
volatile uint32_t touchLastUs = 0;
volatile uint32_t touchMaxUs = 0;
volatile uint32_t touchAverageUs = 0;
volatile uint32_t touchSamples = 0;
volatile uint32_t touchMissedFrames = 0;

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
  if (elapsed > rawMaxUs) rawMaxUs = elapsed;
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
  result.rawMaxUs = rawMaxUs;
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
  rawMaxUs = 0;
  frames = ignored = 0;
  sumUs = counted = 0;
  touchPending = false;
  touchFramePending = false;
  touchStartedAt = touchLastUs = touchMaxUs = touchAverageUs = 0;
  touchSamples = touchMissedFrames = 0;
}

void nb4TouchLatencyPressed(uint32_t interruptAtUs)
{
  touchStartedAt = interruptAtUs;
  touchPending = true;
  touchFramePending = false;
}

void nb4TouchLatencyFrameQueued()
{
  if (touchPending) touchFramePending = true;
}

void nb4TouchLatencyPresented()
{
  // A panel acknowledgement may belong to a frame that was already in flight
  // when the user pressed. Only complete the sample after a newer UI frame was
  // actually queued with that press pending.
  if (!touchPending || !touchFramePending) return;
  touchPending = false;
  touchFramePending = false;
  const uint32_t elapsed = clock() - touchStartedAt;
  if (elapsed > Nb4TouchLatencyPlausibleUs) {
    ++touchMissedFrames;
    return;
  }
  touchLastUs = elapsed;
  if (elapsed > touchMaxUs) touchMaxUs = elapsed;
  // An exponential running mean cannot overflow and continues to follow the
  // unit after a long session. The first sample seeds it exactly.
  touchAverageUs = touchSamples ? (touchAverageUs * 7 + elapsed) / 8 : elapsed;
  ++touchSamples;
}

Nb4TouchLatency nb4TouchLatencyRead()
{
  return {touchLastUs, touchAverageUs, touchMaxUs, touchSamples,
          touchMissedFrames};
}

void nb4LatencySetClock(uint32_t (*source)())
{
  clock = source ? source : timersGetUsTick;
}

#endif  // RADIO_NB4_FAMILY
