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

#include "stm32_adc.h"
#include "stm32_gpio.h"
#include "stm32_spi.h"

#include "hal/adc_driver.h"
#include "hal/trainer_driver.h"
#include "hal/switch_driver.h"
#include "hal/abnormal_reboot.h"
#include "hal/watchdog_driver.h"
#include "hal/usb_driver.h"
#include "hal/gpio.h"
#include "hal/rotary_encoder.h"

#include "board.h"
#include "boards/generic_stm32/analog_inputs.h"
#include "boards/generic_stm32/module_ports.h"
#include "boards/generic_stm32/rgb_leds.h"

#include "globals.h"
#include "sdcard.h"
#include "touch.h"
#include "debug.h"

#if defined(AUDIO_SPI)
  #include "vs1053b.h"
#endif

#include "delays_driver.h"
#include "timers_driver.h"
#include "battery_driver.h"
#include "touch_driver.h"

#include "bitmapbuffer.h"
#include "colors.h"

#include <string.h>

// Common ADC driver
extern const etx_hal_adc_driver_t _adc_driver;

extern "C" void SDRAM_Init();

#if defined(SEMIHOSTING)
extern "C" void initialise_monitor_handles();
#endif

#if defined(SPI_FLASH)
extern "C" void flushFTL();
#endif

#if defined(RADIO_NV14_FAMILY)
  HardwareOptions hardwareOptions;

  static uint8_t boardGetPcbRev()
  {
    delaysInit();
    gpio_init(INTMODULE_PWR_GPIO, GPIO_IN, GPIO_PIN_SPEED_LOW);
    delay_ms(1); // delay to let the input settle, else it does not work properly

    // detect NV14 vs EL18
    if (gpio_read(INTMODULE_PWR_GPIO)) {
      // pull-up connected: EL18
      return PCBREV_EL18;
    } else {
      // pull-down connected: NV14
      return PCBREV_NV14;
    }
  }
#endif

#if defined(AUDIO_SPI)
static void audio_set_rst_pin(bool set)
{
  gpio_write(AUDIO_RST_GPIO, set);
}

static void audio_set_mute_pin(bool set)
{
#if defined(INVERTED_MUTE_PIN)
  gpio_write(AUDIO_MUTE_GPIO, !set);
#else
  gpio_write(AUDIO_MUTE_GPIO, set);
#endif
}

static void audioInit()
{
  static stm32_spi_t spi_dev = {
      .SPIx = AUDIO_SPI,
      .SCK = AUDIO_SPI_SCK_GPIO,
      .MISO = AUDIO_SPI_MISO_GPIO,
      .MOSI = AUDIO_SPI_MOSI_GPIO,
      .CS = AUDIO_CS_GPIO,
  };

  static vs1053b_t vs1053 = {
      .spi = &spi_dev,
      .XDCS = AUDIO_XDCS_GPIO,
      .DREQ = AUDIO_DREQ_GPIO,
      .set_rst_pin = audio_set_rst_pin,
      .set_mute_pin = audio_set_mute_pin,
      .mute_delay_ms = AUDIO_MUTE_DELAY,
      .unmute_delay_ms = AUDIO_UNMUTE_DELAY,
  };

  gpio_init(AUDIO_RST_GPIO, GPIO_OUT, 0);
  gpio_init(AUDIO_MUTE_GPIO, GPIO_OUT, 0);
  audio_set_mute_pin(true);

  vs1053b_init(&vs1053);
}
#endif

void delay_self(int count)
{
   for (int i = 50000; i > 0; i--)
   {
       for (; count > 0; count--);
   }
}

#if defined(LED_STRIP_GPIO)
void ledStripOff()
{
  rgbLedClearAll();
}
#endif

