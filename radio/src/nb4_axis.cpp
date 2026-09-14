/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "edgetx.h"

#if defined(RADIO_NB4_FAMILY)

#include "nb4_axis.h"
#include "mixes.h"
#include "nb4_car_state.h"  // nb4Text
#include "nb4_racing.h"

namespace {

Nb4AxisView fail(const char* reason, Nb4Blocker blocker,
                 Nb4AxisStatus status = Nb4AxisStatus::NotRepresentable)
{
  Nb4AxisView v{};
  v.status = status;
  v.reason = reason;
  v.blocker = blocker;
  v.outputChannel = 0xFF;
  v.inputChannel = 0xFF;
  v.lineForPositive = v.lineForNegative = -1;
  v.curveTypePositive = v.curveTypeNegative = -1;
  v.inputSign = v.mixSign = v.chainSign = 1;
  v.weightSignPositive = v.weightSignNegative = 1;
  v.inputReversalApplies = false;
  return v;
}

bool isReference(uint16_t stored)
{
  SourceNumVal v;
  v.rawValue = stored;
  return v.isSource;
}

int16_t literalOf(uint16_t stored)
{
  SourceNumVal v;
  v.rawValue = stored;
  return v.value;
}

}  // namespace

Nb4AxisView nb4ResolveAxis(Nb4AxisRole role)
{
  const uint8_t channel = role == Nb4AxisRole::Steering
                              ? g_model.nb4Racing.steeringChannel
                              : g_model.nb4Racing.throttleChannel;
  if (channel >= MAX_OUTPUT_CHANNELS)
    return fail(nb4Text("el canal declarado está fuera de rango",
                "the declared channel is out of range"), Nb4Blocker::Mix);

  int8_t mixIdx = -1;
  for (uint8_t i = 0; i < MAX_MIXERS; i += 1) {
    const MixData* mix = mixAddress(i);
    if (!mix->srcRaw) continue;
    if (mix->destCh != channel) continue;
    if (mixIdx >= 0) return fail(nb4Text("el canal lo alimentan varias mezclas",
                             "several mixes feed this channel"), Nb4Blocker::Mix);
    mixIdx = (int8_t)i;
  }
  if (mixIdx < 0) return fail(nb4Text("el canal no tiene ninguna mezcla",
                              "the channel has no mix at all"), Nb4Blocker::Mix);

  const MixData* mix = mixAddress(mixIdx);
  if (mix->swtch) return fail(nb4Text("la mezcla depende de un interruptor",
                                  "the mix depends on a switch"), Nb4Blocker::Mix);
  if (mix->srcRaw < MIXSRC_FIRST_INPUT || mix->srcRaw > MIXSRC_LAST_INPUT)
    return fail(nb4Text("la mezcla no pasa por una entrada, así que el expo no se aplica",
                 "the mix does not go through an input, so expo is not applied"),
                Nb4Blocker::Mix);
  if (isReference(mix->weight))
    return fail(nb4Text("el peso de la mezcla es una referencia",
                 "the mix weight is a reference"), Nb4Blocker::Mix,
                Nb4AxisStatus::Dynamic);

  const int16_t mixWeight = literalOf(mix->weight);
  if (mixWeight == 0) return fail(nb4Text("el peso de la mezcla es cero",
                                 "the mix weight is zero"), Nb4Blocker::Mix);

  Nb4AxisView v{};
  v.status = Nb4AxisStatus::Ready;
  v.reason = nullptr;
  v.outputChannel = channel;
  v.inputChannel = (uint8_t)(mix->srcRaw - MIXSRC_FIRST_INPUT);
  v.lineForPositive = v.lineForNegative = -1;
  v.curveTypePositive = v.curveTypeNegative = -1;
  v.curveValuePositive = v.curveValueNegative = 0;
  v.mixSign = mixWeight < 0 ? -1 : 1;
  v.outputReversed = limitAddress(channel)->revert != 0;
  v.inputSign = 1;
  v.weightSignPositive = v.weightSignNegative = 1;

  v.inputReversalApplies =
      (role == Nb4AxisRole::Throttle) && g_model.throttleReversed;

  uint8_t lines = 0;
  bool dynamic = false;
  int16_t firstSrcRaw = 0;
  for (uint8_t i = 0; i < MAX_EXPOS; i += 1) {
    ExpoData* ed = expoAddress(i);
    if (!ed->srcRaw) continue;
    if (ed->chn != v.inputChannel) continue;
    lines += 1;
    if (ed->swtch) return fail(nb4Text("una línea de la entrada depende de un interruptor",
                                 "an input line depends on a switch"),
                                Nb4Blocker::Input);

    const mixsrc_t src = ed->srcRaw < 0 ? (mixsrc_t)-ed->srcRaw : (mixsrc_t)ed->srcRaw;
    if (src < MIXSRC_FIRST_STICK || src > MIXSRC_LAST_STICK)
      return fail(nb4Text("la entrada no viene de un mando de la emisora",
                   "the input does not come from a stick"), Nb4Blocker::Input);

    if (lines == 1) {
      firstSrcRaw = ed->srcRaw;
      if (ed->srcRaw < 0) v.inputSign = -1;
    } else if (ed->srcRaw != firstSrcRaw) {
      return fail(nb4Text("las dos líneas no son el mismo mando con el mismo signo",
                   "the two lines are not the same stick with the same sign"),
                  Nb4Blocker::Input);
    }

    if (isReference(ed->weight) || isReference(ed->curve.value)) dynamic = true;

    int8_t weightSign = 1;
    if (!isReference(ed->weight)) {
      const int16_t w = literalOf(ed->weight);
      if (w == 0) return fail(nb4Text("una de las líneas tiene el peso a cero",
                              "one of the lines has zero weight"), Nb4Blocker::Input);
      weightSign = w < 0 ? -1 : 1;
    }

    if (ed->mode & 2) {  // v >= 0
      if (v.lineForPositive >= 0) return fail(nb4Text("dos líneas se pelean por el lado positivo",
                              "two lines fight over the positive side"),
                                              Nb4Blocker::Input);
      v.lineForPositive = (int8_t)i;
      v.curveTypePositive = (int8_t)ed->curve.type;
      v.curveValuePositive = literalOf(ed->curve.value);
      v.weightSignPositive = weightSign;
    }
    if (ed->mode & 1) {  // v < 0
      if (v.lineForNegative >= 0) return fail(nb4Text("dos líneas se pelean por el lado negativo",
                              "two lines fight over the negative side"),
                                              Nb4Blocker::Input);
      v.lineForNegative = (int8_t)i;
      v.curveTypeNegative = (int8_t)ed->curve.type;
      v.curveValueNegative = literalOf(ed->curve.value);
      v.weightSignNegative = weightSign;
    }
  }

  if (!lines) return fail(nb4Text("la entrada no tiene ninguna línea",
                            "the input has no line at all"), Nb4Blocker::Input);
  if (lines > 2) return fail(nb4Text("la entrada tiene más de dos líneas",
                                "the input has more than two lines"), Nb4Blocker::Input);

  if (v.lineForPositive < 0 || v.lineForNegative < 0)
    return fail(nb4Text("un lado del mando se queda sin línea, y ese lado queda muerto",
                 "one side of the stick has no line, and that side is dead"),
                Nb4Blocker::Input);

  v.chainSign = (int8_t)(v.inputSign * v.mixSign * (v.outputReversed ? -1 : 1));

  if (dynamic) {
    v.status = Nb4AxisStatus::Dynamic;
    v.reason = nb4Text("algún peso o valor de curva es una referencia a fuente",
                       "some weight or curve value is a source reference");

    v.blocker = Nb4Blocker::Input;
    return v;
  }

  if (v.lineForPositive == v.lineForNegative) {
    v.status = Nb4AxisStatus::Shared;
    v.reason = nb4Text("una sola línea atiende los dos lados",
                       "a single line serves both sides");

    v.blocker = Nb4Blocker::Input;
    return v;
  }

  return v;
}

