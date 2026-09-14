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

#include "form.h"

class ToggleSwitch : public FormField
{
 public:
  ToggleSwitch(Window* parent, const rect_t& rect,
           std::function<uint8_t()> getValue,
           std::function<void(uint8_t)> setValue);

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "ToggleSwitch"; }
#endif

  void onClicked() override;

  uint8_t getValue() const { return _getValue(); }
  void setValue(uint8_t value) { _setValue(value); }

  void setSetValueHandler(std::function<void(uint8_t)> handler)
  {
    _setValue = std::move(handler);
  }

  void setGetValueHandler(std::function<uint8_t()> handler)
  {
    _getValue = std::move(handler);
  }

  void update() const;

  static LAYOUT_VAL_SCALED(TOGGLE_W, 52)

  /* Painted height, deliberately NOT UI_ELEMENT_HEIGHT.
   *
   * In LVGL the knob is a square of the switch's own height and the travel is
   * `width - height` (lv_switch.c, draw_main). EdgeTX's 52x32 therefore slides
   * the knob 20 px, which is what makes on and off tell apart at a glance. The
   * NB4 raised UI_ELEMENT_HEIGHT to 44 for a bigger finger target, and that
   * silently cut the travel to 8 px inside the same 52 px track: the knob
   * covered the track in both states.
   *
   * So the paint stays at EdgeTX's size and the finger target is restored by
   * extending the click area past the paint (see switch_constructor), which
   * costs nothing in layout because the object's own box does not grow. */
#if defined(RADIO_NB4_FAMILY)
  static constexpr coord_t TOGGLE_H = 32;
#else
  static constexpr coord_t TOGGLE_H = EdgeTxStyles::UI_ELEMENT_HEIGHT;
#endif

  static_assert(TOGGLE_W - TOGGLE_H >= 20,
                "the knob must visibly travel: see the note above");

 protected:
  std::function<uint8_t()> _getValue;
  std::function<void(uint8_t)> _setValue;

  void checkEvents() override;

  static void toggleswitch_event_handler(lv_event_t* e);
};
