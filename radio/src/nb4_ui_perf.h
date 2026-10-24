/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <stdint.h>

#if defined(RADIO_NB4_FAMILY)

// Interface timing on the radio, shown on the diagnostics page so a slow view
// can be measured instead of described. Every figure is in microseconds and
// covers the last one-second window in which at least one frame was drawn
// (an idle second leaves them unchanged), except the view figures, which
// keep the last and the slowest view change of the session.
//
// "View open" runs from the moment a view is pushed or popped (inside the tap
// handler, before the view is built) to the first frame presented after it:
// building, layout, rendering, copying and the wait for vertical blank.
struct Nb4UiPerf {
  uint32_t fps;            // Frames presented per second
  uint32_t loopAvgUs;      // UI task iteration: window pass + LVGL
  uint32_t loopMaxUs;
  uint32_t lvglAvgUs;      // LVGL timers, layout and rendering
  uint32_t lvglMaxUs;
  uint32_t windowAvgUs;    // Window tree checkEvents pass
  uint32_t copyAvgUs;      // Frame copies in the flush, per presented frame
  uint32_t waitAvgUs;      // Time the UI waited for vertical blank, per frame
  uint32_t viewLastUs;     // Last view open or close
  uint32_t viewMaxUs;
  uint32_t views;          // View changes measured
};

void nb4UiPerfLoop(uint32_t windowUs, uint32_t lvglUs);
void nb4UiPerfCopy(uint32_t us);
void nb4UiPerfWait(uint32_t us);
void nb4UiPerfFrameQueued();
void nb4UiPerfPresented();
void nb4UiPerfViewChanged();
// Layer changes while quiet are not views the user opened (cache warm-up).
void nb4UiPerfQuiet(bool quiet);

Nb4UiPerf nb4UiPerfRead();
void nb4UiPerfReset();

uint32_t nb4UiPerfNowUs();

#endif  // RADIO_NB4_FAMILY
