/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "gtests.h"

#if defined(RADIO_NB4_FAMILY)
#include "model_init.h"
#include "mixes.h"
#include "gui/colorlcd/model/nb4_params.h"
#include "storage/yaml/yaml_datastructs.h"
#include "storage/yaml/yaml_tree_walker.h"
#include "storage/yaml/yaml_parser.h"
#include <string>

namespace {

constexpr uint8_t SIDE_NEG = 1;  // mode&1 applies when v < 0
constexpr uint8_t SIDE_POS = 2;  // mode&2 applies when v >= 0
constexpr uint8_t SIDE_BOTH = 3;

int16_t inputAfterExpos(uint8_t inputChn, int16_t rawValue, uint8_t stickIdx)
{
  int16_t lanes[MAX_INPUTS] = {0};
  calibratedAnalogs[stickIdx] = rawValue;
  applyExpos(lanes, e_perout_mode_normal);
  return lanes[inputChn];
}

void splitThrottleIntoTwoSides(uint8_t inputChn, uint8_t lineA, uint8_t lineB)
{
  ExpoData* a = expoAddress(lineA);
  ExpoData* b = expoAddress(lineB);
  *b = *a;                       // Same source, weight, and channel
  a->mode = SIDE_POS;
  b->mode = SIDE_NEG;
  a->curve.type = CURVE_REF_EXPO; a->curve.value = makeSourceNumVal(0);
  b->curve.type = CURVE_REF_EXPO; b->curve.value = makeSourceNumVal(0);
}

}  // namespace

TEST(Nb4Axis, TheEngineSelectsAnInputLineByTheSideOfTheStick)
{
  MODEL_RESET(); MIXER_RESET();
  setDefaultInputs();

  ExpoData* line = expoAddress(0);
  const uint8_t stick = line->srcRaw - MIXSRC_FIRST_STICK;
  const uint8_t chn = line->chn;

  line->mode = SIDE_POS;
  EXPECT_EQ(inputAfterExpos(chn, 512, stick), 512);
  EXPECT_EQ(inputAfterExpos(chn, -512, stick), 0);

  line->mode = SIDE_NEG;
  EXPECT_EQ(inputAfterExpos(chn, -512, stick), -512);
  EXPECT_EQ(inputAfterExpos(chn, 512, stick), 0);
}

TEST(Nb4Axis, TwoSidedLinesWithZeroExpoAreExactlyTheLinearResponse)
{
  MODEL_RESET(); MIXER_RESET();
  setDefaultInputs();
  const uint8_t chn = expoAddress(0)->chn;
  const uint8_t stick = expoAddress(0)->srcRaw - MIXSRC_FIRST_STICK;

  int16_t before[9];
  const int16_t probes[9] = {-1024, -768, -512, -256, 0, 256, 512, 768, 1024};
  for (int i = 0; i < 9; ++i) before[i] = inputAfterExpos(chn, probes[i], stick);

  splitThrottleIntoTwoSides(chn, 0, 1);
  for (int i = 0; i < 9; ++i)
    EXPECT_EQ(inputAfterExpos(chn, probes[i], stick), before[i]);
}

TEST(Nb4Axis, ChangingTheThrottleExpoLeavesTheBrakeResponseUntouched)
{
  MODEL_RESET(); MIXER_RESET();
  setDefaultInputs();
  const uint8_t chn = expoAddress(0)->chn;
  const uint8_t stick = expoAddress(0)->srcRaw - MIXSRC_FIRST_STICK;
  splitThrottleIntoTwoSides(chn, 0, 1);

  const int16_t brakeProbes[4] = {-1024, -700, -350, -100};
  int16_t brakeBefore[4];
  for (int i = 0; i < 4; ++i) brakeBefore[i] = inputAfterExpos(chn, brakeProbes[i], stick);

  expoAddress(0)->curve.value = makeSourceNumVal(60);

  for (int i = 0; i < 4; ++i)
    EXPECT_EQ(inputAfterExpos(chn, brakeProbes[i], stick), brakeBefore[i]);

  bool throttleMoved = false;
  for (int16_t v : {200, 500, 800})
    if (inputAfterExpos(chn, v, stick) != v) throttleMoved = true;
  EXPECT_TRUE(throttleMoved);
}

TEST(Nb4Axis, TheTwoSidesMeetAtNeutralWithoutAStep)
{
  MODEL_RESET(); MIXER_RESET();
  setDefaultInputs();
  const uint8_t chn = expoAddress(0)->chn;
  const uint8_t stick = expoAddress(0)->srcRaw - MIXSRC_FIRST_STICK;
  splitThrottleIntoTwoSides(chn, 0, 1);
  expoAddress(0)->curve.value = makeSourceNumVal(70);    // Progressive throttle
  expoAddress(1)->curve.value = makeSourceNumVal(-40);   // Opposite brake response

  EXPECT_EQ(inputAfterExpos(chn, 0, stick), 0);

  const int16_t justBelow = inputAfterExpos(chn, -1, stick);
  const int16_t justAbove = inputAfterExpos(chn, +1, stick);
  EXPECT_LT(justAbove - justBelow, 16);

  int16_t previous = inputAfterExpos(chn, -64, stick);
  for (int16_t v = -63; v <= 64; ++v) {
    const int16_t now = inputAfterExpos(chn, v, stick);
    EXPECT_GE(now, previous);
    EXPECT_LT(now - previous, 16);
    if (v == 0) EXPECT_EQ(now, 0);
    previous = now;
  }
}

