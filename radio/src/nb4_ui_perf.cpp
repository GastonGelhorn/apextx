/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_ui_perf.h"

#if defined(RADIO_NB4_FAMILY)

#include "timers_driver.h"

namespace {

// Everything below is written by the interface task, except the present hook,
// which runs in the LTDC interrupt and only touches the presented/view words.
// Each is a single aligned word; a torn read costs one diagnostic sample.
constexpr uint32_t kWindowUs = 1000000;

struct Window {
  uint32_t startedAt = 0;
  uint32_t loops = 0;
  uint64_t loopSum = 0;
  uint32_t loopMax = 0;
  uint64_t lvglSum = 0;
  uint32_t lvglMax = 0;
  uint64_t windowSum = 0;
  uint64_t copySum = 0;
  uint64_t waitSum = 0;
};

Window current;
volatile uint32_t presented = 0;
Nb4UiPerf published = {};

volatile bool viewPending = false;
volatile bool viewFrameQueued = false;
volatile uint32_t viewStartedAt = 0;
volatile uint32_t viewLastUs = 0;
volatile uint32_t viewMaxUs = 0;
volatile uint32_t views = 0;

void publishIfDue(uint32_t now)
{
  const uint32_t elapsed = now - current.startedAt;
  if (elapsed < kWindowUs) return;
  const uint32_t frames = presented;
  presented = 0;
  // An idle second (nothing redrawn) would overwrite the figures with zeros
  // before anyone can read them; keep the last second that drew something.
  if (frames == 0) {
    current = Window();
    current.startedAt = now;
    return;
  }
  Nb4UiPerf next = published;
  next.fps = elapsed ? (uint32_t)((uint64_t)frames * 1000000u / elapsed) : 0;
  if (current.loops) {
    next.loopAvgUs = (uint32_t)(current.loopSum / current.loops);
    next.lvglAvgUs = (uint32_t)(current.lvglSum / current.loops);
    next.windowAvgUs = (uint32_t)(current.windowSum / current.loops);
  }
  next.loopMaxUs = current.loopMax;
  next.lvglMaxUs = current.lvglMax;
  next.copyAvgUs = frames ? (uint32_t)(current.copySum / frames) : 0;
  next.waitAvgUs = frames ? (uint32_t)(current.waitSum / frames) : 0;
  published = next;
  current = Window();
  current.startedAt = now;
}

}  // namespace

uint32_t nb4UiPerfNowUs() { return timersGetUsTick(); }

void nb4UiPerfLoop(uint32_t windowUs, uint32_t lvglUs)
{
  const uint32_t now = nb4UiPerfNowUs();
  const uint32_t loop = windowUs + lvglUs;
  ++current.loops;
  current.loopSum += loop;
  current.lvglSum += lvglUs;
  current.windowSum += windowUs;
  if (loop > current.loopMax) current.loopMax = loop;
  if (lvglUs > current.lvglMax) current.lvglMax = lvglUs;
  publishIfDue(now);
}

void nb4UiPerfCopy(uint32_t us) { current.copySum += us; }

void nb4UiPerfWait(uint32_t us) { current.waitSum += us; }

void nb4UiPerfFrameQueued()
{
  if (viewPending) viewFrameQueued = true;
}

void nb4UiPerfPresented()
{
  ++presented;
  // Only a frame queued after the view change shows the new view.
  if (!viewPending || !viewFrameQueued) return;
  viewPending = false;
  viewFrameQueued = false;
  const uint32_t elapsed = nb4UiPerfNowUs() - viewStartedAt;
  viewLastUs = elapsed;
  if (elapsed > viewMaxUs) viewMaxUs = elapsed;
  ++views;
}

void nb4UiPerfViewChanged()
{
  // A tap can push several layers (a page and its first dialog); time the
  // whole change from the first one.
  if (viewPending) return;
  viewStartedAt = nb4UiPerfNowUs();
  viewFrameQueued = false;
  viewPending = true;
}

Nb4UiPerf nb4UiPerfRead()
{
  Nb4UiPerf result = published;
  result.viewLastUs = viewLastUs;
  result.viewMaxUs = viewMaxUs;
  result.views = views;
  return result;
}

void nb4UiPerfReset()
{
  current = Window();
  current.startedAt = nb4UiPerfNowUs();
  presented = 0;
  published = {};
  viewPending = viewFrameQueued = false;
  viewLastUs = viewMaxUs = views = 0;
}

#endif  // RADIO_NB4_FAMILY
