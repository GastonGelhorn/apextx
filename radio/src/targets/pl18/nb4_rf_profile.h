/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include <stdint.h>

namespace nb4 {

enum class Nb4RfCandidate : uint8_t {
  Unqualified,
  Usart3Pb10Pb11,
  Usart6Pc6Pc7,
};

enum class Nb4RfBuildProfile : uint8_t {
  Unqualified,
  CandidateUsart3,
  CandidateUsart6,
  RecoveredUsart6,
  Qualified,
};

enum class Nb4RfClockBus : uint8_t {
  None,
  Apb1,
  Apb2,
};

struct Nb4RfTransportDescriptor {
  Nb4RfCandidate candidate;
  const char* uart;
  const char* txPin;
  const char* rxPin;
  uint8_t alternateFunction;
  uint8_t dmaController;
  uint8_t dmaChannel;
  uint8_t txDmaStream;
  uint8_t rxDmaStream;
  Nb4RfClockBus clockBus;
  uint32_t peripheralClockHz;
  uint32_t baudrate;
  uint16_t baudDivisor;
};

constexpr Nb4RfTransportDescriptor kNb4RfUnqualifiedTransport = {
  Nb4RfCandidate::Unqualified, "none", "none", "none", 0, 0, 0, 0, 0,
  Nb4RfClockBus::None, 0, 0, 0,
};

constexpr Nb4RfTransportDescriptor kNb4RfCandidateUsart3 = {
  Nb4RfCandidate::Usart3Pb10Pb11, "USART3", "PB10", "PB11", 7, 1, 4, 3, 1,
  Nb4RfClockBus::Apb1, 42000000, 1500000, 28,
};

constexpr Nb4RfTransportDescriptor kNb4RfCandidateUsart6 = {
  Nb4RfCandidate::Usart6Pc6Pc7, "USART6", "PC6", "PC7", 8, 2, 5, 7, 2,
  Nb4RfClockBus::Apb2, 84000000, 1500000, 56,
};

enum class Nb4RfSharedLine : uint8_t {
  Pd11,
  Pi8,
};

enum class Nb4RfPinMode : uint8_t {
  Input,
  InputPullUp,
  InputPullDown,
  OutputPushPull,
  OutputOpenDrain,
};

enum class Nb4RfLevel : uint8_t {
  Keep,
  Low,
  High,
};

struct Nb4RfElectricalStep {
  Nb4RfSharedLine line;
  Nb4RfPinMode mode;
  Nb4RfLevel level;
  uint32_t delayAfterUs;
};

constexpr uint8_t NB4_RF_MAX_ELECTRICAL_STEPS = 8;

struct Nb4RfElectricalSequence {
  Nb4RfElectricalStep steps[NB4_RF_MAX_ELECTRICAL_STEPS];
  uint8_t count;
};

enum class Nb4RfFraming : uint8_t {
  Unknown,
  AddressedSlip,
  AddresslessSlip,
};

struct Nb4RfFramingProfile {
  Nb4RfFraming framing;
  uint8_t frameAddress;
  bool uartInverted;
  uint32_t cadenceUs;
  uint32_t responseTimeoutUs;
};

constexpr Nb4RfFramingProfile kNb4RfUnqualifiedFraming = {
  Nb4RfFraming::Unknown, 0, false, 0, 0,
};

constexpr uint16_t nb4RfResponseRetries(const Nb4RfFramingProfile& profile)
{
  return profile.cadenceUs && profile.responseTimeoutUs >= profile.cadenceUs
           ? profile.responseTimeoutUs / profile.cadenceUs - 1
           : 0;
}

struct Nb4QualifiedHardwareProfile {
  bool qualified;
  bool releaseQualified;
  const char* boardRevision;
  Nb4RfTransportDescriptor transport;
  Nb4RfElectricalSequence boot;
  Nb4RfElectricalSequence enable;
  Nb4RfElectricalSequence disable;
  Nb4RfElectricalSequence shutdown;
  Nb4RfFramingProfile framing;
  const char* benchAcceptanceSha256;
};

struct Nb4RfHal {
  void* context;
  void (*configure)(void*, Nb4RfSharedLine, Nb4RfPinMode);
  void (*write)(void*, Nb4RfSharedLine, Nb4RfLevel);
  void (*delayUs)(void*, uint32_t);
};

inline bool nb4RfApplyElectricalSequence(const Nb4RfElectricalSequence& sequence,
                                         const Nb4RfHal& hal)
{
  if (sequence.count > NB4_RF_MAX_ELECTRICAL_STEPS || !hal.configure ||
      !hal.write || !hal.delayUs) {
    return false;
  }

  for (uint8_t i = 0; i < sequence.count; ++i) {
    const auto& step = sequence.steps[i];
    const bool output = step.mode == Nb4RfPinMode::OutputPushPull ||
                        step.mode == Nb4RfPinMode::OutputOpenDrain;
    // Write BSRR before switching MODER to output. This prevents a transient
    // through the reset value when the mux is selected.
    if (output && step.level != Nb4RfLevel::Keep)
      hal.write(hal.context, step.line, step.level);
    hal.configure(hal.context, step.line, step.mode);
    if (step.delayAfterUs)
      hal.delayUs(hal.context, step.delayAfterUs);
  }
  return true;
}

}  // namespace nb4

