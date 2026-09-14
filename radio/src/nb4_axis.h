/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <stdint.h>

enum class Nb4AxisRole : uint8_t { Steering, Throttle };

enum class Nb4AxisStatus : uint8_t {
  Ready,
  Shared,
  Dynamic,
  NotRepresentable,  // The adapter cannot interpret this configuration
};

enum class Nb4Blocker : uint8_t {
  None,
  Input,
  Mix,      // A MixData entry, edited in Mixes
  Output,   // LimitData entry, edited in Outputs
};

struct Nb4AxisView {
  Nb4AxisStatus status;
  const char* reason;
  Nb4Blocker blocker;        // Location where the configuration can be fixed

  uint8_t outputChannel;     // Zero-based output channel
  uint8_t inputChannel;      // Logical input that feeds the output
  int8_t lineForPositive;
  int8_t lineForNegative;
  int8_t curveTypePositive;
  int8_t curveTypeNegative;

  int16_t curveValuePositive;
  int16_t curveValueNegative;

  bool outputReversed;       // LimitData.revert
  int8_t inputSign;
  int8_t mixSign;

  int8_t weightSignPositive;
  int8_t weightSignNegative;

  bool inputReversalApplies;

  int8_t chainSign;
};

Nb4AxisView nb4ResolveAxis(Nb4AxisRole role);

// Expo line selected by a physical stick side, before line/mix weights and
// output reverse. Includes negative sources and the throttle input reversal.
int8_t nb4InputLineForStickSide(const Nb4AxisView& view, int8_t stickSide);

enum class Nb4Endpoint : uint8_t { Min, Max, Unknown };

Nb4Endpoint nb4EndpointForStickSide(const Nb4AxisView& view, int8_t stickSide);

enum class Nb4RacingConvention : uint8_t {
  Holds,
  Inverted,    // Positive values would affect throttle
  Unknown,     // The graph cannot determine the convention
};

Nb4RacingConvention nb4RacingConventionFor(const Nb4AxisView& view,
                                           int8_t brakeStickSide);

uint8_t nb4AxisSharedCurveResource(const Nb4AxisView& view);

uint8_t nb4CurveOtherUsers(uint8_t curveNumber, const Nb4AxisView& view);

bool nb4AxisMapIsDrawable(const Nb4AxisView& view);
int16_t nb4AxisDrawnOutput(const Nb4AxisView& view, bool isThrottle, int x);

int32_t nb4AxisVisual(bool isThrottle, int32_t value);

struct Nb4ThrottleSides {
  bool known;
  int8_t accelSign;
  int8_t brakeSign;   // Opposite direction
};

Nb4ThrottleSides nb4ResolveThrottleSides();

const char* nb4RacingSignGateReason(const Nb4AxisView& throttleView,
                                    const Nb4ThrottleSides& sides);

void nb4RacingNeutraliseSignDependent();
