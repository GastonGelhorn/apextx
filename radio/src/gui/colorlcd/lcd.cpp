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

#include "lcd.h"

#include <lvgl/lvgl.h>

#include "bitmapbuffer.h"
#include "board.h"
#include "etx_lv_theme.h"
#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)
#include <atomic>
#include <algorithm>
#if !defined(SIMU)
#include "os/sleep.h"
#include "os/task.h"
#include "os/time.h"
#endif
#endif
#if !LV_USE_GPU_STM32_DMA2D && !defined(SIMU)
#include "dma2d.h"
#endif

#if LV_MEM_CUSTOM == 0
char LVGL_MEM_BUFFER[LV_MEM_SIZE] __SDRAM __ALIGNED(16);

char* get_lvgl_mem(int nbytes)
{
  UNUSED(nbytes);
  return LVGL_MEM_BUFFER;
}
#endif

#if defined(LCD_DUAL_ORIENTATION) && !defined(BOOT)
// Runtime dimensions shared by native layouts; persisted data stays invariant.
coord_t lcdWidth = LCD_W;
coord_t lcdHeight = LCD_H;
#endif

pixel_t LCD_FIRST_FRAME_BUFFER[DISPLAY_BUFFER_SIZE] __SDRAM __ALIGNED(64);
pixel_t LCD_SECOND_FRAME_BUFFER[DISPLAY_BUFFER_SIZE] __SDRAM __ALIGNED(64);

static lv_disp_draw_buf_t disp_buf;
static lv_disp_drv_t disp_drv;

#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)
// One LVGL drawing buffer plus two physical scanout buffers. The panel always
// scans 320x480; LVGL rotates dirty rectangles and input coordinates together.
static pixel_t nb4ScanBuffer[DISPLAY_BUFFER_SIZE] __SDRAM __ALIGNED(64);
static pixel_t* nb4Front = LCD_SECOND_FRAME_BUFFER;
static pixel_t* nb4Back = nb4ScanBuffer;
static int dirtyTop = LCD_PHYS_H, dirtyBottom = -1;
static int syncTop = LCD_PHYS_H, syncBottom = -1;
static std::atomic<uint32_t> presentedSize{(LCD_PHYS_W << 16) | LCD_PHYS_H};

#if !defined(SIMU)
// When the present was handed to the panel, and how long the interface will
// wait for the acknowledgement before giving up on it. The panel scans at
// about 60 Hz, so this is several frames: long enough that a late interrupt
// cannot still be in flight, short enough not to be felt.
static volatile uint32_t presentArmedAt = 0;
constexpr uint32_t PresentTimeoutMs = 100;
#endif

void lcdPresentedSize(unsigned* width, unsigned* height)
{
  auto size = presentedSize.load(std::memory_order_acquire);
  *width = size >> 16; *height = size & 0xffff;
}

bool lcdSetOrientation(bool landscape)
{
  auto display = lv_disp_get_default();
  if (!display) return false;
  const auto rotation = landscape ? LV_DISP_ROT_90 : LV_DISP_ROT_NONE;
  if (lv_disp_get_rotation(display) == rotation) {
    lcdWidth = landscape ? LCD_PHYS_H : LCD_PHYS_W;
    lcdHeight = landscape ? LCD_PHYS_W : LCD_PHYS_H;
    return true;
  }
  if (lv_disp_get_draw_buf(display)->flushing) return false;
  for (auto input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input)) {
    lv_indev_reset(input, nullptr);
    lv_indev_wait_release(input);
  }
  lcdWidth = landscape ? LCD_PHYS_H : LCD_PHYS_W;
  lcdHeight = landscape ? LCD_PHYS_W : LCD_PHYS_H;
  lv_disp_set_rotation(display, rotation);
  return true;
}
#endif

// Call backs
static void (*lcd_flush_cb)(lv_disp_drv_t*, uint16_t* buffer,
                            const rect_t& area) = nullptr;

void lcdSetFlushCb(void (*cb)(lv_disp_drv_t*, uint16_t*, const rect_t&))
{
  lcd_flush_cb = cb;
}

