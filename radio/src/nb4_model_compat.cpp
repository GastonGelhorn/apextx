/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_model_compat.h"
#include "nb4_racing.h"
#include "storage/sdcard_yaml.h"
#include "storage/yaml/yaml_parser.h"
#include <atomic>

namespace {
std::atomic<bool> blocked{false};
const char* issue = nullptr;
struct ModelCheck {
  char keys[MAX_DEPTH][32] = {};
  unsigned level = 0;
  unsigned channelsStart = 0;
  unsigned channelsCount = 8;
  int moduleLevel = -1;
  int functionLevel = -1;
  bool overrideChannel = false;
  bool hasModelData = false;
  int overrideIndex = 0;
  const char* error = nullptr;

  bool contains(const char* key) const {
    for (unsigned i = 0; i <= level; ++i) if (!strcmp(keys[i], key)) return true;
    return false;
  }
  void fail(const char* reason) { if (!error) error = reason; }
  bool outside(const std::string& text, long maximum) {
    char* end;
    auto value = strtol(text.c_str(), &end, 10);
    return end == text.c_str() || *end || value < 0 || value > maximum;
  }
  void finishModule() {
    if (moduleLevel >= 0 &&
#if defined(RADIO_NB4)
        (channelsStart != 0 || channelsCount < 2 || channelsCount > 8)) {
      fail("NB4 RF range must start at CH1 and contain 2..8 channels");
#else
        (channelsStart > 7 || channelsCount < 1 || channelsCount > 8 ||
         channelsStart > 8 - channelsCount)) {
      fail("RF channel range exceeds CH8");
#endif
    }
    moduleLevel = -1;
  }
  void finishFunction() {
    if (functionLevel >= 0 && overrideChannel && (overrideIndex < 0 || overrideIndex >= 8))
      fail("Channel override exceeds CH8");
    functionLevel = -1;
  }
  void finish() { finishModule(); finishFunction(); }
  bool parent() {
    if (!level) return false;
    --level;
    if (moduleLevel > static_cast<int>(level)) finishModule();
    if (functionLevel > static_cast<int>(level)) finishFunction();
    return true;
  }
  void field(const char* key, unsigned length) {
    auto count = min<size_t>(length, sizeof(keys[0]) - 1);
    memcpy(keys[level], key, count); keys[level][count] = 0;
    if (!level && (!strcmp(keys[level], "header") || !strcmp(keys[level], "mixData") ||
        !strcmp(keys[level], "expoData") || !strcmp(keys[level], "limitData") ||
        !strcmp(keys[level], "moduleData") || !strcmp(keys[level], "nb4Racing"))) hasModelData = true;
    if (level && !strcmp(keys[level - 1], "moduleData")) {
      finishModule(); moduleLevel = level; channelsStart = 0; channelsCount = 8;
    }
    if (level && !strcmp(keys[level - 1], "customFn")) {
      finishFunction(); functionLevel = level; overrideChannel = false; overrideIndex = 0;
    }
    if (!level || !isdigit(keys[level][0])) return;
    const char* parent = keys[level - 1];
    unsigned index = strtoul(keys[level], nullptr, 10);
    if (!strcmp(parent, "flightModeData") && index > 0) fail("Flight modes require review");
    if ((!strcmp(parent, "limitData") || !strcmp(parent, "failsafeChannels") ||
         !strcmp(parent, "usbJoystickCh")) && index >= 8)
      fail("Channels above CH8 require review");
  }
  void value(const char* value, unsigned length) {
    std::string v(value, length);
    const char* key = keys[level];
    if (!level && !strcmp(key, "nb4ScreenVersion") && outside(v, 1))
      fail("Newer home screen version requires review");
    if (contains("swashR") && v != "0" && !v.empty()) fail("Helicopter setup requires review");
    if (contains("varioData") && !strcmp(key, "source") && v != "0" && !v.empty()) fail("Variometer requires review");
    if (contains("trainerData") && !strcmp(key, "mode") && v != "0" && !v.empty()) fail("Trainer setup requires review");
    if (contains("moduleData") && !strcmp(key, "type") &&
        v != "TYPE_NONE" && v != "TYPE_FLYSKY_AFHDS3" && v != "0") fail("Only the AFHDS3 module is supported");
    if (!strcmp(key, "destCh") && outside(v, 7)) fail("Mix destination exceeds CH8");
    if (contains("moduleData")) {
      if (!strcmp(key, "channelsStart")) {
        if (outside(v, 7)) fail("RF channel range exceeds CH8");
        channelsStart = strtoul(v.c_str(), nullptr, 10);
      }
      if (!strcmp(key, "channelsCount")) {
        if (outside(v, 8)) fail("RF channel range exceeds CH8");
        channelsCount = strtoul(v.c_str(), nullptr, 10);
      }
    }
    if (contains("nb4Racing")) {
      if ((!strcmp(key, "steeringChannel") || !strcmp(key, "throttleChannel")) && outside(v, 7))
        fail("Driving channel exceeds CH8");
      if (!strcmp(key, "version") && outside(v, NB4_RACING_VERSION)) fail("Newer car model version requires review");
    }
    if (!strcmp(key, "flightModes") && v.find('1') != std::string::npos) fail("Flight-dependent mix requires review");
    if (!strcmp(key, "func") && (v == "TRAINER" || v == "FUNC_TRAINER")) fail("Trainer function requires review");
    if (!strcmp(key, "func") && (v == "VARIO" || v == "FUNC_VARIO")) fail("Variometer function requires review");
    if (functionLevel >= 0) {
      if (!strcmp(key, "func")) overrideChannel = v == "OVERRIDE_CHANNEL";
      if (!strcmp(key, "def")) overrideIndex = outside(v.substr(0, v.find(',')), 7) ? -1 : atoi(v.c_str());
    }
    // Source references are serialized symbolically; never reinterpret a
    // source offset after compiling out channels or flight functionality.
    bool reference = !strcmp(key, "srcRaw") || !strcmp(key, "src") || !strcmp(key, "source") ||
      !strcmp(key, "swtch") || !strcmp(key, "switch") || !strcmp(key, "andSw") ||
      !strcmp(key, "v1") || !strcmp(key, "v2");
    if (!reference) return;
    auto start = v.empty() || v[0] != '!' ? 0u : 1u;
    auto source = v.substr(start);
    if (source.compare(0, 3, "ch(") == 0 &&
        outside(source.substr(3, source.find(')') - 3), 7))
      fail("Source above CH8 requires review");
    if (source.compare(0, 4, "trn(") == 0 || source.compare(0, 3, "CYC") == 0)
      fail("Aircraft source requires review");
    if (source.compare(0, 2, "FM") == 0 && source.size() > 2 && isdigit(source[2]) && source[2] != '0')
      fail("Flight-mode switch requires review");
  }
};
const YamlParserCalls calls = {
  [](void* p) { return static_cast<ModelCheck*>(p)->parent(); },
  [](void* p) { auto& c = *static_cast<ModelCheck*>(p); if (c.level + 1 >= MAX_DEPTH) { c.fail("Model nesting exceeds limit"); return false; } c.keys[++c.level][0] = 0; return true; },
  [](void* p) {
    auto& c = *static_cast<ModelCheck*>(p);
    // EdgeTX writes mixes/inputs as sequences, while channel banks, modules
    // and functions use explicit indices. Reject only ambiguous bank indices.
    if (c.contains("flightModeData") || c.contains("limitData") || c.contains("failsafeChannels") ||
        c.contains("moduleData") || c.contains("customFn") || c.contains("usbJoystickCh"))
      c.fail("Unindexed model bank requires review");
    return true;
  },
  [](void* p, const char* key, uint8_t n) { static_cast<ModelCheck*>(p)->field(key, n); return true; },
  [](void* p, const char* value, uint16_t n) { static_cast<ModelCheck*>(p)->value(value, n); }
};
}

