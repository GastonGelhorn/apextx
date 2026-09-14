/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "gtests.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_controls.h"
#include "nb4_racing.h"
#include "nb4_model_compat.h"
#include "storage/yaml/yaml_datastructs.h"
#include "storage/yaml/yaml_parser.h"
#include "storage/yaml/yaml_tree_walker.h"

class Nb4Controls : public testing::Test {
 protected:
  void SetUp() override {
    SYSTEM_RESET();
    memset(&g_model, 0, sizeof(g_model));
    memset(timersStates, 0, sizeof(timersStates));
    nb4AcceptNewCarModel();
    nb4RacingDefaults(g_model.nb4Racing);
    nb4RacingReset();
    scan(0);
  }
  void TearDown() override { nb4RacingReset(); scan(0); }
  void scan(uint16_t physical, unsigned ticks = 8) {
    for (unsigned i = 0; i < ticks; ++i) {
      keysOut = ((physical & 1) ? 1u << KEY_EXIT : 0) | ((physical & 2) ? 1u << KEY_ENTER : 0);
      trimsOut = physical >> 4;
      nb4ControlsFilter(keysOut, trimsOut, (physical >> 2) & 3);
    }
  }
  void tick(unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
      nb4ControlsProcessCommands(); nb4RaceProcessCommands();
      evalTimers(64, 1); nb4RacingTick(1);
    }
  }
  void press(unsigned index, unsigned duration = 8) {
    scan(0); scan(1u << index, duration); tick(1); scan(0); tick(1);
  }
  uint32_t keysOut = 0, trimsOut = 0;
};

TEST_F(Nb4Controls, OldModelsKeepEveryNativeKeyAndTrim) {
  scan(0xfff);
  EXPECT_EQ(keysOut, (1u << KEY_EXIT) | (1u << KEY_ENTER));
  EXPECT_EQ(trimsOut, 0xffu);
  EXPECT_EQ(nb4ControlsPressed(), 0xfff);
}

TEST_F(Nb4Controls, RemappedTrimNavigatesWithoutAdjustingTheTrim) {
  nb4ControlSetBinding(4, NB4_CONTROL_NEXT); scan(0);
  scan(1u << 4);
  EXPECT_EQ(keysOut, 1u << KEY_DOWN);
  EXPECT_EQ(trimsOut, 0u);
  nb4ControlSetBinding(4, NB4_CONTROL_DEFAULT); scan(0); scan(1u << 4);
  EXPECT_EQ(keysOut, 0u);
  EXPECT_EQ(trimsOut, 1u);
}

TEST_F(Nb4Controls, GripAndWheelButtonsCanExchangeNavigationRoles) {
  nb4ControlSetBinding(0, NB4_CONTROL_RUN_PAUSE);
  nb4ControlSetBinding(2, NB4_CONTROL_ENTER);
  nb4ControlSetBinding(3, NB4_CONTROL_BACK); scan(0);
  scan(1u << 0); EXPECT_EQ(keysOut, 0u);
  scan(0); scan(1u << 2); EXPECT_EQ(keysOut, 1u << KEY_ENTER);
  scan(0); scan(1u << 3); EXPECT_EQ(keysOut, 1u << KEY_EXIT);
}

TEST_F(Nb4Controls, ReassignmentAndModelLoadWaitForARelease) {
  scan(1u << 2);
  nb4ControlSetBinding(2, NB4_CONTROL_RUN_PAUSE);
  scan(1u << 2, 100); tick(1);
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Ready);
  press(2);
  ASSERT_EQ(nb4RacePhase(), Nb4RacePhase::Running);
  nb4RacingReset();
  scan(1u << 2, 100); tick(1);
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Ready);
}

TEST_F(Nb4Controls, BounceIsIgnoredAndHoldingDoesNotToggleRepeatedly) {
  nb4ControlSetBinding(2, NB4_CONTROL_RUN_PAUSE); scan(0);
  for (unsigned i = 0; i < 6; ++i) { scan(1u << 2, 1); scan(0, 1); }
  tick(1); EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Ready);
  scan(1u << 2, 100); tick(1);
  ASSERT_EQ(nb4RacePhase(), Nb4RacePhase::Running);
  scan(1u << 2, 100); tick(100);
  EXPECT_FALSE(nb4RaceIsPaused());
}

