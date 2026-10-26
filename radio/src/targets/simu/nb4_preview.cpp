// SPDX-License-Identifier: GPL-2.0-only

// Headless front end for the PC theme editor: runs the real firmware on a
// folder laid out like the transmitter's storage and hands out the frames it
// draws, so the editor preview is what the transmitter shows.

#include "simpgmspace.h"
#include "edgetx.h"
#include "simulcd.h"
#include "hal/adc_driver.h"

#include "mainview/layout.h"
#include "mainview/view_main.h"

#include <lvgl/lvgl.h>
#include <string.h>

#if defined(_WIN32)
#define NB4P_EXPORT extern "C" __declspec(dllexport)
#else
#define NB4P_EXPORT extern "C" __attribute__((visibility("default")))
#endif

extern "C" void lcdFlushed();
extern struct TouchState simTouchState;
extern bool simTouchOccured;

// Raw ADC values (0..4095) the simulated ADC driver reads; centred inputs.
static uint16_t analogs[MAX_ANALOG_INPUTS];

uint16_t simu_get_analog(uint8_t idx)
{
  return idx < DIM(analogs) ? analogs[idx] : 2048;
}

NB4P_EXPORT int nb4p_version() { return 1; }

NB4P_EXPORT int nb4p_start(const char* storagePath)
{
  if (simuIsRunning()) return 1;
  for (auto& value : analogs) value = 2048;
  simuInit();
  // The settings live on the same storage as the themes and models.
  simuStart(false, storagePath, storagePath);
  return simuIsRunning() ? 1 : 0;
}

NB4P_EXPORT void nb4p_stop() { simuStop(); }

NB4P_EXPORT int nb4p_running() { return simuIsRunning() ? 1 : 0; }

// Copies the last complete frame (RGB565, row-major, as the user sees it) when
// one was drawn since the previous call. Returns 1 with the size in w/h, else
// 0. The display buffer stays in the panel's portrait scan order; landscape is
// LVGL's software rotation by 90 degrees, undone here.
NB4P_EXPORT int nb4p_frame(uint16_t* out, int capacity, int* w, int* h)
{
  if (!simuIsRunning() || !simuLcdRefresh || !simuLcdBuf) return 0;
  if (LCD_W * LCD_H > capacity) return 0;
  simuLcdRefresh = false;
  const bool rotated = lv_disp_get_rotation(nullptr) == LV_DISP_ROT_90;
  if (!rotated) {
    memcpy(out, simuLcdBuf, sizeof(uint16_t) * LCD_W * LCD_H);
    *w = LCD_W;
    *h = LCD_H;
  } else {
    for (int y = 0; y < LCD_W; y++)
      for (int x = 0; x < LCD_H; x++)
        out[y * LCD_H + x] = simuLcdBuf[(LCD_H - 1 - x) * LCD_W + y];
    *w = LCD_H;
    *h = LCD_W;
  }
  // Like the panel acknowledging a present: the firmware draws the next
  // frame only after this (OpenTxSimulator::lcdFlushed() in Companion).
  lcdFlushed();
  return 1;
}

NB4P_EXPORT int nb4p_analog_count() { return (int)DIM(analogs); }

// Raw ADC value of an input, as the hardware reports it (0..4095).
NB4P_EXPORT void nb4p_set_analog(int index, int value)
{
  if (index >= 0 && index < (int)DIM(analogs))
    analogs[index] = (uint16_t)limit(0, value, 4095);
}

NB4P_EXPORT void nb4p_touch(int down, int x, int y)
{
#if defined(HARDWARE_TOUCH)
  if (down) {
    simTouchState.event = TE_DOWN;
    simTouchState.startX = simTouchState.x = x;
    simTouchState.startY = simTouchState.y = y;
  } else if (simTouchState.event == TE_DOWN) {
    simTouchState.event = TE_UP;
    simTouchState.x = simTouchState.startX;
    simTouchState.y = simTouchState.startY;
    simTouchState.tapCount = 1;
  }
  simTouchOccured = true;
#endif
}

// Zones of the main view on screen, as x, y, w, h per zone: the layout grid
// exactly as the firmware computes it. Returns the number of zones.
NB4P_EXPORT int nb4p_zones(int16_t* out, int capacity)
{
  // ViewMain::instance() would create the view from this thread when the
  // interface has not built it yet; the first screen marks that it has.
  if (!simuIsRunning() || !customScreens[0]) return 0;
  const unsigned view = ViewMain::instance()->getCurrentMainView();
  if (view >= MAX_CUSTOM_SCREENS || !customScreens[view]) return 0;
  auto screen = customScreens[view];
  const int count = (int)screen->getZonesCount();
  int n = 0;
  for (int i = 0; i < count && n < capacity / 4; i++, n++) {
    const rect_t r = screen->getZone(i);
    out[n * 4] = r.x;
    out[n * 4 + 1] = r.y;
    out[n * 4 + 2] = r.w;
    out[n * 4 + 3] = r.h;
  }
  return n;
}

NB4P_EXPORT int nb4p_current_view()
{
  if (!simuIsRunning() || !customScreens[0]) return -1;
  return (int)ViewMain::instance()->getCurrentMainView();
}

NB4P_EXPORT int nb4p_view_count()
{
  if (!simuIsRunning() || !customScreens[0]) return 0;
  return (int)ViewMain::instance()->getMainViewsCount();
}

// Shows a main view the way a special function does: the request is carried
// out by the interface task (main.cpp, REQUEST_MAIN_VIEW).
NB4P_EXPORT void nb4p_show_view(int view)
{
  if (view < 0 || view >= MAX_CUSTOM_SCREENS) return;
  g_model.view = (uint8_t)view;
  mainRequestFlags |= (1u << REQUEST_MAIN_VIEW);
}

// Key press or release; -1 selects the confirm key (KEY_ENTER).
NB4P_EXPORT void nb4p_key(int key, int down)
{
  if (key < 0) key = KEY_ENTER;
  if (key < MAX_KEYS) simuSetKey((uint8_t)key, down != 0);
}

NB4P_EXPORT const char* nb4p_error() { return main_thread_error; }
