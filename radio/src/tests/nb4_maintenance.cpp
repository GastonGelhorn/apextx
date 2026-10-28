/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "gtests.h"
#if defined(RADIO_NB4)
#include "nb4_maintenance_protocol.h"
using namespace nb4;

TEST(Nb4Maintenance, FragmentedPacketsKeepRequestAndCheckIntegrity) {
  uint8_t frame[maintenanceFrameSize];
  const char payload[] = "{\"rf_write\":false}";
  const auto count = maintenanceEncode(frame, sizeof(frame), 2, 0x12345678,
                                       payload, sizeof(payload) - 1);
  ASSERT_GT(count, 0u);
  MaintenanceDecoder decoder;
  for (unsigned repeat = 0; repeat < 2; ++repeat) {
    for (size_t i = 0; i < count; ++i)
      EXPECT_EQ(decoder.push(frame[i]), i == count - 1);
    EXPECT_EQ(decoder.command(), 2);
    EXPECT_EQ(decoder.request(), 0x12345678u);
    EXPECT_EQ(decoder.length(), sizeof(payload) - 1);
    EXPECT_EQ(memcmp(decoder.payload(), payload, decoder.length()), 0);
  }
  frame[count - 1] ^= 1;
  for (size_t i = 0; i < count; ++i) EXPECT_FALSE(decoder.push(frame[i]));
}
TEST(Nb4Maintenance, MalformedAndOversizedFramesCannotEscapeBuffer) {
  uint8_t frame[maintenanceFrameSize];
  EXPECT_EQ(maintenanceEncode(frame, sizeof(frame), 1, 1, frame,
                              maintenanceMaxPayload + 1), 0u);
  EXPECT_EQ(maintenanceEncode(frame, 2, 1, 1, nullptr, 0), 0u);
  MaintenanceDecoder decoder;
  const uint8_t invalid[] = {'A', 'A', 'P', 'X', 'M', 1, 1, 0xff, 0xff, 0, 0, 0, 0};
  for (auto byte : invalid) EXPECT_FALSE(decoder.push(byte));
  const size_t size = maintenanceEncode(frame, sizeof(frame), 1, 4, nullptr, 0);
  for (size_t i = 0; i < size; ++i) EXPECT_EQ(decoder.push(frame[i]), i == size - 1);
  EXPECT_EQ(decoder.request(), 4u);
  EXPECT_EQ(maintenanceCrc(reinterpret_cast<const uint8_t*>("123456789"), 9), 0xcbf43926u);
}
#endif
