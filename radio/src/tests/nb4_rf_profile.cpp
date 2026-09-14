/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "gtests.h"

#if defined(RADIO_NB4_FAMILY)

#if defined(RADIO_NB4)

#include "targets/pl18/nb4_rf_controller.h"

#include <vector>

namespace {

enum class OperationType { Configure, Write, Delay };

struct Operation {
  OperationType type;
  nb4::Nb4RfSharedLine line;
  uint32_t value;
};

struct FakeHal {
  std::vector<Operation> operations;

  static void configure(void* context, nb4::Nb4RfSharedLine line,
                        nb4::Nb4RfPinMode mode)
  {
    static_cast<FakeHal*>(context)->operations.push_back(
      {OperationType::Configure, line, static_cast<uint32_t>(mode)});
  }

  static void write(void* context, nb4::Nb4RfSharedLine line,
                    nb4::Nb4RfLevel level)
  {
    static_cast<FakeHal*>(context)->operations.push_back(
      {OperationType::Write, line, static_cast<uint32_t>(level)});
  }

  static void delay(void* context, uint32_t duration)
  {
    static_cast<FakeHal*>(context)->operations.push_back(
      {OperationType::Delay, nb4::Nb4RfSharedLine::Pd11, duration});
  }

  nb4::Nb4RfHal descriptor()
  {
    return {this, configure, write, delay};
  }
};

}  // namespace

TEST(Nb4RfProfile, CandidateDescriptorsAreIndependentAndExact)
{
  const auto& a = nb4::kNb4RfCandidateUsart3;
  EXPECT_EQ(a.candidate, nb4::Nb4RfCandidate::Usart3Pb10Pb11);
  EXPECT_STREQ(a.uart, "USART3");
  EXPECT_STREQ(a.txPin, "PB10");
  EXPECT_STREQ(a.rxPin, "PB11");
  EXPECT_EQ(a.alternateFunction, 7);
  EXPECT_EQ(a.dmaController, 1);
  EXPECT_EQ(a.dmaChannel, 4);
  EXPECT_EQ(a.txDmaStream, 3);
  EXPECT_EQ(a.rxDmaStream, 1);
  EXPECT_EQ(a.clockBus, nb4::Nb4RfClockBus::Apb1);
  EXPECT_EQ(a.peripheralClockHz, 42000000u);
  EXPECT_EQ(a.baudDivisor, 28);

  const auto& b = nb4::kNb4RfCandidateUsart6;
  EXPECT_EQ(b.candidate, nb4::Nb4RfCandidate::Usart6Pc6Pc7);
  EXPECT_STREQ(b.uart, "USART6");
  EXPECT_STREQ(b.txPin, "PC6");
  EXPECT_STREQ(b.rxPin, "PC7");
  EXPECT_EQ(b.alternateFunction, 8);
  EXPECT_EQ(b.dmaController, 2);
  EXPECT_EQ(b.dmaChannel, 5);
  EXPECT_EQ(b.txDmaStream, 7);
  EXPECT_EQ(b.rxDmaStream, 2);
  EXPECT_EQ(b.clockBus, nb4::Nb4RfClockBus::Apb2);
  EXPECT_EQ(b.peripheralClockHz, 84000000u);
  EXPECT_EQ(b.baudDivisor, 56);
}

TEST(Nb4RfProfile, InactiveDevelopmentProfilesAreFailClosed)
{
#if defined(NB4_RF_PROFILE_RECOVERED_LAB)
  EXPECT_TRUE(nb4::nb4RfProfileCanTransmit());
  EXPECT_EQ(nb4::nb4RfSelectedTransport().candidate,
            nb4::Nb4RfCandidate::Usart6Pc6Pc7);
  EXPECT_EQ(nb4::nb4RfSelectedFraming().framing,
            nb4::Nb4RfFraming::AddresslessSlip);
#else
  EXPECT_FALSE(nb4::nb4RfProfileCanTransmit());
  EXPECT_FALSE(nb4::Nb4RfController::prepare());
  EXPECT_EQ(nb4::Nb4RfController::getFault(),
            nb4::Nb4RfFault::UnqualifiedProfile);
#endif
}

TEST(Nb4RfProfile, UartErrorsAreDeferredOutOfInterruptContext)
{
  nb4::Nb4RfController::notifyUartErrorFromIsr();
  EXPECT_TRUE(nb4::Nb4RfController::servicePendingFault());
  EXPECT_EQ(nb4::Nb4RfController::getFault(), nb4::Nb4RfFault::UartError);
  EXPECT_FALSE(nb4::Nb4RfController::servicePendingFault());
#if defined(NB4_RF_PROFILE_RECOVERED_LAB)
  EXPECT_TRUE(nb4::Nb4RfController::prepare());
  EXPECT_EQ(nb4::Nb4RfController::getFault(), nb4::Nb4RfFault::None);
#endif
}

TEST(Nb4RfProfile, ResponseTimeoutIsConvertedToExactSchedulerRetries)
{
  const nb4::Nb4RfFramingProfile profile = {
    nb4::Nb4RfFraming::AddressedSlip, 0x31, false, 3000, 12000,
  };
  EXPECT_EQ(nb4::nb4RfResponseRetries(profile), 3);
  EXPECT_EQ(nb4::nb4RfResponseRetries(nb4::kNb4RfUnqualifiedFraming), 0);
}

TEST(Nb4RfProfile, FakeHalRecordsEveryElectricalOperationInOrder)
{
  nb4::Nb4RfElectricalSequence sequence = {};
  sequence.count = 2;
  sequence.steps[0] = {nb4::Nb4RfSharedLine::Pi8,
                       nb4::Nb4RfPinMode::OutputPushPull,
                       nb4::Nb4RfLevel::Low, 125};
  sequence.steps[1] = {nb4::Nb4RfSharedLine::Pd11,
                       nb4::Nb4RfPinMode::InputPullUp,
                       nb4::Nb4RfLevel::Keep, 0};

  FakeHal fake;
  EXPECT_TRUE(nb4::nb4RfApplyElectricalSequence(sequence, fake.descriptor()));
  ASSERT_EQ(fake.operations.size(), 4u);
  EXPECT_EQ(fake.operations[0].type, OperationType::Write);
  EXPECT_EQ(fake.operations[0].line, nb4::Nb4RfSharedLine::Pi8);
  EXPECT_EQ(fake.operations[1].type, OperationType::Configure);
  EXPECT_EQ(fake.operations[2].type, OperationType::Delay);
  EXPECT_EQ(fake.operations[2].value, 125u);
  EXPECT_EQ(fake.operations[3].type, OperationType::Configure);
  EXPECT_EQ(fake.operations[3].line, nb4::Nb4RfSharedLine::Pd11);
}

TEST(Nb4RfProfile, InvalidSequenceCannotTouchTheHal)
{
  nb4::Nb4RfElectricalSequence sequence = {};
  sequence.count = nb4::NB4_RF_MAX_ELECTRICAL_STEPS + 1;
  FakeHal fake;
  EXPECT_FALSE(nb4::nb4RfApplyElectricalSequence(sequence, fake.descriptor()));
  EXPECT_TRUE(fake.operations.empty());
}

#endif  // RADIO_NB4

#endif  // RADIO_NB4_FAMILY
