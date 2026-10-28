/* SPDX-License-Identifier: GPL-2.0-only */
#include "gtests.h"
#if defined(RADIO_NB4)
#include "bootloader/nb4_nor_restore_core.h"
#include <array>
#include <vector>
using namespace nb4;
namespace {
struct Nor {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(NorRestoreCore::capacity, 0x33);
  unsigned erases = 0, programs = 0;
  int busy = 0; bool corrupt = false;
  NorRestoreIo io() { return {this,
    [](void* p, uint32_t at, uint8_t* out, size_t n) {
      auto& m = *static_cast<Nor*>(p);
      if (at > m.bytes.size() || n > m.bytes.size() - at) return false;
      memcpy(out, m.bytes.data() + at, n); if (m.corrupt && m.programs) out[0] ^= 1; return true;
    }, [](void* p, uint32_t at) {
      auto& m = *static_cast<Nor*>(p); ++m.erases;
      if (at % 4096 || at > m.bytes.size() - 4096) return false;
      memset(m.bytes.data() + at, 0xff, 4096); return true;
    }, [](void* p, uint32_t at, const uint8_t* in, size_t n) {
      auto& m = *static_cast<Nor*>(p); ++m.programs;
      if (n != 256 || at % 256 || at > m.bytes.size() - n) return false;
      for (unsigned i = 0; i < n; ++i) m.bytes[at+i] &= in[i]; return true;
    }, [](void* p) { return static_cast<Nor*>(p)->busy; }}; }
};
std::array<uint8_t,32> sha(const uint8_t* data, size_t size) {
  Sha256 hash; hash.update(data,size); std::array<uint8_t,32> result; hash.finish(result.data()); return result;
}
void sector(NorRestoreCore& core, uint32_t index, const uint8_t* data) {
  const auto hash = sha(data,4096);
  ASSERT_TRUE(core.begin(index,hash.data())); ASSERT_TRUE(core.append(0,data,4096)); ASSERT_TRUE(core.commit());
}
void drain(NorRestoreCore& core, Nor& nor) {
  for (unsigned i=0;i<5000 && core.working();++i) core.service(i,nor.io());
}
}
TEST(Nb4NorRestore, UnarmedIncompleteOrAlteredSectorsNeverErase) {
  Nor nor; NorRestoreCore core; std::array<uint8_t,4096> block{}; const auto hash=sha(block.data(),block.size());
  EXPECT_FALSE(core.begin(0,hash.data())); EXPECT_FALSE(core.commit());
  ASSERT_TRUE(core.arm(hash.data())); EXPECT_FALSE(core.arm(hash.data())); EXPECT_FALSE(core.begin(2048,hash.data()));
  ASSERT_TRUE(core.begin(0,hash.data())); EXPECT_FALSE(core.append(1,block.data(),100));
  ASSERT_TRUE(core.append(0,block.data(),100)); EXPECT_FALSE(core.commit());
  block[100]=1; ASSERT_TRUE(core.append(100,block.data()+100,3996)); EXPECT_FALSE(core.commit());
  drain(core,nor); EXPECT_EQ(core.error(),NorRestoreError::InputHash); EXPECT_EQ(nor.erases,0u);
}
TEST(Nb4NorRestore, CompleteReadbackAndFullHashAreRequired) {
  Nor nor; NorRestoreCore core; auto expected=nor.bytes; memset(expected.data(),0x55,4096);
  const auto hash=sha(expected.data(),expected.size()); ASSERT_TRUE(core.arm(hash.data()));
  for (unsigned i=0;i<2048;++i) { sector(core,i,expected.data()+i*4096); drain(core,nor); }
  EXPECT_EQ(core.phase(),NorRestorePhase::Complete); EXPECT_EQ(core.completed(),2048u);
  EXPECT_EQ(core.changed(),1u); EXPECT_EQ(nor.erases,1u); EXPECT_EQ(nor.programs,16u); EXPECT_EQ(nor.bytes,expected);
}
TEST(Nb4NorRestore, BusyTimeoutAndBadReadbackStopFurtherSectors) {
  for (bool corrupt : {false,true}) {
    Nor nor; NorRestoreCore core; std::array<uint8_t,4096> block{};
    auto hash=sha(block.data(),block.size()); ASSERT_TRUE(core.arm(hash.data())); sector(core,0,block.data());
    nor.busy=corrupt?0:1; nor.corrupt=corrupt; drain(core,nor);
    EXPECT_EQ(core.phase(),NorRestorePhase::Error);
    EXPECT_EQ(core.error(),corrupt?NorRestoreError::Readback:NorRestoreError::Timeout);
    EXPECT_FALSE(core.begin(1,hash.data())); EXPECT_EQ(core.completed(),0u);
  }
}
TEST(Nb4NorRestore, ResetRequiresRestartAndCannotSkipAnUnverifiedPrefix) {
  Nor nor; NorRestoreCore first; std::array<uint8_t,4096> block{};
  const auto hash=sha(block.data(),block.size()); ASSERT_TRUE(first.arm(hash.data())); sector(first,0,block.data());
  for (unsigned i=0;i<12;++i) first.service(i,nor.io());
  NorRestoreCore restarted; ASSERT_TRUE(restarted.arm(hash.data())); EXPECT_FALSE(restarted.begin(1,hash.data()));
  sector(restarted,0,block.data()); drain(restarted,nor); EXPECT_EQ(restarted.completed(),1u);
}
#endif
