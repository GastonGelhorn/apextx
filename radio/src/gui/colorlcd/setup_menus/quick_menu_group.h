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

#include "form.h"
#include "bitmaps.h"
#include "button.h"
#include "static.h"
#include "etx_lv_theme.h"
#include <vector>

lv_obj_t* etx_quick_button_create(lv_obj_t* parent);

class QuickMenuGroup : public Window
{
 public:
  QuickMenuGroup(Window* parent);

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "QuickMenuGroup"; }
#endif

  // Add a new button to the carousel
  ButtonBase* addButton(EdgeTxIcon icon, const char* title,
                 std::function<void(void)> pressHandler,
                 std::function<bool(void)> visibleHandler = nullptr,
                 std::function<void(void)> focusHandler = nullptr);

  void setGroup();
  void setFocus();
  void clearFocus();
  void setDisabled(bool all);
  void setEnabled();
  void setCurrent(ButtonBase* b);
  void setCurrent(int b) { setCurrent(btns[b]); }
  void doLayout(int cols);
  void nextEntry();
  void prevEntry();
  ButtonBase* getFocusedButton();

#if defined(LCD_RUNTIME_LAYOUT)

  static LAYOUT_VAL_SCALED(QM_BUTTON_WIDTH, 72)
#elif PORTRAIT
  static LAYOUT_VAL_SCALED(QM_BUTTON_WIDTH, 72)
#else
  static LAYOUT_SIZE_SCALED(QM_BUTTON_WIDTH, 72, 60)
#endif
  static LAYOUT_VAL_SCALED(QM_BUTTON_HEIGHT, 70)

  static LAYOUT_VAL_SCALED(QM_ICON_SIZE, 30)
  static LAYOUT_ORIENTATION(QM_ICON_PAD, PAD_MEDIUM, PAD_SMALL)

 protected:
  std::vector<ButtonBase*> btns;
  ButtonBase* curBtn = nullptr;
  lv_group_t* group = nullptr;

  void deleteLater(bool detach = true, bool trash = true) override;
};

