/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "gtests.h"
#include "location.h"
#if defined(RADIO_NB4)
#include "nb4_rf_update_core.h"
#include "nb4_sha256.h"
#include "nb4_rf_update.h"
#include "nb4_rf_images.h"
#include "pulses/afhds3_transport.h"
#include "nb4_maintenance_protocol.h"
#include "nb4_history.h"
#include "bootloader/nb4_update_id.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>
using namespace nb4;
namespace {
struct Peer {
  uint8_t opcode = 0, sequence = 0;
  bool response = false, disk = true, writable = true;
  std::vector<uint8_t> payload;
  std::vector<uint8_t> checkpoint;
  std::vector<std::pair<uint32_t, size_t>> reads;
  RfUpdateIo io() {
    return {this, [](void* ctx, uint32_t at, uint8_t* out, size_t size) {
      static_cast<Peer*>(ctx)->reads.emplace_back(at, size);
      for (size_t i = 0; i < size; ++i) out[i] = uint8_t(at + i);
      return static_cast<Peer*>(ctx)->disk;
    }, [](void* ctx, bool response, uint8_t opcode, uint8_t sequence,
          const uint8_t* bytes, uint8_t count) {
      auto& self = *static_cast<Peer*>(ctx);
      if (!self.writable) return false;
      self.response = response; self.opcode = opcode; self.sequence = sequence;
      self.payload.assign(bytes, bytes + count);
      return true;
    }, [](void* ctx, const uint8_t* bytes, uint8_t count) {
      auto& self = *static_cast<Peer*>(ctx);
      self.checkpoint.assign(bytes, bytes + count);
      return self.disk;
    }};
  }
};
const uint8_t hash[32]{};
const RfUpdateIdentity identity{3, 4, 0x12340004, 7, 0x112, 0x103};
const RfUpdateImage image{0x12340004, 0x113, 20, hash, true};
void boot(RfUpdateCore& core, Peer& peer) {
  const uint8_t receiver[] = {4,0,0x34,0x12,0x12,1,1,2,3,4,5,6,7,8};
  const uint8_t module[] = {3,1,0,0};
  core.receive(core.receiver() ? 3 : 2, core.receiver() ? 0x33 : 0x24, 90,
               core.receiver() ? receiver : module, core.receiver() ? 14 : 4, 90, peer.io());
}
void chunk(RfUpdateCore& core, Peer& peer, uint8_t offset, uint8_t count) {
  if (core.phase() == RfUpdatePhase::Entering) boot(core, peer);
  const uint8_t request[] = {offset, 0, 0, 0, count};
  core.receive(1, core.receiver() ? 0x31 : 0x26, 91, request, sizeof(request), 100, peer.io());
}
TEST(Nb4RfUpdate, Sha256MatchesStandardVectorsAcrossBlockAndPaddingBoundaries) {
  const std::pair<std::string, const char*> vectors[] = {
    {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    {std::string(1000000, 'a'), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
    {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"}
  };
  for (const auto& vector : vectors) {
    Sha256 sha;
    for (const auto byte : vector.first) {
      const auto input = uint8_t(byte); sha.update(&input, 1);
    }
    uint8_t digest[32]; sha.finish(digest);
    char text[65];
    for (unsigned i = 0; i < 32; ++i) snprintf(text + i * 2, 3, "%02x", digest[i]);
    EXPECT_STREQ(text, vector.second);
  }
}
TEST(Nb4RfUpdate, FailedModuleEntryNeedsFreshOriginalApplicationAndNoBootCheckpointBeforeUnlocking) {
  const RfUpdateIdentity original{5,7,0x10003,0x10003,0x10021,0,0,0x30000};
  const RfUpdateImage module{0x12340003,0x10021,20,hash,false,0x10003};
  Peer peer; RfUpdateCore core;
  ASSERT_TRUE(core.prepare(module,original,1,true)); ASSERT_TRUE(core.start(original,0)); core.service(0,peer.io());
  core.fail(RfUpdateError::Timeout);
  EXPECT_FALSE(core.cancelUntouchedApplication(original,5));
  auto fresh=original; fresh.epoch=6;
  auto wrong=fresh; wrong.version=0x10020; EXPECT_FALSE(core.cancelUntouchedApplication(wrong,5));
  wrong=fresh; wrong.rfLibrary=0x30001; EXPECT_FALSE(core.cancelUntouchedApplication(wrong,5));
  EXPECT_TRUE(core.cancelUntouchedApplication(fresh,5)); EXPECT_EQ(core.phase(),RfUpdatePhase::Cancelled);
  RfUpdateCore entered;
  ASSERT_TRUE(entered.prepare(module,original,2,true)); ASSERT_TRUE(entered.start(original,0)); entered.service(0,peer.io());
  boot(entered,peer); entered.fail(RfUpdateError::Timeout);
  EXPECT_FALSE(entered.cancelUntouchedApplication(fresh,5)); EXPECT_EQ(entered.phase(),RfUpdatePhase::RecoveryRequired);
  RfUpdateCore receiver; ASSERT_TRUE(receiver.prepare(image,identity,3));
  ASSERT_TRUE(receiver.start(identity,0)); receiver.service(0,peer.io()); receiver.fail(RfUpdateError::Timeout);
  auto freshReceiver=identity; ++freshReceiver.epoch;
  auto otherCar=freshReceiver; ++otherCar.car; EXPECT_FALSE(receiver.cancelUntouchedApplication(otherCar,identity.epoch));
  EXPECT_TRUE(receiver.cancelUntouchedApplication(freshReceiver,identity.epoch));
}
}
TEST(Nb4RfUpdate, PinsIdentityRejectsDowngradesAndOnlyCancelsBeforeEntry) {
  RfUpdateCore core; Peer peer;
  auto old = image; old.version = identity.version;
  EXPECT_FALSE(core.prepare(old, identity, 1));
  EXPECT_FALSE(core.prepare(image, identity, 0));
  ASSERT_TRUE(core.prepare(image, identity, 1));
  auto changed = identity; ++changed.epoch;
  EXPECT_FALSE(core.start(changed, 0));
  ASSERT_TRUE(core.start(identity, 0));
  ASSERT_TRUE(core.cancel());
  EXPECT_EQ(core.phase(), RfUpdatePhase::Cancelled);
  ASSERT_TRUE(core.prepare(image, identity, 2));
  ASSERT_TRUE(core.start(identity, 0)); core.service(0, peer.io());
  EXPECT_EQ(peer.opcode, 0x30);
  EXPECT_EQ(peer.payload, (std::vector<uint8_t>{0,0,4,0,0x34,0x12,0,0,0,0,20,0,0,0}));
  EXPECT_FALSE(core.cancel());
  core.service(10001, peer.io());
  EXPECT_EQ(core.phase(), RfUpdatePhase::RecoveryRequired);
  EXPECT_TRUE(core.ownsLink());
  EXPECT_FALSE(core.prepare(image, identity, 3));
}
TEST(Nb4RfUpdate, DuplicateAndOutOfOrderChunksCountEachByteOnceAndVerifyFreshVersion) {
  RfUpdateCore core; Peer peer;
  ASSERT_TRUE(core.prepare(image, identity, 1)); ASSERT_TRUE(core.start(identity, 0));
  core.service(0, peer.io());
  chunk(core, peer, 10, 10); EXPECT_EQ(core.transferred(), 10u);
  chunk(core, peer, 10, 10); EXPECT_EQ(core.transferred(), 10u);
  EXPECT_EQ(peer.sequence, 91); EXPECT_TRUE(peer.response);
  EXPECT_EQ(peer.payload.size(), 15u); EXPECT_EQ(peer.payload.back(), 19);
  chunk(core, peer, 0, 10); EXPECT_EQ(core.transferred(), 20u);
  uint8_t result = 2;
  core.receive(5, 0x32, 92, &result, 1, 100, peer.io());
  EXPECT_EQ(core.phase(), RfUpdatePhase::VerifyVersion);
  core.verifyVersion(image.product, image.version, false);
  EXPECT_EQ(core.phase(), RfUpdatePhase::VerifyVersion);
  core.verifyVersion(image.product, image.version, true);
  EXPECT_EQ(core.phase(), RfUpdatePhase::Complete);
}
TEST(Nb4RfUpdate, ReceiverEntryChecksTheCurrentApplicationVersionInsteadOfTheBootloader) {
  const RfUpdateIdentity current{3,4,0x12340006,0x104,0x111,0x107,0x104,0x301};
  const RfUpdateImage candidate{0x12340006,0x111,50596,image.sha256,true};
  const uint8_t manifest[]{6,0,0x34,0x12,0x11,1,0xa0,0xc5,0,0,0x75,0xf7,0x72,0xbf};
  Peer peer; RfUpdateCore core;
  ASSERT_TRUE(core.prepare(candidate,current,1,true)); ASSERT_TRUE(core.start(current,0));
  core.service(0,peer.io());
  core.receive(3,0x33,90,manifest,sizeof(manifest),1,peer.io());
  ASSERT_EQ(core.error(),RfUpdateError::None);
  EXPECT_EQ(peer.checkpoint,(std::vector<uint8_t>(manifest,manifest+sizeof(manifest))));
  const uint8_t request[]{0,0,0,0,64};
  core.receive(1,0x31,91,request,sizeof(request),2,peer.io());
  EXPECT_EQ(core.transferred(),64u);
  for (const uint16_t changed : {0x107,0x110,0x112}) {
    Peer rejected; RfUpdateCore other;
    ASSERT_TRUE(other.prepare(candidate,current,2,true)); ASSERT_TRUE(other.start(current,0));
    other.service(0,rejected.io());
    auto wrong=std::vector<uint8_t>(manifest,manifest+sizeof(manifest));
    wrong[4]=uint8_t(changed); wrong[5]=uint8_t(changed>>8);
    other.receive(3,0x33,90,wrong.data(),wrong.size(),1,rejected.io());
    EXPECT_EQ(other.error(),RfUpdateError::IdentityChanged);
    EXPECT_EQ(other.transferred(),0u); EXPECT_TRUE(rejected.checkpoint.empty());
  }
}
TEST(Nb4RfUpdate, AFinalChunkOrEarlySuccessDoesNotHideMissingBytes) {
  RfUpdateCore core; Peer peer;
  ASSERT_TRUE(core.prepare(image, identity, 1)); ASSERT_TRUE(core.start(identity, 0));
  core.service(0, peer.io()); chunk(core, peer, 19, 1);
  const uint8_t result = 2;
  core.receive(3, 0x32, 1, &result, 1, 100, peer.io());
  EXPECT_EQ(core.phase(), RfUpdatePhase::RecoveryRequired);
  EXPECT_EQ(core.error(), RfUpdateError::Protocol);
}
TEST(Nb4RfUpdate, ReceiverFinalWindowPadsWithoutReadingOrCountingBytesOutsideTheImage) {
  for (const uint32_t size : {50596u,51432u}) {
    auto candidate=image; candidate.size=size;
    RfUpdateCore core; Peer peer;
    ASSERT_TRUE(core.prepare(candidate,identity,1)); ASSERT_TRUE(core.start(identity,0));
    core.service(0,peer.io()); boot(core,peer);
    const uint32_t wireSize=(size+255u)&~255u;
    for (uint32_t offset=0; offset<wireSize; offset+=64) {
      const uint8_t request[]{uint8_t(offset),uint8_t(offset>>8),uint8_t(offset>>16),0,64};
      const auto previousReads=peer.reads.size();
      core.receive(1,0x31,91,request,sizeof(request),100,peer.io());
      ASSERT_EQ(core.error(),RfUpdateError::None); ASSERT_EQ(peer.payload.size(),69u);
      if (offset>=size) EXPECT_EQ(peer.reads.size(),previousReads);
      for (unsigned i=0;i<64;++i)
        EXPECT_EQ(peer.payload[5+i],offset+i<size ? uint8_t(offset+i) : 0xff);
    }
    for (const auto& read : peer.reads) EXPECT_LE(read.first+read.second,size);
    EXPECT_EQ(core.transferred(),size);
    const uint8_t result=2; core.receive(3,0x32,92,&result,1,101,peer.io());
    EXPECT_EQ(core.phase(),RfUpdatePhase::VerifyVersion);
  }
}
TEST(Nb4RfUpdate, ReceiverPaddingHasStrictAlignmentAndWindowBoundsAndCannotHideMissingFirmware) {
  auto candidate=image; candidate.size=100;
  for (const auto range : {std::pair<uint32_t,uint8_t>{128,64},{256,64},{128,63},{100,64},{0xffffffc0,64}}) {
    RfUpdateCore core; Peer peer;
    ASSERT_TRUE(core.prepare(candidate,identity,1)); ASSERT_TRUE(core.start(identity,0));
    core.service(0,peer.io()); boot(core,peer);
    const auto offset=range.first;
    const uint8_t request[]{uint8_t(offset),uint8_t(offset>>8),uint8_t(offset>>16),uint8_t(offset>>24),range.second};
    core.receive(1,0x31,91,request,sizeof(request),1,peer.io());
    EXPECT_EQ(core.transferred(),0u); EXPECT_TRUE(peer.reads.empty());
    if (offset==128 && range.second==64) {
      ASSERT_EQ(core.error(),RfUpdateError::None);
      const uint8_t result=2; core.receive(3,0x32,92,&result,1,2,peer.io());
      EXPECT_EQ(core.error(),RfUpdateError::Protocol);
    } else EXPECT_EQ(core.error(),RfUpdateError::Bounds);
  }
  candidate.receiver=false; RfUpdateCore module; Peer peer;
  ASSERT_TRUE(module.prepare(candidate,identity,1)); ASSERT_TRUE(module.start(identity,0));
  module.service(0,peer.io()); boot(module,peer);
  module.receive(1,0x25,90,nullptr,0,1,peer.io());
  const uint8_t request[]{64,0,0,0,64};
  module.receive(1,0x26,91,request,sizeof(request),2,peer.io());
  EXPECT_EQ(module.error(),RfUpdateError::Bounds); EXPECT_TRUE(peer.reads.empty());
}
TEST(Nb4RfUpdate, BadRangesStorageFailureAndPeerRejectionKeepTheLinkLocked) {
  for (unsigned fault = 0; fault < 4; ++fault) {
    RfUpdateCore core; Peer peer;
    ASSERT_TRUE(core.prepare(image, identity, 1)); ASSERT_TRUE(core.start(identity, 0));
    core.service(0, peer.io());
    if (fault == 0) chunk(core, peer, 19, 2);
    if (fault == 1) { peer.disk = false; chunk(core, peer, 0, 10); }
    if (fault == 2) chunk(core, peer, 0, 0);
    if (fault == 3) { uint8_t result = 1; core.receive(5, 0x32, 2, &result, 1, 1, peer.io()); }
    EXPECT_EQ(core.phase(), RfUpdatePhase::RecoveryRequired);
    EXPECT_EQ(core.transferred(), 0u);
  }
}
TEST(Nb4RfUpdate, ModuleUsesDifferentManifestAndResultThenRequestsNormalMode) {
  auto module = image; module.receiver = false;
  RfUpdateCore core; Peer peer;
  ASSERT_TRUE(core.prepare(module, identity, 1)); ASSERT_TRUE(core.start(identity, 0));
  core.service(0, peer.io()); EXPECT_EQ(peer.opcode, 0x23); EXPECT_TRUE(peer.payload.empty());
  core.receive(2, 0x22, 7, nullptr, 0, 1, peer.io());
  EXPECT_EQ(peer.opcode, 0x22); EXPECT_EQ(peer.sequence, 7); EXPECT_TRUE(peer.response);
  EXPECT_EQ(peer.payload, (std::vector<uint8_t>{1}));
  boot(core, peer);
  core.receive(1, 0x25, 8, nullptr, 0, 1, peer.io());
  EXPECT_EQ(peer.payload, (std::vector<uint8_t>{0,0,0,0,0,0,20,0,0,0}));
  chunk(core, peer, 0, 20);
  const uint8_t result = 1;
  core.receive(3, 0x27, 92, &result, 1, 100, peer.io());
  core.verifyVersion(module.product, module.version, true);
  EXPECT_EQ(core.phase(), RfUpdatePhase::VerifyVersion);
  core.service(100, peer.io());
  EXPECT_EQ(peer.opcode, 0x27); EXPECT_EQ(peer.payload, std::vector<uint8_t>{1});
  core.verifyVersion(module.product, module.version, true);
  EXPECT_EQ(core.phase(), RfUpdatePhase::Complete);
}
TEST(Nb4RfUpdate, BootReadyRepliesDoNotAuthorizeImageDataWithoutThePeerCheckpoint) {
  auto module = image; module.receiver = false;
  RfUpdateCore core; Peer peer;
  ASSERT_TRUE(core.prepare(module, identity, 1));
  core.receive(2, 0x22, 1, nullptr, 0, 1, peer.io());
  EXPECT_EQ(peer.opcode, 0);
  ASSERT_TRUE(core.start(identity, 0)); core.service(0, peer.io());
  peer.writable = false;
  core.receive(2, 0x22, 2, nullptr, 0, 1, peer.io());
  EXPECT_EQ(peer.opcode, 0x23);
  peer.writable = true;
  for (uint8_t sequence : {3, 3, 4}) {
    core.receive(2, 0x22, sequence, nullptr, 0, 2, peer.io());
    EXPECT_EQ(peer.opcode, 0x22); EXPECT_EQ(peer.sequence, sequence);
    EXPECT_EQ(peer.payload, (std::vector<uint8_t>{1}));
    EXPECT_TRUE(peer.checkpoint.empty()); EXPECT_EQ(core.transferred(), 0u);
  }
  core.receive(1, 0x25, 5, nullptr, 0, 3, peer.io());
  EXPECT_EQ(core.error(), RfUpdateError::Protocol);
  EXPECT_EQ(core.phase(), RfUpdatePhase::RecoveryRequired);
  EXPECT_EQ(core.transferred(), 0u);
}
TEST(Nb4RfUpdate, RecoveryRestartsTheWholeImageAndRejectsADifferentBootPeer) {
  RfUpdateCore core; Peer peer;
  ASSERT_TRUE(core.prepare(image, identity, 1)); ASSERT_TRUE(core.start(identity, 0));
  core.service(0, peer.io()); chunk(core, peer, 0, 10);
  const auto saved = peer.checkpoint;
  core.service(20000, peer.io()); ASSERT_EQ(core.phase(), RfUpdatePhase::RecoveryRequired);
  ASSERT_TRUE(core.recover(image, identity, 1, saved.data(), saved.size(), 20001));
  EXPECT_EQ(core.transferred(), 0u);
  core.service(20001, peer.io());
  auto different = saved; different[6] ^= 1;
  core.receive(3, 0x33, 90, different.data(), different.size(), 20002, peer.io());
  EXPECT_EQ(core.error(), RfUpdateError::IdentityChanged);
  EXPECT_EQ(core.phase(), RfUpdatePhase::RecoveryRequired);
  ASSERT_TRUE(core.recover(image, identity, 1, saved.data(), saved.size(), 20003));
  core.service(20003, peer.io()); chunk(core, peer, 0, 20);
  const uint8_t result = 2;
  core.receive(3, 0x32, 91, &result, 1, 20004, peer.io());
  core.verifyVersion(image.product, image.version, true);
  EXPECT_EQ(core.phase(), RfUpdatePhase::Complete);
}
TEST(Nb4RfUpdate, PinnedModuleRecoveryCanRepeatMetadataAfterAWarmRadioRestart) {
  auto module = image; module.receiver = false;
  RfUpdateCore core; Peer peer;
  ASSERT_TRUE(core.prepare(module, identity, 1)); ASSERT_TRUE(core.start(identity, 0));
  core.service(0, peer.io()); boot(core, peer);
  const auto saved = peer.checkpoint;
  core.fail(RfUpdateError::Timeout);
  ASSERT_TRUE(core.recover(module, identity, 1, saved.data(), saved.size(), 20));
  core.service(20, peer.io());
  core.receive(1, 0x25, 91, nullptr, 0, 21, peer.io());
  ASSERT_EQ(core.error(), RfUpdateError::None);
  ASSERT_EQ(peer.opcode, 0x25); ASSERT_EQ(peer.payload.size(), 10u);
  EXPECT_EQ(peer.payload[6], module.size);
  chunk(core, peer, 0, module.size);
  const uint8_t success = 1;
  core.receive(3, 0x27, 92, &success, 1, 22, peer.io()); core.service(22, peer.io());
  core.verifyVersion(module.product, module.version, true);
  EXPECT_EQ(core.phase(), RfUpdatePhase::Complete);
  RfUpdateCore changed;
  changed.requireRecovery(RfUpdateError::Timeout);
  ASSERT_TRUE(changed.recover(module, identity, 1, saved.data(), saved.size(), 20));
  changed.service(20, peer.io());
  auto different = saved; different[0] ^= 1;
  changed.receive(2, 0x24, 93, different.data(), different.size(), 21, peer.io());
  EXPECT_EQ(changed.error(), RfUpdateError::IdentityChanged);
  EXPECT_EQ(changed.transferred(), 0u);
}
TEST(Nb4RfUpdate, NoImageByteIsSentBeforeBootIdentityIsDurablyPinned) {
  RfUpdateCore core; Peer peer;
  ASSERT_TRUE(core.prepare(image, identity, 1)); ASSERT_TRUE(core.start(identity, 0));
  core.service(0, peer.io());
  const auto previous = peer.payload;
  const uint8_t request[] = {0,0,0,0,10};
  core.receive(1, 0x31, 91, request, sizeof(request), 1, peer.io());
  EXPECT_EQ(core.transferred(), 0u);
  EXPECT_EQ(core.error(), RfUpdateError::Protocol);
  EXPECT_EQ(peer.payload, previous);
}

TEST(Nb4RfUpdate, BootReadyStopsEntryRetriesAndCoalescesOnlyUnconsumedRetransmissions) {
  nb4RfUpdateTestReset();
  struct Reset { ~Reset() { nb4RfUpdateTestReset(); } } reset;
  uint8_t buffer[MODULE_BUFFER_SIZE]{}; afhds3::Transport transport;
  transport.init(buffer, nullptr, 0x14);
  nb4RfUpdateTestStartModule();
  ASSERT_TRUE(nb4RfUpdateFrame(transport)); ASSERT_TRUE(transport.waiting());
  transport.sendBuffer();
  for (unsigned i = 0; i < 32; ++i) nb4RfUpdateReceive(2, 0x22, 90, nullptr, 0);
  nb4RfUpdateService();
  ASSERT_FALSE(nb4RfUpdateNeedsRecovery());
  ASSERT_TRUE(nb4RfUpdateFrame(transport)); EXPECT_FALSE(transport.waiting());
  transport.sendBuffer();
  const auto& sent = transport.getDiagnostics();
  ASSERT_EQ(sent.txFrames, 2u);
  ASSERT_GE(sent.lastTxSize, 7u);
  EXPECT_EQ(sent.lastTx[1], 90); EXPECT_EQ(sent.lastTx[2], 0x10);
  EXPECT_EQ(sent.lastTx[3], 0x22); EXPECT_EQ(sent.lastTx[4], 1);
  for (unsigned i = 0; i < 100; ++i) nb4RfUpdateFrame(transport);
  nb4RfUpdateService(); EXPECT_FALSE(nb4RfUpdateNeedsRecovery());
  nb4RfUpdateReceive(2, 0x22, 90, nullptr, 0);
  nb4RfUpdateService(); nb4RfUpdateFrame(transport); transport.sendBuffer();
  EXPECT_EQ(sent.txFrames, 3u); EXPECT_EQ(sent.lastTx[1], 90);
}
TEST(Nb4RfUpdate, DistinctRequestsStillRespectTheBoundedReceiveQueue) {
  nb4RfUpdateTestReset();
  struct Reset { ~Reset() { nb4RfUpdateTestReset(); } } reset;
  nb4RfUpdateTestStartModule();
  for (unsigned i = 0; i < 32; ++i) nb4RfUpdateReceive(2, 0x22, i, nullptr, 0);
  nb4RfUpdateService();
  EXPECT_TRUE(nb4RfUpdateNeedsRecovery()); EXPECT_TRUE(nb4RfUpdateDrivingLocked());
}
TEST(Nb4RfUpdate, BootIdentityStopsEntryRetriesBeforeTheMaintenanceTaskCanCheckpoint) {
  nb4RfUpdateTestReset();
  struct Reset { ~Reset() { nb4RfUpdateTestReset(); } } reset;
  uint8_t buffer[MODULE_BUFFER_SIZE]{}; afhds3::Transport transport;
  transport.init(buffer, nullptr, 0x14); nb4RfUpdateTestStartModule();
  ASSERT_TRUE(nb4RfUpdateFrame(transport)); ASSERT_TRUE(transport.waiting());
  const uint8_t bootIdentity[]{0x21, 0, 1, 0};
  nb4RfUpdateReceive(2, 0x24, 90, bootIdentity, sizeof(bootIdentity));
  ASSERT_TRUE(nb4RfUpdateFrame(transport));
  EXPECT_FALSE(transport.waiting()); EXPECT_FALSE(nb4RfUpdateNeedsRecovery());
}
#if defined(NB4_RF_UPDATE_BENCH)
TEST(Nb4RfUpdate, BenchCapturesTheWholeReceiverBootIdentityEvenWhenTheWriterIsNotActive) {
  nb4RfUpdateTestReset();
  struct Reset { ~Reset() { nb4RfUpdateTestReset(); } } reset;
  const uint8_t peer[]{6,0,0x34,0x12, 4,1,2,3, 4,5,6,7, 8,9};
  nb4RfUpdateReceive(3, 0x33, 90, peer, sizeof(peer));
  const uint8_t operation[4]{}; char result[1024]{};
  ASSERT_TRUE(nb4RfUpdateCommand(19, operation, sizeof(operation), result, sizeof(result)));
  EXPECT_NE(std::string(result).find("\"boot_reply\":[305397766,50462980,117835012,2312]"), std::string::npos);
  EXPECT_FALSE(nb4RfUpdateDrivingLocked());
}
#endif

namespace {
struct JournalFixture {
  struct Record {
    uint8_t magic[8];
    uint32_t sequence, operation, image, phase;
    RfUpdateIdentity identity;
    uint8_t backup[32];
    uint32_t peerLength;
    uint8_t peer[16];
    uint32_t uid[3], target, crc;
  };
  std::filesystem::path root = std::filesystem::temp_directory_path() /
      ("apextx-rf-journal-" + std::to_string(getpid()));
  JournalFixture() {
    nb4RfUpdateTestReset();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "FIRMWARE/RF");
    simuFatfsSetPaths(root.c_str(), nullptr); simuFatfsSetFaults(0);
    nb4StorageResume();
  }
  ~JournalFixture() {
    nb4RfUpdateTestReset(); nb4StorageResume();
    simuFatfsSetPaths(TESTS_PATH, nullptr); std::filesystem::remove_all(root);
  }
  void write(uint32_t sequence, RfUpdatePhase phase, bool damaged = false, bool module = false,
             bool checkpoint = false) {
    Record data{}; static_assert(sizeof(data) == 128);
    memcpy(data.magic, "APXRFW3", 8);
    data.sequence = sequence; data.operation = 17; data.image = 1; data.phase = uint32_t(phase);
    data.identity = identity; memset(data.backup, 0x55, sizeof(data.backup));
    if (module) { data.image=0; data.identity={5,7,0x10003,0x10003,0x10021,0,0,0x30000}; }
    if (checkpoint) {
      const uint8_t peer[]{4,0,0x34,0x12,0x12,1,16,0,0,0,0x78,0x56,0x34,0x12};
      data.peerLength=sizeof(peer); memcpy(data.peer,peer,sizeof(peer));
    }
    data.uid[0] = 1; data.target = NB4_UPDATE_TARGET;
    data.crc = maintenanceCrc(reinterpret_cast<const uint8_t*>(&data), sizeof(data) - 4);
    if (damaged) data.crc ^= 1;
    auto path = root / (sequence & 1 ? "FIRMWARE/RF/operation.alt" : "FIRMWARE/RF/operation.dat");
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(&data), sizeof(data));
  }
  std::string command(uint8_t command) {
    const uint8_t payload[]{17, 0, 0, 0, 1}; char output[512]{};
    EXPECT_TRUE(nb4RfUpdateCommand(command, payload, command == 18 ? 5 : 4, output, sizeof(output)));
    return output;
  }
};
}
TEST(Nb4RfJournal, ACorruptCheckpointRetainsTheEarlierEntryAndDrivingLock) {
  JournalFixture fixture;
  fixture.write(1, RfUpdatePhase::Entering);
  fixture.write(2, RfUpdatePhase::Transfer, true);
  nb4RfUpdateService();
  EXPECT_TRUE(nb4RfUpdateDrivingLocked()); EXPECT_TRUE(nb4RfUpdateNeedsRecovery());
  EXPECT_NE(fixture.command(19).find("\"operation\":17"), std::string::npos);
}
TEST(Nb4RfJournal, AnIntactModuleEntryCanProbeButAChangedJournalCannotAuthorizeTheProbe) {
  JournalFixture fixture; fixture.write(1,RfUpdatePhase::Entering,false,true);
  nb4RfUpdateService(); EXPECT_TRUE(nb4RfUpdateTestEntryProbeActive());
  EXPECT_TRUE(nb4RfUpdateDrivingLocked()); EXPECT_TRUE(nb4RfUpdateNeedsRecovery());
  nb4RfUpdateTestReset(); fixture.write(2,RfUpdatePhase::Transfer,true,true);
  nb4RfUpdateService(); EXPECT_FALSE(nb4RfUpdateTestEntryProbeActive());
  EXPECT_TRUE(nb4RfUpdateDrivingLocked()); EXPECT_TRUE(nb4RfUpdateNeedsRecovery());
}
TEST(Nb4RfJournal, AnInterruptedReceiverCanReturnToInventoryWithoutUnlockingOrRepeatingEntry) {
  JournalFixture fixture; fixture.write(1,RfUpdatePhase::RecoveryRequired,false,false,true);
  nb4RfUpdateService();
  EXPECT_TRUE(nb4RfUpdateTestEntryProbeActive()); EXPECT_TRUE(nb4RfUpdateDrivingLocked());
  uint8_t buffer[MODULE_BUFFER_SIZE]{}; afhds3::Transport transport;
  transport.init(buffer,nullptr,0x14);
  EXPECT_TRUE(nb4RfUpdateFrame(transport)); // Clear the previous transaction.
  EXPECT_FALSE(nb4RfUpdateFrame(transport)); // Inventory can use the normal handshake.
  EXPECT_FALSE(transport.waiting());
  for (unsigned i=0;i<5;++i) nb4RfUpdateService();
  EXPECT_TRUE(nb4RfUpdateDrivingLocked()); EXPECT_TRUE(nb4RfUpdateNeedsRecovery());
  EXPECT_NE(fixture.command(19).find("\"phase\":7"),std::string::npos);
}
TEST(Nb4RfJournal, OnlyANewerVerifiedCompletionReleasesDrivingAfterRestart) {
  JournalFixture fixture;
  fixture.write(1, RfUpdatePhase::Transfer);
  fixture.write(2, RfUpdatePhase::Complete);
  nb4RfUpdateService(); EXPECT_FALSE(nb4RfUpdateDrivingLocked());
  const auto completed = fixture.command(19);
  EXPECT_NE(completed.find("\"operation\":17"), std::string::npos);
  EXPECT_NE(completed.find("\"phase\":5"), std::string::npos);
  EXPECT_NE(completed.find("\"transferred\":" + std::to_string(rfImages[1].size)), std::string::npos);
  nb4RfUpdateTestReset();
  fixture.write(3, RfUpdatePhase::Entering);
  nb4RfUpdateService(); EXPECT_TRUE(nb4RfUpdateDrivingLocked());
}
TEST(Nb4RfJournal, ACancelledResultRetainsItsOperationAcrossRestartWithoutLockingDriving) {
  JournalFixture fixture;
  fixture.write(1, RfUpdatePhase::Entering);
  fixture.write(2, RfUpdatePhase::Cancelled);
  nb4RfUpdateService();
  const auto cancelled = fixture.command(19);
  EXPECT_NE(cancelled.find("\"operation\":17"), std::string::npos);
  EXPECT_NE(cancelled.find("\"phase\":6"), std::string::npos);
  EXPECT_NE(cancelled.find("\"transferred\":0"), std::string::npos);
  EXPECT_FALSE(nb4RfUpdateDrivingLocked()); EXPECT_FALSE(nb4RfUpdateNeedsRecovery());
}
TEST(Nb4RfJournal, DamagedRecordsCannotAuthorizeDrivingThroughAnOlderReadyRecord) {
  JournalFixture fixture;
  fixture.write(1, RfUpdatePhase::Ready);
  fixture.write(2, RfUpdatePhase::Entering, true);
  nb4RfUpdateService(); EXPECT_TRUE(nb4RfUpdateNeedsRecovery());
  EXPECT_FALSE(nb4RfUpdatePrepared());
}
TEST(Nb4RfJournal, RecoveryValidationCannotBeCancelledOrStartedTwice) {
  JournalFixture fixture; fixture.write(1, RfUpdatePhase::Entering);
  nb4RfUpdateService(); nb4RfUpdateTestBeginValidation(true);
  EXPECT_NE(fixture.command(20).find("cannot_cancel_after_entry"), std::string::npos);
  EXPECT_NE(fixture.command(21).find("operation_in_progress"), std::string::npos);
  EXPECT_NE(fixture.command(18).find("operation_in_progress"), std::string::npos);
  EXPECT_TRUE(nb4RfUpdateDrivingLocked()); EXPECT_TRUE(nb4RfUpdateNeedsRecovery());
}
TEST(Nb4RfJournal, RecoveryKeepsVerifyingTheImageBeforeFinalizingThePreviousRecoveryPhase) {
  JournalFixture fixture; fixture.write(1, RfUpdatePhase::Entering);
  {
    std::ofstream file(fixture.root / "FIRMWARE/RF/12340004.bin", std::ios::binary);
    const std::vector<uint8_t> imageBytes(rfImages[1].size, 0);
    file.write(reinterpret_cast<const char*>(imageBytes.data()), imageBytes.size());
  }
  nb4RfUpdateService(); nb4RfUpdateTestBeginValidation(true);
  for (unsigned i = 0; i < 3; ++i) {
    nb4RfUpdateService();
    EXPECT_NE(fixture.command(21).find("operation_in_progress"), std::string::npos);
    EXPECT_TRUE(nb4RfUpdateDrivingLocked());
  }
  for (unsigned i = 0; i < rfImages[1].size / 512 + 2; ++i) nb4RfUpdateService();
  EXPECT_TRUE(nb4RfUpdateNeedsRecovery());
  EXPECT_NE(fixture.command(19).find("\"transferred\":0"), std::string::npos);
}
TEST(Nb4RfJournal, ARejectedPreparationPreservesThePreviousPreparedOperation) {
  JournalFixture fixture; fixture.write(1, RfUpdatePhase::Ready);
  nb4RfUpdateService(); ASSERT_TRUE(nb4RfUpdatePrepared()) << fixture.command(19);
  uint8_t payload[45]{};
  payload[0] = 1; payload[1] = 18;
  memset(payload + 5, 0xff, 4); memset(payload + 13, 0x55, 32);
  char output[512]{};
  EXPECT_TRUE(nb4RfUpdateCommand(16, payload, sizeof(payload), output, sizeof(output)));
  EXPECT_NE(std::string(output).find("error"), std::string::npos);
  EXPECT_TRUE(nb4RfUpdatePrepared());
  EXPECT_NE(fixture.command(19).find("\"operation\":17"), std::string::npos);
}
#endif