int8_t nb4InputLineForStickSide(const Nb4AxisView& view, int8_t stickSide)
{
  if ((view.status != Nb4AxisStatus::Ready && view.status != Nb4AxisStatus::Shared) || !stickSide)
    return -1;
  const int8_t atExpo = stickSide * view.inputSign * (view.inputReversalApplies ? -1 : 1);
  return atExpo > 0 ? view.lineForPositive : view.lineForNegative;
}

Nb4Endpoint nb4EndpointForStickSide(const Nb4AxisView& view, int8_t stickSide)
{
  if (view.status != Nb4AxisStatus::Ready && view.status != Nb4AxisStatus::Shared)
    return Nb4Endpoint::Unknown;
  if (stickSide == 0) return Nb4Endpoint::Unknown;

  const int8_t afterInputReversal =
      view.inputReversalApplies ? (int8_t)-stickSide : stickSide;

  const int8_t atExpo = (int8_t)(afterInputReversal * view.inputSign);

  const int8_t lineIndex = atExpo >= 0 ? view.lineForPositive : view.lineForNegative;
  if (lineIndex < 0) return Nb4Endpoint::Unknown;
  const int8_t weightSign =
      atExpo >= 0 ? view.weightSignPositive : view.weightSignNegative;

  const int8_t atClamp = (int8_t)(atExpo * weightSign * view.mixSign);
  return atClamp < 0 ? Nb4Endpoint::Min : Nb4Endpoint::Max;
}

