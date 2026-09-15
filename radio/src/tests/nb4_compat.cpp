/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "gtests.h"
#include "location.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_model_compat.h"
#include "nb4_car_state.h"
#include "nb4_racing.h"
#include "nb4_home.h"
#include "nb4_routes.h"
#include "storage/sdcard_yaml.h"
#include "storage/sdcard_common.h"
#include "storage/modelslist.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace {
std::string readRaw(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct ScopedFatfsRoot {
  explicit ScopedFatfsRoot(const char* name) :
    root(std::filesystem::temp_directory_path() /
         (std::string(name) + "-" + std::to_string(getpid()))),
    path(root.string())
  {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    simuFatfsSetPaths(path.c_str(), nullptr);
  }

  ~ScopedFatfsRoot()
  {
    simuFatfsSetPaths(TESTS_PATH, nullptr);
    std::filesystem::remove_all(root);
  }

  std::filesystem::path root;
  std::string path;
};
}

TEST(Nb4Compatibility, RadioMigrationBacksUpRawYamlAndPreservesCalibration)
{
  const auto root = std::filesystem::temp_directory_path() / ("nb4-radio-migration-" + std::to_string(getpid()));
  ASSERT_FALSE(std::filesystem::exists(root));
  std::filesystem::create_directories(root / "RADIO");
  struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() { simuFatfsSetPaths(TESTS_PATH, nullptr); std::filesystem::remove_all(root); }
  } cleanup{root};
  auto path = root.string(); simuFatfsSetPaths(path.c_str(), nullptr);
  SYSTEM_RESET();
  g_eeGeneral.nb4UiVersion = 0;
  g_eeGeneral.templateSetup = 1;
  g_eeGeneral.calib[0].mid = 1789;
  strAppend(g_eeGeneral.currModelFilename, "my-car.yml", LEN_MODEL_FILENAME);
  ASSERT_EQ(writeGeneralSettings(), nullptr);
  auto original = readRaw((root / "RADIO/radio.yml").string());
  ASSERT_EQ(loadRadioSettingsYaml(true), nullptr);
  EXPECT_EQ(g_eeGeneral.nb4UiVersion, NB4_UI_VERSION);
  EXPECT_EQ(g_eeGeneral.nb4Home, NB4_HOME_INSTRUMENTS);
  EXPECT_EQ(g_eeGeneral.nb4Orientation, 0);
  EXPECT_EQ(g_eeGeneral.templateSetup, 0);
  EXPECT_EQ(g_eeGeneral.calib[0].mid, 1789);
  EXPECT_STREQ(g_eeGeneral.currModelFilename, "my-car.yml");
  EXPECT_EQ(readRaw((root / "RADIO/radio-pre-car-ui.yml").string()), original);
  const auto current = readRaw((root / "RADIO/radio.yml").string());
  simuFatfsSetFaults(0, 0); // A short checksum-header write must not replace radio.yml.
  EXPECT_NE(writeGeneralSettings(), nullptr);
  simuFatfsSetFaults(0);
  EXPECT_EQ(readRaw((root / "RADIO/radio.yml").string()), current);
  g_eeGeneral.nb4Home = NB4_HOME_ESSENTIAL;
  g_eeGeneral.nb4Orientation = 1;
  ASSERT_EQ(writeGeneralSettings(), nullptr);
  ASSERT_EQ(loadRadioSettingsYaml(true), nullptr);
  EXPECT_EQ(g_eeGeneral.nb4Home, NB4_HOME_ESSENTIAL);
  EXPECT_EQ(g_eeGeneral.nb4Orientation, 1);
  EXPECT_EQ(g_eeGeneral.calib[0].mid, 1789);
  EXPECT_EQ(readRaw((root / "RADIO/radio-pre-car-ui.yml").string()), original);
  EXPECT_EQ(readRaw((root / "RADIO/radio.yml.previous").string()), original);
  // Interrupted promotion: restore the last complete settings, including calibration.
  std::filesystem::remove(root / "RADIO/radio.yml");
  EXPECT_EQ(loadRadioSettingsYaml(true), nullptr);
  EXPECT_EQ(g_eeGeneral.calib[0].mid, 1789);
  EXPECT_EQ(readRaw((root / "RADIO/radio.yml").string()), original);
}