TEST(Nb4Axis, DiffIsNotAPerSideExpoAndTheEngineTreatsThemDifferently)
{
  MODEL_RESET(); MIXER_RESET();
  setDefaultInputs();
  ExpoData* line = expoAddress(0);
  const uint8_t chn = line->chn;
  const uint8_t stick = line->srcRaw - MIXSRC_FIRST_STICK;
  line->mode = SIDE_BOTH;

  line->curve.type = CURVE_REF_EXPO; line->curve.value = makeSourceNumVal(50);
  const int16_t expoPos = inputAfterExpos(chn, 512, stick);
  const int16_t expoNeg = inputAfterExpos(chn, -512, stick);

  line->curve.type = CURVE_REF_DIFF; line->curve.value = makeSourceNumVal(50);
  const int16_t diffPos = inputAfterExpos(chn, 512, stick);
  const int16_t diffNeg = inputAfterExpos(chn, -512, stick);

  EXPECT_NE(expoPos, diffPos);

  EXPECT_TRUE(diffPos == 512 || diffNeg == -512);
  EXPECT_NE(expoNeg, diffNeg);
}

TEST(Nb4Axis, TheDefaultModelIsUnsplitAndThereIsRoomForTheRacingTemplate)
{
  MODEL_RESET(); MIXER_RESET();
  setDefaultInputs();

  unsigned used = 0;
  for (unsigned i = 0; i < MAX_EXPOS; ++i)
    if (expoAddress(i)->srcRaw) ++used;

  EXPECT_LE(used + 2, (unsigned)MAX_EXPOS);
  for (unsigned i = 0; i < used; ++i)
    EXPECT_EQ(expoAddress(i)->mode, SIDE_BOTH);

  EXPECT_EQ(expoAddress(0)->curve.type, CURVE_REF_EXPO);
  EXPECT_EQ(expoAddress(0)->curve.value, 0);
}

#include "nb4_axis.h"
#include "nb4_racing.h"

namespace {

void plainCarModel()
{
  MODEL_RESET(); MIXER_RESET();
  setDefaultInputs();
  for (uint8_t i = 0; i < 2; ++i) {
    MixData* mix = mixAddress(i);
    mix->destCh = i;
    mix->srcRaw = MIXSRC_FIRST_INPUT + i;
    mix->weight = makeSourceNumVal(100);
  }
  g_model.nb4Racing.steeringChannel = 0;
  g_model.nb4Racing.throttleChannel = 1;
}

void splitInput(uint8_t inputChn, uint8_t lineA, uint8_t lineB)
{
  ExpoData* a = expoAddress(lineA);
  ExpoData* b = expoAddress(lineB);
  *b = *a;
  a->mode = SIDE_POS; a->curve.type = CURVE_REF_EXPO; a->curve.value = makeSourceNumVal(0);
  b->mode = SIDE_NEG; b->curve.type = CURVE_REF_EXPO; b->curve.value = makeSourceNumVal(0);
  (void)inputChn;
}

}  // namespace

TEST(Nb4Resolver, TheDefaultModelResolvesAsSharedNotAsTwoIndependentSides)
{
  plainCarModel();
  const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
  EXPECT_EQ(v.status, Nb4AxisStatus::Shared);
  EXPECT_EQ(v.outputChannel, 1);
  EXPECT_EQ(v.inputChannel, 1);
  EXPECT_EQ(v.lineForPositive, v.lineForNegative);
}

TEST(Nb4Resolver, TheRacingTemplateResolvesAsReadyWithOneLinePerSide)
{
  plainCarModel();
  splitInput(1, 1, 2);
  const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
  ASSERT_EQ(v.status, Nb4AxisStatus::Ready);
  EXPECT_NE(v.lineForPositive, v.lineForNegative);
  EXPECT_EQ(v.curveTypePositive, CURVE_REF_EXPO);
  EXPECT_EQ(v.curveTypeNegative, CURVE_REF_EXPO);
}

TEST(Nb4Resolver, OutputReverseFlipsTheServoButNotWhichSideIsThrottle)
{
  plainCarModel();
  splitInput(1, 1, 2);
  const auto before = nb4ResolveThrottleSides();
  ASSERT_TRUE(before.known);

  limitAddress(1)->revert = 1;
  const auto after = nb4ResolveThrottleSides();
  ASSERT_TRUE(after.known);
  EXPECT_EQ(after.accelSign, before.accelSign);

  const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
  EXPECT_TRUE(v.outputReversed);
  EXPECT_EQ(v.chainSign, -1);
}

