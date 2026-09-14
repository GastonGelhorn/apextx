/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "gtests.h"
#include "nb4_health.h"
#include <thread>
#include <chrono>
#if defined(RADIO_NB4_FAMILY)
TEST(Nb4Health, VisualStallDoesNotStopControlWatchdogButTimerStallDoes)
{
  nb4HealthClearPrevious(); nb4HealthInit();
  nb4HealthBeat(NB4_TASK_UI);
  for (unsigned i = 0; i < 56; ++i) {
    nb4HealthBeat(NB4_TASK_TIMER);
    nb4HealthBeat(NB4_TASK_MIXER);
    EXPECT_TRUE(nb4HealthSupervisor());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  nb4HealthBeat(NB4_TASK_TIMER);
  EXPECT_TRUE(nb4HealthSupervisor());
  nb4HealthInit();
  Nb4HealthRecord record;
  ASSERT_TRUE(nb4HealthPrevious(&record));
  EXPECT_EQ(record.fault, NB4_FAULT_UI_STALL);
  nb4HealthBeat(NB4_TASK_TIMER);
  std::this_thread::sleep_for(std::chrono::milliseconds(650));
  EXPECT_FALSE(nb4HealthSupervisor());
  nb4HealthInit();
  ASSERT_TRUE(nb4HealthPrevious(&record));
  EXPECT_EQ(record.fault, NB4_FAULT_TIMER_STALL);
  nb4HealthClearPrevious(); nb4HealthInit();
}

TEST(Nb4Health, RetainsFaultUntilExplicitClear)
{
  nb4HealthClearPrevious();
  nb4HealthInit();
  EXPECT_FALSE(nb4HealthRecovery());
  nb4HealthBeat(NB4_TASK_MIXER);
  nb4HealthFault(NB4_FAULT_UI_ASSERT, 456);
  nb4HealthFault(NB4_FAULT_UI_STALL, 6000); // preserve the original cause
  nb4HealthInit(); // simulate a warm reset
  EXPECT_TRUE(nb4HealthRecovery());
  Nb4HealthRecord saved;
  ASSERT_TRUE(nb4HealthPrevious(&saved));
  EXPECT_EQ(saved.fault, NB4_FAULT_UI_ASSERT);
  EXPECT_EQ(saved.detail, 456u);
  EXPECT_GT(saved.progress[NB4_TASK_MIXER], 0u);
  // Clearing diagnostic evidence is explicit, not a side effect of viewing it.
  ASSERT_TRUE(nb4HealthPrevious(&saved));
  nb4HealthClearPrevious();
  nb4HealthInit();
  EXPECT_FALSE(nb4HealthRecovery());
  EXPECT_FALSE(nb4HealthPrevious(&saved));
}
#endif