TEST(Nb4Compatibility, RacingMigrationPreservesV1AppearanceAndDefersOnBackupFailure)
{
  const auto root = std::filesystem::temp_directory_path() / ("nb4-racing-migration-" + std::to_string(getpid()));
  std::filesystem::create_directories(root / "RADIO");
  struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() { simuFatfsSetFaults(0); simuFatfsSetPaths(TESTS_PATH, nullptr); std::filesystem::remove_all(root); }
  } cleanup{root};
  simuFatfsSetPaths(root.c_str(), nullptr); SYSTEM_RESET();
  g_eeGeneral.nb4UiVersion = 1; g_eeGeneral.nb4Home = NB4_HOME_PIT;
  g_eeGeneral.nb4Orientation = 1; g_eeGeneral.nb4Accent = 7;
  g_eeGeneral.calib[0].mid = 1827;
  strAppend(g_eeGeneral.selectedTheme, "ApexTX Light", SELECTED_THEME_NAME_LEN);
  strAppend(g_eeGeneral.currModelFilename, "my-car.yml", LEN_MODEL_FILENAME);
  ASSERT_EQ(writeGeneralSettings(), nullptr);
  const auto original = readRaw((root / "RADIO/radio.yml").string());
  simuFatfsSetFaults(0, 0);
  ASSERT_EQ(loadRadioSettingsYaml(true), nullptr);
  EXPECT_EQ(g_eeGeneral.nb4UiVersion, 1);
  EXPECT_EQ(readRaw((root / "RADIO/radio.yml").string()), original);
  simuFatfsSetFaults(0); ASSERT_EQ(loadRadioSettingsYaml(true), nullptr);
  EXPECT_EQ(g_eeGeneral.nb4UiVersion, NB4_UI_VERSION);
  EXPECT_EQ(g_eeGeneral.nb4Home, NB4_HOME_INSTRUMENTS);
  EXPECT_EQ(g_eeGeneral.nb4Orientation, 1); EXPECT_EQ(g_eeGeneral.nb4Accent, 7);
  EXPECT_EQ(g_eeGeneral.calib[0].mid, 1827);
  EXPECT_STREQ(g_eeGeneral.selectedTheme, "ApexTX Light");
  EXPECT_STREQ(g_eeGeneral.currModelFilename, "my-car.yml");
  EXPECT_EQ(readRaw((root / "RADIO/radio-pre-racing-ui-v2.yml").string()), original);
  ASSERT_EQ(writeGeneralSettings(), nullptr); ASSERT_EQ(loadRadioSettingsYaml(true), nullptr);
  EXPECT_EQ(readRaw((root / "RADIO/radio-pre-racing-ui-v2.yml").string()), original);
}

#if defined(RADIO_NB4)
TEST(Nb4Compatibility, DeferredSettingsSaveImmutableBytesAndKeepNewEditsAndFailedWrites)
{
  nb4FlushSettings();
  ScopedFatfsRoot fatfs("nb4-deferred-settings");
  std::filesystem::create_directories(fatfs.root / "RADIO");
  std::filesystem::create_directories(fatfs.root / "MODELS");
  SYSTEM_RESET(); MODEL_RESET(); nb4AcceptNewCarModel();
  strAppend(g_eeGeneral.currModelFilename, "model1.yml", LEN_MODEL_FILENAME);
  g_eeGeneral.nb4UiVersion = NB4_UI_VERSION;
  g_eeGeneral.nb4TonesOnly = 0;
  ASSERT_EQ(writeGeneralSettings(), nullptr);
  const auto original = readRaw((fatfs.root / "RADIO/radio.yml").string());
  g_eeGeneral.nb4TonesOnly = 1;
  const auto queued = nb4QueueSettings(EE_GENERAL | EE_MODEL | EE_LABELS);
  EXPECT_EQ(queued, EE_GENERAL | EE_MODEL | EE_LABELS);
  EXPECT_TRUE(nb4SettingsPending());
  EXPECT_EQ(nb4QueueSettings(EE_GENERAL | EE_MODEL | EE_LABELS), 0);
  const auto expectedLabels = modelslist.serialize();
  EXPECT_EQ(readRaw((fatfs.root / "RADIO/radio.yml").string()), original);
  g_eeGeneral.nb4TonesOnly = 0; // a newer edit must not alter the pending snapshot
  storageDirty(EE_GENERAL);
  nb4WritePendingSettings(); nb4PollSettings();
  EXPECT_FALSE(nb4SettingsPending());
  EXPECT_EQ(readRaw((fatfs.root / "MODELS/labels.yml").string()), expectedLabels);
  EXPECT_NE(storageDirtyMsk & EE_GENERAL, 0);
  EXPECT_EQ(readRaw((fatfs.root / "RADIO/radio.yml.previous").string()), original);
  EXPECT_NE(readRaw((fatfs.root / "RADIO/radio.yml").string()).find("nb4TonesOnly: 1"), std::string::npos);
  const auto saved = readRaw((fatfs.root / "RADIO/radio.yml").string());
  EXPECT_EQ(nb4QueueSettings(EE_GENERAL), EE_GENERAL);
  simuFatfsSetFaults(0, 0);
  nb4WritePendingSettings();
  simuFatfsSetFaults(0);
  storageDirtyMsk = 0;
  nb4PollSettings();
  EXPECT_NE(storageDirtyMsk & EE_GENERAL, 0);
  EXPECT_EQ(readRaw((fatfs.root / "RADIO/radio.yml").string()), saved);
  ASSERT_EQ(writeGeneralSettings(), nullptr);
  ASSERT_EQ(loadRadioSettingsYaml(true), nullptr);
  EXPECT_EQ(g_eeGeneral.nb4TonesOnly, 0);
}

