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

#include "edgetx_types.h"

#include "colors.h"

#if defined(LCD_PHYS_W) && defined(LCD_PHYS_H)
  #define DISPLAY_PIXELS_COUNT         (LCD_PHYS_W * LCD_PHYS_H)
#else
  #define DISPLAY_PIXELS_COUNT         (LCD_W * LCD_H)
#endif
#define DISPLAY_BUFFER_SIZE            (DISPLAY_PIXELS_COUNT)

#if defined(BOOT)
  #define BLINK_ON_PHASE               (0)
#else
  #define BLINK_ON_PHASE               (g_blinkTmr10ms & (1<<6))
  #define SLOW_BLINK_ON_PHASE          (g_blinkTmr10ms & (1<<7))
#endif

struct _lv_disp_drv_t;
typedef _lv_disp_drv_t lv_disp_drv_t;

// Call backs
void lcdSetFlushCb(void (*cb)(lv_disp_drv_t *, uint16_t*, const rect_t&));
#if defined(SIMU)
void lcdSetWaitCb(void (*cb)(lv_disp_drv_t *));
#endif

// Init LVGL and its display driver
void lcdInitDisplayDriver();

#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)
// Called between UI iterations, after editors and gestures have been resolved.
bool lcdSetOrientation(bool landscape);
void lcdPresentedSize(unsigned* width, unsigned* height);

// Painting on the panel without LVGL: no allocation, no locks, nothing that
// an interface fault can have broken. lcdSpareCanvas hands back the scanout
// buffer the panel is not reading, which the caller may paint over as many
// calls as it likes, and lcdPresentSpare shows it at the next vertical blank.
// The panel always scans portrait; `landscape` says whether the picture should
// read as landscape, and the painter does that mapping itself.
uint16_t* lcdSpareCanvas(unsigned* width, unsigned* height, bool* landscape);
void lcdPresentSpare();
#endif

void lcdClear();

void lcdRefresh();

extern "C" void lcdFlushed();
