/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace nb4 {
constexpr size_t maintenanceMaxPayload = 1024;
constexpr size_t maintenanceHeaderSize = 12;
constexpr size_t maintenanceFrameSize = maintenanceHeaderSize + maintenanceMaxPayload + 4;
enum class MaintenanceCommand : uint8_t {
  Capabilities = 1, Inventory = 2, StorageSession = 3,
  Prepare = 16, Upload = 17, Start = 18, Status = 19, Cancel = 20,
  Recover = 21,
  // Catalog assets kept on the card for a later in-radio installation.
  AssetBegin = 22, AssetData = 23,
  // Restart into the update (DFU) mode once the reply has been sent.
  RebootUpdate = 24,
};
uint32_t maintenanceCrc(const uint8_t* data, size_t length);
size_t maintenanceEncode(uint8_t* output, size_t capacity, uint8_t command,
                         uint32_t request, const void* payload, size_t length);
// Single-task bounded decoder. USB IRQs enqueue bytes, never execute commands.
class MaintenanceDecoder {
 public:
  bool push(uint8_t byte);
  void reset() { used = 0; }
  uint8_t command() const { return bytes[5]; }
  uint32_t request() const;
  const uint8_t* payload() const { return bytes + maintenanceHeaderSize; }
  size_t length() const { return bytes[6] | (size_t(bytes[7]) << 8); }
 private:
  uint8_t bytes[maintenanceFrameSize] = {};
  size_t used = 0;
};
}
