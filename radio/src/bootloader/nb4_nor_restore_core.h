/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "nb4_sha256.h"
#include <atomic>
#include <stddef.h>
#include <stdint.h>

namespace nb4 {
enum class NorRestorePhase : uint32_t {
  Unarmed, Ready, Receiving, Checking, Erasing, Programming, Verifying, Complete, Error
};
enum class NorRestoreError : uint32_t { None, InputHash, Io, Timeout, Readback, FullHash };
struct NorRestoreIo {
  void* context;
  bool (*read)(void*, uint32_t, uint8_t*, size_t);
  bool (*erase)(void*, uint32_t);
  bool (*program)(void*, uint32_t, const uint8_t*, size_t);
  int (*busy)(void*); // -1: I/O error, 0: ready, 1: busy
};
// Only the separate SRAM restore agent links this writer. A complete checked
// sector is required before erasing; no filesystem or internal-flash access.
class NorRestoreCore {
 public:
  static constexpr uint32_t capacity = 8388608, sectorSize = 4096, pageSize = 256;
  bool arm(const uint8_t hash[32]);
  bool begin(uint32_t index, const uint8_t hash[32]);
  bool append(uint32_t offset, const uint8_t*, size_t);
  bool commit();
  void service(uint32_t now, const NorRestoreIo&);
  NorRestorePhase phase() const { return phase_.load(); }
  NorRestoreError error() const { return error_; }
  uint32_t completed() const { return completed_; }
  uint32_t changed() const { return changed_; }
  bool working() const { return phase() >= NorRestorePhase::Checking && phase() <= NorRestorePhase::Verifying; }
 private:
  void fail(NorRestoreError);
  void completeSector();
  std::atomic<NorRestorePhase> phase_{NorRestorePhase::Unarmed};
  NorRestoreError error_ = NorRestoreError::None;
  uint32_t completed_ = 0, changed_ = 0, received_ = 0, offset_ = 0, since_ = 0;
  bool matches_ = true, waiting_ = false;
  uint8_t expected_[32]{}, blockHash_[32]{}, data_[sectorSize]{};
  Sha256 whole_;
};
}
