/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "gtests.h"
#include "nb4_car_state.h"
#include "nb4_leds.h"

#if defined(RADIO_NB4_FAMILY)

#include "nb4_racing.h"
#include "hal/adc_driver.h"
#include "storage/yaml/yaml_datastructs.h"
#include "storage/yaml/yaml_parser.h"
#include "storage/yaml/yaml_tree_walker.h"

namespace {

Nb4RacingData defaults()
{
  Nb4RacingData cfg;
  nb4RacingDefaults(cfg);
  return cfg;
}

Nb4RacingState freshState()
{
  Nb4RacingState st = {};
  return st;
}

}  // namespace

TEST(Nb4Racing, DefaultsDoNotTouchTheChannel)
{
  auto cfg = defaults();
  auto st = freshState();

  EXPECT_EQ(nb4RacingThrottle(cfg, 700, false, false, st, 1), 700);
  EXPECT_EQ(nb4RacingThrottle(cfg, -700, false, false, st, 1), -700);
  EXPECT_EQ(nb4RacingThrottle(cfg, 0, false, false, st, 1), 0);
}

TEST(Nb4Racing, EngineCutBeatsEverything)
{
  auto cfg = defaults();
  cfg.idleUp = 30;
  cfg.dragBrake = 20;
  auto st = freshState();

  EXPECT_EQ(nb4RacingThrottle(cfg, 1024, true, true, st, 1), -RESX);

  cfg.engineCutPos = 0;
  EXPECT_EQ(nb4RacingThrottle(cfg, 1024, true, true, st, 1), 0);
}

TEST(Nb4Racing, IdleUpRaisesTheFloorButNeverTheBrake)
{
  auto cfg = defaults();
  cfg.idleUp = 25;
  auto st = freshState();

  int16_t floorValue = RESX / 4;

  EXPECT_EQ(nb4RacingThrottle(cfg, 0, false, true, st, 1), floorValue);
  EXPECT_EQ(nb4RacingThrottle(cfg, 100, false, true, st, 1), floorValue);
  EXPECT_EQ(nb4RacingThrottle(cfg, 900, false, true, st, 1), 900);

  EXPECT_EQ(nb4RacingThrottle(cfg, -500, false, true, st, 1), -500);

  EXPECT_EQ(nb4RacingThrottle(cfg, 0, false, false, st, 1), 0);
}

TEST(Nb4Racing, BrakeMaxOnlyCutsTheBrakeSide)
{
  auto cfg = defaults();
  cfg.brakeMax = 60;
  cfg.absEnable = 0;
  auto st = freshState();

  EXPECT_EQ(nb4RacingThrottle(cfg, -1000, false, false, st, 1), -600);
  EXPECT_EQ(nb4RacingThrottle(cfg, 1000, false, false, st, 1), 1000);
}

TEST(Nb4Racing, DragBrakeOnlyAtNeutral)
{
  auto cfg = defaults();
  cfg.dragBrake = 20;
  cfg.absEnable = 0;
  auto st = freshState();

  EXPECT_EQ(nb4RacingThrottle(cfg, 0, false, false, st, 1), -(RESX * 20) / 100);
  // Throttle must not include any drag brake.
  EXPECT_EQ(nb4RacingThrottle(cfg, 300, false, false, st, 1), 300);

  EXPECT_EQ(nb4RacingThrottle(cfg, -300, false, false, st, 1), -300);
}

TEST(Nb4Racing, DragBrakeUsesBrakeLimitOnce)
{
  auto cfg = defaults();
  cfg.dragBrake = 20;
  cfg.brakeMax = 60;
  cfg.absEnable = 0;
  auto st = freshState();

  // 20% drag brake, limited once to 60% = 12%.
  EXPECT_EQ(nb4RacingThrottle(cfg, 0, false, false, st, 1),
            -(RESX * 20 * 60) / 10000);
}

