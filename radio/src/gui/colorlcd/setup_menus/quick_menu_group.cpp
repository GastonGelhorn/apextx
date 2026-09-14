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

#include "quick_menu_group.h"

#include "bitmaps.h"
#include "button.h"
#include "static.h"
#include "quick_menu_def.h"

static void etx_quick_button_constructor(const lv_obj_class_t* class_p,
                                         lv_obj_t* obj)
{
  etx_obj_add_style(obj, styles->rounded, LV_PART_MAIN);
  etx_txt_color(obj, COLOR_THEME_QM_FG_INDEX, LV_PART_MAIN);
  etx_obj_add_style(obj, styles->pad_zero, LV_PART_MAIN);
  etx_solid_bg(obj, COLOR_THEME_QM_BG_INDEX, LV_PART_MAIN);

  etx_solid_bg(obj, COLOR_THEME_QM_FG_INDEX, LV_PART_MAIN | LV_STATE_FOCUSED);
}

static const lv_obj_class_t etx_quick_button_class = {
    .base_class = &lv_btn_class,
    .constructor_cb = etx_quick_button_constructor,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = QuickMenuGroup::QM_BUTTON_WIDTH,
    .height_def = QuickMenuGroup::QM_BUTTON_HEIGHT,
    .editable = LV_OBJ_CLASS_EDITABLE_INHERIT,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_TRUE,
    .instance_size = sizeof(lv_btn_t),
};

lv_obj_t* etx_quick_button_create(lv_obj_t* parent)
{
  return etx_create(&etx_quick_button_class, parent);
}


QuickMenuGroup::QuickMenuGroup(Window* parent) :
        Window(parent, {0, 0, parent->width(), parent->height()})
{
  padAll(PAD_OUTLINE);
  group = lv_group_create();
}

ButtonBase* QuickMenuGroup::addButton(EdgeTxIcon icon, const char* title,
                                  std::function<void(void)> pressHandler,
                                  std::function<bool(void)> visibleHandler,
                                  std::function<void(void)> focusHandler)
{
  ButtonBase* b = new QuickMenuButton(this, icon, title, [=]() { pressHandler(); return 0; }, visibleHandler);
  b->setLongPressHandler([=]() { pressHandler(); return 0; });
  btns.push_back(b);
  if (group) lv_group_add_obj(group, b->getLvObj());
  b->setFocusHandler([=](bool focus) {
    if (focus) {
      curBtn = b;
      if (focusHandler) focusHandler();
    }
  });
  if (btns.size() == 1) {
    curBtn = b;
    if (focusHandler) focusHandler();
  }
  return b;
}

void QuickMenuGroup::setGroup()
{
  if (group && group != lv_group_get_default()) {
    lv_group_set_default(group);

    lv_indev_t* indev = lv_indev_get_next(NULL);
    while (indev) {
      lv_indev_set_group(indev, group);
      indev = lv_indev_get_next(indev);
    }
  }
}

void QuickMenuGroup::deleteLater(bool detach, bool trash)
{
  if (group) lv_group_del(group);
  Window::deleteLater(detach, trash);
}

void QuickMenuGroup::setFocus()
{
  if (curBtn) {
    lv_event_send(curBtn->getLvObj(), LV_EVENT_FOCUSED, nullptr);
    lv_group_focus_obj(curBtn->getLvObj());
  }
}

void QuickMenuGroup::clearFocus()
{
  if (curBtn) {
    ((QuickMenuButton*)curBtn)->setEnabled();
    lv_event_send(curBtn->getLvObj(), LV_EVENT_DEFOCUSED, nullptr);
  }
}

void QuickMenuGroup::setDisabled(bool all)
{
  for (size_t i = 0; i < btns.size(); i += 1) {
    if (btns[i] != curBtn || all) {
      ((QuickMenuButton*)btns[i])->setDisabled();
      lv_event_send(btns[i]->getLvObj(), LV_EVENT_DEFOCUSED, nullptr);
    }
  }
}

void QuickMenuGroup::setEnabled()
{
  for (size_t i = 0; i < btns.size(); i += 1) {
    ((QuickMenuButton*)btns[i])->setEnabled();
  }
}

void QuickMenuGroup::setCurrent(ButtonBase* b)
{
  curBtn = b;
  ((QuickMenuButton*)b)->setEnabled();
}

void QuickMenuGroup::doLayout(int cols)
{
  int n = 0;
  for (size_t i = 0; i < btns.size(); i += 1) {
    if (((QuickMenuButton*)btns[i])->isVisible()) {
      coord_t x = (n % cols) * (QM_BUTTON_WIDTH + PAD_MEDIUM);
      coord_t y = (n / cols) * (QM_BUTTON_HEIGHT + PAD_MEDIUM);
      lv_obj_set_pos(btns[i]->getLvObj(), x, y);
      btns[i]->show();
      n += 1;
    } else {
      btns[i]->hide();
    }
  }
}

void QuickMenuGroup::nextEntry()
{
  if (group)
    lv_group_focus_next(group);
}

void QuickMenuGroup::prevEntry()
{
  if (group)
    lv_group_focus_prev(group);
}

ButtonBase* QuickMenuGroup::getFocusedButton()
{
  if (group) {
    lv_obj_t* b = lv_group_get_focused(group);
    if (b) {
      for (size_t i = 0; i < btns.size(); i += 1)
        if (btns[i]->getLvObj() == b)
          return btns[i];
    }
  }
  return nullptr;
}
