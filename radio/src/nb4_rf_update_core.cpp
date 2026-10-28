/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "nb4_rf_update_core.h"
#include <string.h>

namespace nb4 {
namespace {
uint32_t u32(const uint8_t* p) {
  return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void put32(uint8_t* p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(n >> (i * 8));
}
}
bool RfUpdateIdentity::operator==(const RfUpdateIdentity& v) const {
  return epoch == v.epoch && car == v.car && product == v.product &&
         hardware == v.hardware && version == v.version && bootloader == v.bootloader &&
         rfHardware == v.rfHardware && rfLibrary == v.rfLibrary;
}
bool RfUpdateCore::prepare(const RfUpdateImage& image, const RfUpdateIdentity& identity,
                           uint32_t operation, bool reinstall) {
  if (ownsLink() || phase_ == RfUpdatePhase::RecoveryRequired || !operation ||
      (image.reportedProduct ? image.reportedProduct : image.product) != identity.product ||
      !image.sha256 || image.size < 8 ||
      image.size > maxImageSize || image.version < identity.version ||
      (image.version == identity.version && !reinstall)) return false;
  image_ = image; identity_ = identity; operation_ = operation;
  phase_ = RfUpdatePhase::Ready; error_ = RfUpdateError::None;
  covered_ = 0; sent_ = manifest_ = finish_ = false;
  peerLength_ = 0; peerPinned_ = false;
  memset(coverage_, 0, sizeof(coverage_));
  return true;
}
bool RfUpdateCore::start(const RfUpdateIdentity& identity, uint32_t now) {
  if (phase_ != RfUpdatePhase::Ready) return false;
  if (!(identity == identity_)) { error_ = RfUpdateError::IdentityChanged; return false; }
  started_ = last_ = now; phase_ = RfUpdatePhase::Entering;
  return true;
}
bool RfUpdateCore::cancellable() const {
  return phase_ == RfUpdatePhase::Ready || (phase_ == RfUpdatePhase::Entering && !sent_);
}
bool RfUpdateCore::cancel() {
  if (!cancellable()) return false;
  phase_ = RfUpdatePhase::Cancelled;
  return true;
}
bool RfUpdateCore::cancelUntouchedApplication(const RfUpdateIdentity& running, uint32_t probeEpoch) {
  if (phase_ != RfUpdatePhase::RecoveryRequired || !operation_ ||
      peerLength_ || covered_ || !running.product || running.epoch == probeEpoch) return false;
  auto checked = running; checked.epoch = identity_.epoch;
  // The internal module belongs to the transmitter. Receiver proof also
  // requires the original car/binding key.
  if (!image_.receiver) checked.car = identity_.car;
  if (!(checked == identity_)) return false;
  phase_ = RfUpdatePhase::Cancelled; error_ = RfUpdateError::None; sent_ = false;
  return true;
}
bool RfUpdateCore::restoreResult(const RfUpdateImage& image, const RfUpdateIdentity& identity,
                                uint32_t operation, RfUpdatePhase phase) {
  if ((phase != RfUpdatePhase::Complete && phase != RfUpdatePhase::Cancelled) ||
      !prepare(image, identity, operation, true)) return false;
  phase_ = phase;
  covered_ = phase == RfUpdatePhase::Complete ? image.size : 0;
  return true;
}
bool RfUpdateCore::recover(const RfUpdateImage& image, const RfUpdateIdentity& identity,
                           uint32_t operation, const uint8_t* peer, uint8_t length, uint32_t now) {
  if (phase_ != RfUpdatePhase::RecoveryRequired ||
      (length && (length != (image.receiver ? 14 : 4) || !peer))) return false;
  phase_ = RfUpdatePhase::Idle;
  if (!prepare(image, identity, operation, true)) {
    requireRecovery(RfUpdateError::Protocol); return false;
  }
  if (length) memcpy(peer_, peer, length);
  peerLength_ = length;
  return start(identity, now);
}
bool RfUpdateCore::ownsLink() const {
  return phase_ == RfUpdatePhase::Entering || phase_ == RfUpdatePhase::Transfer ||
         phase_ == RfUpdatePhase::RecoveryRequired;
}
void RfUpdateCore::fail(RfUpdateError error) {
  error_ = error;
  // Once an entry command has left the transmitter, the peer may have erased
  // flash even if its acknowledgement was lost. Do not release driving.
  phase_ = sent_ ? RfUpdatePhase::RecoveryRequired : RfUpdatePhase::Cancelled;
}
void RfUpdateCore::service(uint32_t now, const RfUpdateIo& io) {
  if (phase_ == RfUpdatePhase::Entering && !sent_) {
    uint8_t manifest[14]{};
    if (image_.receiver) {
      put32(manifest + 2, image_.product); put32(manifest + 10, image_.size);
    }
    if (io.send(io.context, false, image_.receiver ? 0x30 : 0x23, 0, manifest,
                image_.receiver ? sizeof(manifest) : 0)) {
      sent_ = true; last_ = now;
      if (image_.receiver) manifest_ = true;
    }
  }
  if (phase_ == RfUpdatePhase::VerifyVersion && finish_) {
    const uint8_t done = 1;
    if (io.send(io.context, false, 0x27, 0, &done, 1)) finish_ = false;
  }
  if ((phase_ == RfUpdatePhase::Entering || phase_ == RfUpdatePhase::Transfer ||
       phase_ == RfUpdatePhase::VerifyVersion) &&
      (uint32_t(now - last_) >= (phase_ == RfUpdatePhase::VerifyVersion ? 30000u : 10000u) ||
       uint32_t(now - started_) >= 300000))
    fail(RfUpdateError::Timeout);
}
void RfUpdateCore::receive(uint8_t type, uint8_t opcode, uint8_t sequence,
                           const uint8_t* data, size_t length, uint32_t now,
                           const RfUpdateIo& io) {
  if (!sent_ || (phase_ != RfUpdatePhase::Entering && phase_ != RfUpdatePhase::Transfer)) return;
  if (!image_.receiver && opcode == 0x22 && type == 2 && !length &&
      phase_ == RfUpdatePhase::Entering && !manifest_) {
    const uint8_t ready = 1;
    if (io.send(io.context, true, opcode, sequence, &ready, 1)) last_ = now;
    return;
  }
  if ((image_.receiver && opcode == 0x33 && type == 3 && length == 14) ||
      (!image_.receiver && opcode == 0x24 && type == 2 && length == 4)) {
    if ((image_.receiver && (u32(data) != image_.product ||
          (data[4] | (uint32_t(data[5]) << 8)) != identity_.bootloader)) ||
        (!image_.receiver && !u32(data)) ||
        (peerLength_ && (length != peerLength_ || memcmp(peer_, data, length)))) {
      fail(RfUpdateError::IdentityChanged); return;
    }
    if (!peerPinned_) {
      if (!io.checkpoint || !io.checkpoint(io.context, data, length)) {
        fail(RfUpdateError::Storage); return;
      }
      memcpy(peer_, data, length); peerLength_ = length; peerPinned_ = true;
    }
    last_ = now;
    if (image_.receiver) return;
    const uint8_t ready = 1;
    if (io.send(io.context, true, opcode, sequence, &ready, 1)) last_ = now;
    return;
  }
  if (!image_.receiver && opcode == 0x25 && type == 1 && length == 0) {
    if (!peerPinned_) { fail(RfUpdateError::Protocol); return; }
    uint8_t manifest[10]{};
    put32(manifest + 6, image_.size);
    if (io.send(io.context, true, opcode, sequence, manifest, sizeof(manifest))) {
      manifest_ = true; last_ = now;
    }
    return;
  }
  if (opcode == (image_.receiver ? 0x31 : 0x26) && type == 1) {
    if (!manifest_ || !peerPinned_ || length != 5) { fail(RfUpdateError::Protocol); return; }
    const uint32_t offset = u32(data); const unsigned count = data[4];
    if (!count || count > maxChunk || offset > image_.size || count > image_.size - offset) {
      fail(RfUpdateError::Bounds); return;
    }
    uint8_t response[5 + maxChunk];
    memcpy(response, data, 5);
    if (!io.read(io.context, offset, response + 5, count)) { fail(RfUpdateError::Storage); return; }
    if (!io.send(io.context, true, opcode, sequence, response, 5 + count)) return;
    for (uint32_t i = offset; i < offset + count; ++i) {
      const uint8_t bit = 1u << (i & 7);
      if (!(coverage_[i / 8] & bit)) { coverage_[i / 8] |= bit; ++covered_; }
    }
    phase_ = RfUpdatePhase::Transfer; last_ = now;
    return;
  }
  if (opcode == (image_.receiver ? 0x32 : 0x27) && (type == 2 || type == 3 || type == 5)) {
    if (length != 1) { fail(RfUpdateError::Protocol); return; }
    if (data[0] != (image_.receiver ? 2 : 1)) { fail(RfUpdateError::PeerRejected); return; }
    if (covered_ != image_.size) { fail(RfUpdateError::Protocol); return; }
    phase_ = RfUpdatePhase::VerifyVersion;
    last_ = now; finish_ = !image_.receiver;
  }
}
void RfUpdateCore::verifyVersion(uint32_t product, uint32_t version, bool fresh) {
  if (phase_ != RfUpdatePhase::VerifyVersion || !fresh || finish_) return;
  if (product != (image_.reportedProduct ? image_.reportedProduct : image_.product) ||
      version != image_.version) { fail(RfUpdateError::Version); return; }
  phase_ = RfUpdatePhase::Complete;
}
}
