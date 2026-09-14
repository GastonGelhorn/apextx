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

#include "button.h"
#include "dialog.h"
#include "form.h"
#include "toggleswitch.h"
#include <vector>

class ToggleSwitch;

class HWSticks : public Window
{
 public:
  HWSticks(Window* parent);

  // Absolute layout for Pots popup - due to performance issues with lv_textarea
  // in a flex layout
  static LAYOUT_VAL_SCALED(S_LBL_W, 60)
  static constexpr coord_t S_NM_X = S_LBL_W + PAD_SMALL;
  static LAYOUT_VAL_SCALED(S_NM_W, 64)
  static constexpr coord_t S_INV_X = S_NM_X + S_NM_W + PAD_LARGE * 3;
  static constexpr coord_t S_INV_W = ToggleSwitch::TOGGLE_W;
  static constexpr coord_t S_ROW_H = EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_OUTLINE;
  #define S_Y(i) (i * S_ROW_H + PAD_TINY)
};

class HWPots : public Window
{
 public:
  HWPots(Window* parent);

  // Absolute layout for Pots popup - due to performance issues with lv_textarea
  // in a flex layout
#if defined(LCD_RUNTIME_LAYOUT)

  static constexpr LayoutVal P_LBL_W{(coord_t)LAYOUT_SCALE(60),
                                     (coord_t)LAYOUT_SCALE(120)};
  static constexpr LayoutVal P_NM_X = lvAdd(P_LBL_W, PAD_SMALL);
  static LAYOUT_VAL_SCALED(P_NM_W, 64)
  static constexpr LayoutVal P_TYP_X{(coord_t)(P_NM_X.l + P_NM_W + PAD_SMALL),
                                     (coord_t)LAYOUT_SCALE(32)};
  static constexpr LayoutVal P_TYP_W{(coord_t)LAYOUT_SCALE(160), P_LBL_W.p};
  static constexpr LayoutVal P_INV_X{
      (coord_t)(P_TYP_X.l + P_TYP_W.l + PAD_SMALL),
      (coord_t)(P_NM_X.p + P_NM_W + PAD_SMALL)};
  static constexpr coord_t P_INV_W = ToggleSwitch::TOGGLE_W;
  static constexpr LayoutVal P_ROW_H{
      (coord_t)(EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_OUTLINE),
      (coord_t)LAYOUT_SCALE(72)};
  static LAYOUT_ORIENTATION_SCALED(P_OFST_Y, 0, 36)
  static constexpr LayoutVal POTS_WINDOW_WIDTH{
      (coord_t)(P_LBL_W.l + P_NM_W + P_TYP_W.l + P_INV_W + PAD_TINY * 2 +
                PAD_SMALL * 5 + PAD_SCROLL),
      (coord_t)(P_LBL_W.p + P_NM_W + P_INV_W + PAD_TINY * 2 + PAD_SMALL * 4 +
                PAD_SCROLL)};
#else
  static LAYOUT_ORIENTATION_SCALED(P_LBL_W, 60, 120)
  static constexpr coord_t P_NM_X = P_LBL_W + PAD_SMALL;
  static LAYOUT_VAL_SCALED(P_NM_W, 64)
  static LAYOUT_ORIENTATION(P_TYP_X, P_NM_X + P_NM_W + PAD_SMALL, LAYOUT_SCALE(32))
  static LAYOUT_ORIENTATION(P_TYP_W, LAYOUT_SCALE(160), P_LBL_W)
  static LAYOUT_ORIENTATION(P_INV_X, P_TYP_X + P_TYP_W + PAD_SMALL, P_NM_X + P_NM_W + PAD_SMALL)
  static constexpr coord_t P_INV_W = ToggleSwitch::TOGGLE_W;
  static LAYOUT_ORIENTATION(P_ROW_H, EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_OUTLINE, LAYOUT_SCALE(72))
  static LAYOUT_ORIENTATION_SCALED(P_OFST_Y, 0, 36)
  static LAYOUT_ORIENTATION(POTS_WINDOW_WIDTH,
                              P_LBL_W + P_NM_W + P_TYP_W + P_INV_W + PAD_TINY * 2 + PAD_SMALL * 5 + PAD_SCROLL,
                              P_LBL_W + P_NM_W + P_INV_W + PAD_TINY * 2 + PAD_SMALL * 4 + PAD_SCROLL)
#endif
  #define P_Y(i) (i * P_ROW_H + PAD_TINY)

 protected:
  bool potsChanged;
  std::vector<ToggleSwitch*> invertToggles;
};

class HWSwitches : public Window
{
 public:
  HWSwitches(Window* parent);

  static LAYOUT_SIZE_SCALED(SW_CTRL_W, 86, 72)
#if defined(LCD_RUNTIME_LAYOUT)
  static constexpr LayoutVal SW_WINDOW_WIDTH =
      lvAdd(lvMul(SW_CTRL_W, 4), PAD_SMALL * 5 + PAD_TINY * 2 + PAD_SCROLL);
#else
  static constexpr coord_t SW_WINDOW_WIDTH = SW_CTRL_W * 4 + PAD_SMALL * 5 + PAD_TINY * 2 + PAD_SCROLL;
#endif
};

template <class T>
struct HWInputDialog : public BaseDialog {
  HWInputDialog(const char* title, coord_t w = DIALOG_DEFAULT_WIDTH);
};