TEST(Nb4Resolver, ThrottleInputReversalDoesChangeWhichSideAccelerates)
{
  plainCarModel();
  splitInput(1, 1, 2);
  const auto before = nb4ResolveThrottleSides();
  g_model.throttleReversed = 1;
  const auto after = nb4ResolveThrottleSides();
  ASSERT_TRUE(after.known);
  EXPECT_EQ(after.accelSign, -before.accelSign);
  EXPECT_EQ(after.brakeSign, -before.brakeSign);
}

TEST(Nb4Resolver, ANegativeInputSourceAlsoFlipsTheSide)
{
  plainCarModel();
  splitInput(1, 1, 2);
  const auto before = nb4ResolveThrottleSides();
  for (uint8_t i = 0; i < MAX_EXPOS; ++i) {
    ExpoData* ed = expoAddress(i);
    if (ed->srcRaw && ed->chn == 1) ed->srcRaw = -ed->srcRaw;
  }
  const auto after = nb4ResolveThrottleSides();
  ASSERT_TRUE(after.known);
  EXPECT_EQ(after.accelSign, -before.accelSign);
  EXPECT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).inputSign, -1);
}

TEST(Nb4Resolver, ADynamicWeightIsReportedAndNeverFlattenedToANumber)
{
  plainCarModel();
  splitInput(1, 1, 2);
  ASSERT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).status, Nb4AxisStatus::Ready);

  expoAddress(1)->weight = makeSourceNumVal(MIXSRC_FIRST_GVAR, true);
  const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
  EXPECT_EQ(v.status, Nb4AxisStatus::Dynamic);

  SourceNumVal stored; stored.rawValue = expoAddress(1)->weight;
  EXPECT_TRUE(stored.isSource);

  expoAddress(1)->weight = makeSourceNumVal(100);
  mixAddress(1)->weight = makeSourceNumVal(MIXSRC_FIRST_GVAR, true);
  EXPECT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).status, Nb4AxisStatus::Dynamic);
}

TEST(Nb4Resolver, UninterpretableConfigurationsSayNotRepresentableWithAReason)
{
  struct Case { void (*breakIt)(); };
  const Case cases[] = {
    {[]() {
       MixData* m = mixAddress(4);
       m->destCh = 1; m->srcRaw = MIXSRC_FIRST_INPUT + 1; m->weight = makeSourceNumVal(50);
     }},
    {[]() {
       mixAddress(1)->srcRaw = MIXSRC_FIRST_STICK;
     }},
    {[]() {
       mixAddress(1)->swtch = 1;
     }},
    {[]() {
       expoAddress(1)->swtch = 1;
     }},
    {[]() {
       expoAddress(2)->srcRaw = 0;
     }},
    {[]() {
       g_model.nb4Racing.throttleChannel = MAX_OUTPUT_CHANNELS;
     }},
  };

  for (const auto& c : cases) {
    plainCarModel();
    splitInput(1, 1, 2);
    ASSERT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).status, Nb4AxisStatus::Ready);
    c.breakIt();
    const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
    EXPECT_EQ(v.status, Nb4AxisStatus::NotRepresentable);
    EXPECT_NE(v.reason, nullptr);
  }
}

TEST(Nb4Resolver, TheAxisTrimIsSeparateFromTheExpoAndFromTheChannelSubtrim)
{
  plainCarModel();
  splitInput(1, 1, 2);
  const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
  ASSERT_EQ(v.status, Nb4AxisStatus::Ready);

  const uint16_t before = expoAddress(v.lineForPositive)->curve.value;
  const int16_t subtrimBefore = limitAddress(v.outputChannel)->offset;

  ASSERT_TRUE(setTrimValue(0, 1, 42));
  EXPECT_EQ(getTrimValue(0, 1), 42);
  EXPECT_EQ(expoAddress(v.lineForPositive)->curve.value, before);
  EXPECT_EQ(limitAddress(v.outputChannel)->offset, subtrimBefore);
  EXPECT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).status, Nb4AxisStatus::Ready);
}

namespace {

std::string modelToYaml()
{
  std::string out;
  YamlTreeWalker tree;
  tree.reset(get_modeldata_nodes(), (uint8_t*)&g_model);
  const bool ok = tree.generate(
      [](void* opaque, const char* str, size_t len) {
        ((std::string*)opaque)->append(str, len);
        return true;
      },
      &out);
  if (!ok) out.clear();
  return out;
}

bool yamlToModel(const std::string& text)
{
  memset(&g_model, 0, sizeof(g_model));
  YamlTreeWalker tree;
  tree.reset(get_modeldata_nodes(), (uint8_t*)&g_model);
  YamlParser yp;
  yp.init(YamlTreeWalker::get_parser_calls(), &tree);
  if (yp.parse(text.c_str(), text.size()) != YamlParser::CONTINUE_PARSING)
    return false;
  yp.set_eof();
  return true;
}

}  // namespace

