/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   libopenui - https://github.com/opentx/libopenui
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

#include "window.h"

class Layer
{
  static std::list<Layer> stack;

  Window*     window;
  lv_group_t* group;
  lv_group_t* prevGroup;
#if defined(RADIO_NB4_FAMILY)
  bool        covers = false;

  static std::list<Window*> coveredWindows;
  static void updateCoverage();
#endif

 public:
  explicit Layer(Window* w, lv_group_t* g, lv_group_t* pg);
  ~Layer();

  static void push(Window* window);
  static void pop(Window* window);

  static Window* back();
  static Window* getFirstOpaque();
  static Window* getPageGroup();
  static Window* walk(std::function<bool(Window* w)> check);

#if defined(RADIO_NB4_FAMILY)
  // Marks this layer as hiding everything underneath it while it is open.
  // Nothing below is drawn then, so scrolling it redraws only this layer.
  static void cover(Window* window);
#endif
};
