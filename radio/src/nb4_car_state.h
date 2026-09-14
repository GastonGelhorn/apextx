/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stdint.h>

#if defined(RADIO_NB4_FAMILY)
#include "nb4_racing.h"

enum class Nb4Reset : uint8_t {
  Radio,   // g_eeGeneral: theme, home, sound, units, variables...
  Model,   // Open car model, including g_model.nb4Racing
  Both,
};

void nb4FactoryReset(Nb4Reset what);

enum class Nb4Validity : uint8_t { Absent, Valid, Stale, Alarm };
enum class Nb4Unit : uint8_t { Percent, Millivolts, Seconds, Centiseconds, Trim };
struct Nb4Reading {
  int32_t value = 0;
  Nb4Validity validity = Nb4Validity::Absent;
  Nb4Unit unit = Nb4Unit::Percent;
};
struct Nb4ChannelState {
  Nb4Reading output;
  Nb4Reading command; // after car functions, before travel/reverse/failsafe
  bool assigned = false;
};
struct Nb4CarState {
  uint32_t timestampMs = 0;
  char model[33] = {};
  Nb4ChannelState channels[8];
  uint8_t steeringChannel = 0, throttleChannel = 1;
  Nb4Reading steeringInput, throttleInput;
  Nb4Reading steeringTrim, throttleTrim, link, transmitter, receiver, timer;

  uint8_t chargeSource = 0;
  Nb4Reading homeTimer;
  uint32_t homeTimerStart = 0;
  uint8_t homeTimerIndex = 0xff, homeTimerState = 0;
  bool homeTimerVisible = false, homeTimerCountdown = false;
  bool homeShowsRace = false;
  Nb4RacePhase racePhase = Nb4RacePhase::Ready;
  Nb4Reading raceElapsed;
  bool lapsConfigured = false;
  uint8_t laps = 0;
  Nb4Reading lastLap, bestLap, currentLap;
};

struct Nb4SensorReading {
  char name[16] = {};
  int32_t value = 0;
  Nb4Validity validity = Nb4Validity::Absent;
  uint8_t unit = 0, precision = 0;
};
Nb4SensorReading nb4ReadSensor(unsigned index);
// UI/Lua callers only. Native aligned words are read without waiting on control.
const Nb4CarState& nb4ReadCarState();

#if defined(SIMU)

extern uint8_t nb4SimuChargeSource;
#endif

uint8_t nb4BatteryPercent(uint16_t cellMv);
constexpr uint8_t NB4_UI_VERSION = 2;

enum Nb4Home : uint8_t {
  NB4_HOME_INSTRUMENTS,
  NB4_HOME_ESSENTIAL,
  NB4_HOME_PREVIOUS,
  NB4_HOME_CHRONO,
  NB4_HOME_PIT,
  NB4_HOME_TELEMETRY,
  NB4_HOME_BENCH,
  NB4_HOME_COUNT
};
void nb4VisualDefaults();
#endif
