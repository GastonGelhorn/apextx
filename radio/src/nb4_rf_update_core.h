/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace nb4 {
enum class RfUpdatePhase : uint8_t {
  Idle, Ready, Entering, Transfer, VerifyVersion, Complete, Cancelled, RecoveryRequired
};
enum class RfUpdateError : uint8_t {
  None, IdentityChanged, Bounds, Storage, Protocol, PeerRejected, Timeout, Version
};
struct RfUpdateImage {
  uint32_t product, version, size;
  const uint8_t* sha256;
  bool receiver;
  uint32_t reportedProduct = 0;
};
struct RfUpdateIdentity {
  uint32_t epoch, car, product, hardware, version;
  uint32_t bootloader = 0;
  uint32_t rfHardware = 0, rfLibrary = 0;
  bool operator==(const RfUpdateIdentity& other) const;
};
struct RfUpdateIo {
  void* context;
  bool (*read)(void*, uint32_t, uint8_t*, size_t);
  // response=true preserves the peer sequence; requests use the transport's
  // own sequence and correlation. Called only from the maintenance task.
  bool (*send)(void*, bool response, uint8_t opcode, uint8_t sequence,
               const uint8_t*, uint8_t);
  bool (*checkpoint)(void*, const uint8_t*, uint8_t) = nullptr;
};
// No allocation, filesystem, USB or UART access. The same controller serves
// local and USB maintenance. Only the owner task calls its methods.
class RfUpdateCore {
 public:
  static constexpr uint32_t maxImageSize = 65536;
  static constexpr uint8_t maxChunk = 164;
  bool prepare(const RfUpdateImage&, const RfUpdateIdentity&, uint32_t operation,
               bool reinstall = false);
  bool start(const RfUpdateIdentity&, uint32_t now);
  bool cancel();
  bool cancelUntouchedApplication(const RfUpdateIdentity& running, uint32_t probeEpoch);
  bool recover(const RfUpdateImage&, const RfUpdateIdentity&, uint32_t operation,
                const uint8_t* peer, uint8_t peerLength, uint32_t now);
  void service(uint32_t now, const RfUpdateIo&);
  void receive(uint8_t type, uint8_t opcode, uint8_t sequence,
               const uint8_t*, size_t, uint32_t now, const RfUpdateIo&);
  void verifyVersion(uint32_t product, uint32_t version, bool fresh);
  void fail(RfUpdateError);
  void requireRecovery(RfUpdateError error) { sent_ = true; fail(error); }
  bool ownsLink() const;
  bool cancellable() const;
  RfUpdatePhase phase() const { return phase_; }
  RfUpdateError error() const { return error_; }
  uint32_t operation() const { return operation_; }
  uint32_t transferred() const { return covered_; }
  uint32_t size() const { return image_.size; }
  bool receiver() const { return image_.receiver; }
 private:
  RfUpdateImage image_{};
  RfUpdateIdentity identity_{};
  RfUpdatePhase phase_ = RfUpdatePhase::Idle;
  RfUpdateError error_ = RfUpdateError::None;
  uint32_t operation_ = 0, last_ = 0, started_ = 0, covered_ = 0;
  uint8_t coverage_[maxImageSize / 8] = {};
  bool sent_ = false, manifest_ = false, finish_ = false;
  uint8_t peer_[14]{};
  uint8_t peerLength_ = 0;
  bool peerPinned_ = false;
};
}
