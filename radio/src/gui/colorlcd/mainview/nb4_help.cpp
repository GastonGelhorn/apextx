/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_help.h"

#if defined(RADIO_NB4_FAMILY)

#include "button.h"
#include "dialog.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "nb4_car_state.h"
#include "static.h"

class Nb4HelpDialog : public BaseDialog
{
 public:
  Nb4HelpDialog(const char* title, const Nb4HelpEntry* entries, unsigned count) :
      BaseDialog(title, true, (lv_coord_t)(lv_disp_get_hor_res(nullptr) * 0.92),
                 (lv_coord_t)(lv_disp_get_ver_res(nullptr) * 0.92))
  {
    for (unsigned i = 0; i < count; i += 1) {
      auto label = new StaticText(form, {0, 0, LV_PCT(100), 0},
                                  nb4Text(entries[i].labelEs, entries[i].labelEn),
                                  COLOR_THEME_PRIMARY1_INDEX, FONT(BOLD));

      if (i) lv_obj_set_style_pad_top(label->getLvObj(), PAD_MEDIUM, LV_PART_MAIN);

      new StaticText(form, {0, 0, LV_PCT(100), 0},
                     nb4Text(entries[i].bodyEs, entries[i].bodyEn),
                     COLOR_THEME_PRIMARY3_INDEX);
    }

    form->padLeft(PAD_MEDIUM);
    form->padRight(PAD_MEDIUM);

    auto done = new TextButton(form, {0, 0, LV_PCT(100), 0},
                               nb4Text("Entendido", "Got it"),
                               [this]() { deleteLater(); return 0; });
    lv_obj_set_style_pad_top(done->getLvObj(), PAD_LARGE, LV_PART_MAIN);

    lv_obj_clear_flag(done->getLvObj(), LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  }

  void checkEvents() override
  {
    BaseDialog::checkEvents();
    if (deleted()) return;
    if (!settled) {
      settled = true;
      lv_obj_scroll_to_y(form->getLvObj(), 0, LV_ANIM_OFF);
    }
  }

 private:
  bool settled = false;
};

void nb4AddHelp(Window* form, const char* titleEs, const char* titleEn,
                const Nb4HelpEntry* entries, unsigned count)
{
  const char* title = nb4Text(titleEs, titleEn);
  auto btn = new TextButton(
      form, {0, 0, LV_PCT(100), 0},
      nb4Text("?  Qué hace cada ajuste", "?  What each setting does"),
      [=]() {
        new Nb4HelpDialog(title, entries, count);
        return 0;
      });

  btn->setWrap();

  lv_obj_move_to_index(btn->getLvObj(), 0);
}

#endif  // RADIO_NB4_FAMILY