#if defined(SIMU)
// Only used in simulator to prevent lock up when closing simulator
// TODO: find a better way to handle this
static void (*lcd_wait_cb)(lv_disp_drv_t*) = nullptr;
void lcdSetWaitCb(void (*cb)(lv_disp_drv_t*)) { lcd_wait_cb = cb; }
#endif

extern "C" void lcdFlushed()
{
#if defined(RADIO_NB4_FAMILY) && !defined(BOOT) && !defined(SIMU)
  presentArmedAt = 0;
#endif
  lv_disp_flush_ready(&disp_drv);
}

void lcdRefresh() {}

static void flushLcd(lv_disp_drv_t* disp_drv, const lv_area_t* area,
                     lv_color_t* color_p)
{
#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)
  // LVGL's software rotation gives physical coordinates here. Assemble all
  // chunks before presenting at vertical blank; never scan a partial buffer.
  if (area->x1 < 0 || area->y1 < 0 || area->x2 >= LCD_PHYS_W || area->y2 >= LCD_PHYS_H) {
    lv_disp_flush_ready(disp_drv);
    return;
  }
  const unsigned width = area->x2 - area->x1 + 1;
  if (syncBottom >= syncTop) {
    // The preceding flush was acknowledged at vertical blank. Its old front
    // buffer is now free; synchronise it before composing the next frame.
    memcpy(nb4Back + syncTop * LCD_PHYS_W, nb4Front + syncTop * LCD_PHYS_W,
      (syncBottom - syncTop + 1) * LCD_PHYS_W * sizeof(pixel_t));
    syncTop = LCD_PHYS_H; syncBottom = -1;
  }
  for (int y = area->y1; y <= area->y2; ++y)
    memcpy(nb4Back + y * LCD_PHYS_W + area->x1,
      color_p + (y - area->y1) * width, width * sizeof(pixel_t));
  dirtyTop = std::min(dirtyTop, int(area->y1));
  dirtyBottom = std::max(dirtyBottom, int(area->y2));
  if (!lv_disp_flush_is_last(disp_drv)) {
    lv_disp_flush_ready(disp_drv);
    return;
  }
  auto oldFront = nb4Front; nb4Front = nb4Back; nb4Back = oldFront;
  syncTop = dirtyTop; syncBottom = dirtyBottom;
  dirtyTop = LCD_PHYS_H; dirtyBottom = -1;
  presentedSize.store((uint32_t(lcdWidth) << 16) | lcdHeight, std::memory_order_release);
#if !defined(SIMU)
  presentArmedAt = time_get_ms() | 1;  // never zero, which means "not armed"
#endif
  if (lcd_flush_cb) lcd_flush_cb(disp_drv, nb4Front, {0, 0, LCD_PHYS_W, LCD_PHYS_H});
  else lv_disp_flush_ready(disp_drv);
  return;
#endif
  // we're only interested in the last flush in direct mode
  if (disp_drv->direct_mode && !lv_disp_flush_is_last(disp_drv)) {
    lv_disp_flush_ready(disp_drv);
    return;
  }

#if defined(DEBUG_WINDOWS)
  if (area->x1 != 0 || area->x2 != LCD_W - 1 || area->y1 != 0 ||
      area->y2 != LCD_H - 1) {
    TRACE("partial refresh @ 0x%p {%d,%d,%d,%d}", color_p, area->x1, area->y1,
          area->x2, area->y2);
  } else {
    TRACE("full refresh @ 0x%p", color_p);
  }
#endif

  if (lcd_flush_cb) {
    rect_t copy_area = {area->x1, area->y1, area->x2 - area->x1 + 1,
                        area->y2 - area->y1 + 1};

    lcd_flush_cb(disp_drv, (uint16_t*)color_p, copy_area);
  } else {
    lcdFlushed();
  }
}

