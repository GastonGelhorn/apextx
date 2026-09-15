/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <stdint.h>

#if defined(RADIO_NB4_FAMILY)

struct Nb4RacingData;

enum Nb4VehicleType : uint8_t {
  NB4_VEHICLE_UNSET = 0,
  NB4_VEHICLE_ELECTRIC = 1,
  NB4_VEHICLE_NITRO = 2,
  NB4_VEHICLE_CUSTOM = 3,
};

struct Nb4RacingState {
  uint16_t absPhase;
  uint8_t absReleasing;
  int16_t steerPos;
  uint8_t steerValid;
};

int16_t nb4RacingThrottle(const Nb4RacingData& cfg, int16_t value, bool cut,
                          bool idleUp, Nb4RacingState& st, uint8_t tick10ms);

int16_t nb4RacingSteering(const Nb4RacingData& cfg, int16_t value,
                          Nb4RacingState& st, uint8_t tick10ms);

#define NB4_MAX_LAPS 99

struct Nb4LapState {
  uint8_t laps;       // vueltas completadas
  uint32_t elapsed;
  uint32_t last;
  uint32_t best;
  uint8_t pressed;    // Previous push-button state

  uint32_t times[NB4_MAX_LAPS];
};

bool nb4RacingLapTick(Nb4LapState& st, bool pressed, uint8_t tick10ms,
                      uint8_t lapLimit = 0);

uint8_t nb4RacingLaps();
uint32_t nb4RacingLastLap();
uint32_t nb4RacingBestLap();
uint32_t nb4RacingCurrentLap();

void nb4RacingFormatTime(char* buf, uint32_t cs);

uint32_t nb4RacingLapTime(uint8_t idx);

uint32_t nb4RacingRaceTotal();
uint32_t nb4RacingRaceAverage();

enum class Nb4RacePhase : uint8_t { Ready, Running, Finished };
Nb4RacePhase nb4RacePhase();
uint32_t nb4RaceResultToken();
uint32_t nb4RaceElapsed();
// Commands are consumed by the mixer; UI never waits for the mixer mutex.
bool nb4RaceStart();
bool nb4RaceFinish();
bool nb4RaceTogglePause();
bool nb4RaceIsPaused();
void nb4RaceProcessCommands();
void nb4RaceNativeTimerReset();
bool nb4RaceTimerOverride();
bool nb4RaceTimerHeld();
void nb4RaceTimerAdvance(uint8_t tick10ms);

/* Start a new race from zero. */
void nb4RacingLapsReset();

void nb4RacingMarkLap();

bool nb4RacingUndoLap();

int32_t nb4RacingLapDelta();

uint8_t nb4RacingBestLapIndex();

int32_t nb4RacingApplyChannel(uint8_t channel, int32_t valueQ8,
                              uint8_t tick10ms);

void nb4RacingTick(uint8_t tick10ms);

void nb4RacingPerMain();

void nb4RacingReset();

void nb4RacingDefaults(Nb4RacingData& cfg);

bool nb4RacingMigrate(Nb4RacingData& cfg);

void nb4RacingPresetElectric(Nb4RacingData& cfg);
void nb4RacingPresetNitro(Nb4RacingData& cfg);

void nb4RacingSetupRaceTimer(bool nitro);

enum Nb4HomeTimer : uint8_t {
  NB4_HOME_TIMER_AUTO,
  NB4_HOME_TIMER_1,
  NB4_HOME_TIMER_2,
  NB4_HOME_TIMER_HIDDEN,
  NB4_HOME_TIMER_COUNT
};

#define NB4_RACING_VERSION 3

#endif  // RADIO_NB4_FAMILY