TEST(Nb4Compatibility, AudioPreferencePersistsWithoutChangingAdjacentSettings)
{
  ScopedFatfsRoot fatfs("nb4-audio-settings");
  std::filesystem::create_directories(fatfs.root / "RADIO");
  SYSTEM_RESET();
  EXPECT_EQ(g_eeGeneral.nb4TonesOnly, 0);
  g_eeGeneral.nb4UiVersion = NB4_UI_VERSION;
  g_eeGeneral.nb4TonesOnly = 1;
  g_eeGeneral.keyLockEnabled = 1;
  g_eeGeneral.nb4NoAddress = 1;
  g_eeGeneral.pwrOffIfInactive = 7;
  ASSERT_EQ(writeGeneralSettings(), nullptr);
  SYSTEM_RESET();
  ASSERT_EQ(loadRadioSettingsYaml(true), nullptr);
  EXPECT_EQ(g_eeGeneral.nb4TonesOnly, 1);
  EXPECT_EQ(g_eeGeneral.keyLockEnabled, 1);
  EXPECT_EQ(g_eeGeneral.nb4NoAddress, 1);
  EXPECT_EQ(g_eeGeneral.pwrOffIfInactive, 7);
  g_eeGeneral.nb4TonesOnly = 0;
  ASSERT_EQ(writeGeneralSettings(), nullptr);
  SYSTEM_RESET();
  ASSERT_EQ(loadRadioSettingsYaml(true), nullptr);
  EXPECT_EQ(g_eeGeneral.nb4TonesOnly, 0);
}