TEST_F(Nb4Controls, PauseFreezesBothNativeTimerAndLapClockThenContinues) {
  nb4ControlSetBinding(2, NB4_CONTROL_RUN_PAUSE);
  nb4ControlSetBinding(3, NB4_CONTROL_LAP);
  press(2); tick(235);
  press(2);
  ASSERT_TRUE(nb4RaceIsPaused());
  const auto timer = timersStates[0].val;
  const auto fraction = timersStates[0].val_10ms;
  const auto race = nb4RaceElapsed();
  const auto lap = nb4RacingCurrentLap();
  press(3); tick(300);
  EXPECT_EQ(nb4RacingLaps(), 0);
  EXPECT_EQ(timersStates[0].val, timer);
  EXPECT_EQ(timersStates[0].val_10ms, fraction);
  EXPECT_EQ(nb4RaceElapsed(), race);
  EXPECT_EQ(nb4RacingCurrentLap(), lap);
  press(2); tick(100);
  EXPECT_FALSE(nb4RaceIsPaused());
  EXPECT_EQ(nb4RaceElapsed(), race + 102);
  EXPECT_EQ(timersStates[0].val, timer + 1);
  press(3);
  EXPECT_EQ(nb4RacingLaps(), 1);
}

TEST_F(Nb4Controls, ResetRequiresHoldAndClearsPendingActionsOnModelChange) {
  nb4ControlSetBinding(2, NB4_CONTROL_RUN_PAUSE);
  nb4ControlSetBinding(3, NB4_CONTROL_RESET);
  EXPECT_EQ(nb4ControlBinding(3), NB4_CONTROL_RESET | NB4_CONTROL_LONG);
  press(2); tick(250); press(3, 20);
  EXPECT_GT(nb4RaceElapsed(), 200u);
  press(3, 70);
  EXPECT_EQ(nb4RaceElapsed(), 0u);
  EXPECT_EQ(timersStates[0].val, 0);
  scan(0); scan(1u << 2); // queued but not yet consumed
  memset(g_model.nb4Bindings, 0, sizeof(g_model.nb4Bindings));
  nb4RacingReset(); tick(1);
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Ready);
}

TEST_F(Nb4Controls, InvalidBindingsFallBackAndTrimPairUsesNativeDirections) {
  g_model.nb4Bindings[2] = 0x7f;
  nb4ControlsReload();
  EXPECT_EQ(nb4ControlBinding(2), NB4_CONTROL_DEFAULT);
  nb4ControlSetBinding(2, NB4_CONTROL_ST_DOWN);
  nb4ControlSetBinding(3, NB4_CONTROL_ST_UP); scan(0);
  scan(1u << 2); EXPECT_EQ(trimsOut, 1u);
  scan(0); scan(1u << 3); EXPECT_EQ(trimsOut, 2u);
}

TEST_F(Nb4Controls, AssignmentsSurviveYamlRoundTripWithoutChangingRaceSettings) {
  g_model.nb4Racing.brakeMax = 73;
  nb4ControlSetBinding(0, NB4_CONTROL_QUICK);
  nb4ControlSetBinding(2, NB4_CONTROL_RUN_PAUSE);
  nb4ControlSetBinding(11, NB4_CONTROL_RESET);
  YamlTreeWalker writer;
  writer.reset(get_modeldata_nodes(), reinterpret_cast<uint8_t*>(&g_model));
  std::string yaml;
  ASSERT_TRUE(writer.generate([](void* out, const char* str, size_t len) {
    static_cast<std::string*>(out)->append(str, len); return true;
  }, &yaml));
  memset(&g_model, 0, sizeof(g_model));
  YamlTreeWalker reader;
  reader.reset(get_modeldata_nodes(), reinterpret_cast<uint8_t*>(&g_model));
  YamlParser parser;
  parser.init(YamlTreeWalker::get_parser_calls(), &reader);
  ASSERT_EQ(parser.parse(yaml.c_str(), yaml.size()), YamlParser::CONTINUE_PARSING);
  EXPECT_EQ(nb4ControlBinding(0), NB4_CONTROL_QUICK);
  EXPECT_EQ(nb4ControlBinding(2), NB4_CONTROL_RUN_PAUSE);
  EXPECT_EQ(nb4ControlBinding(11), NB4_CONTROL_RESET | NB4_CONTROL_LONG);
  EXPECT_EQ(g_model.nb4Racing.brakeMax, 73);
  EXPECT_EQ(g_model.nb4Racing.version, NB4_RACING_VERSION);
}