class QuickMenuButton : public ButtonBase
{
 public:
  QuickMenuButton(Window* parent, EdgeTxIcon icon, const char* title,
                  std::function<uint8_t(void)> pressHandler,
                  std::function<bool(void)> visibleHandler) :
      ButtonBase(parent, {}, pressHandler, etx_quick_button_create),
      iconId(icon),
      visibleHandler(std::move(visibleHandler))
  {
    iconPtr = new StaticIcon(this, (QuickMenuGroup::QM_BUTTON_WIDTH - QuickMenuGroup::QM_ICON_SIZE) / 2, PAD_SMALL, icon, COLOR_THEME_QM_FG_INDEX);
#if VERSION_MAJOR > 2
    etx_obj_add_style(iconPtr->getLvObj(), styles->qmdisabled, LV_PART_MAIN | LV_STATE_DISABLED);
#endif
    etx_img_color(iconPtr->getLvObj(), COLOR_THEME_QM_BG_INDEX, LV_STATE_USER_1);

    textPtr = new StaticText(this, {0, QuickMenuGroup::QM_ICON_SIZE + PAD_TINY * 2, QuickMenuGroup::QM_BUTTON_WIDTH - 1, 0},
                   title, COLOR_THEME_QM_FG_INDEX, CENTERED | FONT(XS));
#if VERSION_MAJOR > 2
    etx_obj_add_style(textPtr->getLvObj(), styles->qmdisabled, LV_PART_MAIN | LV_STATE_DISABLED);
#endif
    etx_txt_color(textPtr->getLvObj(), COLOR_THEME_QM_BG_INDEX, LV_STATE_USER_1);

    lv_obj_add_event_cb(lvobj, QuickMenuButton::focused_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(lvobj, QuickMenuButton::defocused_cb, LV_EVENT_DEFOCUSED, nullptr);
#if defined(RADIO_NB4_FAMILY)
    // The group may focus the button before its label/icon have been created.
    if (lv_obj_has_state(lvobj, LV_STATE_FOCUSED)) setFocused();
#endif
  }

#if defined(RADIO_NB4_FAMILY)
  void setTileWidth(coord_t width) {
    setWidth(width);
    textPtr->setWidth(width - 1);
    iconPtr->setPos((width - QuickMenuGroup::QM_ICON_SIZE) / 2, PAD_SMALL);
  }

  // Compact app-style tile used by the ApexTX navigation dialogs.  The
  // coloured square is the visual control; the full cell remains the touch
  // target and carries its label below the icon.
  void useAppTile(coord_t size, lv_color_t accent, lv_color_t detail) {
    constexpr coord_t badgeSize = 42;
    constexpr coord_t labelTop = badgeSize + 1;

    setSize(size, size);
    padAll(PAD_ZERO);
    lv_obj_set_style_bg_opa(lvobj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lvobj, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(lvobj, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(lvobj, 0, LV_PART_MAIN);

    badgePtr = new Window(this, {(size - badgeSize) / 2, 0,
                                 badgeSize, badgeSize});
    badgePtr->setWindowFlag(OPAQUE);
    badgePtr->padAll(PAD_ZERO);
    lv_obj_t* badge = badgePtr->getLvObj();
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                                LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_radius(badge, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge, lv_color_lighten(accent, LV_OPA_20),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(badge,
                                   lv_color_darken(detail, LV_OPA_20),
                                   LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(badge, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(badge, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(badge, 2,
                                  LV_PART_MAIN | LV_STATE_USER_1);
    lv_obj_set_style_border_color(badge, lv_color_white(),
                                  LV_PART_MAIN | LV_STATE_USER_1);
    lv_obj_move_to_index(badge, 0);

    shadowPtr = new StaticIcon(this, 0, 0, iconId,
                               COLOR_THEME_QM_BG_INDEX);
    shadowPtr->center(size + 2, badgeSize + 2);
    lv_obj_set_style_img_recolor(shadowPtr->getLvObj(), lv_color_black(),
                                 LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(shadowPtr->getLvObj(), LV_OPA_COVER,
                                     LV_PART_MAIN);
    lv_obj_set_style_opa(shadowPtr->getLvObj(), LV_OPA_30, LV_PART_MAIN);
    lv_obj_move_to_index(shadowPtr->getLvObj(), 1);

    iconPtr->center(size, badgeSize);
    lv_obj_set_style_img_recolor(iconPtr->getLvObj(), lv_color_white(),
                                 LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(iconPtr->getLvObj(), LV_OPA_COVER,
                                     LV_PART_MAIN);
    lv_obj_set_style_img_recolor(iconPtr->getLvObj(), lv_color_white(),
                                 LV_PART_MAIN | LV_STATE_USER_1);

    textPtr->setPos(0, labelTop);
    textPtr->setSize(size, size - labelTop);
    etx_font(textPtr->getLvObj(), FONT_STD_INDEX);
    lv_label_set_long_mode(textPtr->getLvObj(), LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_letter_space(textPtr->getLvObj(), -1, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(textPtr->getLvObj(), 0, LV_PART_MAIN);
    etx_txt_color(textPtr->getLvObj(), COLOR_THEME_QM_FG_INDEX,
                  LV_PART_MAIN | LV_STATE_USER_1);
  }
#endif

#if defined(DEBUG_WINDOWS)
  std::string getName() const override { return "QuickMenuButton"; }
#endif

  static void focused_cb(lv_event_t *e)
  {
    QuickMenuButton *b = (QuickMenuButton *)lv_obj_get_user_data(lv_event_get_target(e));
    if (b) b->setFocused();
  }

  static void defocused_cb(lv_event_t *e)
  {
    QuickMenuButton *b = (QuickMenuButton *)lv_obj_get_user_data(lv_event_get_target(e));
    if (b) b->setDeFocused();
  }

  void setDisabled()
  {
    iconPtr->enable(false);
    textPtr->enable(false);
  }

  void setEnabled()
  {
    iconPtr->enable(true);
    textPtr->enable(true);
  }

  void setFocused()
  {
    if (textPtr && textPtr->getLvObj())
      lv_obj_add_state(textPtr->getLvObj(), LV_STATE_USER_1);
    if (iconPtr && iconPtr->getLvObj())
      lv_obj_add_state(iconPtr->getLvObj(), LV_STATE_USER_1);
#if defined(RADIO_NB4_FAMILY)
    if (badgePtr && badgePtr->getLvObj())
      lv_obj_add_state(badgePtr->getLvObj(), LV_STATE_USER_1);
#endif
  }

  void setDeFocused()
  {
    if (textPtr && textPtr->getLvObj())
      lv_obj_clear_state(textPtr->getLvObj(), LV_STATE_USER_1);
    if (iconPtr && iconPtr->getLvObj())
      lv_obj_clear_state(iconPtr->getLvObj(), LV_STATE_USER_1);
#if defined(RADIO_NB4_FAMILY)
    if (badgePtr && badgePtr->getLvObj())
      lv_obj_clear_state(badgePtr->getLvObj(), LV_STATE_USER_1);
#endif
  }

  bool isVisible() {
    if (visibleHandler)
      return visibleHandler();
    return true;
  }

 protected:
  EdgeTxIcon iconId;
  StaticIcon* iconPtr = nullptr;
  StaticText* textPtr = nullptr;
#if defined(RADIO_NB4_FAMILY)
  Window* badgePtr = nullptr;
  StaticIcon* shadowPtr = nullptr;
#endif
  std::function<bool(void)> visibleHandler = nullptr;
};
