/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stdint.h>
#if defined(RADIO_NB4_FAMILY)
#include "nb4_racing.h"

struct Nb4RaceRecord {
  uint32_t id, date, duration, best, last, average;
  char model[33];
  uint8_t laps;
  uint32_t times[NB4_MAX_LAPS];
};
constexpr unsigned NB4_HISTORY_PAGE_SIZE = 6;
struct Nb4HistoryView {
  uint32_t token, before;
  bool detail, more;
  uint8_t count, error;
  Nb4RaceRecord records[NB4_HISTORY_PAGE_SIZE];
};
enum class Nb4HistoryStatus : uint8_t { Idle, Pending, Saving, Saved, Unavailable, Failed, Full };
// Two reservations bound memory. A result is never evicted to start another run.
bool nb4HistoryCanReserve();
int nb4HistoryReserve();
void nb4HistoryRelease(int slot);
void nb4HistoryPublish(int slot, const Nb4RaceRecord& record);
unsigned nb4HistoryPending();
Nb4HistoryStatus nb4HistoryStatus();
void nb4HistoryRetry();
uint32_t nb4HistoryRequest(uint32_t before = 0, bool detail = false);
bool nb4HistoryPoll(uint32_t token, Nb4HistoryView& view);
void nb4StorageStart();
bool nb4StorageStarted();
bool nb4StorageQuiesce();
void nb4StorageResume();
void nb4StorageRequestLog();
// All FatFs work is here; also usable synchronously by host fault-injection tests.
void nb4StorageProcess();
unsigned nb4StorageStackSize();
unsigned nb4StorageStackUsage();
#endif