#include "nb4_rf_qualified_generated.h"

namespace nb4 {

#if defined(NB4_RF_PROFILE_QUALIFIED) || defined(NB4_RF_PROFILE_RECOVERED_LAB)
static_assert(kNb4QualifiedHardwareProfile.qualified,
              "Active RF build requires a generated evidence profile");
static_assert(kNb4QualifiedHardwareProfile.framing.cadenceUs > 0 &&
                kNb4QualifiedHardwareProfile.framing.responseTimeoutUs >=
                  kNb4QualifiedHardwareProfile.framing.cadenceUs &&
                kNb4QualifiedHardwareProfile.framing.responseTimeoutUs %
                    kNb4QualifiedHardwareProfile.framing.cadenceUs ==
                  0,
              "Qualified RF timing must be positive and exactly representable");
  #if defined(NB4_RF_TRANSPORT_USART3)
static_assert(kNb4QualifiedHardwareProfile.transport.candidate ==
                Nb4RfCandidate::Usart3Pb10Pb11,
              "Generated RF transport and CMake route disagree");
  #elif defined(NB4_RF_TRANSPORT_USART6)
static_assert(kNb4QualifiedHardwareProfile.transport.candidate ==
                Nb4RfCandidate::Usart6Pc6Pc7,
              "Generated RF transport and CMake route disagree");
  #else
static_assert(false, "Qualified RF build has no transport");
  #endif
  #if defined(NB4_RF_FRAMING_ADDRESSED_SLIP)
static_assert(kNb4QualifiedHardwareProfile.framing.framing ==
                Nb4RfFraming::AddressedSlip,
              "Generated RF framing and CMake framing disagree");
  #elif defined(NB4_RF_FRAMING_ADDRESSLESS_SLIP)
static_assert(kNb4QualifiedHardwareProfile.framing.framing ==
                Nb4RfFraming::AddresslessSlip,
              "Generated RF framing and CMake framing disagree");
  #else
static_assert(false, "Qualified RF build has no framing");
  #endif
#endif

constexpr Nb4RfBuildProfile nb4RfBuildProfile()
{
#if defined(NB4_RF_PROFILE_QUALIFIED)
  return Nb4RfBuildProfile::Qualified;
#elif defined(NB4_RF_PROFILE_RECOVERED_LAB)
  return Nb4RfBuildProfile::RecoveredUsart6;
#elif defined(NB4_RF_PROFILE_CANDIDATE_USART3)
  return Nb4RfBuildProfile::CandidateUsart3;
#elif defined(NB4_RF_PROFILE_CANDIDATE_USART6)
  return Nb4RfBuildProfile::CandidateUsart6;
#else
  return Nb4RfBuildProfile::Unqualified;
#endif
}

constexpr const Nb4RfTransportDescriptor& nb4RfSelectedTransport()
{
#if defined(NB4_RF_PROFILE_QUALIFIED) || defined(NB4_RF_PROFILE_RECOVERED_LAB)
  return kNb4QualifiedHardwareProfile.transport;
#elif defined(NB4_RF_PROFILE_CANDIDATE_USART3)
  return kNb4RfCandidateUsart3;
#elif defined(NB4_RF_PROFILE_CANDIDATE_USART6)
  return kNb4RfCandidateUsart6;
#else
  return kNb4RfUnqualifiedTransport;
#endif
}

constexpr const Nb4RfFramingProfile& nb4RfSelectedFraming()
{
#if defined(NB4_RF_PROFILE_QUALIFIED) || defined(NB4_RF_PROFILE_RECOVERED_LAB)
  return kNb4QualifiedHardwareProfile.framing;
#else
  // Candidate builds stay fail-closed even after a profile has been generated.
  // This keeps both alternate transports permanently compilable in CI without
  // allowing either to inherit the qualified route's framing by accident.
  return kNb4RfUnqualifiedFraming;
#endif
}

constexpr bool nb4RfProfileCanTransmit()
{
#if defined(NB4_RF_PROFILE_QUALIFIED) || defined(NB4_RF_PROFILE_RECOVERED_LAB)
  return kNb4QualifiedHardwareProfile.qualified &&
         kNb4QualifiedHardwareProfile.transport.candidate !=
           Nb4RfCandidate::Unqualified &&
         kNb4QualifiedHardwareProfile.framing.framing != Nb4RfFraming::Unknown;
#else
  return false;
#endif
}

}  // namespace nb4
