/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum Nb4Fault {
  NB4_FAULT_NONE,
  NB4_FAULT_UI_ASSERT,
  NB4_FAULT_CPP_ALLOC,
  NB4_FAULT_STACK,
  NB4_FAULT_CPU,
  NB4_FAULT_UI_STALL,
  NB4_FAULT_TIMER_STALL
};

enum Nb4Task {
  NB4_TASK_UI,
  NB4_TASK_MIXER,
  NB4_TASK_TIMER,
  NB4_TASK_STORAGE,
  NB4_TASK_COUNT
};

// This fixed-size record is retained across a warm reset.
typedef struct {
  uint32_t magic, version, fault, detail;
  uint32_t progress[NB4_TASK_COUNT];
  uint32_t checksum;
} Nb4HealthRecord;

#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)
void nb4HealthInit(void);
void nb4HealthBeat(enum Nb4Task task);
bool nb4HealthPrevious(Nb4HealthRecord* out);
bool nb4HealthRecovery(void);
void nb4HealthRequestRecovery(void);
void nb4HealthClearPrevious(void);
bool nb4HealthSupervisor(void);
void nb4HealthAssertUi(void);
void nb4HealthFault(uint32_t fault, uint32_t detail);
__attribute__((noreturn)) void nb4UiAssert(const char* file, unsigned line);
__attribute__((noreturn)) void nb4Fatal(uint32_t fault, uint32_t detail);
#else
static inline void nb4HealthBeat(enum Nb4Task task) {}
static inline bool nb4HealthRecovery(void) { return false; }
#endif

#ifdef __cplusplus
}
#endif
