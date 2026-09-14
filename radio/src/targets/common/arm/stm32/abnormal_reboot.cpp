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

#include "hal/abnormal_reboot.h"

#include "stm32_hal_ll.h"

#define __REBOOT_DATA __attribute__((section(".rebootdata"), aligned(4)))

// This variable is define at a fixed memory location
// and is used in bootloader and firmware to pass
// commands across reboot.
uint32_t _reboot_cmd __REBOOT_DATA;

constexpr uint32_t _SOFTRESET_REQUEST = 0xCAFEDEAD;
constexpr uint32_t _SHUTDOWN_REQUEST = 0xDEADBEEF;

constexpr uint32_t _REBOOT_CAUSE_DEFAULT = 0xFFFFFFFF;

static uint32_t _reboot_cause = _REBOOT_CAUSE_DEFAULT;

#if defined(DEBUG)
uint32_t _dbg_csr = 0xFFFFFFFF;
#endif

void _init_reboot_cause()
{
  if (_reboot_cause != _REBOOT_CAUSE_DEFAULT) return;

#if defined(DEBUG)
  _dbg_csr = LL_RCC_ReadReg(CSR);
#endif
  
#ifdef STM32H7
  if (LL_RCC_IsActiveFlag_IWDG1RST()) {
#else
  if (LL_RCC_IsActiveFlag_IWDGRST()) {
#endif
    _reboot_cause = ARC_Watchdog;
  } else if (LL_RCC_IsActiveFlag_SFTRST()) {
    _reboot_cause = ARC_Software;
  } else {
    _reboot_cause = ARC_None;
  }

#if !defined(BOOT)
  LL_RCC_ClearResetFlags();
#endif
}

void abnormalRebootEnableDetection()
{
  _init_reboot_cause();
  _reboot_cmd = 0;
}

void abnormalRebootRequestSoftReset()
{
  _reboot_cmd = _SOFTRESET_REQUEST;
}

void abnormalRebootRequestDfu()
{
  _reboot_cmd = REBOOT_CMD_DFU;
}

void abnormalRebootRequestRomDfu()
{
  _reboot_cmd = REBOOT_CMD_ROM_DFU;
}

void abnormalRebootRequestResume()
{
  _reboot_cmd = REBOOT_CMD_RESUME;
}

bool abnormalRebootTakeResumeRequest()
{
  if (_reboot_cmd != REBOOT_CMD_RESUME) return false;
  _reboot_cmd = 0;
  return true;
}

// System memory base of the STM32F4 ROM bootloader (AN2606).
#define _ROM_DFU_BASE 0x1FFF0000u

void abnormalRebootEnterRomDfu()
{
  // The request must not survive into the ROM, or leaving DFU would come
  // straight back here instead of starting the firmware.
  _reboot_cmd = 0;

  __disable_irq();

  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;

  for (unsigned i = 0; i < 8; i += 1) {
    NVIC->ICER[i] = 0xFFFFFFFFu;
    NVIC->ICPR[i] = 0xFFFFFFFFu;
  }

  // Return the clock tree to its reset state. Entering the ROM through BOOT0
  // gives it pristine clocks, and its USB setup depends on that: jumping in
  // with the application PLL still running leaves the ROM unable to enumerate
  // as a DFU device. GPIO is deliberately left alone, because resetting it
  // would open the power latch.
  RCC->CR |= RCC_CR_HSION;
  while (!(RCC->CR & RCC_CR_HSIRDY)) {
  }
  RCC->CFGR = 0;
  while (RCC->CFGR & RCC_CFGR_SWS) {
  }
  RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_HSEBYP | RCC_CR_CSSON | RCC_CR_PLLON |
               RCC_CR_PLLI2SON);
  RCC->PLLCFGR = 0x24003010;  // reset value
  RCC->CIR = 0;

  // Map system memory at zero so the ROM finds its own vector table.
  RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
  SYSCFG->MEMRMP = 0x01;
  __DSB();
  __ISB();

  // The ROM is written to run from reset, and a reset leaves VTOR at zero and
  // interrupts enabled. Neither holds after the jump unless done here: VTOR
  // still names this bootloader's table, so any ROM interrupt would land in
  // bootloader code, and PRIMASK is still set from above, so the ROM's USB
  // interrupt would never fire. Both leave the ROM running but unable to
  // enumerate. Nothing can fire in the window before the jump because every
  // NVIC line was disabled and cleared above.
  SCB->VTOR = _ROM_DFU_BASE;
  __set_CONTROL(0);
  __ISB();

  const uint32_t* vectors = (const uint32_t*)_ROM_DFU_BASE;
  __set_MSP(vectors[0]);
  __enable_irq();
  ((void (*)(void))vectors[1])();

  while (true) {
  }
}


uint32_t abnormalRebootGetCause()
{
  _init_reboot_cause();
  return _reboot_cause;
}

uint32_t abnormalRebootGetCmd()
{
  return _reboot_cmd;
}

void abnormalRebootResetCmd()
{
  _reboot_cmd = 0;
}
