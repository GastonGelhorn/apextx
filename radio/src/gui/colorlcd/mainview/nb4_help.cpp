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
#include "static.h"
#include "nb4_routes.h"
#include "layer.h"

std::function<void()> nb4InheritedHelp()
{
  std::function<void()> action;
  Layer::walk([&action](Window* window) {
    action = window->getHelpHandler();
    return bool(action);
  });
  return action ? action : [] { nb4OpenHelp(); };
}

void nb4DismissHelp()
{
  while (Layer::back() && Layer::back()->isHelpPage())
    Layer::back()->deleteLater();
}

std::string nb4InheritedScope()
{
  std::string text;
  Layer::walk([&text](Window* window) {
    text = window->getScopeText();
    return !text.empty();
  });
  return text;
}

void nb4ScopeBar(Window* owner, Window* body, StaticText*& label,
                 const std::string& text)
{
  if (!label && text.empty()) return;
  constexpr coord_t height = 22;
  if (!label) {
    label = new StaticText(owner, {0, body->top(), owner->width(), height},
      text.c_str(), COLOR_THEME_PRIMARY3_INDEX, FONT(XS));
    label->padLeft(PAD_MEDIUM);
    label->padRight(PAD_MEDIUM);
    lv_label_set_long_mode(label->getLvObj(), LV_LABEL_LONG_DOT);
    body->setTop(body->top() + height);
    body->setHeight(body->height() - height);
    lv_obj_set_style_max_height(body->getLvObj(), body->height(), 0);
  }
  label->setText(text);
}

class Nb4HelpDialog : public BaseDialog
{
 public:
  bool isHelpPage() const override { return true; }
  Nb4HelpDialog(const char* title, const Nb4HelpEntry* entries, unsigned count) :
      BaseDialog(title, true, (lv_coord_t)(lv_disp_get_hor_res(nullptr) * 0.92),
                 (lv_coord_t)(lv_disp_get_ver_res(nullptr) * 0.92))
  {
    useSectionHeader();
    for (unsigned i = 0; i < count; i += 1) {
      auto label = new StaticText(form, {0, 0, LV_PCT(100), 0},
                                  entries[i].label(),
                                  COLOR_THEME_PRIMARY1_INDEX, FONT(BOLD));

      if (i) lv_obj_set_style_pad_top(label->getLvObj(), PAD_MEDIUM, LV_PART_MAIN);

      new StaticText(form, {0, 0, LV_PCT(100), 0},
                     entries[i].body(),
                     COLOR_THEME_PRIMARY3_INDEX);
    }

    form->padLeft(PAD_MEDIUM);
    form->padRight(PAD_MEDIUM);

    auto done = new TextButton(form, {0, 0, LV_PCT(100), 0},
                               STR_NB4_GOT_IT,
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

void nb4AddHelp(Window* form, const char* title, const Nb4HelpEntry* entries,
                unsigned count)
{
  // Detailed help uses the same header control as every other setting.
  for (auto owner = form; owner; owner = owner->getParent())
    if (owner->setHelpHandler([=] { new Nb4HelpDialog(title, entries, count); }))
      return;
}

#endif  // RADIO_NB4_FAMILY