TEST(Nb4Resolver, WhatIsWrittenSurvivesASaveAndLoadCycleIncludingReferences)
{
  plainCarModel();
  splitInput(1, 1, 2);
  expoAddress(1)->curve.value = makeSourceNumVal(55);
  expoAddress(2)->curve.value = makeSourceNumVal(-30);                             // Opposite side
  limitAddress(1)->revert = 1;
  limitAddress(1)->offset = 17;
  g_model.nb4Racing.brakeMax = 78;
  expoAddress(0)->weight = makeSourceNumVal(MIXSRC_FIRST_GVAR, true);

  const std::string yaml = modelToYaml();
  ASSERT_FALSE(yaml.empty());

  EXPECT_NE(yaml.find("55"), std::string::npos);
  EXPECT_NE(yaml.find("-30"), std::string::npos);

  ASSERT_TRUE(yamlToModel(yaml));

  SourceNumVal c1; c1.rawValue = expoAddress(1)->curve.value;
  SourceNumVal c2; c2.rawValue = expoAddress(2)->curve.value;
  EXPECT_FALSE(c1.isSource); EXPECT_EQ(c1.value, 55);
  EXPECT_FALSE(c2.isSource); EXPECT_EQ(c2.value, -30);
  EXPECT_EQ(limitAddress(1)->revert, 1);
  EXPECT_EQ(limitAddress(1)->offset, 17);
  EXPECT_EQ(g_model.nb4Racing.brakeMax, 78);
  SourceNumVal w; w.rawValue = expoAddress(0)->weight;
  EXPECT_TRUE(w.isSource);
  EXPECT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).status, Nb4AxisStatus::Ready);
}

TEST(Nb4Resolver, WritingARawNegativeIntoACurveValueSilentlyTurnsItIntoAReference)
{
  plainCarModel();
  splitInput(1, 1, 2);
  ASSERT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).status, Nb4AxisStatus::Ready);

  volatile int16_t malformedValue = -30;
  expoAddress(1)->curve.value = (uint16_t)malformedValue;
  SourceNumVal wrong; wrong.rawValue = expoAddress(1)->curve.value;
  EXPECT_TRUE(wrong.isSource);
  EXPECT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).status, Nb4AxisStatus::Dynamic);

  /* Correct path. */
  expoAddress(1)->curve.value = makeSourceNumVal(-30);
  SourceNumVal right; right.rawValue = expoAddress(1)->curve.value;
  EXPECT_FALSE(right.isSource);
  EXPECT_EQ(right.value, -30);
  EXPECT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).status, Nb4AxisStatus::Ready);
}

namespace {

int16_t outputForStick(uint8_t inputChn, int16_t stickValue, uint8_t outputChannel)
{
  anaSetFiltered(inputMappingConvertMode(inputChn), stickValue);
  evalMixes(1);
  return channelOutputs[outputChannel];
}

struct AxisWiring {
  int8_t srcSign;
  int8_t lineWeightSign;
  int8_t mixWeightSign;
  bool revert;           // LimitData.revert
  bool inputReversed;    // g_model.throttleReversed
};

void wireCar(const AxisWiring& w)
{
  MODEL_RESET(); MIXER_RESET();
  setDefaultInputs();
  const uint8_t thrChn = inputMappingGetThrottle();

  for (uint8_t i = 0; i < 2; ++i) {
    MixData* mix = mixAddress(i);
    mix->destCh = i;
    mix->srcRaw = MIXSRC_FIRST_INPUT + i;
    mix->weight = makeSourceNumVal(100 * w.mixWeightSign);
    mix->swtch = 0;
  }

  nb4RacingDefaults(g_model.nb4Racing);
  g_model.nb4Racing.steeringChannel = 0;
  g_model.nb4Racing.throttleChannel = 1;
  g_model.throttleReversed = w.inputReversed ? 1 : 0;
  limitAddress(1)->revert = w.revert ? 1 : 0;
  limitAddress(1)->min = 0;
  limitAddress(1)->max = 0;

  ExpoData* a = nullptr;
  for (uint8_t i = 0; i < MAX_EXPOS && !a; ++i)
    if (expoAddress(i)->srcRaw && expoAddress(i)->chn == thrChn) a = expoAddress(i);
  ExpoData* b = nullptr;
  for (uint8_t i = 0; i < MAX_EXPOS && !b; ++i)
    if (!expoAddress(i)->srcRaw) b = expoAddress(i);
  if (!a || !b) return;
  *b = *a;
  for (ExpoData* ed : {a, b}) {
    ed->srcRaw = (int16_t)(w.srcSign * abs(ed->srcRaw));
    ed->weight = makeSourceNumVal(100 * w.lineWeightSign);
    ed->curve.type = CURVE_REF_EXPO;
    ed->curve.value = makeSourceNumVal(0);
    ed->swtch = 0;
  }
  a->mode = SIDE_POS;
  b->mode = SIDE_NEG;
}

}  // namespace