uint8_t nb4AxisSharedCurveResource(const Nb4AxisView& view)
{
  if (view.status != Nb4AxisStatus::Ready) return 0;   // Shared is a distinct state
  if (view.lineForPositive == view.lineForNegative) return 0;
  if (view.curveTypePositive != CURVE_REF_CUSTOM) return 0;
  if (view.curveTypeNegative != CURVE_REF_CUSTOM) return 0;

  const int16_t a = view.curveValuePositive;
  const int16_t b = view.curveValueNegative;
  if (a == 0 || b == 0) return 0;  // An input without a curve does not share a resource
  const int16_t absA = a < 0 ? (int16_t)-a : a;
  if (absA != (b < 0 ? (int16_t)-b : b)) return 0;
  if (absA > MAX_CURVES) return 0;
  return (uint8_t)absA;
}

uint8_t nb4CurveOtherUsers(uint8_t curveNumber, const Nb4AxisView& view)
{
  if (curveNumber == 0 || curveNumber > MAX_CURVES) return 0;
  uint8_t users = 0;

  for (uint8_t i = 0; i < MAX_EXPOS; i += 1) {
    if ((int8_t)i == view.lineForPositive || (int8_t)i == view.lineForNegative)
      continue;
    const ExpoData* ed = expoAddress(i);
    if (!ed->srcRaw) continue;
    if (ed->curve.type != CURVE_REF_CUSTOM) continue;
    if (isReference(ed->curve.value)) continue;
    const int16_t val = literalOf(ed->curve.value);
    if (val == curveNumber || val == -curveNumber) users += 1;
  }

  for (uint8_t i = 0; i < MAX_MIXERS; i += 1) {
    const MixData* mix = mixAddress(i);
    if (!mix->srcRaw) continue;
    if (mix->curve.type != CURVE_REF_CUSTOM) continue;
    if (isReference(mix->curve.value)) continue;
    const int16_t val = literalOf(mix->curve.value);
    if (val == curveNumber || val == -curveNumber) users += 1;
  }

  return users;
}

namespace {

const MixData* mixOfChannel(uint8_t channel)
{
  for (uint8_t i = 0; i < MAX_MIXERS; i += 1)
    if (mixAddress(i)->srcRaw && mixAddress(i)->destCh == channel)
      return mixAddress(i);
  return nullptr;
}
}  // namespace

bool nb4AxisMapIsDrawable(const Nb4AxisView& view)
{

  if (view.status == Nb4AxisStatus::NotRepresentable) return false;
  if (view.inputChannel >= MAX_INPUTS) return false;
  if (view.lineForPositive < 0 && view.lineForNegative < 0) return false;
  const MixData* mix = mixOfChannel(view.outputChannel);
  if (!mix) return false;

  return !mix->offset && !mix->curve.value;
}

int32_t nb4AxisVisual(bool isThrottle, int32_t value)
{

  return isThrottle ? value : -value;
}