#if defined(AUDIO)
TEST(Nb4Compatibility, VoiceModePlaysPcmAndTonesModeKeepsAlarmsAndCountdownAudible)
{
  extern uint32_t simuTestNonSilentAudioSamples;
  const auto oldVolume = currentSpeakerVolume;
  {
    ScopedFatfsRoot fatfs("nb4-audio-playback");
    SYSTEM_RESET(); MODEL_RESET();
    memcpy(g_eeGeneral.ttsLanguage, "en", 2);
    const auto dir = fatfs.root / "SOUNDS/en/SYSTEM";
    std::filesystem::create_directories(dir);
    // A short PCM16 mono 8 kHz fixture exercises the installed pack's decoder.
    std::ofstream wav(dir / "telemko.wav", std::ios::binary);
    auto u16 = [&](unsigned v) { wav.put(v & 255); wav.put((v >> 8) & 255); };
    auto u32 = [&](unsigned v) { u16(v & 65535); u16(v >> 16); };
    wav.write("RIFF", 4); u32(36 + 1600); wav.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(1); u32(8000); u32(16000); u16(2); u16(16);
    wav.write("data", 4); u32(1600);
    for (unsigned i = 0; i < 800; ++i) u16(i % 8 < 4 ? 8192 : uint16_t(-8192));
    wav.close();
    referenceSystemAudioFiles();
    currentSpeakerVolume = VOLUME_LEVEL_MAX;
    g_eeGeneral.beepMode = e_mode_all;
    audioQueue.stopAll();
    simuTestNonSilentAudioSamples = 0;
    audioEvent(AU_TELEMETRY_LOST);
    EXPECT_TRUE(audioQueue.isPlaying(ID_PLAY_PROMPT_BASE + AU_TELEMETRY_LOST));
    audioQueue.wakeup();
    EXPECT_GT(simuTestNonSilentAudioSamples, 0u);

    audioQueue.playFile("/SOUNDS/en/SYSTEM/telemko.wav", 0, 42);
    EXPECT_FALSE(audioQueue.pauseFiles());
    audioQueue.wakeup();
    EXPECT_TRUE(audioQueue.pauseFiles());
    EXPECT_FALSE(audioQueue.isPlaying(42));
    audioQueue.playFile("/SOUNDS/en/SYSTEM/telemko.wav", 0, 42);
    EXPECT_FALSE(audioQueue.isPlaying(42));
    audioQueue.resumeFiles();
    audioQueue.playFile("/SOUNDS/en/SYSTEM/telemko.wav", 0, 42);
    EXPECT_TRUE(audioQueue.isPlaying(42));

    audioQueue.stopAll();
    g_eeGeneral.nb4TonesOnly = 1;
    audioQueue.playFile("/SOUNDS/en/SYSTEM/telemko.wav", 0, 42);
    EXPECT_FALSE(audioQueue.isPlaying(42));
    for (const auto event : {AU_TELEMETRY_LOST, AU_TELEMETRY_BACK}) {
      audioQueue.stopAll(); simuTestNonSilentAudioSamples = 0;
      audioEvent(event);
      EXPECT_FALSE(audioQueue.isPlaying(ID_PLAY_PROMPT_BASE + event));
      audioQueue.wakeup();
      EXPECT_GT(simuTestNonSilentAudioSamples, 0u);
    }
    audioQueue.stopAll(); simuTestNonSilentAudioSamples = 0;
    g_model.timers[0].countdownBeep = COUNTDOWN_VOICE;
    audioTimerCountdown(0, 0);
    audioQueue.wakeup();
    EXPECT_GT(simuTestNonSilentAudioSamples, 0u);
    EXPECT_EQ(g_model.timers[0].countdownBeep, COUNTDOWN_VOICE);
    for (unsigned tonesOnly = 0; tonesOnly < 2; ++tonesOnly) {
      audioQueue.stopAll(); simuTestNonSilentAudioSamples = 0;
      g_eeGeneral.nb4TonesOnly = tonesOnly;
      g_eeGeneral.beepMode = e_mode_quiet;
      audioEvent(AU_TELEMETRY_LOST);
      audioTimerCountdown(0, 0);
      audioQueue.wakeup();
      EXPECT_EQ(simuTestNonSilentAudioSamples, 0u);
    }
    audioQueue.stopAll();
  }
  currentSpeakerVolume = oldVolume;
  SYSTEM_RESET();
  referenceSystemAudioFiles();
}
#endif
#endif

