/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#include "nb4_led_diagnostic.h"

#if defined(NB4_LED_DIAGNOSTIC) && !defined(BOOT)
#include "boards/generic_stm32/rgb_leds.h"
#if !defined(SIMU)
#include "hal/gpio.h"
#include "stm32_gpio.h"
#endif
#include <atomic>

namespace {
std::atomic<bool> active{false};
std::atomic<bool> quiet{false};
std::atomic<uint8_t> level{128};
unsigned phase = NB4_LED_DIAG_PHASES;
uint32_t phaseStart = 0;
Nb4LedDiagStats baseline;
Nb4LedDiagStats results[NB4_LED_DIAG_PHASES];

Nb4LedDiagStats delta(const Nb4LedDiagStats& now)
{
  return {now.sent - baseline.sent, now.complete - baseline.complete,
          now.transferErrors - baseline.transferErrors,
          now.directErrors - baseline.directErrors,
          now.fifoErrors - baseline.fifoErrors,
          now.timeouts - baseline.timeouts, now.lastFlags, now.lastRemaining};
}

#if defined(SIMU)
void applyQuiet(bool) {}
#else
// The RF module is switched off by the mixer: getRequiredProtocol() reports
// no protocol while nb4LedDiagQuiet() holds, which de-initialises the module
// and drops its power through the module port. ADC sampling is skipped in
// adcRead() for the same reason. Here only the amplifier and the haptic motor
// are touched; the amplifier pin is put back exactly as it was found.
uint32_t savedMutePin = 0;

void applyQuiet(bool on)
{
#if defined(AUDIO_MUTE_GPIO)
  if (on) {
    savedMutePin = gpio_read(AUDIO_MUTE_GPIO);
#if defined(INVERTED_MUTE_PIN)
    gpio_clear(AUDIO_MUTE_GPIO);
#else
    gpio_set(AUDIO_MUTE_GPIO);
#endif
  } else {
    gpio_write(AUDIO_MUTE_GPIO, savedMutePin ? 1 : 0);
  }
#endif
#if defined(HAPTIC)
  if (on) hapticOff();
#endif
}
#endif
}

#if defined(SIMU)
Nb4LedDiagStats nb4LedDiagDmaStats() { return {}; }
uintptr_t nb4LedDiagDmaBuffer() { return 0; }
static bool simulatedPause = false;
void nb4LedDiagPauseLcd(bool pause) { simulatedPause = pause; }
bool nb4LedDiagLcdPaused() { return simulatedPause; }
#endif

void nb4LedDiagStart(uint32_t now, uint8_t intensity, bool quietPeripherals)
{
  // Do not mutate the pattern while the timer task is consuming it.
  if (active.load(std::memory_order_acquire)) return;
  level.store(intensity ? intensity : 128, std::memory_order_relaxed);
  phase = 0;
  phaseStart = now;
  for (auto& result : results) result = {};
  baseline = nb4LedDiagDmaStats();
  nb4LedDiagPauseLcd(false);
  quiet.store(quietPeripherals, std::memory_order_release);
  if (quietPeripherals) applyQuiet(true);
  active.store(true, std::memory_order_release);
}

void nb4LedDiagTick(uint32_t now)
{
  if (!nb4LedDiagActive() || now - phaseStart < NB4_LED_DIAG_PHASE_MS) return;
  const auto stats = nb4LedDiagDmaStats();
  results[phase] = delta(stats);
  baseline = stats;
  ++phase;
  phaseStart = now;
  if (phase == NB4_LED_DIAG_PHASES) {
    nb4LedDiagStop();
  } else {
    nb4LedDiagPauseLcd(phase == 1);
  }
}

void nb4LedDiagStop()
{
  if (active.exchange(false, std::memory_order_acq_rel) &&
      phase < NB4_LED_DIAG_PHASES)
    results[phase] = delta(nb4LedDiagDmaStats());
  nb4LedDiagPauseLcd(false);
  if (quiet.exchange(false, std::memory_order_acq_rel)) applyQuiet(false);
}

bool nb4LedDiagActive() { return active.load(std::memory_order_acquire); }
bool nb4LedDiagQuiet()
{
  return nb4LedDiagActive() && quiet.load(std::memory_order_acquire);
}
unsigned nb4LedDiagPhase() { return phase; }
Nb4LedDiagStats nb4LedDiagResult(unsigned p)
{
  if (p >= NB4_LED_DIAG_PHASES) return {};
  if (nb4LedDiagActive() && p == phase) return delta(nb4LedDiagDmaStats());
  return results[p];
}

bool nb4LedDiagWritePattern()
{
  if (!nb4LedDiagActive()) return false;
  const uint8_t value = level.load(std::memory_order_relaxed);
  rgbSetLedColor(0, value, 0, 0);
  rgbSetLedColor(1, 0, value, 0);
  rgbSetLedColor(2, 0, 0, value);
  rgbSetLedColor(3, value, value, value);
  rgbLedColorApply();
  return true;
}
#endif
