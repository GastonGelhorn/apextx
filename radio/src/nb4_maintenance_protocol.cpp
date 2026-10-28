/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "nb4_maintenance_protocol.h"
#include <string.h>

namespace nb4 {
static constexpr uint8_t magic[] = {'A', 'P', 'X', 'M'};
static uint32_t read32(const uint8_t* p)
{
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static void write32(uint8_t* p, uint32_t value)
{
  for (unsigned i = 0; i < 4; ++i) p[i] = value >> (8 * i);
}
uint32_t maintenanceCrc(const uint8_t* data, size_t length)
{
  uint32_t crc = ~uint32_t(0);
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((0u - (crc & 1)) & 0xedb88320u);
  }
  return ~crc;
}
size_t maintenanceEncode(uint8_t* output, size_t capacity, uint8_t command,
                         uint32_t request, const void* payload, size_t length)
{
  const size_t total = maintenanceHeaderSize + length + 4;
  if (!output || (!payload && length) || length > maintenanceMaxPayload || capacity < total)
    return 0;
  memcpy(output, magic, sizeof(magic));
  output[4] = 1;
  output[5] = command;
  output[6] = length;
  output[7] = length >> 8;
  write32(output + 8, request);
  if (length) memcpy(output + maintenanceHeaderSize, payload, length);
  write32(output + total - 4, maintenanceCrc(output, total - 4));
  return total;
}
uint32_t MaintenanceDecoder::request() const { return read32(bytes + 8); }
bool MaintenanceDecoder::push(uint8_t byte)
{
  if (used < sizeof(magic) && byte != magic[used]) {
    used = byte == magic[0] ? 1 : 0;
    if (used) bytes[0] = byte;
    return false;
  }
  bytes[used++] = byte;
  if (used == 5 && bytes[4] != 1) { reset(); return false; }
  if (used < maintenanceHeaderSize) return false;
  if (length() > maintenanceMaxPayload) { reset(); return false; }
  const size_t total = maintenanceHeaderSize + length() + 4;
  if (used < total) return false;
  const bool valid = read32(bytes + total - 4) == maintenanceCrc(bytes, total - 4);
  // Payload remains readable until the next push, including an adjacent frame.
  used = 0;
  return valid;
}
}
