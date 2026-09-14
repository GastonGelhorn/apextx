/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include "afhds3_config.h"

#if defined(RADIO_NB4)
namespace afhds3 {
// Normal RF operation is mode 2 on the NB4. Legacy AFHDS3 RUN=3 selects the
// NB4 hardware-test mode instead (wire state 0xff).
inline constexpr uint8_t nb4NormalMode = 2;

// Default receiver template. Runtime-owned receiver identity and hopping data
// are replaced by command 0x05 after bind.
inline constexpr uint8_t nb4DefaultReceiver[168] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0xff, 0x00, 0x03, 0x0a, 0x0a, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x00, 0xe6, 0x00, 0x04, 0x00, 0x21, 0x00, 0x15, 0x00, 0x13, 0x00, 0x0e,
  0x00, 0x0d, 0x00, 0x0c, 0x00, 0x24, 0x00, 0x01, 0x00, 0x18, 0x00, 0x19,
  0x00, 0x03, 0x00, 0x25, 0x00, 0x08, 0x00, 0x14, 0x00, 0x1d, 0x00, 0x05,
  0x00, 0x22, 0x00, 0x16, 0x00, 0x14, 0x00, 0x0f, 0x00, 0x10, 0x00, 0x12,
  0x00, 0x27, 0x00, 0x02, 0x00, 0x1a, 0x00, 0x1b, 0x00, 0x11, 0x00, 0x26,
  0x00, 0x0b, 0x00, 0x19, 0x00, 0x07, 0x00, 0x40, 0x00, 0x01, 0x00, 0x00,
  0x00, 0x02, 0x03, 0x70, 0x04, 0x10, 0x00, 0x00, 0x30, 0x08, 0x45, 0x00,
  0x00, 0x0a, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

struct Nb4RfConfig {
  uint8_t receiver[168];
  bool bound = false;
  static uint16_t get16(const uint8_t* p) { return p[0] | (uint16_t(p[1]) << 8); }
  static void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
  static bool enhanced() { return g_model.moduleData[0].afhds3.phyMode >= ROUTINE_FLCR1_18CH; }
  static uint32_t modelKey() {
    const auto& module = g_model.moduleData[0];
    uint32_t key = sentModuleChannels(0) | (module.afhds3.telemetry << 8) |
                   (module.failsafeMode << 9);
    const unsigned channels = sentModuleChannels(0);
    for (unsigned i = 0; i < channels; ++i)
      key = (key ^ uint16_t(g_model.failsafeChannels[i])) * 16777619u;
    return key;
  }

  void load(Config_u& cfg) {
    const auto* saved = g_model.nb4RfSettings;
    const bool valid = saved[0] == 1 && saved[195] == enhanced();
    memcpy(receiver, valid ? saved + 2 : nb4DefaultReceiver, sizeof(receiver));
    if (!valid && enhanced()) {
      // Enhanced-mode template overrides.
      receiver[0x3a] = 0x19; receiver[0x3d] = 1;
      receiver[0x40] = 1; receiver[0x8b] = 0x30;
    }
    bound = valid && saved[1] == 1;
    cfg.version = enhanced() ? 1 : 0;
    cfg.v0.SignalStrengthRCChannelNb = valid ? receiver[0x36] : 0xff;
    cfg.v0.FailsafeTimeout = saved[0] == 1 ? get16(saved + 193) : 500;
    if (cfg.v0.FailsafeTimeout < 230) cfg.v0.FailsafeTimeout = 500;
    if (enhanced()) {
      for (unsigned i = 0; i < 32; ++i)
        cfg.v1.PWMFrequenciesV1.PWMFrequencies[i] = saved[0] == 1 && i < 8 ? get16(saved + 170 + i * 2) : 50;
      cfg.v1.PWMFrequenciesV1.Synchronized = saved[0] == 1 ? saved[186] : 0;
      for (unsigned i = 0; i < 4; ++i) cfg.v1.NewPortTypes[i] = saved[0] == 1 ? saved[189 + i] : SES_NPT_PWM;
    } else {
      cfg.v0.PWMFrequency.Frequency = saved[0] == 1 ? get16(saved + 170) : 50;
      cfg.v0.PWMFrequency.Synchronized = saved[0] == 1 ? saved[186] & 1 : 0;
      cfg.v0.AnalogOutput = saved[0] == 1 ? saved[187] : 0;
    }
    cfg.others.ExternalBusType = saved[0] == 1 ? saved[188] : EB_BT_IBUS1;
  }