TEST(Nb4Racing, AbsPulsesOnlyPastItsThreshold)
{
  auto cfg = defaults();
  cfg.absEnable = 1;
  cfg.absPoint = 50;    // Engages at half brake travel
  cfg.absRate = 10;     // Ten pulses per second gives a 50 ms half-cycle
  cfg.absRelease = 50;
  auto st = freshState();

  for (int i = 0; i < 50; i++) {
    EXPECT_EQ(nb4RacingThrottle(cfg, -400, false, false, st, 1), -400);
  }

  // Under hard braking, ABS must release at some point.
  st = freshState();
  bool released = false;
  bool held = false;
  for (int i = 0; i < 50; i++) {
    int16_t out = nb4RacingThrottle(cfg, -1000, false, false, st, 1);
    if (out == -500) released = true;
    if (out == -1000) held = true;
  }
  EXPECT_TRUE(released);
  EXPECT_TRUE(held);
}

TEST(Nb4Racing, AbsLeavesNoStateBehindWhenTheBrakeIsReleased)
{
  auto cfg = defaults();
  cfg.absEnable = 1;
  cfg.absPoint = 20;
  cfg.absRate = 10;
  cfg.absRelease = 50;
  auto st = freshState();

  for (int i = 0; i < 20; i++) nb4RacingThrottle(cfg, -1000, false, false, st, 1);

  nb4RacingThrottle(cfg, 500, false, false, st, 1);
  EXPECT_EQ(st.absPhase, 0);
  EXPECT_EQ(st.absReleasing, 0);
  EXPECT_EQ(nb4RacingThrottle(cfg, -1000, false, false, st, 1), -1000);
}

TEST(Nb4Racing, SteeringWithoutSpeedIsInstant)
{
  auto cfg = defaults();
  auto st = freshState();

  EXPECT_EQ(nb4RacingSteering(cfg, 0, st, 1), 0);
  EXPECT_EQ(nb4RacingSteering(cfg, 1000, st, 1), 1000);
  EXPECT_EQ(nb4RacingSteering(cfg, -1000, st, 1), -1000);
}

TEST(Nb4Racing, SteeringSpeedLimitsTheStep)
{
  auto cfg = defaults();
  cfg.steerSpeedTurn = 10;
  auto st = freshState();

  EXPECT_EQ(nb4RacingSteering(cfg, 0, st, 1), 0);

  // 2*RESX over 100 hundredths is about 20 per hundredth.
  int16_t first = nb4RacingSteering(cfg, RESX, st, 1);
  EXPECT_GT(first, 0);
  EXPECT_LT(first, 40);

  for (int i = 0; i < 200; i++) nb4RacingSteering(cfg, RESX, st, 1);
  EXPECT_EQ(nb4RacingSteering(cfg, RESX, st, 1), RESX);
}

TEST(Nb4Racing, SteeringReturnUsesItsOwnSpeed)
{
  auto cfg = defaults();
  cfg.steerSpeedTurn = 0;    // Release is immediate
  cfg.steerSpeedReturn = 10; // Return is slow
  auto st = freshState();

  nb4RacingSteering(cfg, 0, st, 1);
  EXPECT_EQ(nb4RacingSteering(cfg, RESX, st, 1), RESX);

  int16_t back = nb4RacingSteering(cfg, 0, st, 1);
  EXPECT_LT(back, RESX);
  EXPECT_GT(back, RESX - 40);
}

TEST(Nb4Racing, LapsCountOnTheEdgeAndNotWhileHeld)
{
  Nb4LapState st = {};

  for (int i = 0; i < 100; i++) nb4RacingLapTick(st, false, 1);
  EXPECT_EQ(st.laps, 0);

  for (int i = 0; i < 50; i++) nb4RacingLapTick(st, true, 1);
  EXPECT_EQ(st.laps, 1);

  for (int i = 0; i < 50; i++) nb4RacingLapTick(st, false, 1);
  nb4RacingLapTick(st, true, 1);
  EXPECT_EQ(st.laps, 2);
}

