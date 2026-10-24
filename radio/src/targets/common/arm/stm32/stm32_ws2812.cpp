/*
 * Copyright (C) EdgeTx
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

#include "stm32_ws2812.h"
#include "stm32_dma.h"
#include "stm32_gpio.h"

// LED_STRIP_LENGTH (and LED_STRIP_DEBUG_GPIO when DEBUG_WS2812)
#include "hal.h"

#include "definitions.h"
#include "nb4_led_diagnostic.h"

#include <string.h>

// Pixel values
static uint8_t* _led_colors = nullptr;

// LED strip length
static uint8_t _led_strip_len;

static uint8_t _r_offset;
static uint8_t _g_offset;
static uint8_t _b_offset;

#define WS2812_SLOTS_PER_LED        (WS2812_BYTES_PER_LED * 8)

#define WS2812_DMA_IRQ_PRIO    3

#if defined(RADIO_NB4_FAMILY)
// Match the factory NB4+ driver: TIM5 runs directly from its 84 MHz clock and
// emits a 1.214 us bit cell. These values were recovered from the stock image;
// the complete ApexTX sequence still requires NB4+ hardware validation.
#define WS2812_TIMER_TICK_FREQ     LED_STRIP_TIMER_FREQ
#define WS2812_TIMER_PERIOD        102UL
#define WS2812_ONE                 54UL
#define WS2812_ZERO                25UL
#define WS2812_LEAD_RESET_SLOTS    0
#define WS2812_TRAIL_SLOTS         152
#define WS2812_FRAME_REPEATS       2
#else
#define WS2812_FREQ                800000UL // 800 kHz
#define WS2812_TIMER_PERIOD        20UL
#define WS2812_TIMER_TICK_FREQ     (WS2812_FREQ * WS2812_TIMER_PERIOD)
#define WS2812_ONE                 (3 * WS2812_TIMER_PERIOD / 4)
#define WS2812_ZERO                (1 * WS2812_TIMER_PERIOD / 4)

// Hold the line low for 80 us at the start of every DMA transfer.  The line is
// already kept low between frames, but including the reset interval in the
// transfer also makes the first bit deterministic on hardware where restarting
// the timer and DMA briefly races the output compare preload.
#define WS2812_LEAD_RESET_SLOTS    64

// Zero slots appended to the frame: the line stays low while they are sent,
// so the end-of-frame IRQ may run late without emitting a spurious bit.
#define WS2812_TRAIL_SLOTS          4
#define WS2812_FRAME_REPEATS        1
#endif

#if !defined(LED_STRIP_LENGTH)
  #define LED_STRIP_LENGTH           1
#endif

#define WS2812_FRAME_SLOTS \
  (WS2812_LEAD_RESET_SLOTS + LED_STRIP_LENGTH * WS2812_SLOTS_PER_LED + \
   WS2812_TRAIL_SLOTS)

#if defined(RADIO_NB4_FAMILY)
static_assert(LED_STRIP_TIMER_FREQ == 84000000UL,
              "NB4 WS2812 timings require an 84 MHz TIM5 clock");
static_assert(LED_STRIP_TIMER_CHANNEL == LL_TIM_CHANNEL_CH3,
              "NB4 WS2812 DMA request is wired to TIM5 channel 3");
static_assert(LED_STRIP_LENGTH == 4 && WS2812_FRAME_SLOTS == 248,
              "NB4 WS2812 frame must contain 96 data and 152 reset slots");
#endif

// Debug facility
#if defined(LED_STRIP_DEBUG_GPIO) && defined(LED_STRIP_DEBUG_GPIO_PIN)

#define WS2812_DBG_INIT _led_dbg_init()

#define WS2812_DBG_HIGH                                                 \
  LL_GPIO_SetOutputPin(LED_STRIP_DEBUG_GPIO, LED_STRIP_DEBUG_GPIO_PIN)

#define WS2812_DBG_LOW                                                  \
  LL_GPIO_ResetOutputPin(LED_STRIP_DEBUG_GPIO, LED_STRIP_DEBUG_GPIO_PIN)

static void _led_dbg_init() {
  LL_GPIO_InitTypeDef pinInit{0};
  pinInit.Mode = LL_GPIO_MODE_OUTPUT;
  pinInit.Pin = LED_STRIP_DEBUG_GPIO_PIN;
  LL_GPIO_Init(LED_STRIP_DEBUG_GPIO, &pinInit);
  WS2812_DBG_LOW;
}

#else // LED_STRIP_DEBUG_GPIO && LED_STRIP_DEBUG_GPIO_PIN

#define WS2812_DBG_INIT
#define WS2812_DBG_HIGH
#define WS2812_DBG_LOW

#endif

typedef uint16_t led_timer_value_t;
uint8_t pulse_inc = 1;

// The whole frame is emitted in a single DMA transfer: the buffer is filled
// before the transfer starts and no refill deadline exists, so an ISR delayed
// by a higher priority IRQ (SDMMC, USB) can no longer corrupt a frame.
//
// NB4/NB4+: the factory driver sends the frame twice by running a circular
// DMA and stopping it in the second transfer-complete interrupt. That leaves
// the interrupt one bit period (1.2 us) before the circular buffer starts a
// third pass. A late interrupt can cut an unintended partial frame. This
// was a timing hazard, but removing it has NOT resolved the reported NB4+
// corruption. Both copies are therefore laid
// out in the buffer and sent in normal mode: the stream stops by itself on
// the final low slot and the interrupt has no deadline. The waveform on the
// line is unchanged.
#if defined(STM32_SUPPORT_32BIT_TIMERS)
alignas(uint32_t) static led_timer_value_t
    _led_dma_buffer[WS2812_FRAME_SLOTS * WS2812_FRAME_REPEATS * 2] __DMA_NO_CACHE;
#else
static led_timer_value_t
    _led_dma_buffer[WS2812_FRAME_SLOTS * WS2812_FRAME_REPEATS] __DMA_NO_CACHE;
#endif

// Number of slots in one copy of the frame, from the strip length given at init
static uint32_t _frame_slots;

#if defined(NB4_LED_DIAGNOSTIC) && !defined(BOOT)
static volatile uint32_t diagSent, diagComplete, diagTE, diagDME, diagFE;
static volatile uint32_t diagTimeouts, diagFlags, diagRemaining;
static uint32_t diagStartedAt;
extern uint32_t timersGetMsTick();

Nb4LedDiagStats nb4LedDiagDmaStats()
{
  const uint32_t mask = __get_PRIMASK();
  __disable_irq();
  const Nb4LedDiagStats result = {diagSent, diagComplete, diagTE, diagDME,
      diagFE, diagTimeouts, diagFlags, diagRemaining};
  __set_PRIMASK(mask);
  return result;
}
uintptr_t nb4LedDiagDmaBuffer() { return (uintptr_t)_led_dma_buffer; }
#endif

static void _fill_byte(uint8_t c, led_timer_value_t* dma_buffer)
{
  for (int i = 0; i < 8; i++) {
    dma_buffer[i*pulse_inc] = c & 0x80 ? WS2812_ONE : WS2812_ZERO;
    c <<= 1;
  }
}

static void _fill_pulses(const uint8_t* colors, led_timer_value_t* dma_buffer, uint32_t len)
{
  for (uint32_t i = 0; i < len; i++) {
    _fill_byte(*colors, dma_buffer);
    dma_buffer += 8 * pulse_inc;
    colors++;
  }
}

static void _end_of_frame(const stm32_pulse_timer_t* tim)
{
  WS2812_DBG_HIGH;

#if defined(RADIO_NB4_FAMILY)
  LL_TIM_DisableDMAReq_CC3(tim->TIMx);
#else
  LL_TIM_DisableDMAReq_UPDATE(tim->TIMx);
#endif
  LL_DMA_DisableIT_TC(tim->DMAx, tim->DMA_Stream);
#if defined(NB4_LED_DIAGNOSTIC) && !defined(BOOT)
  LL_DMA_DisableIT_TE(tim->DMAx, tim->DMA_Stream);
  LL_DMA_DisableIT_DME(tim->DMAx, tim->DMA_Stream);
  LL_DMA_DisableIT_FE(tim->DMAx, tim->DMA_Stream);
#endif
  LL_DMA_DisableStream(tim->DMAx, tim->DMA_Stream);

  uint32_t timeout = 1000;
  while (LL_DMA_IsEnabledStream(tim->DMAx, tim->DMA_Stream) && timeout--) {
    __NOP();  // Wait
  }

#if defined(RADIO_NB4_FAMILY)
  // FlySky leaves PH12 as a push-pull GPIO driven low between frames.
  LL_TIM_CC_DisableChannel(tim->TIMx, tim->TIM_Channel);
  LL_TIM_DisableCounter(tim->TIMx);
  stm32_pulse_set_cmp_val(tim, 0);
  gpio_clear(tim->GPIO);
  gpio_init(tim->GPIO, GPIO_OUT, GPIO_PIN_SPEED_VERY_HIGH);
#else
  // Leave the channel and counter running with a null compare value: the
  // output is then actively held low between frames.
  stm32_pulse_set_cmp_val(tim, 0);
#endif

  WS2812_DBG_LOW;
}

void ws2812_dma_isr(const stm32_pulse_timer_t* tim)
{
#if defined(NB4_LED_DIAGNOSTIC) && !defined(BOOT)
  // NB4 has DMA1/S0. Snapshot errors before clearing any flags or re-init;
  // a zero error count does not rule out a missed PWM update deadline.
  static_assert(LED_STRIP_TIMER_DMA_STREAM == LL_DMA_STREAM_0,
                "Diagnostic flag decoding is specific to NB4 DMA stream 0");
  const uint32_t flags = tim->DMAx->LISR;
  const uint32_t errors = flags & (DMA_LISR_TEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_FEIF0);
  if (errors) {
    if (errors & DMA_LISR_TEIF0) ++diagTE;
    if (errors & DMA_LISR_DMEIF0) ++diagDME;
    if (errors & DMA_LISR_FEIF0) ++diagFE;
    diagFlags = flags;
    diagRemaining = LL_DMA_GetDataLength(tim->DMAx, tim->DMA_Stream);
    _end_of_frame(tim);
    stm32_dma_clear_flags(tim->DMAx, tim->DMA_Stream);
    return;
  }
#endif
  if (LL_DMA_IsEnabledIT_TC(tim->DMAx, tim->DMA_Stream) &&
      stm32_dma_check_tc_flag(tim->DMAx, tim->DMA_Stream)) {
#if defined(NB4_LED_DIAGNOSTIC) && !defined(BOOT)
    ++diagComplete;
#endif
    _end_of_frame(tim);
  }
}

static void _led_set_dma_periph_addr(const stm32_pulse_timer_t* tim)
{
  volatile uint32_t* cmp_reg = nullptr;
  switch(tim->TIM_Channel) {
  case LL_TIM_CHANNEL_CH1:
  case LL_TIM_CHANNEL_CH1N:
    cmp_reg = &tim->TIMx->CCR1;
    break;
  case LL_TIM_CHANNEL_CH2:
    cmp_reg = &tim->TIMx->CCR2;
    break;
  case LL_TIM_CHANNEL_CH3:
    cmp_reg = &tim->TIMx->CCR3;
    break;
  case LL_TIM_CHANNEL_CH4:
    cmp_reg = &tim->TIMx->CCR4;
    break;
  }

  LL_DMA_SetPeriphAddress(tim->DMAx, tim->DMA_Stream, (uint32_t)cmp_reg);
}

static void _init_timer(const stm32_pulse_timer_t* tim)
{
#if defined(RADIO_NB4_FAMILY)
  // Keep PH12 driven low while TIM5 and DMA are reset. FlySky switches the pin
  // to AF only after configuring the timer; the generic pulse initializer
  // would release the pin before clearing the old timer state.
  stm32_pulse_init(tim, WS2812_TIMER_TICK_FREQ, false);
#else
  stm32_pulse_init(tim, WS2812_TIMER_TICK_FREQ);
#endif
  stm32_pulse_config_output(tim, true, LL_TIM_OCMODE_PWM1, 0);
  LL_TIM_SetAutoReload(tim->TIMx, WS2812_TIMER_PERIOD - 1);

#if defined(RADIO_NB4_FAMILY)
  // Preserve the exact timer and pin configuration used by the factory
  // firmware. CCDS routes the CC3 DMA request through the update event.
  LL_TIM_EnableARRPreload(tim->TIMx);
  LL_TIM_SetUpdateSource(tim->TIMx, LL_TIM_UPDATESOURCE_COUNTER);
  LL_TIM_CC_SetDMAReqTrigger(tim->TIMx, LL_TIM_CCDMAREQUEST_UPDATE);
  gpio_init_af(tim->GPIO, tim->GPIO_Alternate, GPIO_PIN_SPEED_VERY_HIGH);
  LL_GPIO_SetPinPull(gpio_get_port(tim->GPIO), 1 << gpio_get_pin(tim->GPIO),
                     LL_GPIO_PULL_UP);
#else
  // Insurance for the windows where the timer does not drive the pad (boot,
  // de-init): a floating WS2812 input can miss the inter-frame reset.
  LL_GPIO_SetPinPull(gpio_get_port(tim->GPIO), 1 << gpio_get_pin(tim->GPIO),
                     LL_GPIO_PULL_DOWN);
#endif

  // pulse driver uses DMA to ARR, but we need CCRx
  _led_set_dma_periph_addr(tim);

  // Normal mode on every target: on NB4 the repeats are in the buffer.
  LL_DMA_SetMode(tim->DMAx, tim->DMA_Stream, LL_DMA_MODE_NORMAL);

  // we need to use a higher prio to avoid having
  // issues with some other things used during boot
  NVIC_SetPriority(tim->DMA_IRQn, WS2812_DMA_IRQ_PRIO);
}

void ws2812_init(const stm32_pulse_timer_t* timer, uint8_t* strip_colors,
                 uint8_t strip_len, uint8_t type)
{
  WS2812_DBG_INIT;
  pulse_inc = IS_TIM_32B_COUNTER_INSTANCE(timer->TIMx) ? 2 : 1;

  if (strip_len > LED_STRIP_LENGTH) strip_len = LED_STRIP_LENGTH;

  _led_colors = strip_colors;
  _led_strip_len = strip_len;
  _frame_slots = WS2812_LEAD_RESET_SLOTS +
                 strip_len * WS2812_SLOTS_PER_LED + WS2812_TRAIL_SLOTS;
  memset(_led_colors, 0, strip_len * WS2812_BYTES_PER_LED);
  memset(_led_dma_buffer, 0, sizeof(_led_dma_buffer));

  _r_offset = (type >> 4) & 0b11;
  _g_offset = (type >> 2) & 0b11;
  _b_offset = type & 0b11;

  _init_timer(timer);

#if defined(RADIO_NB4_FAMILY)
  // Match the factory idle state until the first frame is requested.
  LL_TIM_CC_DisableChannel(timer->TIMx, timer->TIM_Channel);
  LL_TIM_DisableCounter(timer->TIMx);
  gpio_clear(timer->GPIO);
  gpio_init(timer->GPIO, GPIO_OUT, GPIO_PIN_SPEED_VERY_HIGH);
#else
  // Drive the data line low right away, and keep it driven until the first
  // frame: an idle WS2812 input must never be left floating.
  stm32_pulse_set_cmp_val(timer, 0);
  LL_TIM_CC_EnableChannel(timer->TIMx, timer->TIM_Channel);
  LL_TIM_EnableCounter(timer->TIMx);
#endif
}

void ws2812_set_color_in_buf(uint8_t* buf, uint8_t led,
                             uint8_t r, uint8_t g, uint8_t b)
{
  if (led >= _led_strip_len) return;

  uint8_t* pixel = &buf[led * WS2812_BYTES_PER_LED];
  pixel[_r_offset] = r;
  pixel[_g_offset] = g;
  pixel[_b_offset] = b;
}

uint32_t ws2812_get_color_in_buf(const uint8_t* buf, uint8_t led)
{
  if (led >= _led_strip_len) return 0;

  const uint8_t* pixel = &buf[led * WS2812_BYTES_PER_LED];
  return (pixel[1] << 16) + (pixel[0] << 8) + pixel[2];
}

bool ws2812_get_state_in_buf(const uint8_t* buf, uint8_t led)
{
  if (led >= _led_strip_len) return false;

  const uint8_t* pixel = &buf[led * WS2812_BYTES_PER_LED];
  return pixel[0] || pixel[1] || pixel[2];
}

bool ws2812_is_busy(const stm32_pulse_timer_t* tim)
{
#if defined(NB4_LED_DIAGNOSTIC) && !defined(BOOT)
  if (LL_DMA_IsEnabledStream(tim->DMAx, tim->DMA_Stream) &&
      timersGetMsTick() - diagStartedAt > 20) {
    // Normal frame is <1 ms. Recover a stalled diagnostic without blocking
    // the LED timer task or leaving the strip input toggling indefinitely.
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (LL_DMA_IsEnabledStream(tim->DMAx, tim->DMA_Stream)) {
      ++diagTimeouts;
      diagFlags = tim->DMAx->LISR;
      diagRemaining = LL_DMA_GetDataLength(tim->DMAx, tim->DMA_Stream);
      _end_of_frame(tim);
      stm32_dma_clear_flags(tim->DMAx, tim->DMA_Stream);
    }
    __set_PRIMASK(mask);
  }
#endif
  return LL_DMA_IsEnabledStream(tim->DMAx, tim->DMA_Stream);
}

void ws2812_update(const stm32_pulse_timer_t* tim)
{
  WS2812_DBG_HIGH;
  if (!stm32_pulse_if_not_running_disable(tim)) return;

  // Build the whole frame up front, with deterministic low reset slots before
  // the first pixel and after the final one.
  memset(_led_dma_buffer, 0,
         WS2812_LEAD_RESET_SLOTS * sizeof(led_timer_value_t) * pulse_inc);
  auto* pixel_pulses =
      &_led_dma_buffer[WS2812_LEAD_RESET_SLOTS * pulse_inc];
  _fill_pulses(_led_colors, pixel_pulses,
               _led_strip_len * WS2812_BYTES_PER_LED);
  memset(&pixel_pulses[_led_strip_len * WS2812_SLOTS_PER_LED * pulse_inc],
         0, WS2812_TRAIL_SLOTS * sizeof(led_timer_value_t) * pulse_inc);

  // Further copies of the frame, each with its own low tail.
  const uint32_t frame_values = _frame_slots * pulse_inc;
  for (uint32_t r = 1; r < WS2812_FRAME_REPEATS; r++) {
    memcpy(&_led_dma_buffer[r * frame_values], _led_dma_buffer,
           frame_values * sizeof(led_timer_value_t));
  }

#if defined(RADIO_NB4_FAMILY)
  // The stock implementation reinitialises TIM5, DMA1/S0 and PH12 for every
  // colour change, then returns PH12 to a driven-low GPIO in the TC callback.
  _init_timer(tim);
#endif

  // NDTR and the address are not reloaded by enabling the stream again
  stm32_dma_clear_flags(tim->DMAx, tim->DMA_Stream);
  LL_DMA_SetMemoryAddress(tim->DMAx, tim->DMA_Stream, (uint32_t)_led_dma_buffer);
  LL_DMA_SetDataLength(tim->DMAx, tim->DMA_Stream,
                       _frame_slots * WS2812_FRAME_REPEATS);

  LL_DMA_EnableIT_TC(tim->DMAx, tim->DMA_Stream);
#if defined(NB4_LED_DIAGNOSTIC) && !defined(BOOT)
  LL_DMA_EnableIT_TE(tim->DMAx, tim->DMA_Stream);
  LL_DMA_EnableIT_DME(tim->DMAx, tim->DMA_Stream);
  LL_DMA_EnableIT_FE(tim->DMAx, tim->DMA_Stream);
  diagStartedAt = timersGetMsTick();
  ++diagSent;
#endif
  LL_DMA_EnableStream(tim->DMAx, tim->DMA_Stream);

  LL_TIM_SetCounter(tim->TIMx, 0);
#if defined(RADIO_NB4_FAMILY)
  LL_TIM_EnableDMAReq_CC3(tim->TIMx);
#else
  LL_TIM_EnableDMAReq_UPDATE(tim->TIMx);
#endif
  LL_TIM_CC_EnableChannel(tim->TIMx, tim->TIM_Channel);
  LL_TIM_EnableCounter(tim->TIMx);

  WS2812_DBG_LOW;
}