  void save(const Config_u& cfg) const {
    uint8_t data[196]{};
    data[0] = 1; data[1] = bound; data[195] = cfg.version;
    memcpy(data + 2, receiver, sizeof(receiver));
    put16(data + 2 + 0x34, cfg.v0.FailsafeTimeout >= 230 ? cfg.v0.FailsafeTimeout - 230 : 270);
    data[2 + 0x36] = cfg.v0.SignalStrengthRCChannelNb;
    data[2 + 0x44] = g_model.moduleData[0].afhds3.telemetry ? 3 : 0;
    for (unsigned i = 0; i < 8; ++i)
      put16(data + 170 + i * 2, cfg.version ? cfg.v1.PWMFrequenciesV1.PWMFrequencies[i] : cfg.v0.PWMFrequency.Frequency);
    data[186] = cfg.version ? cfg.v1.PWMFrequenciesV1.Synchronized : cfg.v0.PWMFrequency.Synchronized;
    data[187] = cfg.version ? 0 : cfg.v0.AnalogOutput;
    data[188] = cfg.others.ExternalBusType;
    for (unsigned i = 0; i < 4; ++i) data[189 + i] = cfg.version ? cfg.v1.NewPortTypes[i] : SES_NPT_PWM;
    put16(data + 193, cfg.v0.FailsafeTimeout);
    if (memcmp(data, g_model.nb4RfSettings, sizeof(data))) {
      memcpy(g_model.nb4RfSettings, data, sizeof(data));
      storageDirty(EE_MODEL);
    }
  }

  // At least status + 167 receiver bytes are required. Hardware sends 169
  // bytes, including a reserved tail the module ignores.
  // Copy the same prefix and leave our last reserved receiver byte untouched.
  bool acceptReceiver(const uint8_t* data, unsigned length, Config_u& cfg) {
    if (length < 168) return false;
    memcpy(receiver, data + 1, 167);
    bound = true;
    save(cfg);
    return true;
  }

  uint8_t page(uint8_t index, bool bind, Config_u& cfg, uint8_t* out) {
    memset(out, 0, 170);
    out[0] = bind ? 1 : 2; out[1] = index;
    const unsigned channels = sentModuleChannels(0);
    if (index == 0) return 2;
    if (index == 1) {
      out[2] = enhanced();
      for (unsigned i = 0; i < 32; ++i) {
        uint16_t hz = enhanced() ? cfg.v1.PWMFrequenciesV1.PWMFrequencies[i] : cfg.v0.PWMFrequency.Frequency;
        if (!hz || i >= channels) hz = 50;
        put16(out + 3 + i * 2, hz);
      }
      if (enhanced()) {
        uint32_t sync = cfg.v1.PWMFrequenciesV1.Synchronized;
        for (unsigned i = 0; i < 4; ++i) out[67 + i] = sync >> (i * 8);
        memcpy(out + 71, cfg.v1.NewPortTypes, 4);
        out[75] = cfg.others.ExternalBusType == EB_BT_SBUS1;
        return 78;
      }
      out[71] = cfg.v0.PWMFrequency.Synchronized;
      put16(out + 72, cfg.v0.PWMFrequency.Frequency ? cfg.v0.PWMFrequency.Frequency : 50);
      out[74] = cfg.v0.AnalogOutput;
      out[75] = cfg.others.ExternalBusType == EB_BT_SBUS1;
      return 76;
    }
    if (index == 2) {
      out[2] = 1;
      out[6] = 12; // normal-bind timeout
      out[19] = 100;
      out[23] = channels;
      for (unsigned i = 0; i < 32; ++i) {
        uint16_t value = 0x8001;
        if (i < channels) {
          const auto mode = g_model.moduleData[0].failsafeMode;
          int16_t selected = g_model.failsafeChannels[i];
          if (mode == FAILSAFE_HOLD || (mode == FAILSAFE_CUSTOM && selected == FAILSAFE_CHANNEL_HOLD)) value = 0x8000;
          else if (mode == FAILSAFE_CUSTOM && selected != FAILSAFE_CHANNEL_NOPULSE)
            value = limit<int16_t>(AFHDS3_FAILSAFE_MIN, selected * 10, AFHDS3_FAILSAFE_MAX);
        }
        put16(out + 24 + i * 2, value);
        out[88 + i] = 0x1c;
        out[120 + i] = 0xff;
      }
      return 152;
    }
    if (index == 3) {
      put16(receiver + 0x34, cfg.v0.FailsafeTimeout >= 230 ? cfg.v0.FailsafeTimeout - 230 : 270);
      receiver[0x36] = cfg.v0.SignalStrengthRCChannelNb;
      // 3 enables two-way feedback; 0 is one-way.
      receiver[0x44] = g_model.moduleData[0].afhds3.telemetry ? 3 : 0;
      memcpy(out + 2, receiver, sizeof(receiver));
      return 170;
    }
    return 0;
  }
};
} // namespace afhds3
#endif