TEST(Nb4Racing, LapTimesAndBest)
{
  Nb4LapState st = {};

  for (int i = 0; i < 99; i++) nb4RacingLapTick(st, false, 1);
  nb4RacingLapTick(st, true, 1);
  EXPECT_EQ(st.laps, 1);
  EXPECT_EQ(st.last, 100u);
  EXPECT_EQ(st.best, 100u);

  nb4RacingLapTick(st, false, 1);
  for (int i = 0; i < 148; i++) nb4RacingLapTick(st, false, 1);
  nb4RacingLapTick(st, true, 1);
  EXPECT_EQ(st.last, 150u);
  EXPECT_EQ(st.best, 100u);

  nb4RacingLapTick(st, false, 1);
  for (int i = 0; i < 78; i++) nb4RacingLapTick(st, false, 1);
  nb4RacingLapTick(st, true, 1);
  EXPECT_EQ(st.last, 80u);
  EXPECT_EQ(st.best, 80u);
}

TEST(Nb4Racing, LapTimerDoesNotWrap)
{
  Nb4LapState st = {};
  st.elapsed = UINT32_MAX - 15;

  for (int i = 0; i < 100; i++) nb4RacingLapTick(st, false, 1);

  EXPECT_EQ(st.elapsed, UINT32_MAX);
}

TEST(Nb4Racing, LapLimitFinishesTheRace)
{
  Nb4LapState st = {};

  for (int lap = 0; lap < 3; lap++) {
    for (int i = 0; i < 99; i++) nb4RacingLapTick(st, false, 1, 3);
    EXPECT_TRUE(nb4RacingLapTick(st, true, 1, 3));
    nb4RacingLapTick(st, false, 1, 3);
  }

  EXPECT_EQ(st.laps, 3);
  EXPECT_EQ(st.elapsed, 0u);
  for (int i = 0; i < 1000; i++) nb4RacingLapTick(st, false, 1, 3);
  EXPECT_EQ(st.elapsed, 0u);
  EXPECT_FALSE(nb4RacingLapTick(st, true, 1, 3));
  EXPECT_EQ(st.laps, 3);
}

TEST(Nb4Racing, PresetsAreDifferentAndNeitherAssignsSwitches)
{
  Nb4RacingData e, n;
  nb4RacingPresetElectric(e);
  nb4RacingPresetNitro(n);

  EXPECT_EQ(e.version, NB4_RACING_VERSION);
  EXPECT_EQ(n.version, NB4_RACING_VERSION);
  EXPECT_EQ(e.vehicleType, NB4_VEHICLE_ELECTRIC);
  EXPECT_EQ(n.vehicleType, NB4_VEHICLE_NITRO);

  EXPECT_EQ(e.brakeMax, 100);
  EXPECT_EQ(e.dragBrake, 0);
  EXPECT_EQ(e.idleUp, 0);

  // Nitro: high idle with some engine braking.
  EXPECT_GT(n.idleUp, 0);
  EXPECT_GT(n.dragBrake, 0);
  EXPECT_LT(n.brakeMax, 100);

  EXPECT_EQ(e.idleUpSw, 0);
  EXPECT_EQ(e.engineCutSw, 0);
  EXPECT_EQ(n.idleUpSw, 0);
  EXPECT_EQ(n.engineCutSw, 0);
}

TEST(Nb4Racing, DefaultsRequestPresetWithoutChangingOutputs)
{
  auto cfg = defaults();
  EXPECT_EQ(cfg.vehicleType, NB4_VEHICLE_UNSET);
  EXPECT_EQ(cfg.steeringChannel, 0);
  EXPECT_EQ(cfg.throttleChannel, 1);
}

