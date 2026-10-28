/* SPDX-License-Identifier: GPL-2.0-only */
#include "nb4_nor_restore_core.h"
#include <string.h>
#include <new>
namespace nb4 {
bool NorRestoreCore::arm(const uint8_t hash[32]) {
  if (phase() != NorRestorePhase::Unarmed || !hash) return false;
  uint8_t any = 0; for (unsigned i = 0; i < 32; ++i) any |= hash[i];
  if (!any) return false;
  memcpy(expected_, hash, 32);
  phase_ = NorRestorePhase::Ready;
  return true;
}
bool NorRestoreCore::begin(uint32_t index, const uint8_t hash[32]) {
  if (phase() != NorRestorePhase::Ready || index != completed_ ||
      index >= capacity / sectorSize || !hash) return false;
  memcpy(blockHash_, hash, 32); received_ = 0;
  phase_ = NorRestorePhase::Receiving;
  return true;
}
bool NorRestoreCore::append(uint32_t offset, const uint8_t* data, size_t length) {
  if (phase() != NorRestorePhase::Receiving || !data || !length ||
      offset != received_ || length > sectorSize - received_) return false;
  memcpy(data_ + offset, data, length); received_ += length;
  return true;
}
bool NorRestoreCore::commit() {
  if (phase() != NorRestorePhase::Receiving || received_ != sectorSize) return false;
  Sha256 hash; hash.update(data_, sizeof(data_)); uint8_t digest[32]; hash.finish(digest);
  if (memcmp(digest, blockHash_, 32)) { fail(NorRestoreError::InputHash); return false; }
  offset_ = 0; matches_ = true; waiting_ = false;
  phase_ = NorRestorePhase::Checking;
  return true;
}
void NorRestoreCore::fail(NorRestoreError error) { error_ = error; phase_ = NorRestorePhase::Error; }
void NorRestoreCore::completeSector() {
  whole_.update(data_, sizeof(data_)); ++completed_;
  if (completed_ == capacity / sectorSize) {
    uint8_t digest[32]; whole_.finish(digest);
    if (memcmp(digest, expected_, 32)) { fail(NorRestoreError::FullHash); return; }
    phase_ = NorRestorePhase::Complete;
  } else phase_ = NorRestorePhase::Ready;
}
void NorRestoreCore::service(uint32_t now, const NorRestoreIo& io) {
  const auto state = phase();
  if (!working()) return;
  const uint32_t base = completed_ * sectorSize;
  if (state == NorRestorePhase::Checking || state == NorRestorePhase::Verifying) {
    uint8_t check[512];
    if (!io.read(io.context, base + offset_, check, sizeof(check))) { fail(NorRestoreError::Io); return; }
    if (memcmp(check, data_ + offset_, sizeof(check))) matches_ = false;
    offset_ += sizeof(check);
    if (offset_ < sectorSize) return;
    if (state == NorRestorePhase::Verifying) {
      if (!matches_) fail(NorRestoreError::Readback);
      else completeSector();
    } else if (matches_) completeSector();
    else {
      if (!io.erase(io.context, base)) { fail(NorRestoreError::Io); return; }
      ++changed_; since_ = now; phase_ = NorRestorePhase::Erasing;
    }
    return;
  }
  const int busy = io.busy(io.context);
  if (busy < 0) { fail(NorRestoreError::Io); return; }
  if (state == NorRestorePhase::Erasing || waiting_) {
    if (busy) {
      if (uint32_t(now - since_) >= (state == NorRestorePhase::Erasing ? 3000u : 50u))
        fail(NorRestoreError::Timeout);
      return;
    }
    if (state == NorRestorePhase::Erasing) offset_ = 0;
    else offset_ += pageSize;
    waiting_ = false;
    if (offset_ == sectorSize) {
      offset_ = 0; matches_ = true; phase_ = NorRestorePhase::Verifying; return;
    }
  }
  if (!io.program(io.context, base + offset_, data_ + offset_, pageSize)) {
    fail(NorRestoreError::Io); return;
  }
  waiting_ = true; since_ = now; phase_ = NorRestorePhase::Programming;
}
}
