/* SPDX-License-Identifier: GPL-2.0-only */
#include "gtests.h"
#include "location.h"
#if defined(RADIO_NB4)
#include "nb4_audio_identity.h"
#include "nb4_maintenance_protocol.h"
#include <filesystem>
#include <fstream>
#include <array>
#include <unistd.h>
TEST(Nb4AudioIdentity, InstalledPackChoosesVoiceWithoutChangingTextAndPendingOrBadReceiptsDoNot) {
  const auto root=std::filesystem::temp_directory_path()/("apextx-audio-id-"+std::to_string(getpid()));
  std::filesystem::remove_all(root); std::filesystem::create_directories(root/"SOUNDS/es/SYSTEM");
  simuFatfsSetPaths(root.c_str(),nullptr); simuFatfsSetFaults(0);
  char voice[2],text[2]; memcpy(voice,g_eeGeneral.ttsLanguage,2); memcpy(text,g_eeGeneral.uiLanguage,2);
  memcpy(g_eeGeneral.ttsLanguage,"en",2); memcpy(g_eeGeneral.uiLanguage,"de",2);
  std::array<uint8_t,80> receipt{}; memcpy(receipt.data(),"APXAUD1",8); memcpy(receipt.data()+8,"es",2);
  receipt[10]=1; memcpy(receipt.data()+12,"test",4); memset(receipt.data()+44,0x11,32);
  const auto crc=nb4::maintenanceCrc(receipt.data(),76); memcpy(receipt.data()+76,&crc,4);
  auto write=[&] { std::ofstream file(root/"APEXTX-AUDIO.ID",std::ios::binary); file.write(reinterpret_cast<const char*>(receipt.data()),receipt.size()); };
  {std::ofstream hello(root/"SOUNDS/es/SYSTEM/hello.wav"); hello<<"test recording";}
  write(); nb4AudioIdentityLoad();
  EXPECT_TRUE(nb4AudioIdentity().installed); EXPECT_STREQ(nb4AudioIdentity().language,"es");
  EXPECT_EQ(memcmp(g_eeGeneral.ttsLanguage,"es",2),0); EXPECT_EQ(memcmp(g_eeGeneral.uiLanguage,"de",2),0);
  {std::ofstream pending(root/".APEXTX-AUDIO-INSTALL.json"); pending<<"{}";}
  memcpy(g_eeGeneral.ttsLanguage,"en",2); nb4AudioIdentityLoad();
  EXPECT_TRUE(nb4AudioIdentity().pending); EXPECT_FALSE(nb4AudioIdentity().installed); EXPECT_EQ(memcmp(g_eeGeneral.ttsLanguage,"en",2),0);
  std::filesystem::remove(root/".APEXTX-AUDIO-INSTALL.json"); receipt[76]^=1; write(); nb4AudioIdentityLoad();
  EXPECT_FALSE(nb4AudioIdentity().installed); EXPECT_EQ(memcmp(g_eeGeneral.ttsLanguage,"en",2),0);
  memcpy(g_eeGeneral.ttsLanguage,voice,2); memcpy(g_eeGeneral.uiLanguage,text,2);
  simuFatfsSetPaths(TESTS_PATH,nullptr); std::filesystem::remove_all(root); nb4AudioIdentityLoad();
}
#endif