TEST(Nb4Racing, MigratesOnlyTheSwappedUntouchedDefaultInputs)
{
  MODEL_RESET();
  auto cfg = defaults();
  cfg.version = 2;
  auto& first = g_model.expoData[0];
  auto& second = g_model.expoData[1];
  first.chn = 0;
  first.srcRaw = MIXSRC_FIRST_STICK + ADC_MAIN_TH;
  first.curve.type = CURVE_REF_EXPO;
  first.weight = 100;
  first.mode = 3;
  second.chn = 1;
  second.srcRaw = MIXSRC_FIRST_STICK + ADC_MAIN_ST;
  second.curve.type = CURVE_REF_EXPO;
  second.weight = 100;
  second.mode = 3;

  EXPECT_TRUE(nb4RacingMigrate(cfg));
  EXPECT_EQ(cfg.version, NB4_RACING_VERSION);
  EXPECT_EQ(first.srcRaw, MIXSRC_FIRST_STICK + ADC_MAIN_ST);
  EXPECT_EQ(second.srcRaw, MIXSRC_FIRST_STICK + ADC_MAIN_TH);

  // A user-edited pair is not rewritten merely because its sources are TH/ST.
  cfg.version = 2;
  first.srcRaw = MIXSRC_FIRST_STICK + ADC_MAIN_TH;
  second.srcRaw = MIXSRC_FIRST_STICK + ADC_MAIN_ST;
  first.offset = 5;
  EXPECT_TRUE(nb4RacingMigrate(cfg));
  EXPECT_EQ(first.srcRaw, MIXSRC_FIRST_STICK + ADC_MAIN_TH);
  EXPECT_EQ(second.srcRaw, MIXSRC_FIRST_STICK + ADC_MAIN_ST);
}

TEST(Nb4Racing, MigratesV1WithoutPromptingOrChangingItsChannels)
{
  auto cfg = defaults();
  cfg.version = 1;
  cfg.steeringChannel = 17;
  cfg.throttleChannel = 17;
  cfg.vehicleType = NB4_VEHICLE_UNSET;

  EXPECT_TRUE(nb4RacingMigrate(cfg));
  EXPECT_EQ(cfg.version, NB4_RACING_VERSION);
  EXPECT_EQ(cfg.steeringChannel, 0);
  EXPECT_EQ(cfg.throttleChannel, 1);
  EXPECT_EQ(cfg.vehicleType, NB4_VEHICLE_CUSTOM);
}

TEST(Nb4Racing, InvalidStoredVersionRecoversToSafeDefaults)
{
  auto cfg = defaults();
  cfg.version = 64;  // Typical signature of the shifted legacy YAML descriptor
  cfg.dragBrake = 100;
  cfg.absEnable = 1;
  cfg.engineCutSw = SWSRC_FIRST;

  EXPECT_TRUE(nb4RacingMigrate(cfg));
  EXPECT_EQ(cfg.version, NB4_RACING_VERSION);
  EXPECT_EQ(cfg.vehicleType, NB4_VEHICLE_UNSET);
  EXPECT_EQ(cfg.dragBrake, 0);
  EXPECT_EQ(cfg.absEnable, 0);
  EXPECT_EQ(cfg.engineCutSw, 0);
}

TEST(Nb4Racing, YamlRestoresChannelMappingAndPresetType)
{
  MODEL_RESET();
  const char yaml[] =
      "nb4Racing:\n"
      "   version: 3\n"
      "   brakeMax: 73\n"
      "   lapCount: 12\n"
      "   steeringChannel: 4\n"
      "   vehicleType: 2\n"
      "   throttleChannel: 7\n";

  YamlTreeWalker tree;
  tree.reset(get_modeldata_nodes(), reinterpret_cast<uint8_t*>(&g_model));
  YamlParser parser;
  parser.init(YamlTreeWalker::get_parser_calls(), &tree);
  EXPECT_EQ(YamlParser::CONTINUE_PARSING,
            parser.parse(yaml, sizeof(yaml) - 1));

  EXPECT_EQ(g_model.nb4Racing.version, NB4_RACING_VERSION);
  EXPECT_EQ(g_model.nb4Racing.brakeMax, 73);
  EXPECT_EQ(g_model.nb4Racing.lapCount, 12);
  EXPECT_EQ(g_model.nb4Racing.steeringChannel, 4);
  EXPECT_EQ(g_model.nb4Racing.throttleChannel, 7);
  EXPECT_EQ(g_model.nb4Racing.vehicleType, NB4_VEHICLE_NITRO);
}