#if defined(RADIO_NB4_FAMILY)
void disableVoiceChip()
{
  gpio_init(VOICE_CHIP_EN_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
  gpio_clear(VOICE_CHIP_EN_GPIO);
}
#endif

void boardBLEarlyInit()
{
#if defined(RADIO_PL18U)
  pwrOn();
#endif
  // USB charger status pins
  gpio_init(UCHARGER_GPIO, GPIO_IN, GPIO_PIN_SPEED_LOW);

#if defined(RADIO_NB4_FAMILY) && defined(UCHARGER_CHARGE_END_GPIO)

  gpio_init(UCHARGER_CHARGE_END_GPIO, GPIO_IN, GPIO_PIN_SPEED_LOW);
#endif

#if defined(USB_SW_GPIO)
  gpio_init(USB_SW_GPIO, GPIO_OUT, GPIO_PIN_SPEED_LOW);
#endif

#if defined(RADIO_NV14_FAMILY)
  // detect NV14 vs EL18
  hardwareOptions.pcbrev = boardGetPcbRev();
#endif
}

void boardBLPreJump()
{
  SDRAM_Init();
#if defined(RADIO_NB4_FAMILY)
  LL_ADC_Disable(ADC_MAIN);
#endif
}

void boardBLInit()
{
#if defined(ROTARY_ENCODER_NAVIGATION) && !defined(USE_HATS_AS_KEYS)
  rotaryEncoderInit();
#endif
  SDRAM_Init();
}

static void monitorInit()
{
#if defined(VBUS_MONITOR_GPIO)
  gpio_init(VBUS_MONITOR_GPIO, GPIO_IN, GPIO_PIN_SPEED_LOW);
#endif
}

#if defined(RADIO_NB4)
static bool nb4PowerButtonHeldForStartup()
{
  if (!pwrPressed()) return false;

  const uint32_t pressStart = timersGetMsTick();
  while (pwrPressed()) {
    if (timersGetMsTick() - pressStart >= POWER_ON_DELAY) return true;
    delay_ms(10);
  }
  return false;
}

static bool nb4ExternalPowerPresent()
{
  // The original NB4 has no dedicated VBUS input. Allow the USB line probe to
  // run before deciding that a short press came from battery power alone.
  const uint32_t probeStart = timersGetMsTick();
  do {
    if (IS_UCHARGER_ACTIVE()) return true;
    usbPlugged();
    if (usbPlugged()) return true;
    delay_ms(10);
  } while (timersGetMsTick() - probeStart < 350);

  return false;
}
#endif

void boardInit()
{
#if defined(SEMIHOSTING)
  initialise_monitor_handles();
#endif

#if !defined(SIMU)
  // enable interrupts
  __enable_irq();
#endif

#if defined(RADIO_NB4) && !defined(SIMU)

  delaysInit();
#endif

#if defined(RADIO_NV14_FAMILY)
  // detect NV14 vs EL18
  hardwareOptions.pcbrev = boardGetPcbRev();
  TRACE("\n%s board started :)",
        hardwareOptions.pcbrev == PCBREV_NV14 ?
        "NV14" : "EL18");
#else
  TRACE("\nPL18 board started :)");
#endif

  delay_ms(10);
  TRACE("RCC->CSR = %08x", RCC->CSR);

#if defined(RADIO_NB4)

  SDRAM_Init();
#endif

  pwrInit();
  boardInitModulePorts();

#if defined(AUDIO_SPI)
  gpio_init(AUDIO_RST_GPIO, GPIO_OUT, GPIO_PIN_SPEED_MEDIUM);
  gpio_init(AUDIO_MUTE_GPIO, GPIO_OUT, GPIO_PIN_SPEED_MEDIUM);
#endif

#if !defined(RADIO_NB4_FAMILY)
  board_trainer_init();
#endif
  battery_charge_init();

  gimbalsDetect();
  timersInit();
  touchPanelInit();
  usbInit();

#if defined(LED_STRIP_GPIO)
  rgbLedInit();
#endif

#if !defined(RADIO_NB4)
  uint32_t press_start = 0;
  uint32_t press_end = 0;
#endif

#if defined(RADIO_NB4)
  // Only a restart that explicitly asked to resume may skip the startup press.
  // Testing for a software reset instead would also match the shutdown path,
  // which resets rather than cutting power while externally powered, and the
  // radio would turn itself straight back on.
  if (UNEXPECTED_SHUTDOWN() || abnormalRebootTakeResumeRequest()) {
#else
  if (UNEXPECTED_SHUTDOWN()) {
#endif
    pwrOn();
#if defined(RADIO_NB4)
  } else {
    // The original NB4 qualifies the startup press itself in every case,
    // including while the charging base or USB supplies power. It has no
    // charge screen, so routing an externally powered start into the shared
    // charging loop below skipped this qualification entirely and left the
    // radio to shut down again once the application armed its power checks.
    //
    // Latch power immediately, then qualify the press. With external power the
    // MCU remains powered even after opening the latch, so keep it awake and
    // wait for a new long press instead of entering an unrecoverable stop mode.
    pwrOn();
    while (!nb4PowerButtonHeldForStartup()) {
      if (!nb4ExternalPowerPresent()) {
        pwrOff();
        while (true) {
        }
      }
    }
#else
  } else if (isChargerActive()) {
    while (true) {
      pwrOn();
      uint32_t now = timersGetMsTick();
      if (pwrPressed()) {
        press_end = now;
        if (press_start == 0) press_start = now;
        if ((now - press_start) > POWER_ON_DELAY) {
          break;
        }
        delay_ms(10);
      } else if (!isChargerActive()) {
        boardOff();
      } else {
        uint32_t press_end_touch = press_end;
        if (touchPanelEventOccured()) {
          touchPanelRead();
          press_end_touch = timersGetMsTick();
        }
        press_start = 0;
        handle_battery_charge(press_end_touch);
        delay_ms(10);
        press_end = 0;
      }
    }
    battery_charge_end();
#endif
  }

  keysInit();
  switchInit();
#if defined(ROTARY_ENCODER_NAVIGATION) && !defined(USE_HATS_AS_KEYS)
  rotaryEncoderInit();
#endif
#if defined(RADIO_NB4_FAMILY)
  disableVoiceChip();
#endif

  audioInit();
  monitorInit();
  adcInit(&_adc_driver);
  hapticInit();

 #if defined(RTCLOCK)
  rtcInit(); // RTC must be initialized before rambackupRestore() is called
#endif
#if defined(LED_STRIP_GPIO)
#if !defined(POWER_LED_BLUE)
  ledBlue();
#else
  ledGreen();
#endif
#endif
}

extern void rtcDisableBackupReg();

void boardOff()
{
  lcdOff();

#if defined(RADIO_NB4)
  uint32_t release_start = 0;
  while (release_start == 0 || timersGetMsTick() - release_start < 150) {
    if (pwrPressed()) release_start = 0;
    else if (release_start == 0) release_start = timersGetMsTick();
    WDG_RESET();
    delay_ms(10);
  }
#else
  while (pwrPressed()) {
    WDG_RESET();
  }
#endif

  SysTick->CTRL = 0; // turn off systick

  // Shutdown the Haptic
  hapticDone();

  rtcDisableBackupReg();

#if !defined(BOOT)
#if defined(LED_STRIP_GPIO)
  ledStripOff();
#endif
  if (isChargerActive())
  {
    NVIC_SystemReset();
  }
  else
#endif
  {
    pwrOff();
  }

  // We reach here only in forced power situations, such as hw-debugging with external power
  // Enter STM32 stop mode / deep-sleep
  // Code snippet from ST Nucleo PWR_EnterStopMode example
#define PDMode             0x00000000U
#if defined(PWR_CR_MRUDS) && defined(PWR_CR_LPUDS) && defined(PWR_CR_FPDS)
  MODIFY_REG(PWR->CR, (PWR_CR_PDDS | PWR_CR_LPDS | PWR_CR_FPDS | PWR_CR_LPUDS | PWR_CR_MRUDS), PDMode);
#elif defined(PWR_CR_MRLVDS) && defined(PWR_CR_LPLVDS) && defined(PWR_CR_FPDS)
  MODIFY_REG(PWR->CR, (PWR_CR_PDDS | PWR_CR_LPDS | PWR_CR_FPDS | PWR_CR_LPLVDS | PWR_CR_MRLVDS), PDMode);
#else
  MODIFY_REG(PWR->CR, (PWR_CR_PDDS| PWR_CR_LPDS), PDMode);
#endif // PWR_CR_MRUDS && PWR_CR_LPUDS && PWR_CR_FPDS

  // Set SLEEPDEEP bit of Cortex System Control Register
  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  // To avoid HardFault at return address, end in an endless loop
  while (1) {

  }
}

#if defined(RADIO_NB4)

static uint8_t nb4UsbLinePulledDown(gpio_t pin)
{
  gpio_clear(pin);
  gpio_init(pin, GPIO_OUT, GPIO_PIN_SPEED_LOW);
  delay_us(5);
  gpio_init(pin, GPIO_IN_PU, GPIO_PIN_SPEED_LOW);
  delay_us(200);
  uint8_t pulled = gpio_read(pin) ? 0 : 1;
  gpio_init_af(pin, USB_GPIO_AF, GPIO_PIN_SPEED_VERY_HIGH);
  return pulled;
}

static volatile uint8_t _nb4UsbLines = 0;
static volatile uint8_t _nb4UsbProbing = 0;
static uint32_t _nb4UsbLinesAt = 0;
#define NB4_USB_PROBE_EVERY_10MS  25   /* One quarter of a second */

static uint8_t nb4UsbCableSensed(uint32_t now)
{

  if (!_nb4UsbProbing && (uint32_t)(now - _nb4UsbLinesAt) >= NB4_USB_PROBE_EVERY_10MS) {
    _nb4UsbProbing = 1;
    uint8_t lines = (nb4UsbLinePulledDown(USB_GPIO_DP) ? 1 : 0) |
                    (nb4UsbLinePulledDown(USB_GPIO_DM) ? 2 : 0);
    _nb4UsbLines = lines;
    _nb4UsbLinesAt = now;
    _nb4UsbProbing = 0;
  }
  return (_nb4UsbLines == 0x03) ? 1 : 0;
}

static uint32_t _nb4TickLast = 0;
static uint32_t _nb4TickRest = 0;
static uint32_t _nb4Tick10ms = 0;
static bool _nb4TickArmed = false;

static uint32_t nb4Tick10ms()
{
  uint32_t now = ticksNow();
  if (!_nb4TickArmed) {
    _nb4TickArmed = true;
    _nb4TickLast = now;
    return _nb4Tick10ms;
  }
  _nb4TickRest += (uint32_t)(now - _nb4TickLast);
  _nb4TickLast = now;
  _nb4Tick10ms += _nb4TickRest / (CPU_FREQ / 100);
  _nb4TickRest %= (CPU_FREQ / 100);
  return _nb4Tick10ms;
}

#define NB4_USB_ANSWER_10MS   200    /* 2 s */
#define NB4_USB_QUIET_10MS     50    /* 500 ms */
#define NB4_USB_BURN_10MS   30000    /* 5 min */

static uint32_t _nb4UsbSince = 0;      /* Stack startup */
static uint32_t _nb4UsbQuietSince = 0; /* First quiet period after enumeration */
static uint32_t _nb4UsbBurnUntil = 0;
static uint8_t _nb4UsbBurnRaw = 0xff;
static bool _nb4UsbStarted = false;
static bool _nb4UsbEnumerated = false;
static bool _nb4UsbQuietArmed = false;

static uint8_t nb4UsbConnected()
{
  uint32_t now = nb4Tick10ms();

  if (usbStarted()) {
    if (!_nb4UsbStarted) {
      _nb4UsbStarted = true;
      _nb4UsbEnumerated = false;
      _nb4UsbQuietArmed = false;
      _nb4UsbSince = now;
    }
    if (usbHostEnumerated()) _nb4UsbEnumerated = true;

    if (!_nb4UsbEnumerated) {
      if ((uint32_t)(now - _nb4UsbSince) < NB4_USB_ANSWER_10MS) return 1;

      _nb4UsbBurnUntil = now + NB4_USB_BURN_10MS;
      _nb4UsbBurnRaw = NB4_CHARGE_RAW();
      return 0;
    }

    if (usbHostSessionAlive()) {
      _nb4UsbQuietArmed = false;
      return 1;
    }
    if (!_nb4UsbQuietArmed) {
      _nb4UsbQuietArmed = true;
      _nb4UsbQuietSince = now;
      return 1;
    }
    return ((uint32_t)(now - _nb4UsbQuietSince) < NB4_USB_QUIET_10MS) ? 1 : 0;
  }

  if (_nb4UsbStarted) {

    _nb4UsbLines = 0;
    _nb4UsbLinesAt = now - NB4_USB_PROBE_EVERY_10MS;  /* Force an immediate measurement */
  }
  _nb4UsbStarted = false;
  _nb4UsbQuietArmed = false;

  uint8_t raw = NB4_CHARGE_RAW();
  if (raw == NB4_CHARGE_BASE) return 0;

  if (_nb4UsbBurnRaw != 0xff) {
    if (raw != _nb4UsbBurnRaw || (int32_t)(now - _nb4UsbBurnUntil) >= 0) {
      _nb4UsbBurnRaw = 0xff;      /* Lift the temporary block */
    } else {
      return 0;
    }
  }

  if (raw == NB4_CHARGE_USB) return 1;

  return nb4UsbCableSensed(now);
}

bool nb4BatteryFull()
{
  static bool full = false;
  static uint8_t ticks = 0;
  const uint16_t mv = (uint16_t)(getBatteryVoltage() * 10);  // 10 mV -> mV
  if (!full) {
    if (mv < 4150) { ticks = 0; return false; }
    if (ticks < 0xFF) ticks++;
    if (ticks > 9) { ticks = 0; full = true; }
    return full;
  }
  if (mv >= 4120) { ticks = 0; return true; }
  if (ticks < 0xFF) ticks++;
  if (ticks > 19) { ticks = 0; full = false; }
  return full;
}

uint8_t nb4ChargeSource()
{
  const uint8_t raw = NB4_CHARGE_RAW();
  if (raw == NB4_CHARGE_BASE) return 1;
  if (raw == NB4_CHARGE_USB) return 2;
  return 0;
}

uint8_t nb4UsbDiagBits()
{
  uint8_t bits = NB4_CHARGE_RAW();
  if (usbStarted()) {
    bits |= 0x10;
  } else {
    nb4UsbCableSensed(nb4Tick10ms());
    bits |= (uint8_t)((_nb4UsbLines & 0x03) << 2);
  }
  if (usbPlugged()) bits |= 0x20;
  return bits;
}
#endif

#if !defined(RADIO_NV14_FAMILY)
int usbPlugged()
{
  static uint8_t debouncedState = 0;
  static uint8_t lastState = 0;

#if defined(RADIO_NB4)
  uint8_t state = nb4UsbConnected();
#else
  uint8_t state = IS_UCHARGER_ACTIVE();
#endif

  if (state == lastState)
    debouncedState = state;
  else
    lastState = state;

  return debouncedState;
}
#endif
