/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include <stdint.h>

namespace nb4 {
enum class BackupAction { None, PowerOff, Restart };
class BackupLifetime {
 public:
  void activity(uint32_t now) { lastActivity = now; }
  void powerOff() { off = true; released = holding = false; }
  BackupAction poll(uint32_t now, bool pressed) {
    if (!pressed) { released = true; holding = false; }
    else if (released && !holding) { holding = true; pressedAt = now; }
    if (holding && uint32_t(now - pressedAt) >= 2000) {
      if (off) { released = holding = false; return BackupAction::Restart; }
      powerOff(); return BackupAction::PowerOff;
    }
    if (!off && uint32_t(now - lastActivity) >= 120000) {
      powerOff(); return BackupAction::PowerOff;
    }
    return BackupAction::None;
  }
 private:
  uint32_t lastActivity = 0, pressedAt = 0;
  bool off = false, released = false, holding = false;
};
}