TEST(Nb4Racing, UsesSelectedChannelsAndThenNormalOutputLimits)
{
  MODEL_RESET();
  setModelDefaults();
  auto cfg = defaults();
  cfg.vehicleType = NB4_VEHICLE_CUSTOM;
  cfg.steeringChannel = 4;
  cfg.throttleChannel = 5;
  cfg.dragBrake = 20;
  g_model.nb4Racing = cfg;
  nb4RacingReset();

  EXPECT_EQ(nb4RacingApplyChannel(1, 12345, 1), 12345);
  int32_t logicalBrake = nb4RacingApplyChannel(5, 0, 1);
  EXPECT_LT(logicalBrake, 0);

  g_model.limitData[5].revert = 1;
  EXPECT_GT(applyLimits(5, logicalBrake), 0);

  auto st = freshState();
  int16_t logicalCut = nb4RacingThrottle(cfg, RESX, true, false, st, 1);
  EXPECT_EQ(logicalCut, -RESX);
  EXPECT_GT(applyLimits(5, (int32_t)logicalCut * 256), 0);
}

TEST(Nb4Racing, MixerAppliesSurfaceFunctionsBeforeReverse)
{
  SYSTEM_RESET();
  MODEL_RESET();
  MIXER_RESET();
  setModelDefaults();

  evalMixes(1);
  int16_t unassignedChannelBaseline = channelOutputs[1];

  auto cfg = defaults();
  cfg.vehicleType = NB4_VEHICLE_CUSTOM;
  cfg.steeringChannel = 4;
  cfg.throttleChannel = 5;
  cfg.dragBrake = 20;
  g_model.nb4Racing = cfg;
  g_model.limitData[5].revert = 1;
  nb4RacingReset();

  evalMixes(1);

  EXPECT_GT(channelOutputs[5], 0);

  EXPECT_EQ(channelOutputs[1], unassignedChannelBaseline);
}

TEST(Nb4Racing, ChangingChannelMappingDropsPreviousDynamicState)
{
  MODEL_RESET();
  setModelDefaults();

  auto cfg = defaults();
  cfg.vehicleType = NB4_VEHICLE_CUSTOM;
  cfg.steeringChannel = 4;
  cfg.throttleChannel = 5;
  cfg.steerSpeedTurn = 10;
  g_model.nb4Racing = cfg;
  nb4RacingReset();

  EXPECT_EQ(nb4RacingApplyChannel(4, (int32_t)-RESX * 256, 1),
            (int32_t)-RESX * 256);

  g_model.nb4Racing.steeringChannel = 6;
  EXPECT_EQ(nb4RacingApplyChannel(6, (int32_t)RESX * 256, 1),
            (int32_t)RESX * 256);
}

TEST(Nb4Racing, SurfaceTargetHasNoAircraftModes)
{
  SYSTEM_RESET();
  MODEL_RESET();
  setModelDefaults();

  EXPECT_EQ(MAX_FLIGHT_MODES, 1);
  EXPECT_FALSE(modelFMEnabled());
  EXPECT_FALSE(modelHeliEnabled());

  /* Tools useful for car models remain available. */
  EXPECT_TRUE(modelLSEnabled());
  EXPECT_TRUE(modelSFEnabled());

  EXPECT_FALSE(modelGVEnabled());
}

TEST(Nb4Racing, VariablesAreOffByDefaultButStillReachable)
{
  SYSTEM_RESET();
  MODEL_RESET();
  setModelDefaults();
  ASSERT_FALSE(modelGVEnabled());

  g_eeGeneral.modelGVDisabled = 0;
  EXPECT_TRUE(modelGVEnabled());

  g_eeGeneral.modelGVDisabled = 1;
  g_model.modelGVDisabled = OVERRIDE_ON;
  EXPECT_TRUE(modelGVEnabled());
  g_model.modelGVDisabled = OVERRIDE_GLOBAL;

  g_eeGeneral.modelGVDisabled = 0;
  generalDefault();
  EXPECT_FALSE(modelGVEnabled());
}