TEST(Nb4Compatibility, RealYamlRoundTripPreservesCarSettingsAndRawBackup)
{
  ScopedFatfsRoot fatfs("nb4-real-roundtrip");
  SYSTEM_RESET(); MODEL_RESET(); nb4AcceptNewCarModel();
  g_model.resetScreenData();
  nb4RacingDefaults(g_model.nb4Racing);
  g_model.moduleData[0].type = MODULE_TYPE_FLYSKY_AFHDS3;
  g_model.limitData[7].min = 217;
  g_model.limitData[1].revert = 1;
  g_model.failsafeChannels[1] = -734;
  g_model.nb4Racing.homeTimer = NB4_HOME_TIMER_2;
  g_model.mixData[0].destCh = 7;
  g_model.mixData[0].srcRaw = MIXSRC_FIRST_STICK;
  g_model.flightModeData[0].gvars[0] = 43;
  g_model.setScreenLayoutId(0, "Layout1x1");
  g_model.setScreenLayoutId(1, "Layout2x1");
  const char* filename = "nb4_compat_roundtrip_test.yml";
  char path[256]; getModelPath(path, filename);
  std::string real = simuFatfsGetRealPath(path);
  ASSERT_FALSE(std::filesystem::exists(real));
  std::filesystem::create_directories(std::filesystem::path(real).parent_path());
  ASSERT_EQ(writeModelYaml(filename), nullptr);
  auto original = readRaw(real);
#if !defined(SIMU_DISKIO)
  simuFatfsSetFaults(0, 0); // FatFS can report FR_OK with a short write
  EXPECT_NE(writeModelYaml(filename), nullptr);
  simuFatfsSetFaults(0);
  EXPECT_EQ(readRaw(real), original);
  // Exercise slow metadata/I/O during backup, validation and deserialization.
  simuFatfsSetFaults(2);
  struct ResetFaults { ~ResetFaults() { simuFatfsSetFaults(0); } } resetFaults;
#endif
  auto error = nb4ValidateModelFile(path);
  EXPECT_EQ(error, nullptr);
  EXPECT_FALSE(nb4ModelBlocked());
  EXPECT_EQ(readRaw(real + ".pre-car-ui"), original);
  g_model.limitData[7].min = 0;
  g_model.failsafeChannels[1] = 0;
  ASSERT_EQ(readModelYaml(filename, reinterpret_cast<uint8_t*>(&g_model), sizeof(g_model)), nullptr);
  EXPECT_EQ(g_model.limitData[7].min, 217);
  EXPECT_EQ(g_model.limitData[1].revert, 1);
  EXPECT_EQ(g_model.failsafeChannels[1], -734);
  EXPECT_EQ(g_model.nb4Racing.homeTimer, NB4_HOME_TIMER_2);
  EXPECT_EQ(g_model.mixData[0].destCh, 7);
  EXPECT_EQ(g_model.mixData[0].srcRaw, MIXSRC_FIRST_STICK);
  EXPECT_EQ(g_model.flightModeData[0].gvars[0], 43);
  EXPECT_STREQ(g_model.getScreenLayoutId(0), "Layout1x1");
  EXPECT_STREQ(g_model.getScreenLayoutId(1), "Layout2x1");
  EXPECT_EQ(nb4ValidateModelFile(path), nullptr);
  EXPECT_EQ(readRaw(real + ".pre-car-ui"), original);
  g_model.limitData[7].min = 99;
  EXPECT_EQ(writeModelYaml(filename), nullptr);
  EXPECT_EQ(readRaw(real + ".previous"), original);
  // Power interruption after renaming the old file, before promoting .tmp.
  std::filesystem::remove(real);
  EXPECT_EQ(nb4ValidateModelFile(path), nullptr);
  EXPECT_EQ(readRaw(real), original);
  std::filesystem::remove(real); std::filesystem::remove(real + ".pre-car-ui");
  std::filesystem::remove(real + ".previous");
}

TEST(Nb4Compatibility, IncompatibleModelCannotBeOverwritten)
{
  ScopedFatfsRoot fatfs("nb4-incompatible-model");
  const char* filename = "nb4_compat_blocked_test.yml";
  char path[256]; getModelPath(path, filename);
  std::string real = simuFatfsGetRealPath(path);
  ASSERT_FALSE(std::filesystem::exists(real));
  std::filesystem::create_directories(std::filesystem::path(real).parent_path());
  const char* content = "header:\n  name: Old car\nmixData:\n  0:\n    destCh: 31\n";
  { std::ofstream file(real); file << content; }
  EXPECT_NE(nb4ValidateModelFile(path), nullptr);
  EXPECT_TRUE(nb4ModelBlocked());
  EXPECT_NE(writeModelYaml(filename), nullptr);
  EXPECT_EQ(readRaw(real), content);
  EXPECT_FALSE(std::filesystem::exists(real + ".pre-car-ui"));
  { std::ofstream file(real, std::ios::trunc); }
  EXPECT_NE(nb4ValidateModelFile(path), nullptr);
  EXPECT_TRUE(nb4ModelBlocked());
  EXPECT_NE(writeModelYaml(filename), nullptr);
  EXPECT_EQ(readRaw(real), "");
  std::filesystem::remove(real);
  nb4AcceptNewCarModel();
  EXPECT_FALSE(nb4ModelBlocked());
}

