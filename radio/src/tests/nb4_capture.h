/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

// Frame capture for the NB4 render tests: hooks the LVGL flush callback, keeps
// the physical 320x480 panel and writes PNG crops of it. In landscape the
// logical frame is the panel rotated, exactly as the other NB4 tests read it.

#include "mainwindow.h"
#include "lcd.h"
#include "stb/stb_image_write.h"

#include <cstdlib>
#include <filesystem>
#include <set>
#include <vector>

namespace nb4capture {

inline std::vector<lv_color_t> frame;
inline void (*originalFlush)(lv_disp_drv_t*, const lv_area_t*, lv_color_t*) = nullptr;

inline void captureFrame(lv_disp_drv_t* d, const lv_area_t* area, lv_color_t* pixels)
{
  const int w = d->hor_res, h = d->ver_res;
  frame.resize((size_t)w * h);
  if (d->direct_mode) {
    memcpy(frame.data(), pixels, (size_t)w * h * sizeof(lv_color_t));
  } else {
    for (int y = area->y1; y <= area->y2; ++y)
      memcpy(frame.data() + (size_t)y * w + area->x1,
             pixels + (size_t)(y - area->y1) * (area->x2 - area->x1 + 1),
             (size_t)(area->x2 - area->x1 + 1) * sizeof(lv_color_t));
  }
  lv_disp_flush_ready(d);
}

inline void install()
{
  auto driver = lv_disp_get_default()->driver;
  originalFlush = driver->flush_cb;
  driver->flush_cb = captureFrame;
}

inline void restore() { lv_disp_get_default()->driver->flush_cb = originalFlush; }

inline void render(MainWindow* root)
{
  lv_obj_invalidate(root->getLvObj());
  lv_obj_update_layout(root->getLvObj());
  lv_tick_inc(50);
  lv_timer_handler();
  root->run();
}

// Where a render test writes its images: the named environment variable, or
// a folder of that name in the system temp directory.
inline std::filesystem::path outputDirectory(const char* variable, const char* fallback)
{
  const char* env = getenv(variable);
  std::filesystem::path p =
      env ? std::filesystem::path(env) : std::filesystem::temp_directory_path() / fallback;
  std::filesystem::create_directories(p);
  return p;
}

// Writes the crop and returns how many distinct colours it holds, so a test
// can tell a drawn image from an empty one.
inline unsigned savePng(const std::filesystem::path& file, rect_t r)
{
  const bool landscape = lv_disp_get_hor_res(nullptr) > lv_disp_get_ver_res(nullptr);
  std::vector<uint8_t> rgb((size_t)r.w * r.h * 3);
  std::set<uint16_t> distinct;
  for (int y = 0; y < r.h; ++y) {
    for (int x = 0; x < r.w; ++x) {
      const int lx = r.x + x, ly = r.y + y;
      const lv_color_t px = frame[landscape ? (size_t)(LCD_PHYS_H - 1 - lx) * LCD_PHYS_W + ly
                                            : (size_t)ly * LCD_PHYS_W + lx];
      distinct.insert(px.full);
      lv_color32_t c;
      c.full = lv_color_to32(px);
      uint8_t* p = &rgb[((size_t)y * r.w + x) * 3];
      p[0] = c.ch.red;
      p[1] = c.ch.green;
      p[2] = c.ch.blue;
    }
  }
  stbi_write_png(file.string().c_str(), r.w, r.h, 3, rgb.data(), r.w * 3);
  return (unsigned)distinct.size();
}

}  // namespace nb4capture
