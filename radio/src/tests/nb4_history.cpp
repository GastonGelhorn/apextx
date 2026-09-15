/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "gtests.h"
#include "location.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_history.h"
#include "nb4_car_state.h"
#include "timers.h"
#include "nb4_pit.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace {
struct StorageFixture {
  std::filesystem::path root;
  StorageFixture() : root(std::filesystem::temp_directory_path() / ("nb4-racing-history-" + std::to_string(getpid()))) {
    std::filesystem::create_directories(root);
    simuFatfsSetPaths(root.c_str(), nullptr);
    simuFatfsSetFaults(0); simuFatfsSetRenameFault(0);
    nb4StorageResume(); nb4RacingReset();
    nb4HistoryRetry(); nb4StorageProcess();
    SYSTEM_RESET(); MODEL_RESET(); nb4RacingDefaults(g_model.nb4Racing);
    strcpy(g_model.header.name, "SRX8 'Rally'");
  }
  ~StorageFixture() {
    nb4RacingReset(); simuFatfsSetFaults(0); simuFatfsSetRenameFault(0);
    nb4StorageResume(); nb4HistoryRetry(); nb4StorageProcess();
    simuFatfsSetPaths(TESTS_PATH, nullptr); std::filesystem::remove_all(root);
  }
  unsigned saved() {
    unsigned count = 0;
    if (std::filesystem::exists(root / "LOGS/NB4"))
      for (auto& file : std::filesystem::directory_iterator(root / "LOGS/NB4"))
        count += file.path().extension() == ".yml";
    return count;
  }
};
void tick(unsigned count, int throttle = 64) {
  for (unsigned i = 0; i < count; ++i) {
    nb4RaceProcessCommands(); evalTimers(throttle, 1); nb4RacingTick(1);
  }
}
void finishOneRun() {
  ASSERT_TRUE(nb4RaceStart()); tick(100);
  nb4RacingMarkLap(); tick(1);
  ASSERT_TRUE(nb4RaceFinish()); tick(1);
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Finished);
}
Nb4HistoryView readHistory(uint32_t id = 0) {
  Nb4HistoryView view{};
  auto token = nb4HistoryRequest(id, id != 0);
  EXPECT_NE(token, 0u); nb4StorageProcess();
  EXPECT_TRUE(nb4HistoryPoll(token, view)); return view;
}
}
TEST(Nb4RaceSession, WaitsForNativeStartAndFreezesAtLapLimit) {
  StorageFixture fixture;
  g_model.timers[0].mode = TMRMODE_THR_START; timerReset(0);
  g_model.nb4Racing.lapCount = 2;
  tick(700);
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Ready);
  EXPECT_EQ(nb4RaceElapsed(), 0u); EXPECT_EQ(nb4RacingCurrentLap(), 0u);
  nb4RacingMarkLap(); tick(1); EXPECT_EQ(nb4RacingLaps(), 0);
  tick(100, 128);
  EXPECT_EQ(timersStates[0].val, 1);
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Running);
  EXPECT_EQ(nb4RaceElapsed(), 100u);
  nb4RacingMarkLap(); tick(1, 128);
  EXPECT_EQ(nb4RacingLapTime(0), 101u);
  tick(100, 128); nb4RacingMarkLap(); tick(1, 128);
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Finished);
  auto elapsed = nb4RaceElapsed(); auto native = timersStates[0].val;
  nb4RacingMarkLap(); tick(1000, 128);
  EXPECT_EQ(nb4RacingLaps(), 2); EXPECT_EQ(nb4RaceElapsed(), elapsed);
  EXPECT_EQ(timersStates[0].val, native);
  EXPECT_FALSE(nb4RacingUndoLap());
  nb4StorageProcess(); EXPECT_EQ(fixture.saved(), 1u);
  auto view = readHistory(); ASSERT_EQ(view.count, 1);
  EXPECT_EQ(view.records[0].duration, elapsed);
  EXPECT_EQ(view.records[0].laps, 2);
  EXPECT_STREQ(view.records[0].model, "SRX8 'Rally'");
  EXPECT_EQ(view.records[0].times[0], 101u);
}

TEST(Nb4History, ResultReceiptNeverPointsToAnEarlierRunOnSaveFailure) {
  StorageFixture fixture;
  finishOneRun();
  const auto firstToken = nb4RaceResultToken();
  EXPECT_NE(firstToken, 0u);
  EXPECT_EQ(nb4HistorySavedId(firstToken), 0u);
  nb4StorageProcess();
  const auto firstId = nb4HistorySavedId(firstToken);
  ASSERT_NE(firstId, 0u);
  finishOneRun();
  const auto secondToken = nb4RaceResultToken();
  EXPECT_NE(firstToken, secondToken);
  simuFatfsSetFaults(0, 0);
  nb4StorageProcess();
  EXPECT_EQ(nb4HistorySavedId(secondToken), 0u);
  simuFatfsSetFaults(0);
  nb4HistoryRetry(); nb4StorageProcess();
  const auto secondId = nb4HistorySavedId(secondToken);
  EXPECT_GT(secondId, firstId);
  const auto record = readHistory(secondId);
  ASSERT_EQ(record.count, 1);
  EXPECT_EQ(record.records[0].id, secondId);
}