TEST(Nb4Racing, FactoryResetIsThreeSeparatePromises)
{
  SYSTEM_RESET();
  MODEL_RESET();
  setModelDefaults();

  auto dirty = [&]() {
    strAppend(g_eeGeneral.selectedTheme, "Otro tema", SELECTED_THEME_NAME_LEN);
    g_eeGeneral.modelGVDisabled = 0;
    g_eeGeneral.inactivityTimer = 42;
    memcpy(g_eeGeneral.uiLanguage, "es", 2);
    for (int i = 0; i < adcGetMaxCalibratedInputs(); i += 1) {
      g_eeGeneral.calib[i].mid = 900 + i;
      g_eeGeneral.calib[i].spanNeg = 700 + i;
      g_eeGeneral.calib[i].spanPos = 800 + i;
    }
    g_model.nb4Racing.brakeMax = 37;
    g_model.nb4Racing.steerSpeedTurn = 21;
    strAppend(g_model.header.name, "Mi coche", LEN_MODEL_NAME);
  };

  Nb4RacingData factory;
  nb4RacingDefaults(factory);

  auto calibrationSurvived = [&]() {
    for (int i = 0; i < adcGetMaxCalibratedInputs(); i += 1) {
      if (g_eeGeneral.calib[i].mid != 900 + i) return false;
      if (g_eeGeneral.calib[i].spanNeg != 700 + i) return false;
      if (g_eeGeneral.calib[i].spanPos != 800 + i) return false;
    }
    return true;
  };

  dirty();
  nb4FactoryReset(Nb4Reset::Radio);
  EXPECT_STREQ(g_eeGeneral.selectedTheme, "ApexTX Dark");
  EXPECT_EQ(g_eeGeneral.inactivityTimer, 10);
  EXPECT_FALSE(modelGVEnabled());
  EXPECT_TRUE(calibrationSurvived());
  EXPECT_EQ(memcmp(g_eeGeneral.uiLanguage, "es", 2), 0);
  EXPECT_EQ(g_model.nb4Racing.brakeMax, 37);
  EXPECT_EQ(strncmp(g_model.header.name, "Mi coche", 8), 0);

  SYSTEM_RESET(); MODEL_RESET(); setModelDefaults();
  dirty();
  nb4FactoryReset(Nb4Reset::Model);
  EXPECT_EQ(g_model.nb4Racing.brakeMax, factory.brakeMax);
  EXPECT_EQ(g_model.nb4Racing.version, NB4_RACING_VERSION);
  EXPECT_EQ(strncmp(g_model.header.name, "Mi coche", 8), 0);
  EXPECT_STREQ(g_eeGeneral.selectedTheme, "Otro tema");
  EXPECT_EQ(g_eeGeneral.inactivityTimer, 42);

  // --- Both
  SYSTEM_RESET(); MODEL_RESET(); setModelDefaults();
  dirty();
  nb4FactoryReset(Nb4Reset::Both);
  EXPECT_STREQ(g_eeGeneral.selectedTheme, "ApexTX Dark");
  EXPECT_EQ(g_model.nb4Racing.brakeMax, factory.brakeMax);
  EXPECT_TRUE(calibrationSurvived());
  EXPECT_EQ(strncmp(g_model.header.name, "Mi coche", 8), 0);

  SYSTEM_RESET();
  MODEL_RESET();
  setModelDefaults();
}