int16_t nb4AxisDrawnOutput(const Nb4AxisView& view, bool isThrottle, int visualX)
{
  if (!nb4AxisMapIsDrawable(view)) return 0;

  const int x = (int)nb4AxisVisual(isThrottle, visualX);
  const int8_t line =
      view.lineForPositive >= 0 ? view.lineForPositive : view.lineForNegative;
  const MixData* mix = mixOfChannel(view.outputChannel);

  int32_t logical = x;
  if (view.inputReversalApplies) logical = -logical;
  logical *= view.inputSign;

  int16_t anas[MAX_INPUTS] = {0};
  applyExpos(anas, e_perout_mode_inactive_flight_mode, expoAddress(line)->srcRaw,
             (int16_t)logical);

  int32_t weight = getSourceNumFieldValue(mix->weight, -RESX, RESX);
  weight = calc100to256_16Bits(weight);
  int32_t dv = divRoundClosest((int32_t)anas[view.inputChannel] * weight, 10);

  const bool racingActive =
      isThrottle && g_model.nb4Racing.version == NB4_RACING_VERSION;

  if (racingActive) {

    Nb4RacingState scratch = {};
    const Nb4RacingData& cfg = g_model.nb4Racing;
    const bool cut = cfg.engineCutSw != 0 && getSwitch(cfg.engineCutSw);
    const bool idleUp = cfg.idleUpSw != 0 && getSwitch(cfg.idleUpSw);
    int16_t v = (int16_t)limit<int32_t>(INT16_MIN, dv / 256, INT16_MAX);
    v = nb4RacingThrottle(cfg, v, cut, idleUp, scratch, 0);
    dv = (int32_t)v * 256;
  }

  return applyLimits(view.outputChannel, dv);
}

Nb4RacingConvention nb4RacingConventionFor(const Nb4AxisView& view,
                                           int8_t brakeStickSide)
{
  switch (nb4EndpointForStickSide(view, brakeStickSide)) {
    case Nb4Endpoint::Min: return Nb4RacingConvention::Holds;
    case Nb4Endpoint::Max: return Nb4RacingConvention::Inverted;
    default:               return Nb4RacingConvention::Unknown;
  }
}

Nb4ThrottleSides nb4ResolveThrottleSides()
{
  Nb4ThrottleSides s{};
  s.known = false;
  s.accelSign = 1;
  s.brakeSign = -1;

  const Nb4AxisView v = nb4ResolveAxis(Nb4AxisRole::Throttle);
  if (v.status == Nb4AxisStatus::NotRepresentable) return s;

  int8_t accel = v.inputSign;
  if (g_model.throttleReversed) accel = (int8_t)-accel;

  s.known = true;
  s.accelSign = accel;
  s.brakeSign = (int8_t)-accel;
  return s;
}

const char* nb4RacingSignGateReason(const Nb4AxisView& throttleView,
                                    const Nb4ThrottleSides& sides)
{
  if (!sides.known)
    return nb4Text(
        "No se sabe qué mitad del gatillo frena, así que tampoco se sabe si el "
        "tope de freno y el ABS actuarían sobre el freno o sobre el gas.",
        "Which half of the trigger brakes is unknown, so whether brake limit and "
        "ABS would act on the brake or on the throttle is unknown too.");

  switch (nb4RacingConventionFor(throttleView, sides.brakeSign)) {
    case Nb4RacingConvention::Holds:
      return nullptr;
    case Nb4RacingConvention::Inverted:
      return nb4Text(
          "En este canal el freno llega con el signo del gas. El tope de freno y "
          "el ABS actuarían sobre el gas, y el ralentí alto sobre el freno.",
          "On this channel the brake arrives with the throttle's sign. Brake "
          "limit and ABS would act on the throttle, and idle-up on the brake.");
    default:
      return nb4Text(
          "No se puede seguir el signo del gatillo hasta la salida, así que no "
          "hay manera de saber sobre qué mitad actuarían el freno y el ABS.",
          "The trigger's sign cannot be followed to the output, so there is no "
          "way to know which half brake and ABS would act on.");
  }
}

void nb4RacingNeutraliseSignDependent()
{
  g_model.nb4Racing.brakeMax = 100;
  g_model.nb4Racing.dragBrake = 0;
  g_model.nb4Racing.absEnable = 0;
  g_model.nb4Racing.idleUp = 0;
}

#endif  // RADIO_NB4_FAMILY
