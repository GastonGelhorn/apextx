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

#include "layer.h"

#include <algorithm>
#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)
#include "nb4_ui_perf.h"
#endif

std::list<Layer> Layer::stack;
#if defined(RADIO_NB4_FAMILY)
std::list<Window*> Layer::coveredWindows;

void Layer::updateCoverage()
{
  // Everything below the topmost covering layer is hidden; everything this
  // hid earlier and is no longer below one is shown again. Windows hidden
  // for other reasons are left alone.
  std::list<Window*> below;
  bool covered = false;
  for (auto layer = stack.crbegin(); layer != stack.crend(); ++layer) {
    if (covered) below.push_back(layer->window);
    else if (layer->covers) covered = true;
  }
  for (auto it = coveredWindows.begin(); it != coveredWindows.end();) {
    if (std::find(below.begin(), below.end(), *it) == below.end()) {
      (*it)->show();
      it = coveredWindows.erase(it);
    } else {
      ++it;
    }
  }
  for (auto w : below) {
    // Home hides its screens and stops refreshing widgets in show(false)
    // without hiding its own object, so it still reads as visible here.
    if (!w->isVisible() ||
        std::find(coveredWindows.begin(), coveredWindows.end(), w) != coveredWindows.end())
      continue;
    w->hide();
    coveredWindows.push_back(w);
  }
}

void Layer::cover(Window* w)
{
  for (auto& layer : stack)
    if (layer.window == w) layer.covers = true;
  updateCoverage();
}
#endif

Layer::Layer(Window* w, lv_group_t* g, lv_group_t* pg) : window(w), group(g), prevGroup(pg) {}
Layer::~Layer() { lv_group_del(group); }

void _assign_lv_group(lv_group_t* g)
{
  lv_group_set_default(g);

  // associate it with all input devices
  lv_indev_t* indev = lv_indev_get_next(NULL);
  while (indev) {
    lv_indev_set_group(indev, g);
    indev = lv_indev_get_next(indev);
  }
}

void Layer::push(Window* w)
{
#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)
  nb4UiPerfViewChanged();
#endif
  // save prev group
  auto pg = lv_group_get_default();

  // create a new group
  auto g = lv_group_create();
  _assign_lv_group(g);
  
  // and store
  stack.emplace_back(w, g, pg);
#if defined(RADIO_NB4_FAMILY)
  updateCoverage();
#endif
}

void Layer::pop(Window* w)
{
#if defined(RADIO_NB4_FAMILY) && !defined(BOOT)
  nb4UiPerfViewChanged();
#endif
  for (auto layer = stack.begin(); layer != stack.end(); ++layer) {
    if (layer->window != w) continue;
    auto next = std::next(layer);
    // A dialog can outlive the page underneath it. Its return focus must skip
    // that page, and the active group's lifetime must not be shortened.
    if (next != stack.end())
      next->prevGroup = layer->prevGroup;
    else
      _assign_lv_group(layer->prevGroup);
    stack.erase(layer);
#if defined(RADIO_NB4_FAMILY)
    // A window leaving the stack is being closed or parked, not uncovered.
    coveredWindows.remove(w);
    updateCoverage();
#endif
    return;
  }
}

Window* Layer::back()
{
  if (stack.empty()) return nullptr;
  return stack.back().window;
}

Window* Layer::getFirstOpaque()
{
  for (auto layer = stack.crbegin(); layer != stack.crend(); layer++) {
    auto w = layer->window;
    if (w && w->isVisible() && w->hasWindowFlag(OPAQUE)) return w;
  }

  return nullptr;
}

Window* Layer::walk(std::function<bool(Window* w)> check)
{
  for (auto layer = stack.crbegin(); layer != stack.crend(); layer++) {
    if (layer->window && check(layer->window))
      return layer->window;
  }

  return nullptr;
}

Window* Layer::getPageGroup()
{
  Window* w = Layer::walk([=](Window *w) mutable -> bool {
    return w->isPageGroup();
  });
  return w;
}