TEST(Nb4Leds, TheColourFollowsTheModeExceptWhenItMatters)
{
  const Nb4LedRgb off{0, 0, 0};
  const Nb4LedRgb green = nb4LedPalette(NB4_LED_GREEN);
  const Nb4LedRgb blue = nb4LedPalette(NB4_LED_BLUE);

  // Off means fully off.
  EXPECT_TRUE(nb4LedColorNow(NB4_LED_OFF, NB4_LED_BLUE, false, 80, 0) == off);

  EXPECT_TRUE(nb4LedColorNow(NB4_LED_OFF, NB4_LED_BLUE, true, 50, 0) == off);

  EXPECT_TRUE(nb4LedColorNow(NB4_LED_FIXED, NB4_LED_BLUE, false, 80, 0) == blue);
  EXPECT_TRUE(nb4LedColorNow(NB4_LED_FIXED, NB4_LED_BLUE, false, 80, 5000) == blue);

  const Nb4LedRgb a = nb4LedColorNow(NB4_LED_BREATHE, NB4_LED_BLUE, false, 80, 0);
  const Nb4LedRgb b = nb4LedColorNow(NB4_LED_BREATHE, NB4_LED_BLUE, false, 80, 1200);
  EXPECT_FALSE(a == b);
  EXPECT_GT((int)a.b + a.r + a.g, 0);
  EXPECT_GT((int)b.b + b.r + b.g, 0);
  EXPECT_EQ(a.r, 0); EXPECT_EQ(a.g, 0);   // Remains blue with varying brightness

  const Nb4LedRgb charging =
      nb4LedColorNow(NB4_LED_FIXED, NB4_LED_BLUE, true, 50, 0);
  EXPECT_GT((int)charging.g, 0);
  EXPECT_EQ(charging.b, 0);

  EXPECT_TRUE(nb4LedColorNow(NB4_LED_FIXED, NB4_LED_BLUE, true, 100, 0) == green);
  EXPECT_TRUE(nb4LedColorNow(NB4_LED_FIXED, NB4_LED_BLUE, true, 100, 700) == green);

  EXPECT_TRUE(nb4LedColorNow(NB4_LED_BATTERY, NB4_LED_BLUE, false, 80, 0) == green);
  const Nb4LedRgb low = nb4LedColorNow(NB4_LED_BATTERY, NB4_LED_BLUE, false, 25, 0);
  EXPECT_GT((int)low.r, 0); EXPECT_GT((int)low.g, 0); EXPECT_EQ(low.b, 0);  // Amber
  const Nb4LedRgb crit0 = nb4LedColorNow(NB4_LED_BATTERY, NB4_LED_BLUE, false, 5, 0);
  const Nb4LedRgb crit1 = nb4LedColorNow(NB4_LED_BATTERY, NB4_LED_BLUE, false, 5, 1200);
  EXPECT_EQ(crit0.g, 0); EXPECT_EQ(crit0.b, 0);      // Red
  EXPECT_FALSE(crit0 == crit1);
}

TEST(Nb4Racing, NitroPresetIdlesButStillBrakes)
{
  Nb4RacingData cfg;
  nb4RacingPresetNitro(cfg);
  Nb4RacingState st = {};

  EXPECT_EQ(nb4RacingThrottle(cfg, 0, false, true, st, 1),
            (int16_t)((int32_t)RESX * cfg.idleUp / 100));

  EXPECT_EQ(nb4RacingThrottle(cfg, -1000, false, true, st, 1),
            (int16_t)(-1000 * (int32_t)cfg.brakeMax / 100));
}

TEST(Nb4Racing, RaceSummaryAddsUpAndSurvivesTheLimit)
{
  Nb4LapState st = {};

  // Three laps of 100, 200, and 300 hundredths.
  const uint32_t want[] = {100, 200, 300};
  for (uint32_t w : want) {
    for (uint32_t i = 1; i < w; i++) nb4RacingLapTick(st, false, 1);
    nb4RacingLapTick(st, true, 1);
  }

  EXPECT_EQ(st.laps, 3);
  EXPECT_EQ(st.times[0], 100u);
  EXPECT_EQ(st.times[1], 200u);
  EXPECT_EQ(st.times[2], 300u);
  EXPECT_EQ(st.best, 100u);

  for (int i = 0; i < NB4_MAX_LAPS + 20; i++) {
    nb4RacingLapTick(st, false, 1);
    nb4RacingLapTick(st, true, 1);
  }
  EXPECT_EQ(st.laps, NB4_MAX_LAPS);
}

TEST(Nb4Racing, TimeFormatting)
{
  char buf[16];

  nb4RacingFormatTime(buf, 0);
  EXPECT_STREQ(buf, "-");

  nb4RacingFormatTime(buf, 2431);
  EXPECT_STREQ(buf, "24.31");      // Less than one minute

  nb4RacingFormatTime(buf, 6100);
  EXPECT_STREQ(buf, "1:01.00");

  nb4RacingFormatTime(buf, 70000);
  EXPECT_STREQ(buf, "11:40.00");
}

#endif  // RADIO_NB4_FAMILY
