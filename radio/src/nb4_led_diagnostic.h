/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stdint.h>

#if defined(NB4_LED_DIAGNOSTIC) && !defined(BOOT)
// Bench-only, opt-in; never changes stored radio/model preferences.
constexpr uint32_t NB4_LED_DIAG_PHASE_MS = 6000;
constexpr unsigned NB4_LED_DIAG_PHASES = 3;
struct Nb4LedDiagStats {
  uint32_t sent = 0, complete = 0;
  uint32_t transferErrors = 0, directErrors = 0, fifoErrors = 0;
  uint32_t timeouts = 0, lastFlags = 0, lastRemaining = 0;
};

// quiet: while the test runs, the internal RF module is powered down, the
// audio amplifier is muted and the DAC, its DMA and trigger timer are stopped,
// the haptic motor is off, ADC sampling, touch-panel polling and the storage
// task are paused, so an electrical cause outside the LED driver can be told
// apart from the waveform. Everything resumes when the test ends.
void nb4LedDiagStart(uint32_t now, uint8_t intensity, bool quiet = false);
void nb4LedDiagTick(uint32_t now);  // UI task, including while scanout is stopped
void nb4LedDiagStop();             // restores scanout even when cancelled
bool nb4LedDiagActive();
bool nb4LedDiagQuiet();            // active and quiet: module off, ADC paused
bool nb4LedDiagStoragePaused();    // quiet and the storage task accepted the pause
unsigned nb4LedDiagPhase();
bool nb4LedDiagWritePattern();     // sole LED timer writer while active
Nb4LedDiagStats nb4LedDiagResult(unsigned phase);
Nb4LedDiagStats nb4LedDiagDmaStats();
uintptr_t nb4LedDiagDmaBuffer();
void nb4LedDiagPauseLcd(bool pause); // pause LTDC only; backlight stays unchanged
bool nb4LedDiagLcdPaused();
#endif
