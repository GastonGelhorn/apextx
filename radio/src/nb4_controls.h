/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stdint.h>

#if defined(RADIO_NB4_FAMILY)
// Stored IDs: append new actions; never reorder existing values.
enum Nb4ControlAction : uint8_t {
  NB4_CONTROL_DEFAULT, NB4_CONTROL_OFF,
  NB4_CONTROL_PREVIOUS, NB4_CONTROL_NEXT, NB4_CONTROL_ENTER, NB4_CONTROL_BACK,
  NB4_CONTROL_SETTINGS, NB4_CONTROL_QUICK,
  NB4_CONTROL_RUN_PAUSE, NB4_CONTROL_LAP, NB4_CONTROL_FINISH,
  NB4_CONTROL_RESET, NB4_CONTROL_UNDO,
  NB4_CONTROL_ST_DOWN, NB4_CONTROL_ST_UP,
  NB4_CONTROL_TH_DOWN, NB4_CONTROL_TH_UP,
  NB4_CONTROL_ACTION_COUNT
};
constexpr unsigned NB4_CONTROL_COUNT = 12;
constexpr uint8_t NB4_CONTROL_LONG = 0x80;
// SW1-L, SW1-R, SW2, SW3, followed by the eight native trim directions.
uint8_t nb4ControlBinding(unsigned index);
bool nb4ControlsHasAction(uint8_t action);
void nb4ControlSetBinding(unsigned index, uint8_t binding);
void nb4ControlsReload();
// Timer task: bounded RAM only, at the existing 10 ms key scan cadence.
void nb4ControlsFilter(uint32_t& keys, uint32_t& trims, uint8_t switches);
uint16_t nb4ControlsPressed();
// Mixer task owns timer/lap mutations.
void nb4ControlsProcessCommands();
bool nb4ControlIsNavigation(uint8_t action);
bool nb4ControlCanHold(uint8_t action);
// Capture physical inputs without executing their current assignment.
void nb4ControlsBeginLearn();
void nb4ControlsEndLearn();
bool nb4ControlsLearning();
uint8_t nb4ControlsLearned(); // index, 0xff = waiting, 0xfe = multiple controls
uint8_t nb4ControlsReadSwitches();
uint8_t nb4ControlsSwitchSource(uint8_t index, uint8_t physical);
uint32_t nb4ControlsTrimSource(uint32_t physical);
#endif
