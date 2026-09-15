/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <stdint.h>

#if defined(RADIO_NB4_FAMILY)

// How long the radio takes between sampling the wheel and the trigger and
// handing the frame that carries those positions to the module's UART.
//
// This is the part of the control latency the firmware owns. It does not
// include the time the frame spends on the wire, nor the module, the radio
// link, the receiver or the servo, none of which the firmware can observe.
// docs/nb4/LATENCY.md explains what the number covers and what it does not.
//
// The cost of measuring is two timestamp reads per mixer cycle, so it is
// always compiled rather than hidden behind a debug build: a figure nobody
// can read on a released radio is a figure nobody checks.
struct Nb4ControlLatency {
  uint16_t lastUs;
  uint16_t minUs;
  uint16_t maxUs;
  uint16_t averageUs;
  uint32_t rawMaxUs; // Includes preemption/debugger outliers
  uint32_t frames;   // Cycles that carried channel positions
  uint32_t ignored;  // Cycles whose measurement was discarded as implausible
};

struct Nb4TouchLatency {
  uint32_t lastUs;
  uint32_t averageUs;
  uint32_t maxUs;
  uint32_t samples;
  uint32_t missedFrames;
};

// The mixer cycle has just read the controls. Starts the measurement.
void nb4LatencySampled();

// The frame queued in this cycle carries the positions read by the last
// nb4LatencySampled(). A cycle that sends configuration or polls status
// instead never reaches the timing statistics.
void nb4LatencyCarriesChannels();

// The buffer has been handed to the module port. Completes the measurement.
void nb4LatencySent();

Nb4ControlLatency nb4LatencyRead();
void nb4LatencyReset();

// Touch IRQ to the first subsequently presented frame. This measures the UI
// path on the radio; it deliberately does not run LVGL from the interrupt.
void nb4TouchLatencyPressed(uint32_t interruptAtUs);
void nb4TouchLatencyFrameQueued();
void nb4TouchLatencyPresented();
Nb4TouchLatency nb4TouchLatencyRead();

// A measurement longer than this is discarded rather than recorded: the radio
// was preempted, or a debugger stopped it, and one such outlier would sit in
// the maximum for the rest of the session. One mixer period is already far
// longer than the path being measured.
constexpr uint16_t Nb4LatencyPlausibleUs = 5000;
constexpr uint32_t Nb4TouchLatencyPlausibleUs = 250000;

// The radio reads the microsecond tick. Tests substitute a clock they drive,
// because the simulator's tick does not advance.
void nb4LatencySetClock(uint32_t (*clock)());

#endif  // RADIO_NB4_FAMILY