TEST(Nb4Chain, ClampingOneHalfOfTheTriggerLeavesTheOtherHalfAlone)
{
  unsigned checked = 0;
  for (int8_t srcSign : {(int8_t)+1, (int8_t)-1})
    for (int8_t lineW : {(int8_t)+1, (int8_t)-1})
      for (int8_t mixW : {(int8_t)+1, (int8_t)-1})
        for (bool revert : {false, true})
          for (bool inputRev : {false, true}) {
            const AxisWiring w{srcSign, lineW, mixW, revert, inputRev};
            wireCar(w);
            const uint8_t chn = inputMappingGetThrottle();

            const auto view = nb4ResolveAxis(Nb4AxisRole::Throttle);
            ASSERT_EQ(view.status, Nb4AxisStatus::Ready);

            const int16_t fullPos = outputForStick(chn, +1024, 1);
            const int16_t fullNeg = outputForStick(chn, -1024, 1);
            ASSERT_NE(fullPos, 0);
            ASSERT_NE(fullNeg, 0);

            const Nb4Endpoint target = nb4EndpointForStickSide(view, +1);
            ASSERT_NE(target, Nb4Endpoint::Unknown);

            LimitData* out = limitAddress(1);
            if (target == Nb4Endpoint::Min) out->min = +500;   // -50 %
            else                            out->max = -500;   // +50 %

            const int16_t clampedPos = outputForStick(chn, +1024, 1);
            const int16_t clampedNeg = outputForStick(chn, -1024, 1);

            EXPECT_LT(abs(clampedPos), abs(fullPos));
            EXPECT_EQ(clampedNeg, fullNeg);
            checked += 1;
          }
  EXPECT_EQ(checked, 32u);
}

TEST(Nb4Chain, AndTheSameHoldsForTheOtherHalf)
{
  for (int8_t srcSign : {(int8_t)+1, (int8_t)-1})
    for (int8_t lineW : {(int8_t)+1, (int8_t)-1})
      for (int8_t mixW : {(int8_t)+1, (int8_t)-1})
        for (bool revert : {false, true})
          for (bool inputRev : {false, true}) {
            wireCar({srcSign, lineW, mixW, revert, inputRev});
            const uint8_t chn = inputMappingGetThrottle();
            const auto view = nb4ResolveAxis(Nb4AxisRole::Throttle);
            ASSERT_EQ(view.status, Nb4AxisStatus::Ready);

            const int16_t fullPos = outputForStick(chn, +1024, 1);
            const int16_t fullNeg = outputForStick(chn, -1024, 1);
            ASSERT_NE(fullPos, 0);
            ASSERT_NE(fullNeg, 0);

            const Nb4Endpoint target = nb4EndpointForStickSide(view, -1);
            ASSERT_NE(target, Nb4Endpoint::Unknown);
            LimitData* out = limitAddress(1);
            if (target == Nb4Endpoint::Min) out->min = +500;   // -50 %
            else                            out->max = -500;   // +50 %

            EXPECT_LT(abs(outputForStick(chn, -1024, 1)), abs(fullNeg));
            EXPECT_EQ(outputForStick(chn, +1024, 1), fullPos);
          }
}

TEST(Nb4Chain, BrakeMaxActsOnWhicheverHalfArrivesWithTheBrakeSign)
{
  const uint8_t chn = inputMappingGetThrottle();

  int16_t limitedWithPositiveMix = 0, fullWithPositiveMix = 0;
  int16_t limitedWithNegativeMix = 0, fullWithNegativeMix = 0;

  for (int8_t mixW : {(int8_t)+1, (int8_t)-1}) {
    wireCar({+1, +1, mixW, false, false});

    g_model.nb4Racing.brakeMax = 100;
    const int16_t full = outputForStick(chn, +1024, 1);

    /* With brake travel limited to 50%. */
    g_model.nb4Racing.brakeMax = 50;
    const int16_t limited = outputForStick(chn, +1024, 1);

    if (mixW > 0) { fullWithPositiveMix = full; limitedWithPositiveMix = limited; }
    else          { fullWithNegativeMix = full; limitedWithNegativeMix = limited; }
  }

  const bool positiveMixAffected =
      abs(limitedWithPositiveMix) != abs(fullWithPositiveMix);
  const bool negativeMixAffected =
      abs(limitedWithNegativeMix) != abs(fullWithNegativeMix);

  EXPECT_NE(positiveMixAffected, negativeMixAffected);

  EXPECT_TRUE(negativeMixAffected);
}

