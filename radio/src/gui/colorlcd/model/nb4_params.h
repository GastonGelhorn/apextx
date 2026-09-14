/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <functional>

#include "libui/form.h"

#if defined(RADIO_NB4_FAMILY)

enum class Nb4Param : uint8_t {

  ThrottleReversed,
  ThrottleTraceSource,
  ThrottleTrimIdleOnly,
  ThrottleTrimSource,

  ChannelTravelMin,
  ChannelTravelMax,
  ChannelSubtrim,
  ChannelReverse,

  SteeringTrim,
  ThrottleTrim,

  // NB4-specific fields in g_model.nb4Racing
  VehicleType,
  SteerSpeedTurn,
  SteerSpeedReturn,
  BrakeMax,
  DragBrake,
  AbsEnable,
  AbsPoint,
  AbsRate,
  AbsRelease,
  IdleUp,
  IdleUpSwitch,
  EngineCutSwitch,
  EngineCutPos,

  InputDualRate,
  InputResponse,

  COUNT,
};

struct Nb4ParamCtx {
  static constexpr uint8_t NONE = 0xFF;
  uint8_t channel = NONE;
  int8_t line = -1;

  std::function<void()> afterChange = nullptr;

  bool magnitude = false;
};

Window* nb4ParamControl(Window* parent, const rect_t& rect, Nb4Param p,
                        const Nb4ParamCtx& ctx = {});

enum class Nb4NumericKind : uint8_t { None, Plain, Gvar, Source };

struct Nb4Numeric {
  Nb4NumericKind kind = Nb4NumericKind::None;
  int32_t min = 0, max = 0;
  int32_t step = 1;
  std::function<int32_t()> get;
  std::function<void(int32_t)> set;
  const char* suffix = nullptr;
  const char* zeroText = nullptr;
  bool prec1 = false;           // Displayed with one decimal place
  bool magnitude = false;
  int32_t voffset = 0;
  int32_t vdefault = 0;

  Nb4NumericKind storedReference = Nb4NumericKind::None;
  bool valid() const { return kind != Nb4NumericKind::None; }
};

Nb4Numeric nb4ParamNumeric(Nb4Param p, const Nb4ParamCtx& ctx = {});

void nb4ParamRow(Window* form, FlexGridLayout& grid, Nb4Param p,
                 const Nb4ParamCtx& ctx = {}, const char* labelOverride = nullptr);

void nb4ParamPair(Window* form, FlexGridLayout& grid, Nb4Param a,
                  const Nb4ParamCtx& ctxA, const char* labelA, Nb4Param b,
                  const Nb4ParamCtx& ctxB, const char* labelB);

void nb4ParamRegistryReset();
unsigned nb4ParamBuilt(Nb4Param p, uint8_t channel = Nb4ParamCtx::NONE);

bool nb4ParamOwnsObject(const void* lvObject);
unsigned nb4ParamRegistrySize();
void nb4ParamRegistryEntry(unsigned index, Nb4Param* p, uint8_t* channel,
                           unsigned* count);

const char* nb4ParamLabel(Nb4Param p);

const char* nb4ParamUnit();

#endif  // RADIO_NB4_FAMILY