TEST(Nb4Compatibility, AcceptsCarModelsAndNamesThatLookLikeSources)
{
  const char* text = "header:\n  name: FM2\nmoduleData:\n  0:\n    type: TYPE_FLYSKY_AFHDS3\n    channelsStart: 0\n    channelsCount: 8\nflightModeData:\n  0:\n    name: Base\nnb4Racing:\n  version: 2\n  steeringChannel: 0\n  throttleChannel: 1\nmixData:\n  0:\n    destCh: 7\n    srcRaw: ch(0)\n    flightModes: 000000000\n";
  EXPECT_EQ(nb4ValidateModelText(text, strlen(text)), nullptr);
  const char* range = "moduleData:\n  0:\n    channelsStart: 0\n    channelsCount: 2\n";
  EXPECT_EQ(nb4ValidateModelText(range, strlen(range)), nullptr);
}

TEST(Nb4Compatibility, DetectsUnsupportedDataBeforeDeserialization)
{
  const char* files[] = {
    "nb4ScreenVersion: 2\n",
    "moduleData:\n  0:\n    type: TYPE_CROSSFIRE\n",
    "moduleData:\n  0:\n    type: TYPE_FLYSKY_AFHDS3\n    channelsCount: 12\n",
#if defined(RADIO_NB4)
    "moduleData:\n  0:\n    type: TYPE_FLYSKY_AFHDS3\n    channelsCount: 1\n",
#endif
    "moduleData:\n  0:\n    channelsStart: 1\n    channelsCount: 8\n",
    "flightModeData:\n  1:\n    name: Air\n",
    "swashR:\n  type: 1\n",
    "trainerData:\n  mode: 1\n",
    "varioData:\n  source: 1\n",
    "mixData:\n  0:\n    destCh: 8\n",
    "mixData:\n  0:\n    srcRaw: ch(8)\n",
    "mixData:\n  0:\n    flightModes: 010000000\n",
    "nb4Racing:\n  steeringChannel: 12\n",
    "nb4Racing:\n  version: 4\n",
    "limitData:\n  31:\n    name: Extra\n"
    ,"usbJoystickCh:\n  8:\n    mode: CH_AXIS\n"
    ,"mixData:\n  0:\n    destCh: -1\n"
    ,"moduleData:\n  0:\n    channelsStart: 1\n"
    ,"customFn:\n  0:\n    func: OVERRIDE_CHANNEL\n    def: 8,-100,1\n"
    ,"customFn:\n  0:\n    def: -1,-100,1\n    func: OVERRIDE_CHANNEL\n"
  };
  for (auto text : files) EXPECT_NE(nb4ValidateModelText(text, strlen(text)), nullptr);
}

