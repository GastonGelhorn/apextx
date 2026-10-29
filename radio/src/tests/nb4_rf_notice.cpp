/* SPDX-License-Identifier: GPL-2.0-only */
#include "gtests.h"
#if defined(RADIO_NB4)
#include "nb4_rf_notice.h"
#include "location.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>
TEST(Nb4RfNotice, DeferredVersionsSurviveReopenButTornOrDifferentKeysDoNotMatch) {
  const auto root=std::filesystem::temp_directory_path()/("apextx-rf-notice-"+std::to_string(getpid()));
  std::filesystem::remove_all(root); std::filesystem::create_directories(root);
  simuFatfsSetPaths(root.c_str(),nullptr); simuFatfsSetFaults(0);
  uint8_t key[32]{}; key[0]=1;
  EXPECT_FALSE(nb4RfNoticeTestDeferred(key,false)); EXPECT_TRUE(nb4RfNoticeTestDeferred(key,true));
  EXPECT_TRUE(nb4RfNoticeTestDeferred(key,false)); key[0]=2;
  EXPECT_FALSE(nb4RfNoticeTestDeferred(key,false)); key[0]=1;
  const auto path=root/"FIRMWARE/RF/deferred.dat";
  std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out); file.seekp(40); file.put(0); file.put(0); file.put(0); file.put(0); file.close();
  EXPECT_FALSE(nb4RfNoticeTestDeferred(key,false));
  simuFatfsSetPaths(TESTS_PATH,nullptr); std::filesystem::remove_all(root);
}
#endif