TEST(Nb4History, ZeroLapResultsGetTheirOwnReceiptsAndResetDoesNotExposeOldRuns) {
  StorageFixture fixture;
  uint32_t previousId = 0;
  for (unsigned run = 0; run < 3; ++run) {
    ASSERT_TRUE(nb4RaceStart()); tick(1);
    ASSERT_TRUE(nb4RaceFinish()); tick(1);
    EXPECT_EQ(nb4RacingLaps(), 0);
    const auto receipt = nb4RaceResultToken();
    ASSERT_NE(receipt, 0u);
    EXPECT_EQ(nb4HistorySavedId(receipt), 0u);
    nb4StorageProcess();
    const auto id = nb4HistorySavedId(receipt);
    EXPECT_GT(id, previousId);
    auto detail = readHistory(id);
    ASSERT_EQ(detail.count, 1);
    EXPECT_EQ(detail.records[0].id, id);
    EXPECT_EQ(detail.records[0].laps, 0);
    EXPECT_EQ(detail.records[0].best, 0u);
    previousId = id;
  }
  nb4RacingReset();
  EXPECT_EQ(nb4RaceResultToken(), 0u);
  EXPECT_EQ(nb4HistorySavedId(0), 0u);
}
TEST(Nb4RaceSession, ManualCommandsUseNativeTimerWithoutEditingConfiguration) {
  StorageFixture fixture;
  g_model.timers[0].mode = TMRMODE_THR_START;
  g_model.timers[0].start = 180;
  nb4PitConfigure(8); timerReset(0);
  auto pitOriginal = g_model.timers[NB4_PIT_TIMER];
  auto original = g_model.timers[0];
  ASSERT_TRUE(nb4RaceStart()); EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Ready);
  tick(200); EXPECT_EQ(nb4RaceElapsed(), 200u); EXPECT_EQ(timersStates[0].val, 178);
  ASSERT_TRUE(nb4RaceFinish()); tick(1); tick(200);
  EXPECT_EQ(nb4RaceElapsed(), 200u); EXPECT_EQ(timersStates[0].val, 178);
  EXPECT_EQ(memcmp(&original, &g_model.timers[0], sizeof(original)), 0);
  EXPECT_EQ(nb4PitRemaining(), 478);
  EXPECT_EQ(memcmp(&pitOriginal, &g_model.timers[NB4_PIT_TIMER], sizeof(pitOriginal)), 0);
  nb4StorageProcess();
  ASSERT_TRUE(nb4RaceStart()); tick(1);
  EXPECT_EQ(nb4RacingLaps(), 0); EXPECT_EQ(nb4RaceElapsed(), 1u);
}
TEST(Nb4History, ShortWritesKeepPendingAndNeverReplaceSavedRaces) {
  StorageFixture fixture;
  finishOneRun(); nb4StorageProcess(); ASSERT_EQ(fixture.saved(), 1u);
  auto first = readHistory(); ASSERT_EQ(first.count, 1);
  finishOneRun(); simuFatfsSetFaults(0, 0); nb4StorageProcess();
  EXPECT_EQ(nb4HistoryPending(), 1u); EXPECT_EQ(fixture.saved(), 1u);
  EXPECT_EQ(nb4HistoryStatus(), Nb4HistoryStatus::Failed);
  simuFatfsSetFaults(0); nb4HistoryRetry(); nb4StorageProcess();
  EXPECT_EQ(nb4HistoryPending(), 0u); EXPECT_EQ(fixture.saved(), 2u);
  nb4HistoryRetry(); nb4StorageProcess(); EXPECT_EQ(fixture.saved(), 2u);
  auto detail = readHistory(first.records[0].id);
  ASSERT_EQ(detail.count, 1); EXPECT_EQ(detail.records[0].duration, first.records[0].duration);
}
TEST(Nb4RaceSession, NativeTimerAssignmentSurvivesDeferredRaceSynchronization) {
  StorageFixture fixture;
  g_model.timers[0].mode = TMRMODE_ON;
  g_model.timers[0].start = 180;
  finishOneRun(); nb4StorageProcess();
  timerSet(0, 42); nb4RaceProcessCommands();
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Ready);
  EXPECT_EQ(timersStates[0].val, 42);
  EXPECT_EQ(nb4RaceElapsed(), 0u);
  tick(100);
  EXPECT_EQ(timersStates[0].val, 41);
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Running);
  EXPECT_EQ(nb4RaceElapsed(), 100u);
  EXPECT_EQ(g_model.timers[0].start, 180);
}
TEST(Nb4History, InterruptedPromotionAndAmbiguousSuccessAreIdempotent) {
  StorageFixture fixture;
  finishOneRun(); simuFatfsSetRenameFault(1); nb4StorageProcess();
  EXPECT_EQ(fixture.saved(), 0u); EXPECT_EQ(nb4HistoryPending(), 1u);
  nb4HistoryRetry(); nb4StorageProcess(); EXPECT_EQ(fixture.saved(), 1u);
  finishOneRun(); simuFatfsSetRenameFault(2); nb4StorageProcess();
  EXPECT_EQ(fixture.saved(), 2u); EXPECT_EQ(nb4HistoryPending(), 1u);
  nb4HistoryRetry(); nb4StorageProcess();
  EXPECT_EQ(fixture.saved(), 2u); EXPECT_EQ(nb4HistoryPending(), 0u);
  nb4RacingReset(); auto view = readHistory();
  EXPECT_EQ(view.count, 2); EXPECT_EQ(view.error, 0);
}
TEST(Nb4History, BoundedQueueRetainsTwoResultsUntilRetry) {
  StorageFixture fixture; simuFatfsSetFaults(0, 0);
  finishOneRun(); nb4StorageProcess(); finishOneRun(); nb4StorageProcess();
  EXPECT_EQ(nb4HistoryPending(), 2u);
  EXPECT_FALSE(nb4RaceStart());
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Finished);
  simuFatfsSetFaults(0); nb4HistoryRetry(); nb4StorageProcess();
  EXPECT_EQ(nb4HistoryPending(), 0u); EXPECT_EQ(fixture.saved(), 2u);
}
TEST(Nb4History, PaginationCorruptFilesAndTemporaryFilesAreExplicit) {
  StorageFixture fixture;
  for (unsigned n = 0; n < 8; ++n) { finishOneRun(); nb4StorageProcess(); }
  auto first = readHistory(); ASSERT_EQ(first.count, 6); EXPECT_TRUE(first.more);
  auto token = nb4HistoryRequest(first.records[5].id); nb4StorageProcess();
  Nb4HistoryView second; ASSERT_TRUE(nb4HistoryPoll(token, second));
  EXPECT_EQ(second.count, 2); EXPECT_FALSE(second.more);
  std::ofstream(fixture.root / "LOGS/NB4/race-4294967294.tmp") << "interrupted";
  std::ofstream(fixture.root / "LOGS/NB4/race-4294967293.yml") << "version: 99\n";
  auto corrupted = readHistory(); ASSERT_EQ(corrupted.count, 6);
  EXPECT_EQ(corrupted.records[0].id, 4294967293u);
  EXPECT_STREQ(corrupted.records[0].model, "");
  EXPECT_NE(readHistory(4294967293u).error, 0);
}
TEST(Nb4History, QuiescingDefersWritesWithoutDroppingResults) {
  StorageFixture fixture; finishOneRun();
  EXPECT_TRUE(nb4StorageQuiesce()); nb4StorageProcess();
  EXPECT_EQ(fixture.saved(), 0u); EXPECT_EQ(nb4HistoryPending(), 1u);
  nb4StorageResume(); simuFatfsSetFaults(1); nb4StorageProcess();
  EXPECT_EQ(fixture.saved(), 1u); EXPECT_EQ(nb4HistoryPending(), 0u);
}

TEST(Nb4History, NinetyNineLapsRoundTripAndNativeResetRearmsTheSession) {
  StorageFixture fixture;
  ASSERT_TRUE(nb4RaceStart()); tick(1);
  for (unsigned lap = 0; lap < NB4_MAX_LAPS; ++lap) { tick(19); nb4RacingMarkLap(); tick(1); }
  ASSERT_EQ(nb4RacePhase(), Nb4RacePhase::Finished);
  EXPECT_EQ(nb4RacingLaps(), 99);
  nb4StorageProcess();
  auto list = readHistory(); ASSERT_EQ(list.count, 1);
  auto detail = readHistory(list.records[0].id); ASSERT_EQ(detail.count, 1);
  EXPECT_EQ(detail.records[0].laps, 99);
  EXPECT_EQ(detail.records[0].times[98], 20u);
  timerReset(0); nb4RaceProcessCommands();
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Ready);
  EXPECT_EQ(nb4RaceElapsed(), 0u); EXPECT_EQ(nb4RacingLaps(), 0);
  EXPECT_EQ(fixture.saved(), 1u);
}
#endif