TEST(Nb4Compatibility, HomeMigrationBacksUpHiddenScreenAndNeverChangesOtherScreens)
{
  nb4FlushSettings();
  ScopedFatfsRoot fatfs("nb4-home-migration");
  std::filesystem::create_directories(fatfs.root / "MODELS");
  SYSTEM_RESET(); MODEL_RESET(); nb4AcceptNewCarModel();
  g_model.resetScreenData();
  g_model.setScreenLayoutId(0, "Layout1x1");
  g_model.getScreenLayoutData(0)->setWidgetName(0, "Value");
  g_model.setScreenLayoutId(1, "Layout2x1");
  g_model.getScreenLayoutData(1)->setWidgetName(1, "NB4Battery");
  g_model.limitData[0].min = 321;
  ASSERT_EQ(writeModelYaml("old.yml"), nullptr);
  const auto original = readRaw((fatfs.root / "MODELS/old.yml").string());
  simuFatfsSetFaults(0, 0);
  EXPECT_FALSE(nb4MigrateHome("/MODELS/old.yml"));
  simuFatfsSetFaults(0);
  EXPECT_EQ(g_model.nb4ScreenVersion, 0);
  EXPECT_STREQ(g_model.getScreenLayoutId(0), "Layout1x1");
  EXPECT_FALSE(std::filesystem::exists(fatfs.root / "MODELS/old.yml.pre-apextx-home"));

  {
    std::ofstream conflicting(fatfs.root / "MODELS/old.yml.pre-apextx-home");
    conflicting << "unrelated backup";
  }
  EXPECT_FALSE(nb4MigrateHome("/MODELS/old.yml"));
  EXPECT_EQ(g_model.nb4ScreenVersion, 0);
  std::filesystem::remove(fatfs.root / "MODELS/old.yml.pre-apextx-home");

  simuFatfsSetRenameFault(1);
  EXPECT_FALSE(nb4MigrateHome("/MODELS/old.yml"));
  simuFatfsSetRenameFault(0);
  EXPECT_EQ(g_model.nb4ScreenVersion, 0);
  EXPECT_TRUE(nb4MigrateHome("/MODELS/old.yml"));
  EXPECT_EQ(readRaw((fatfs.root / "MODELS/old.yml.pre-apextx-home").string()), original);
  EXPECT_EQ(g_model.nb4ScreenVersion, 1);
  EXPECT_STREQ(g_model.getScreenLayoutId(0), "ApexTXRacing");
  EXPECT_STREQ(g_model.getScreenLayoutData(0)->getWidgetName(0), "ApexSteering");
  EXPECT_STREQ(g_model.getScreenLayoutId(1), "Layout2x1");
  EXPECT_STREQ(g_model.getScreenLayoutData(1)->getWidgetName(1), "NB4Battery");
  EXPECT_EQ(g_model.limitData[0].min, 321);

  // Reopening a migrated model must retain customized/empty zones.
  g_model.getScreenLayoutData(0)->clearZone(1);
  ASSERT_EQ(writeModelYaml("old.yml"), nullptr);
  MODEL_RESET(); g_model.resetScreenData();
  ASSERT_EQ(readModelYaml("old.yml", reinterpret_cast<uint8_t*>(&g_model), sizeof(g_model)), nullptr);
  EXPECT_TRUE(nb4MigrateHome("/MODELS/old.yml"));
  EXPECT_FALSE(g_model.getScreenLayoutData(0)->hasWidget(1));
  EXPECT_STREQ(g_model.getScreenLayoutId(1), "Layout2x1");
  EXPECT_EQ(readRaw((fatfs.root / "MODELS/old.yml.pre-apextx-home").string()), original);
}

TEST(Nb4Compatibility, QuickAccessPersistsStableIdsOrderEmptySlotsAndLegacyPreferences)
{
  nb4FlushSettings();
  ScopedFatfsRoot fatfs("nb4-quick-roundtrip");
  std::filesystem::create_directories(fatfs.root / "RADIO");
  SYSTEM_RESET();
  g_eeGeneral.nb4UiVersion = NB4_UI_VERSION;
  g_eeGeneral.nb4Cards[0] = 17;
  nb4QuickAccessReset();
  ASSERT_TRUE(nb4QuickAccessSet(2, 0));
  const auto original0 = g_eeGeneral.nb4QuickAccess[0];
  const auto original1 = g_eeGeneral.nb4QuickAccess[1];
  nb4QuickAccessMove(0, 1);
  ASSERT_EQ(writeGeneralSettings(), nullptr);
  SYSTEM_RESET();
  ASSERT_EQ(loadRadioSettingsYaml(true), nullptr);
  nb4QuickAccessNormalize();
  EXPECT_EQ(g_eeGeneral.nb4QuickAccessVersion, 2);
  EXPECT_EQ(g_eeGeneral.nb4QuickAccess[0], original1);
  EXPECT_EQ(g_eeGeneral.nb4QuickAccess[1], original0);
  EXPECT_EQ(g_eeGeneral.nb4QuickAccess[2], 0u);
  EXPECT_EQ(g_eeGeneral.nb4Cards[0], 17);
}

TEST(Nb4CarState, SeparatesDrivingCommandFromReversedServoOutput)
{
  SYSTEM_RESET();
  nb4RacingDefaults(g_model.nb4Racing);
  g_model.mixData[0].destCh = 1;
  g_model.mixData[0].srcRaw = MIXSRC_FIRST_STICK + 1;
  channelOutputs[1] = RESX / 2;
  ex_chans[1] = -RESX / 2;
  const auto& state = nb4ReadCarState();
  EXPECT_EQ(state.channels[1].command.value, -50);
  EXPECT_EQ(state.channels[1].output.value, 50);
  EXPECT_EQ(state.channels[0].output.validity, Nb4Validity::Absent);
  EXPECT_EQ(state.channels[2].output.validity, Nb4Validity::Absent);
  EXPECT_EQ(state.timer.validity, Nb4Validity::Absent);
}
#endif