TEST_F(Nb4Controls, LearnDetectsThePhysicalButtonWithoutRunningItsOldOrNewAction) {
  nb4ControlSetBinding(2, NB4_CONTROL_RUN_PAUSE); scan(0);
  nb4ControlsBeginLearn();
  scan(1u << 2); tick(1);
  ASSERT_EQ(nb4ControlsLearned(), 2);
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Ready);
  nb4ControlSetBinding(2, NB4_CONTROL_RUN_PAUSE);
  nb4ControlsEndLearn();
  scan(1u << 2, 100); tick(1);
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Ready);
  press(2);
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Running);
}

TEST_F(Nb4Controls, LearnIgnoresAlreadyHeldButtonsAndRejectsAmbiguousPresses) {
  scan(1u << 0);
  nb4ControlsBeginLearn(); scan(1u << 0, 40);
  EXPECT_EQ(nb4ControlsLearned(), 0xff);
  EXPECT_EQ(keysOut, 0u);
  scan(0); scan((1u << 2) | (1u << 3));
  EXPECT_EQ(nb4ControlsLearned(), 0xfe);
  scan(0); scan(1u << 3);
  EXPECT_EQ(nb4ControlsLearned(), 3);
  nb4ControlsEndLearn();
}

TEST_F(Nb4Controls, LearnCapturesTrimsAndSuppressesSelectBackAndNativeSources) {
  nb4ControlsSwitchSource(0, SWITCH_HW_UP);
  nb4ControlsBeginLearn(); scan(1u << 2);
  EXPECT_EQ(nb4ControlsSwitchSource(0, SWITCH_HW_DOWN), SWITCH_HW_UP);
  nb4ControlsEndLearn();
  EXPECT_EQ(nb4ControlsSwitchSource(0, SWITCH_HW_DOWN), SWITCH_HW_UP);
  scan(0);
  EXPECT_EQ(nb4ControlsSwitchSource(0, SWITCH_HW_DOWN), SWITCH_HW_DOWN);
  nb4ControlsBeginLearn(); scan(1u << 7);
  EXPECT_EQ(nb4ControlsLearned(), 7);
  EXPECT_EQ(trimsOut, 0u);
  EXPECT_EQ(nb4ControlsTrimSource(1u << 3), 0u);
  nb4ControlsEndLearn();
  scan(1u << 7, 20); EXPECT_EQ(trimsOut, 0u);
  scan(0); scan(1u << 7); EXPECT_EQ(trimsOut, 1u << 3);
  scan(0); nb4ControlsBeginLearn(); scan(1u << 1);
  EXPECT_EQ(nb4ControlsLearned(), 1);
  EXPECT_EQ(keysOut, 0u);
  nb4ControlsEndLearn();
}

TEST_F(Nb4Controls, ChangingModelStopsLearningAndNeverStoresAnAssignment) {
  nb4ControlsBeginLearn(); scan(1u << 2);
  nb4RacingReset();
  EXPECT_FALSE(nb4ControlsLearning());
  for (unsigned i = 0; i < NB4_CONTROL_COUNT; ++i)
    EXPECT_EQ(nb4ControlBinding(i), NB4_CONTROL_DEFAULT);
}
#endif