TEST(Nb4Chain, TheBrakeIsOnlyEditableWhereTheEngineReallyCutsTheBrakeHalf)
{
  unsigned open = 0, closed = 0;
  for (int8_t srcSign : {(int8_t)+1, (int8_t)-1})
    for (int8_t lineW : {(int8_t)+1, (int8_t)-1})
      for (int8_t mixW : {(int8_t)+1, (int8_t)-1})
        for (bool revert : {false, true})
          for (bool inputRev : {false, true}) {
            wireCar({srcSign, lineW, mixW, revert, inputRev});
            const uint8_t chn = inputMappingGetThrottle();

            const auto view = nb4ResolveAxis(Nb4AxisRole::Throttle);
            const auto sides = nb4ResolveThrottleSides();
            ASSERT_EQ(view.status, Nb4AxisStatus::Ready);
            ASSERT_TRUE(sides.known);

            const int16_t fullBrake = outputForStick(chn, (int16_t)(sides.brakeSign * 1024), 1);
            const int16_t fullAccel = outputForStick(chn, (int16_t)(sides.accelSign * 1024), 1);
            ASSERT_NE(fullBrake, 0);
            ASSERT_NE(fullAccel, 0);

            g_model.nb4Racing.brakeMax = 50;
            const int16_t cutBrake = outputForStick(chn, (int16_t)(sides.brakeSign * 1024), 1);
            const int16_t cutAccel = outputForStick(chn, (int16_t)(sides.accelSign * 1024), 1);
            g_model.nb4Racing.brakeMax = 100;

            const bool engineCutsTheBrake = abs(cutBrake) < abs(fullBrake);
            const bool engineCutsTheAccel = abs(cutAccel) < abs(fullAccel);
            const bool engineAgrees = engineCutsTheBrake && !engineCutsTheAccel;

            const char* gate = nb4RacingSignGateReason(view, sides);
            if (gate) ++closed; else ++open;

            EXPECT_EQ(gate == nullptr, engineAgrees);
          }

  EXPECT_GT(open, 0u);
  EXPECT_GT(closed, 0u);
}

TEST(Nb4Chain, TheModelsPeopleActuallyHaveKeepTheirBrakeEditable)
{
  plainCarModel();
  nb4RacingDefaults(g_model.nb4Racing);
  g_model.nb4Racing.steeringChannel = 0;
  g_model.nb4Racing.throttleChannel = 1;
  {
    const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
    const auto sd = nb4ResolveThrottleSides();
    ASSERT_EQ(v.status, Nb4AxisStatus::Shared);
    const char* gate = nb4RacingSignGateReason(v, sd);
    EXPECT_EQ(gate, nullptr);
  }

  splitInput(1, 1, 2);
  {
    const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
    const auto sd = nb4ResolveThrottleSides();
    ASSERT_EQ(v.status, Nb4AxisStatus::Ready);
    const char* gate = nb4RacingSignGateReason(v, sd);
    EXPECT_EQ(gate, nullptr);
  }

  g_model.throttleReversed = 1;
  {
    const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
    const auto sd = nb4ResolveThrottleSides();
    const char* gate = nb4RacingSignGateReason(v, sd);
    EXPECT_EQ(gate, nullptr);
  }
}

TEST(Nb4Chain, TurningTheSignDependentFunctionsOffLeavesThemHarmless)
{
  plainCarModel();
  nb4RacingDefaults(g_model.nb4Racing);
  g_model.nb4Racing.brakeMax = 40;
  g_model.nb4Racing.dragBrake = 30;
  g_model.nb4Racing.absEnable = 1;
  g_model.nb4Racing.idleUp = 20;
  g_model.nb4Racing.engineCutPos = -80;
  g_model.nb4Racing.absPoint = 55;

  nb4RacingNeutraliseSignDependent();

  EXPECT_EQ(g_model.nb4Racing.brakeMax, 100);
  EXPECT_EQ(g_model.nb4Racing.dragBrake, 0);
  EXPECT_EQ(g_model.nb4Racing.absEnable, 0);
  EXPECT_EQ(g_model.nb4Racing.idleUp, 0);
  EXPECT_EQ(g_model.nb4Racing.engineCutPos, -80);
  EXPECT_EQ(g_model.nb4Racing.absPoint, 55);

  const uint8_t chn = inputMappingGetThrottle();
  EXPECT_EQ(outputForStick(chn, -1024, 1), -1024);
  EXPECT_EQ(outputForStick(chn, +1024, 1), +1024);
}

namespace {

void setSideCurves(int16_t positiveCurve, int16_t negativeCurve)
{
  const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
  ExpoData* pos = expoAddress(v.lineForPositive);
  ExpoData* neg = expoAddress(v.lineForNegative);
  pos->curve.type = CURVE_REF_CUSTOM;
  pos->curve.value = makeSourceNumVal(positiveCurve);
  neg->curve.type = CURVE_REF_CUSTOM;
  neg->curve.value = makeSourceNumVal(negativeCurve);
}

}  // namespace

TEST(Nb4Curves, SharingACurveIsNotSharingALineAndTheStateStaysReady)
{
  plainCarModel();
  splitInput(1, 1, 2);
  setSideCurves(3, 3);

  const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
  ASSERT_EQ(v.status, Nb4AxisStatus::Ready);
  EXPECT_NE(v.lineForPositive, v.lineForNegative);
  EXPECT_EQ(nb4AxisSharedCurveResource(v), 3u);

  ExpoData* pos = expoAddress(v.lineForPositive);
  pos->curve.value = makeSourceNumVal(5);
  const auto after = nb4ResolveAxis(Nb4AxisRole::Throttle);
  EXPECT_EQ(after.curveValuePositive, 5);
  EXPECT_EQ(after.curveValueNegative, 3);
  EXPECT_EQ(nb4AxisSharedCurveResource(after), 0u);
}

TEST(Nb4Curves, TheSameCurveMirroredIsStillTheSamePoints)
{
  plainCarModel();
  splitInput(1, 1, 2);
  setSideCurves(4, -4);

  const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
  EXPECT_EQ(nb4AxisSharedCurveResource(v), 4u);
}