#if defined(RADIO_NB4_FAMILY) && !defined(BOOT) && !defined(SIMU)
// LVGL waits here for the panel to acknowledge a present. With the display
// rotated it waits once per frame, inside its rotation loop, and the
// acknowledgement is up to a whole frame period away; without a callback it
// spins on the flag and burns the interface's time slice for nothing. Hand the
// time back instead.
//
// The wait also gets a deadline. Only the line interrupt clears the flag, so
// if it never arrives the buffer is never released and the interface spins on
// it for the rest of the session while the mixer carries on none the wiser: a
// dead screen on a radio that is still driving the car. Releasing the buffer
// early can at worst tear one frame, which the next one repairs. A wedged
// interface repairs nothing.
//
// Before the scheduler starts there is nothing to hand the time to and the
// millisecond tick does not advance, so the boot path keeps the plain spin.
static void waitForPresent(lv_disp_drv_t* drv)
{
  if (!scheduler_is_running()) return;
  sleep_ms(1);
  const uint32_t armed = presentArmedAt;
  if (armed && time_get_ms() - armed > PresentTimeoutMs) {
    presentArmedAt = 0;
    lv_disp_flush_ready(drv);
  }
}
#endif

static void clear_frame_buffers()
{
#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)
  memset(nb4ScanBuffer, 0, sizeof(nb4ScanBuffer));
#endif
  memset(LCD_FIRST_FRAME_BUFFER, 0, sizeof(LCD_FIRST_FRAME_BUFFER));
  memset(LCD_SECOND_FRAME_BUFFER, 0, sizeof(LCD_SECOND_FRAME_BUFFER));
}

static void init_lvgl_disp_drv()
{
  int direct_mode = 1;
#if defined(LCD_VERTICAL_INVERT)
#if defined(RADIO_F16)
  direct_mode = (hardwareOptions.pcbrev > 0) ? 1 : 0;
#else
  direct_mode = 0;
#endif
#endif

#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)
  direct_mode = 0;
#endif
  lv_disp_draw_buf_init(&disp_buf,
                        LCD_FIRST_FRAME_BUFFER,
                        direct_mode ? LCD_SECOND_FRAME_BUFFER : nullptr,
                        LCD_W * LCD_H);
  lv_disp_drv_init(&disp_drv); /*Basic initialization*/

  disp_drv.draw_buf = &disp_buf; /*Set an initialized buffer*/
  disp_drv.flush_cb = flushLcd;  /*Set a flush callback to draw to the display*/
#if defined(SIMU)
  disp_drv.wait_cb = lcd_wait_cb; /*Set a wait callback*/
#elif defined(RADIO_NB4_FAMILY) && !defined(BOOT)
  disp_drv.wait_cb = waitForPresent;
#endif

  disp_drv.hor_res = LCD_W; /*Set the horizontal resolution in pixels*/
  disp_drv.ver_res = LCD_H; /*Set the vertical resolution in pixels*/
  disp_drv.direct_mode = direct_mode;
#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)
  disp_drv.hor_res = LCD_PHYS_W;
  disp_drv.ver_res = LCD_PHYS_H;
  disp_drv.sw_rotate = 1;
#endif
}

void lcdInitDisplayDriver()
{
  static bool lcdDriverStarted = false;
  // we already have a display: exit
  if (lcdDriverStarted) return;
  lcdDriverStarted = true;

#if !LV_USE_GPU_STM32_DMA2D && !defined(SIMU)
  DMAInit();
#endif

  // Full LVGL init in firmware mode
  lv_init();
  // Initialise styles
  useMainStyle();

  // Clear buffers first
  clear_frame_buffers();
#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)
  lcdSetInitalFrameBuffer(LCD_SECOND_FRAME_BUFFER);
#else
  lcdSetInitalFrameBuffer(LCD_FIRST_FRAME_BUFFER);
#endif

  // Init hardware LCD driver
  lcdInit();
  backlightInit();

  init_lvgl_disp_drv();

  // Register the driver and save the created display object
  lv_disp_drv_register(&disp_drv);

  // remove all styles on default screen (makes it transparent as well)
  lv_obj_remove_style_all(lv_scr_act());
}
