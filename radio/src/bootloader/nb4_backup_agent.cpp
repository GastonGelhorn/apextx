/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "board.h"
#include "stm32_spi.h"
#include "stm32_gpio.h"
#include "delays_driver.h"
#include "hal/usb_driver.h"
#include "nb4_backup_lifetime.h"
#if defined(NB4_NOR_RESTORE_AGENT)
#include "nb4_nor_restore_core.h"
#include <atomic>
#endif
#include <string.h>
extern "C" {
#include "usbd_core.h"
#include "usbd_dfu.h"
extern PCD_HandleTypeDef hpcd_USB_OTG;
__attribute__((section(".agent_entry"))) volatile uint32_t nb4BackupEntryIpsr;
__attribute__((section(".agent_entry"))) volatile uint32_t nb4BackupEntryScr;
}

// Both programs execute entirely in SRAM. The backup build has no storage
// writer. The separate restore build can write only the external NOR.
namespace {
USBD_HandleTypeDef device;
volatile uint32_t lastRead = 0;
volatile uint32_t exitAt = 0;
volatile bool exitRequested = false;
nb4::BackupLifetime lifetime;
stm32_spi_t flashBus = {.SPIx=FLASH_SPI, .SCK=FLASH_SPI_SCK_GPIO,
  .MISO=FLASH_SPI_MISO_GPIO, .MOSI=FLASH_SPI_MOSI_GPIO, .CS=FLASH_SPI_CS_GPIO};
struct Info {
  uint8_t magic[8];
  uint32_t protocol, mcu, uid[3], jedec, norSize, flashSize, entryIpsr, entryScr;
};
#if defined(NB4_NOR_RESTORE_AGENT)
Info info{{'A','P','X','N','O','R','1',0}, 4, 0, {}, 0, 0, 2097152, 0, 0};
nb4::NorRestoreCore restore;
uint8_t restoreNonce[16]{};
bool spiFailed = false;
uint8_t byte(uint8_t n) {
  if (spiFailed) return 0xff;
  const auto since = HAL_GetTick();
  while (!LL_SPI_IsActiveFlag_TXE(FLASH_SPI))
    if (uint32_t(HAL_GetTick()-since) >= 10) { spiFailed = true; return 0xff; }
  LL_SPI_TransmitData8(FLASH_SPI,n);
  while (!LL_SPI_IsActiveFlag_RXNE(FLASH_SPI))
    if (uint32_t(HAL_GetTick()-since) >= 10) { spiFailed = true; return 0xff; }
  const auto result=LL_SPI_ReceiveData8(FLASH_SPI);
  while (LL_SPI_IsActiveFlag_BSY(FLASH_SPI))
    if (uint32_t(HAL_GetTick()-since) >= 10) { spiFailed = true; return 0xff; }
  return result;
}
uint32_t word(const uint8_t* p) { return p[0] | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
bool nonce(const uint8_t* p) { return !memcmp(p,restoreNonce,sizeof(restoreNonce)); }
void commandAddress(uint8_t command, uint32_t at) { byte(command); byte(at>>16); byte(at>>8); byte(at); }
int norBusy(void*) {
  stm32_spi_select(&flashBus); byte(5); const auto status=byte(0xff); stm32_spi_unselect(&flashBus);
  return spiFailed ? -1 : status & 1;
}
bool norWriteEnable() {
  if (norBusy(nullptr) != 0) return false;
  stm32_spi_select(&flashBus); byte(6); stm32_spi_unselect(&flashBus);
  stm32_spi_select(&flashBus); byte(5); const auto status=byte(0xff); stm32_spi_unselect(&flashBus);
  // Never remove protection or touch status/security registers.
  return !spiFailed && status == 2;
}
bool norErase(void*, uint32_t at) {
  if (at % 4096 || at >= 8388608 || !norWriteEnable()) return false;
  stm32_spi_select(&flashBus); commandAddress(0x20,at); stm32_spi_unselect(&flashBus); return !spiFailed;
}
bool norProgram(void*, uint32_t at, const uint8_t* data, size_t size) {
  if (at % 256 || size != 256 || at > 8388608-size || !norWriteEnable()) return false;
  stm32_spi_select(&flashBus); commandAddress(2,at);
  for (size_t i=0;i<size;++i) byte(data[i]);
  stm32_spi_unselect(&flashBus); return !spiFailed;
}
bool norRead(void*, uint32_t at, uint8_t* out, size_t size) {
  if (size > 8388608 || at > 8388608-size) return false;
  stm32_spi_select(&flashBus); commandAddress(3,at);
  for (size_t i=0;i<size;++i) out[i]=byte(0xff);
  stm32_spi_unselect(&flashBus); return !spiFailed;
}
const nb4::NorRestoreIo restoreIo{nullptr,norRead,norErase,norProgram,norBusy};
#else
Info info{{'A','P','X','B','K','P','3',0}, 3, 0, {}, 0, 0, 2097152, 0, 0};
uint8_t byte(uint8_t n) { return stm32_spi_transfer_byte(&flashBus, n); }
#endif
bool span(uint32_t at, uint32_t count, uint32_t base, uint32_t size) {
  return at >= base && count <= size && at - base <= size - count;
}
uint16_t init() { return USBD_OK; }
uint16_t stop() { return USBD_OK; }
uint16_t erase(uint32_t) { return USBD_FAIL; }
uint16_t write(uint8_t* input, uint8_t* address, uint32_t count) {
#if defined(NB4_NOR_RESTORE_AGENT)
  const auto at=uintptr_t(address);
  bool accepted=false;
  if (at == 0x92000000 && count == 76 && !memcmp(input,"APXNOR1",8) &&
      !memcmp(input+8,info.uid,12) && word(input+20) == info.jedec &&
      info.jedec == 0xc84017 && word(input+24) == info.norSize && !spiFailed) {
    uint8_t any=0; for (unsigned i=60;i<76;++i) any |= input[i];
    if (any && restore.arm(input+28)) { memcpy(restoreNonce,input+60,16); accepted=true; }
  } else if (at == 0x92000100 && count == 60 && !memcmp(input,"APXBLK1",8) && nonce(input+8))
    accepted=restore.begin(word(input+24),input+28);
  else if (at >= 0x92001000 && at <= 0x92002000 && count && count <= 4096 &&
           count <= 0x92002000-at)
    accepted=restore.append(at-0x92001000,input,count);
  else if (at == 0x92000180 && count == 24 && !memcmp(input,"APXCMT1",8) && nonce(input+8))
    accepted=restore.commit();
  if (accepted) { lastRead=HAL_GetTick(); return USBD_OK; }
  if (restore.working()) return USBD_FAIL;
#endif
  // This synthetic control address accepts only exit, never memory contents.
  if (uintptr_t(address) != 0x91000100 || count != 8 || memcmp(input,"APXEXIT1",8))
    return USBD_FAIL;
  if (!exitRequested) { exitAt = HAL_GetTick(); exitRequested = true; }
  return USBD_OK;
}
uint8_t* read(uint8_t* address, uint8_t* output, uint32_t count) {
  const auto at = uintptr_t(address);
  if (!count || count > USBD_DFU_XFER_SIZE) return nullptr;
  if (span(at, count, 0x91000000, sizeof(info))) {
    memcpy(output, reinterpret_cast<uint8_t*>(&info) + at - 0x91000000, count);
#if defined(NB4_NOR_RESTORE_AGENT)
  } else if (span(at,count,0x92000200,32)) {
    uint32_t state[8] = {uint32_t(restore.phase()),uint32_t(restore.error()),restore.completed(),restore.changed()};
    memcpy(state+4,restoreNonce,16); memcpy(output,reinterpret_cast<uint8_t*>(state)+at-0x92000200,count);
#endif
  } else if (span(at, count, 0x08000000, info.flashSize)) {
    memcpy(output, address, count);
  } else if (info.norSize && span(at, count, 0x90000000, info.norSize)) {
#if defined(NB4_NOR_RESTORE_AGENT)
    if (restore.working()) return nullptr;
#endif
    const uint32_t offset = at - 0x90000000;
    stm32_spi_select(&flashBus);
    byte(3); byte(offset >> 16); byte(offset >> 8); byte(offset);
    for (uint32_t i = 0; i < count; ++i) output[i] = byte(0xff);
    stm32_spi_unselect(&flashBus);
  } else return nullptr;
  lastRead = HAL_GetTick();
  return output;
}
uint16_t status(uint32_t, uint8_t, uint8_t* output) {
  output[1]=1; output[2]=output[3]=0; return USBD_OK;
}
USBD_DFU_MediaTypeDef media = {
#if defined(NB4_NOR_RESTORE_AGENT)
  reinterpret_cast<const uint8_t*>("@ApexTX NOR Restore /0x92000000/001*016Kg"),
#else
  reinterpret_cast<const uint8_t*>("@ApexTX Backup /0x90000000/008*1024Ka"),
#endif
  init, stop, erase, write, read, status
};
uint8_t descriptor[] = {18,1,0,2,0,0,0,64,0x83,4,0x11,0xdf,0,1,1,2,3,1};
uint8_t language[] = {4,3,9,4};
uint8_t text[128];
uint8_t* dev(USBD_SpeedTypeDef, uint16_t* size) { *size=sizeof(descriptor); return descriptor; }
uint8_t* lang(USBD_SpeedTypeDef, uint16_t* size) { *size=sizeof(language); return language; }
uint8_t* string(const char* value, uint16_t* size) {
  USBD_GetString(reinterpret_cast<uint8_t*>(const_cast<char*>(value)), text, size); return text;
}
uint8_t* maker(USBD_SpeedTypeDef, uint16_t* size) { return string("ApexTX", size); }
uint8_t* product(USBD_SpeedTypeDef, uint16_t* size) {
#if defined(NB4_NOR_RESTORE_AGENT)
  return string("ApexTX NOR Restore Agent", size);
#else
  return string("ApexTX Read-only Backup Agent", size);
#endif
}
uint8_t* serial(USBD_SpeedTypeDef, uint16_t* size) {
  char value[13]{}; constexpr char hex[]="0123456789ABCDEF";
  const uint32_t a=info.uid[0]+info.uid[2], b=info.uid[1] >> 16;
  for (unsigned i=0;i<8;++i) value[i]=hex[(a >> (28-i*4)) & 15];
  for (unsigned i=0;i<4;++i) value[8+i]=hex[(b >> (12-i*4)) & 15];
  return string(value,size);
}
USBD_DescriptorsTypeDef descriptions{dev,lang,maker,product,serial,product,product};
}
extern "C" void SysTick_Handler() {
  // Use HAL's own time base: USB initialization and its polling timeouts
  // must advance even when the driver library inlines HAL_GetTick with LTO.
  HAL_IncTick();
}
extern "C" void OTG_FS_IRQHandler() { HAL_PCD_IRQHandler(&hpcd_USB_OTG); }
extern "C" void usbHostSofReceived() {}
extern "C" int getSelectedUsbMode() { return USB_DFU_MODE; }
extern "C" __attribute__((used,section(".bootstrap"))) void nb4BackupAgentEarlyInit() {
  // ROM Leave is a branch, not a reset. Latch power before touching its
  // clocks, and return the PLL to a reset state before SystemClock_Config.
  // This runs before .bss and constructors: use registers only.
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOIEN;
  (void)RCC->AHB1ENR;
  GPIOI->BSRR = GPIO_BSRR_BS_14;
  GPIOI->OTYPER &= ~(1u << 14);
  GPIOI->MODER = (GPIOI->MODER & ~(3u << 28)) | (1u << 28);
  __DSB();
  // A stalled clock/SPI/USB initialization must not hold the power latch
  // indefinitely. Only the working main loop reloads this watchdog.
  IWDG->KR = 0xcccc;
  IWDG->KR = 0x5555;
  IWDG->PR = 6; // /256, approximately 11..31 seconds across the LSI range
  IWDG->RLR = 2047;
  while (IWDG->SR) {}
  IWDG->KR = 0xaaaa;
  nb4BackupEntryScr = SCB->SCR;
  SCB->SCR = 0;
  SysTick->CTRL = 0;
  SysTick->LOAD = SysTick->VAL = 0;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
  RCC->CR |= RCC_CR_HSION;
  while (!(RCC->CR & RCC_CR_HSIRDY)) {}
  RCC->CFGR = 0;
  while (RCC->CFGR & RCC_CFGR_SWS) {}
  RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_PLLI2SON | RCC_CR_HSEON |
               RCC_CR_HSEBYP | RCC_CR_CSSON);
  while (RCC->CR & RCC_CR_PLLRDY) {}
  RCC->PLLCFGR = 0x24003010;
  RCC->CIR = 0;
}
int main() {
  for (unsigned i=0;i<8;++i) { NVIC->ICER[i]=0xffffffff; NVIC->ICPR[i]=0xffffffff; }
  gpio_init(PWR_ON_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW); gpio_set(PWR_ON_GPIO);
  gpio_init(PWR_SWITCH_GPIO, GPIO_IN_PU, GPIO_PIN_SPEED_LOW);
  delaysInit();
  memcpy(info.uid,reinterpret_cast<const void*>(UID_BASE),sizeof(info.uid));
  info.mcu=DBGMCU->IDCODE;
  info.entryIpsr = nb4BackupEntryIpsr; info.entryScr = nb4BackupEntryScr;
  stm32_spi_init(&flashBus, LL_SPI_DATAWIDTH_8BIT);
  stm32_spi_unselect(&flashBus);
  LL_SPI_Disable(FLASH_SPI);
  stm32_spi_set_max_baudrate(&flashBus, 10000000);
  LL_SPI_Enable(FLASH_SPI);
  delay_ms(1);
  stm32_spi_select(&flashBus); byte(0x9f);
  info.jedec=uint32_t(byte(0xff)) << 16; info.jedec|=uint32_t(byte(0xff)) << 8; info.jedec|=byte(0xff);
  stm32_spi_unselect(&flashBus);
  if ((info.jedec & 0xff) == 0x17) info.norSize=8388608;
  gpio_init_af(USB_GPIO_DM,USB_GPIO_AF,GPIO_PIN_SPEED_VERY_HIGH);
  gpio_init_af(USB_GPIO_DP,USB_GPIO_AF,GPIO_PIN_SPEED_VERY_HIGH);
  // Discard the ROM USB controller's endpoint and interrupt state before the
  // new device stack owns it. No peripheral reset touches either memory.
  RCC->AHB2RSTR |= RCC_AHB2RSTR_OTGFSRST;
  __DSB();
  RCC->AHB2RSTR &= ~RCC_AHB2RSTR_OTGFSRST;
  __set_BASEPRI(0);
  __set_FAULTMASK(0);
  SysTick_Config(SystemCoreClock/1000); __enable_irq();
  USBD_Init(&device,&descriptions,0);
  USBD_RegisterClass(&device,&USBD_DFU);
  USBD_DFU_RegisterMedia(&device,&media);
  USBD_Start(&device);
  NVIC_EnableIRQ(OTG_FS_IRQn);
  bool poweredOff = false;
#if defined(NB4_NOR_RESTORE_AGENT)
  bool pendingPowerOff = false;
#endif
  for (;;) {
    const uint32_t now = HAL_GetTick();
#if defined(NB4_NOR_RESTORE_AGENT)
    restore.service(now,restoreIo);
    if (restore.working()) lastRead=now;
#endif
    lifetime.activity(lastRead);
    auto action = lifetime.poll(now, !gpio_read(PWR_SWITCH_GPIO));
#if defined(NB4_NOR_RESTORE_AGENT)
    if (action == nb4::BackupAction::PowerOff) pendingPowerOff=true;
    if (pendingPowerOff) {
      action=restore.working() ? nb4::BackupAction::None : nb4::BackupAction::PowerOff;
      if (!restore.working()) pendingPowerOff=false;
    }
#endif
    // Allow the last DFU status transaction to complete before detaching.
    if (!poweredOff && exitRequested && uint32_t(now - exitAt) >= 500) {
      lifetime.powerOff(); action = nb4::BackupAction::PowerOff;
    }
    if (!poweredOff && action == nb4::BackupAction::PowerOff) {
      NVIC_DisableIRQ(OTG_FS_IRQn);
      auto regs = reinterpret_cast<USB_OTG_DeviceTypeDef*>(
          uintptr_t(USB_OTG_FS) + USB_OTG_DEVICE_BASE);
      regs->DCTL |= USB_OTG_DCTL_SDIS;
      gpio_clear(PWR_ON_GPIO);
      poweredOff = true;
    }
    if (action == nb4::BackupAction::Restart) {
      gpio_set(PWR_ON_GPIO);
      NVIC_SystemReset();
    }
    IWDG->KR = 0xaaaa;
    __WFI();
  }
}
