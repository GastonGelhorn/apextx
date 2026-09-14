// SPDX-License-Identifier: GPL-2.0-only
#include "nb4_update.h"
#include "boot.h"
#include "board.h"
#include "hal/usb_driver.h"
#include "hal/abnormal_reboot.h"
#include "os/time.h"
#include "lcd.h"
#include "stm32_hal_ll.h"
extern "C" {
#include "usbd_dfu.h"
}

using namespace nb4update;
extern const etx_flash_driver_t stm32_flash_driver;

// Linker-allocated SDRAM, separate from framebuffers and the heap. SDRAM is
// initialized by boardBLInit before USB can access this NOLOAD storage.
alignas(4) static uint8_t staging[StageCapacity] __attribute__((section(".sdram")));
static Updater updater(staging);
static bool mayExit = false;

static int eraseForUpdate(uint32_t address)
{
  if (address == AppAddress) {
    // Begin a new, validated transaction with clean completion/error flags.
    // HAL otherwise rejects its first operation on a pending error from before
    // the update. New erase failures are still checked and reported by HAL.
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                          FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
#if defined(FLASH_FLAG_RDERR)
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_RDERR);
#endif
  }
  return stm32_flash_driver.erase_sector(address);
}

static int programAndFlush(uint32_t address, void* data, uint32_t size)
{
  int result = stm32_flash_driver.program(address, data, size);
  // F4 blocking HAL_FLASH_Program does not invalidate the ART data cache.
  // In particular, reading the first body bytes can cache erased vectors in
  // the same line. Readback must see flash after the vectors are committed.
  FLASH_FlushCaches();
  __DSB();
  __ISB();
  return result;
}

bool nb4BootApplicationValid()
{
  auto manifest = reinterpret_cast<const Manifest*>(ManifestAddress);
  return validApplication(*manifest, reinterpret_cast<const uint8_t*>(AppAddress));
}

static uint16_t mediaInit() { return USBD_OK; }
static uint16_t mediaDeInit() { return USBD_OK; }
static uint16_t mediaErase(uint32_t address)
{
  return updater.erase(address) ? USBD_OK : USBD_FAIL;
}
static uint16_t mediaWrite(uint8_t* src, uint8_t* dest, uint32_t size)
{
  return updater.write(reinterpret_cast<uintptr_t>(dest), src, size) ? USBD_OK : USBD_FAIL;
}
static uint8_t* mediaRead(uint8_t* src, uint8_t* dest, uint32_t size)
{
  return updater.read(reinterpret_cast<uintptr_t>(src), dest, size) ? dest : nullptr;
}
static uint16_t mediaStatus(uint32_t, uint8_t, uint8_t* status)
{
  status[1] = 1; status[2] = status[3] = 0; // RAM copy, no flash in USB IRQ
  return USBD_OK;
}
static const USBD_DFU_MediaTypeDef media = {
  reinterpret_cast<const uint8_t*>("@ApexTX NB4 Update /0xC0200000/015*128Kg"),
  mediaInit, mediaDeInit, mediaErase, mediaWrite, mediaRead, mediaStatus,
};

static void restartApplication()
{
  usbStop();
  // boardInit consumes this request to skip the startup power-button hold.
  // A plain software reset also occurs during shutdown and cannot imply resume.
  abnormalRebootRequestResume();
  NVIC_SystemReset();
}

// The watchdog has to outlast one blocking sector erase, which takes up to two
// seconds on this part and longer as the pack runs down. Nothing can feed it
// in the meantime: the erase runs inside the HAL and does not return until the
// sector is gone. watchdogInit cannot express enough margin, because its fixed
// /32 prescaler caps the window at 4095 ms nominal, and the LSI that clocks
// the watchdog is only specified to 17-47 kHz, so a window programmed in
// nominal milliseconds can be a third shorter than it reads. A reset landing
// inside an erase leaves the application invalid and the radio back here,
// failing at the same sector on every retry, with only the internal boot
// button left to recover it.
//
// Program the watchdog directly with a /256 prescaler instead: eight seconds
// nominal, five and a half at the fast end of the LSI range, still short
// enough to recover a bootloader that has genuinely stopped.
static void armUpdateWatchdog()
{
  LL_IWDG_EnableWriteAccess(IWDG);
  LL_IWDG_SetPrescaler(IWDG, LL_IWDG_PRESCALER_256);
  LL_IWDG_EnableWriteAccess(IWDG);
  LL_IWDG_SetReloadCounter(IWDG, 1000);
  LL_IWDG_ReloadCounter(IWDG);
  LL_IWDG_Enable(IWDG);
}

static bool poweringOff = false;

static void service()
{
  watchdogReset();
  static uint32_t lastFrame = 0;
  if (time_get_ms() - lastFrame < 50) return;
  lastFrame = time_get_ms();
  const char* label = "Open ApexTX Updater";
  if (poweringOff) {
    bootloaderDrawNB4Update("Disconnect USB to switch off", 0, false);
    lcdRefresh();
    return;
  }
  switch (updater.state()) {
    case State::Receiving:
      if (updater.percent()) label = "Receiving firmware";
      break;
    case State::Queued: label = "Checking firmware"; break;
    case State::Installing: label = "Installing firmware"; break;
    case State::Done: label = "Verified. Ready to restart"; break;
    case State::Error: label = "Update failed. Please retry"; break;
    case State::Restart: label = "Restarting..."; break;
  }
  bootloaderDrawNB4Update(label, updater.percent(), mayExit);
  lcdRefresh();
}

void bootloaderNB4Update()
{
  // A software reset may have left the independent watchdog running; once
  // enabled it cannot be stopped, only reprogrammed.
  armUpdateWatchdog();
  mayExit = nb4BootApplicationValid();
  usbRegisterDFUMedia(&media);
  usbStart(); // NB4 has no reliable VBUS pin; wait even without a cable.
  etx_flash_driver_t updateFlash = stm32_flash_driver;
  updateFlash.erase_sector = eraseForUpdate;
  updateFlash.program = programAndFlush;
  bool released = false;
  uint32_t pressedSince = 0;
  for (;;) {
    service();
    if (updater.state() == State::Queued) {
      mayExit = false;
      updater.install(updateFlash,
                      reinterpret_cast<const uint8_t*>(AppAddress), service);
      mayExit = nb4BootApplicationValid();
    }
    if (!pwrPressed()) { released = true; pressedSince = 0; }
    else if (released) {
      if (!pressedSince) pressedSince = time_get_ms();
      if (time_get_ms() - pressedSince > 1500) {
        if (mayExit) restartApplication();
        // There is no application to start, but the user must still be able to
        // switch the radio off rather than watch it sit here until the pack is
        // flat. Open the power latch: on battery that cuts power at once, and
        // while externally powered the MCU stays awake until the cable comes
        // out, which is the same behaviour as the ROM DFU hand-off.
        usbStop();
        pwrOff();
        poweringOff = true;
        for (;;) service();
      }
    }
    if (updater.state() == State::Restart) {
      // Let the final USB status request complete before detaching.
      uint32_t start = time_get_ms();
      while (time_get_ms() - start < 750) service();
      restartApplication();
    }
  }
}