const char* nb4ValidateModelText(const char* text, size_t length)
{
  ModelCheck check;
  YamlParser parser;
  parser.init(&calls, &check); parser.set_eof();
  if (parser.parse(text, length) == YamlParser::STRING_OVERFLOW) return "Model string exceeds limit";
  check.finish();
  return check.error;
}

const char* nb4ValidateModelFile(const char* path)
{
  ModelCheck check;
  FILINFO info;
  if (f_stat(path, &info) == FR_NO_FILE) {
    std::string previous = std::string(path) + ".previous";
    if (f_stat(previous.c_str(), &info) == FR_OK && f_rename(previous.c_str(), path) != FR_OK)
      check.fail("Interrupted model save could not be recovered");
  }
  auto readError = readYamlFile(path, &calls, &check, nullptr);
  check.finish();
  if (!readError && !check.hasModelData) check.fail("Model data missing; original file preserved");
  if (readError && f_stat(path, &info) == FR_OK)
    check.fail("Model could not be read; original file preserved");
  if (!readError && !check.error) {
    // Preserve raw YAML, including fields unknown to this build, before any
    // model migration/sanitisation can mark the in-memory model for writing.
    std::string backup = std::string(path) + ".pre-car-ui";
    FILINFO info;
    auto status = f_stat(backup.c_str(), &info);
    if (status == FR_NO_FILE) {
      std::string temporary = backup + ".tmp";
      if (sdCopyFile(path, temporary.c_str()) || f_rename(temporary.c_str(), backup.c_str()) != FR_OK)
        check.fail("Model backup failed; original file preserved");
    } else if (status != FR_OK) {
      check.fail("Model backup unavailable; original file preserved");
    }
  }
  issue = check.error;
  blocked.store(issue != nullptr, std::memory_order_release);
  return issue ? issue : readError;
}
const char* nb4InspectModelFile(const char* path)
{
  ModelCheck check;
  const auto error = readYamlFile(path, &calls, &check, nullptr);
  check.finish();
  if (!error && !check.hasModelData) check.fail("Model data missing");
  return error ? error : check.error;
}
bool nb4ModelBlocked() { return blocked.load(std::memory_order_acquire); }
const char* nb4ModelCompatibilityIssue() { return issue; }
void nb4AcceptNewCarModel() { issue = nullptr; blocked.store(false, std::memory_order_release); }
#endif
