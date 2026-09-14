/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
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

#include "hal/key_driver.h"

#include "stm32_hal_ll.h"
#include "stm32_gpio_driver.h"

#include "hal.h"
#include "delays_driver.h"
#include "keys.h"

#if !defined(BOOT)
  #include "hal/adc_driver.h"
#endif

#define BOOTLOADER_KEYS                 0x42

enum PhysicalTrims {
  STD = 0,   // Steering, down
  STU,       // Steering, up
  THD = 2,   // Throttle, down
  THU,       // Throttle, up
  T3D = 4,   // TR1-LR, decrease
  T3U,
  T4D = 6,   // TR2-LR, decrease
  T4U,
};

#define NB4_SW1_R_MAX      299
#define NB4_SW1_BOTH_MIN  1701
#define NB4_SW1_BOTH_MAX  2299
#define NB4_SW1_L_MIN     3601

#define NB4_TRIM_W1_MAX    299   /* Front-back axis, positive */
#define NB4_TRIM_W2_MIN    801   /* Left-right axis, negative */
#define NB4_TRIM_W2_MAX   1199
#define NB4_TRIM_W3_MIN   1801   /* Front-back axis, negative */
#define NB4_TRIM_W3_MAX   2199
#define NB4_TRIM_W4_MIN   2801   /* Left-right axis, positive */
#define NB4_TRIM_W4_MAX   3199

void keysInit()
{
#if defined(BOOT)
  LL_GPIO_InitTypeDef pinInit;
  LL_GPIO_StructInit(&pinInit);

  pinInit.Mode = LL_GPIO_MODE_ANALOG;
  pinInit.Pull = LL_GPIO_PULL_NO;
  pinInit.Pin = ADC_GPIO_PIN_RAW1;

  stm32_gpio_enable_clock(ADC_GPIO_RAW1);
  LL_GPIO_Init(ADC_GPIO_RAW1, &pinInit);

  uint32_t adc_idx = (((uint32_t) ADC_MAIN) - ADC1_BASE) / 0x100UL;
  uint32_t adc_msk = RCC_APB2ENR_ADC1EN << adc_idx;
  LL_APB2_GRP1_EnableClock(adc_msk);

  LL_ADC_CommonInitTypeDef commonInit;
  LL_ADC_CommonStructInit(&commonInit);
  commonInit.CommonClock = LL_ADC_CLOCK_SYNC_PCLK_DIV4;
  LL_ADC_CommonInit(__LL_ADC_COMMON_INSTANCE(ADC_MAIN), &commonInit);

  LL_ADC_Disable(ADC_MAIN);

  LL_ADC_InitTypeDef adcInit;
  LL_ADC_StructInit(&adcInit);
  adcInit.SequencersScanMode = LL_ADC_SEQ_SCAN_DISABLE;
  adcInit.DataAlignment = LL_ADC_DATA_ALIGN_RIGHT;
  adcInit.Resolution = LL_ADC_RESOLUTION_12B;
  LL_ADC_Init(ADC_MAIN, &adcInit);

  LL_ADC_REG_InitTypeDef adcRegInit;
  LL_ADC_REG_StructInit(&adcRegInit);
  adcRegInit.TriggerSource = LL_ADC_REG_TRIG_SOFTWARE;
  adcRegInit.ContinuousMode = LL_ADC_REG_CONV_SINGLE;
  LL_ADC_REG_Init(ADC_MAIN, &adcRegInit);

  LL_ADC_Enable(ADC_MAIN);
#endif
}

#if defined(BOOT)

static uint16_t _adcRead()
{
  LL_ADC_REG_SetSequencerRanks(ADC_MAIN, LL_ADC_REG_RANK_1, ADC_CHANNEL_RAW1);
  LL_ADC_SetChannelSamplingTime(ADC_MAIN, ADC_CHANNEL_RAW1, LL_ADC_SAMPLINGTIME_3CYCLES);
  LL_ADC_REG_StartConversionSWStart(ADC_MAIN);
  while (!LL_ADC_IsActiveFlag_EOCS(ADC_MAIN));
  return LL_ADC_REG_ReadConversionData12(ADC_MAIN);
}
#endif

static bool adcHasData()
{
#if defined(BOOT)
  return true;
#else
  return getAnalogValue(8) != 0;   /* Index 8 = VBAT */
#endif
}

uint32_t readKeys()
{
  uint32_t result = 0;

  if (!adcHasData()) return 0;

#if defined(BOOT)
  uint16_t value = _adcRead();
#else
  uint16_t value = getAnalogValue(4);   // RAW1 = channel 11 = PC1 = SW1
#endif

  if (value >= NB4_SW1_L_MIN) {
    result |= 1u << KEY_EXIT;
  } else if (value <= NB4_SW1_R_MAX) {
    result |= 1u << KEY_ENTER;
  }

  return result;
}

uint32_t readTrims()
{
  uint32_t result = 0;

  if (!adcHasData()) return 0;

#if defined(BOOT)
  uint16_t value = _adcRead();
  if (value >= NB4_SW1_BOTH_MIN && value <= NB4_SW1_BOTH_MAX) {
    result = BOOTLOADER_KEYS;
  }
#else
  const uint16_t tr1 = getAnalogValue(6);   // RAW3 = channel 6  = PA6 = TR1
  const uint16_t tr2 = getAnalogValue(7);   // RAW4 = channel 14 = PC4 = TR2

  if (tr1 <= NB4_TRIM_W1_MAX) {
    result |= 1u << STU;                                   // TR1-FB positive
  } else if (tr1 >= NB4_TRIM_W2_MIN && tr1 <= NB4_TRIM_W2_MAX) {
    result |= 1u << T3D;                                   // TR1-LR negative
  } else if (tr1 >= NB4_TRIM_W3_MIN && tr1 <= NB4_TRIM_W3_MAX) {
    result |= 1u << STD;                                   // TR1-FB negative
  } else if (tr1 >= NB4_TRIM_W4_MIN && tr1 <= NB4_TRIM_W4_MAX) {
    result |= 1u << T3U;                                   // TR1-LR positive
  }

  if (tr2 <= NB4_TRIM_W1_MAX) {
    result |= 1u << THU;                                   // TR2-FB positive
  } else if (tr2 >= NB4_TRIM_W2_MIN && tr2 <= NB4_TRIM_W2_MAX) {
    result |= 1u << T4D;                                   // TR2-LR negative
  } else if (tr2 >= NB4_TRIM_W3_MIN && tr2 <= NB4_TRIM_W3_MAX) {
    result |= 1u << THD;                                   // TR2-FB negative
  } else if (tr2 >= NB4_TRIM_W4_MIN && tr2 <= NB4_TRIM_W4_MAX) {
    result |= 1u << T4U;                                   // TR2-LR positive
  }
#endif

  return result;
}