TEST(Nb4Curves, WhatIsNotASharedResource)
{
  plainCarModel();
  splitInput(1, 1, 2);

  struct Case { void (*setup)(); uint8_t expected; };
  const Case cases[] = {
    {[]() { setSideCurves(2, 7); }, 0},
    {[]() { setSideCurves(2, 0); }, 0},
    {[]() {
       const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
       for (int8_t i : {v.lineForPositive, v.lineForNegative}) {
         expoAddress(i)->curve.type = CURVE_REF_EXPO;
         expoAddress(i)->curve.value = makeSourceNumVal(30);
       }
     }, 0},
    {[]() { setSideCurves(6, 6); }, 6},
  };

  for (const auto& c : cases) {
    c.setup();
    const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
    EXPECT_EQ(nb4AxisSharedCurveResource(v), c.expected);
  }

  plainCarModel();
  const auto shared = nb4ResolveAxis(Nb4AxisRole::Throttle);
  ASSERT_EQ(shared.status, Nb4AxisStatus::Shared);
  ExpoData* one = expoAddress(shared.lineForPositive);
  one->curve.type = CURVE_REF_CUSTOM;
  one->curve.value = makeSourceNumVal(3);
  EXPECT_EQ(nb4AxisSharedCurveResource(nb4ResolveAxis(Nb4AxisRole::Throttle)), 0u);
}

TEST(Nb4Curves, TheOtherUsersOfTheCurveAreCountedOutsideTheAxis)
{
  plainCarModel();
  splitInput(1, 1, 2);
  setSideCurves(3, 3);
  const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);

  EXPECT_EQ(nb4CurveOtherUsers(3, v), 0u);

  ExpoData* steer = expoAddress(0);
  steer->curve.type = CURVE_REF_CUSTOM;
  steer->curve.value = makeSourceNumVal(3);
  EXPECT_EQ(nb4CurveOtherUsers(3, v), 1u);

  mixAddress(0)->curve.type = CURVE_REF_CUSTOM;
  mixAddress(0)->curve.value = makeSourceNumVal(-3);
  EXPECT_EQ(nb4CurveOtherUsers(3, v), 2u);

  EXPECT_EQ(nb4CurveOtherUsers(7, v), 0u);
  EXPECT_EQ(nb4CurveOtherUsers(0, v), 0u);
}

TEST(Nb4Resolver, EveryBlockedConfigurationSaysWhereItIsFixed)
{
  struct Case { void (*breakIt)(); Nb4Blocker where; };
  const Case cases[] = {
    {[]() {
       mixAddress(1)->srcRaw = MIXSRC_FIRST_STICK;
     }, Nb4Blocker::Mix},
    {[]() {
       MixData* extra = mixAddress(4);
       extra->destCh = 1;
       extra->srcRaw = MIXSRC_FIRST_INPUT + 1;
       extra->weight = makeSourceNumVal(50);
     }, Nb4Blocker::Mix},
    {[]() {
       mixAddress(1)->weight = makeSourceNumVal(MIXSRC_FIRST_GVAR, true);
     }, Nb4Blocker::Mix},
    {[]() {
       mixAddress(1)->swtch = SWSRC_FIRST_SWITCH;
     }, Nb4Blocker::Mix},
    {[]() {
       expoAddress(1)->swtch = SWSRC_FIRST_SWITCH;
     }, Nb4Blocker::Input},
    {[]() {
       expoAddress(1)->weight = makeSourceNumVal(0);
     }, Nb4Blocker::Input},
    {[]() {
       expoAddress(2)->srcRaw = -expoAddress(2)->srcRaw;
     }, Nb4Blocker::Input},
    {[]() {
       expoAddress(1)->weight = makeSourceNumVal(MIXSRC_FIRST_GVAR, true);
     }, Nb4Blocker::Input},
    {[]() {
       expoAddress(2)->srcRaw = 0;
       expoAddress(1)->mode = 3;
     }, Nb4Blocker::Input},
  };

  for (const auto& c : cases) {
    plainCarModel();
    splitInput(1, 1, 2);
    ASSERT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).status, Nb4AxisStatus::Ready);
    c.breakIt();
    const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
    ASSERT_NE(v.status, Nb4AxisStatus::Ready);
    EXPECT_EQ(v.blocker, c.where);
  }

  plainCarModel();
  splitInput(1, 1, 2);
  EXPECT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).blocker, Nb4Blocker::None);
}

