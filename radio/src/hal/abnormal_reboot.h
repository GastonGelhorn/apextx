/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#pragma once

#include <stdint.h>

enum AbnormalRebootCause {
  ARC_None = 0,
  ARC_Watchdog,
  ARC_Software,
};

// Reboot command asking the bootloader to start in USB DFU mode instead of
// launching the application. Shared so the bootloader and the firmware cannot
// drift apart.
#define REBOOT_CMD_DFU 0x55464442

// Reboot command asking the bootloader to hand the radio to the STM32 ROM DFU
// in system memory, so it can be reflashed over USB without opening it to
// reach the internal boot button.
#define REBOOT_CMD_ROM_DFU 0x4D4F5244

// Reboot command marking a restart that must come straight back up without a
// new startup press. A plain software reset is not enough to tell these apart:
// a shutdown while externally powered also resets, and that one must leave the
// radio off.
#define REBOOT_CMD_RESUME 0x52534D45

// Enable detecting abnormal reboots
// This should be called after booting
void abnormalRebootEnableDetection();

// Disable detecting abnormal reboots
// This should be called on normal shutdowns / reboots
void abnormalRebootDisableDetection();

// Test for abnormal reboot conditions
// (see AbnormalRebootCause)
uint32_t abnormalRebootGetCause();

// Retrieve last reboot command
uint32_t abnormalRebootGetCmd();

// Reset reboot command
void abnormalRebootResetCmd();

// Request that the next boot enters the bootloader's USB DFU mode. This only
// records the request; the caller performs the reset.
void abnormalRebootRequestDfu();

// Request that the next boot hands the radio to the STM32 ROM DFU. This only
// records the request; the caller performs the reset.
void abnormalRebootRequestRomDfu();

// Hand control to the STM32 ROM DFU in system memory. Never returns. Must run
// with power already latched and before peripherals are brought up.
void abnormalRebootEnterRomDfu();

// Request that the next boot resumes without a startup press. This only
// records the request; the caller performs the reset.
void abnormalRebootRequestResume();

// Consume a pending resume request, clearing it so a later shutdown reset
// cannot inherit it.
bool abnormalRebootTakeResumeRequest();

#define UNEXPECTED_SHUTDOWN() \
  (abnormalRebootGetCause() == ARC_Watchdog)

#define WAS_RESET_BY_WATCHDOG_OR_SOFTWARE() \
  (abnormalRebootGetCause() != ARC_None)

#define WAS_RESET_BY_SOFTWARE() \
  (abnormalRebootGetCause() == ARC_Software)