TEST(Nb4Chain, TheDrawnMapIsWhatTheChannelReallyDoes)
{
  struct Setup { void (*apply)(); bool throttle; };
  const Setup setups[] = {
    {[]() {}, false},
    {[]() {
       expoAddress(0)->curve.type = CURVE_REF_EXPO;
       expoAddress(0)->curve.value = makeSourceNumVal(45);
     }, false},
    {[]() {
       limitAddress(0)->min = 300;              // -70 %
       limitAddress(0)->max = -200;             // +80 %
     }, false},
    {[]() { limitAddress(0)->revert = 1; }, false},

    {[]() {
       for (uint8_t i = 0; i < MAX_EXPOS; i += 1)
         if (expoAddress(i)->srcRaw && expoAddress(i)->chn == 0)
           expoAddress(i)->srcRaw = (int16_t)-expoAddress(i)->srcRaw;
     }, false},
    {[]() { g_model.throttleReversed = 1; }, true},
    {[]() {
       expoAddress(0)->weight = makeSourceNumVal(60);
     }, false},
    {[]() {
       g_model.nb4Racing.brakeMax = 50;
     }, true},
    {[]() {
       g_model.nb4Racing.brakeMax = 80;
       g_model.nb4Racing.dragBrake = 20;
     }, true},
  };

  for (const auto& setup : setups) {
    plainCarModel();
    nb4RacingDefaults(g_model.nb4Racing);
    g_model.nb4Racing.steeringChannel = 0;
    g_model.nb4Racing.throttleChannel = 1;

    for (auto& fm : g_model.flightModeData) memclear(&fm.trim, sizeof(fm.trim));
#if defined(STICK_DEAD_ZONE)
    g_eeGeneral.stickDeadZone = 0;
#endif
    setup.apply();

    const auto role = setup.throttle ? Nb4AxisRole::Throttle : Nb4AxisRole::Steering;
    const auto view = nb4ResolveAxis(role);
    ASSERT_TRUE(nb4AxisMapIsDrawable(view));

    const uint8_t stick = setup.throttle
                              ? inputMappingGetThrottle()
                              : (uint8_t)(inputMappingGetThrottle() == 0 ? 1 : 0);

    for (int x = -1024; x <= 1024; x += 128) {
      const int visualX = (int)nb4AxisVisual(setup.throttle, x);
      const int16_t drawn = nb4AxisDrawnOutput(view, setup.throttle, visualX);
      const int16_t real = outputForStick(stick, (int16_t)x, view.outputChannel);

      EXPECT_NEAR(drawn, real, 2);
    }
  }
}

TEST(Nb4Params, SteppingAValueNeverTurnsItIntoASourceReference)
{
  plainCarModel();
  splitInput(1, 1, 2);
  const auto view = nb4ResolveAxis(Nb4AxisRole::Throttle);
  ASSERT_EQ(view.status, Nb4AxisStatus::Ready);

  struct Case { Nb4Param param; int16_t start; };
  const Case cases[] = {
    {Nb4Param::InputResponse, -30},
    {Nb4Param::InputResponse, 45},
    {Nb4Param::InputDualRate, -60},
  };

  for (const auto& c : cases) {
    ExpoData* line = expoAddress(view.lineForPositive);
    line->curve.type = CURVE_REF_EXPO;
    if (c.param == Nb4Param::InputResponse)
      line->curve.value = makeSourceNumVal(c.start);
    else
      line->weight = makeSourceNumVal(c.start);

    Nb4ParamCtx ctx;
    ctx.line = view.lineForPositive;
    const Nb4Numeric spec = nb4ParamNumeric(c.param, ctx);
    ASSERT_TRUE(spec.valid());

    EXPECT_EQ(spec.get(), c.start);
    spec.set(limit<int32_t>(spec.min, spec.get() + spec.step, spec.max));

    const int16_t raw = c.param == Nb4Param::InputResponse ? line->curve.value
                                                           : line->weight;
    SourceNumVal after;
    after.rawValue = raw;
    EXPECT_FALSE(after.isSource);
    EXPECT_EQ(after.value, c.start + spec.step);
  }
}

TEST(Nb4Chain, ADynamicWeightStillDrawsItsMap)
{
  plainCarModel();
  splitInput(1, 1, 2);
  ASSERT_TRUE(nb4AxisMapIsDrawable(nb4ResolveAxis(Nb4AxisRole::Throttle)));

  expoAddress(1)->weight = makeSourceNumVal(MIXSRC_FIRST_GVAR, true);
  const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
  ASSERT_EQ(v.status, Nb4AxisStatus::Dynamic);

  EXPECT_TRUE(nb4AxisMapIsDrawable(v));

  const uint8_t stick = inputMappingGetThrottle();
  for (auto& fm : g_model.flightModeData) memclear(&fm.trim, sizeof(fm.trim));
#if defined(STICK_DEAD_ZONE)
  g_eeGeneral.stickDeadZone = 0;
#endif
  for (int x = -1024; x <= 1024; x += 256) {
    const int16_t drawn = nb4AxisDrawnOutput(v, true, x);
    const int16_t real = outputForStick(stick, (int16_t)x, v.outputChannel);
    EXPECT_NEAR(drawn, real, 2);
  }

  mixAddress(1)->srcRaw = MIXSRC_FIRST_STICK;
  const auto broken = nb4ResolveAxis(Nb4AxisRole::Throttle);
  ASSERT_EQ(broken.status, Nb4AxisStatus::NotRepresentable);
  EXPECT_FALSE(nb4AxisMapIsDrawable(broken));
}

#endif  // RADIO_NB4_FAMILY
