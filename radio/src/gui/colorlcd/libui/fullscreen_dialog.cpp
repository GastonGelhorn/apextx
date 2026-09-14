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

#include "fullscreen_dialog.h"

#include "LvglWrapper.h"
#include "mainwindow.h"
#include "edgetx.h"
#include "etx_lv_theme.h"
#include "os/sleep.h"
#include "view_main.h"
#include "hal/watchdog_driver.h"

#if defined(RADIO_NB4_FAMILY)
#include "nb4_routes.h"
#include "nb4_ui.h"
#endif

FullScreenDialog::FullScreenDialog(
    uint8_t type, std::string title, std::string message, std::string action,
    const std::function<void(void)>& confirmHandler) :
    Window(MainWindow::instance(), {0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)}),
    type(type),
    title(std::move(title)),
    message(std::move(message)),
    action(std::move(action)),
    confirmHandler(confirmHandler)
{
  setWindowFlag(OPAQUE);
#if defined(RADIO_NB4_FAMILY)
  // A warning fills the screen with the palette's warning colour, which is
  // what this radio has always done; a confirmation uses the page ground.
  etx_solid_bg(lvobj, type == WARNING_TYPE_ALERT  ? COLOR_THEME_WARNING_INDEX
                      : type == WARNING_TYPE_INFO ? COLOR_THEME_SECONDARY1_INDEX
                                                  : COLOR_THEME_PRIMARY2_INDEX);
#else
  etx_solid_bg(lvobj, (type == WARNING_TYPE_ALERT) ? COLOR_THEME_WARNING_INDEX : COLOR_THEME_SECONDARY1_INDEX);
#endif

  // In case alert raised while splash screen is showing.
  cancelSplash();

  pushLayer();

  bringToTop();

  build();
}

void FullScreenDialog::build()
{
#if defined(RADIO_NB4_FAMILY)
  // Firmware update dialogs position their progress bar within the original
  // info layout. Keep that layout while restyling alerts and confirmations.
  if (type != WARNING_TYPE_INFO) {
    buildNb4();
    return;
  }
#endif
  auto div = new Window(this, {0, ALERT_FRAME_TOP, lv_disp_get_hor_res(nullptr), ALERT_FRAME_HEIGHT});
  div->setWindowFlag(NO_FOCUS);
  etx_solid_bg(div->getLvObj(), COLOR_THEME_PRIMARY2_INDEX);

  new StaticIcon(
      this, ALERT_BITMAP_LEFT, ALERT_BITMAP_TOP,
      (type == WARNING_TYPE_INFO) ? ICON_BUSY : ICON_ERROR,
      COLOR_THEME_WARNING_INDEX);

  std::string t;
  if (type == WARNING_TYPE_ALERT) {
#if defined(TRANSLATIONS_FR) || defined(TRANSLATIONS_IT) || \
    defined(TRANSLATIONS_CZ)
    t = std::string(STR_WARNING) + "\n" + title;
#else
    t = title + "\n" + STR_WARNING;
#endif
  } else if (!title.empty()) {
    t = title;
  }
  new StaticText(this,
                 rect_t{ALERT_TITLE_LEFT, ALERT_TITLE_TOP,
                        lv_disp_get_hor_res(nullptr) - ALERT_TITLE_LEFT - PAD_MEDIUM,
                        lv_disp_get_ver_res(nullptr) - ALERT_TITLE_TOP - PAD_MEDIUM},
                 t.c_str(), COLOR_THEME_WARNING_INDEX, FONT(XL));

  messageLabel =
      new StaticText(this,
                     rect_t{ALERT_MESSAGE_LEFT, ALERT_MESSAGE_TOP,
                            lv_disp_get_hor_res(nullptr) - ALERT_MESSAGE_LEFT - PAD_MEDIUM,
                            lv_disp_get_ver_res(nullptr) - ALERT_MESSAGE_TOP - PAD_MEDIUM},
                     message.c_str(), COLOR_THEME_PRIMARY1_INDEX, FONT(BOLD));

  if (!action.empty()) {
    auto btn = new TextButton(
        this, {(lv_disp_get_hor_res(nullptr) - ONEBTN_W) / 2, lv_disp_get_ver_res(nullptr) - ONEBTN_H - PAD_LARGE, ONEBTN_W, ONEBTN_H}, action.c_str(),
        [=]() {
          closeDialog();
          return 0;
        });
    etx_bg_color(btn->getLvObj(), COLOR_THEME_SECONDARY3_INDEX);
    etx_txt_color(btn->getLvObj(), COLOR_THEME_PRIMARY1_INDEX);
  } else {
    if (type == WARNING_TYPE_CONFIRM) {
      auto btn = new TextButton(
          this, {lv_disp_get_hor_res(nullptr) / 3 - TWOBTN_W / 2, lv_disp_get_ver_res(nullptr) - TWOBTN_H - PAD_LARGE, TWOBTN_W, TWOBTN_H}, STR_CANCEL,
          [=]() {
            deleteLater();
            return 0;
          });
      etx_bg_color(btn->getLvObj(), COLOR_THEME_SECONDARY3_INDEX);
      etx_txt_color(btn->getLvObj(), COLOR_THEME_PRIMARY1_INDEX);
      btn = new TextButton(
          this, {lv_disp_get_hor_res(nullptr) * 2 / 3 - TWOBTN_W / 2, lv_disp_get_ver_res(nullptr) - TWOBTN_H - PAD_LARGE, TWOBTN_W, TWOBTN_H}, STR_OK,
          [=]() {
            closeDialog();
            return 0;
          });
      etx_bg_color(btn->getLvObj(), COLOR_THEME_SECONDARY3_INDEX);
      etx_txt_color(btn->getLvObj(), COLOR_THEME_PRIMARY1_INDEX);
    }
  }
}

#if defined(RADIO_NB4_FAMILY)
namespace {

const lv_color_t kWhite = lv_color_hex(0xFFFFFF);

// Over the warning's red ground the text is always white, whatever the
// palette, because every palette's warning colour is a dark red. A
// confirmation keeps the page ground instead and takes the palette's colours.
lv_color_t alertInk(bool alert)
{
  return alert ? kWhite : Nb4Ui::color(COLOR_THEME_PRIMARY1_INDEX);
}

// White reads at only 3:1 on that red, which is enough for the large type and
// too weak for a label inside a white button, so the filled button takes a
// deepened shade of the same red.
lv_color_t buttonInk() { return lv_color_darken(Nb4Ui::color(COLOR_THEME_WARNING_INDEX), LV_OPA_40); }

StaticText* alertText(Window* parent, const char* value, LcdFlags font,
                      lv_color_t colour, lv_text_align_t align)
{
  auto label = new StaticText(parent, {0, 0, LV_PCT(100), 0}, value,
                              COLOR_THEME_PRIMARY1_INDEX, font);
  lv_obj_t* o = label->getLvObj();
  lv_obj_set_style_text_color(o, colour, LV_PART_MAIN);
  lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(o, align, LV_PART_MAIN);
  return label;
}

// A filled white button, or a transparent one outlined in white.
TextButton* alertButton(Window* parent, rect_t rect, const char* label,
                        std::function<void()> action, bool primary)
{
  auto button = new TextButton(parent, rect, label, [action]() { action(); return 0; });
  button->setFont(FONT_BOLD_INDEX);
  lv_obj_t* o = button->getLvObj();
  const lv_color_t ink = primary ? buttonInk() : kWhite;
  lv_obj_set_style_radius(o, 10, LV_PART_MAIN);
  lv_obj_set_style_border_width(o, primary ? 0 : 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(o, kWhite, LV_PART_MAIN);
  // The theme styles the focused and pressed states, and a themed style for a
  // specific state beats a local one for the default state, so each state is
  // set here explicitly. Without this the focused button took the theme's
  // accent fill and the label stopped reading.
  const lv_state_t states[] = {
      LV_STATE_DEFAULT, LV_STATE_FOCUSED, LV_STATE_FOCUS_KEY, LV_STATE_PRESSED,
      (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_PRESSED),
      (lv_state_t)(LV_STATE_FOCUS_KEY | LV_STATE_PRESSED)};
  for (lv_state_t state : states) {
    lv_obj_set_style_bg_color(o, kWhite, LV_PART_MAIN | state);
    lv_obj_set_style_bg_opa(o, primary ? LV_OPA_COVER : LV_OPA_TRANSP,
                            LV_PART_MAIN | state);
    lv_obj_set_style_text_color(o, ink, LV_PART_MAIN | state);
    if (lv_obj_get_child_cnt(o))
      lv_obj_set_style_text_color(lv_obj_get_child(o, 0), ink, LV_PART_MAIN | state);
    // The ring LVGL draws on focus is invisible against white; the fill
    // already says which button is the default.
    lv_obj_set_style_outline_width(o, 0, LV_PART_MAIN | state);
  }
  return button;
}

}  // namespace

// NB4: the warning keeps the full red ground it has always had and spends it
// on one clear hierarchy. Severity, title and the radio's own one-line message
// in decreasing size, then, in smaller type, why it appeared and what to do,
// then real buttons. Where a setting fixes the warning, one button opens it;
// the route is taken by the main loop once the alert is gone, because alerts
// run in a nested loop of their own.
void FullScreenDialog::buildNb4()
{
  const coord_t W = lv_disp_get_hor_res(nullptr);
  const coord_t H = lv_disp_get_ver_res(nullptr);
  const bool landscape = W > H;
  constexpr coord_t M = 22, GAP = 12, BTN = 48, ICON = 96;

  const bool alert = type == WARNING_TYPE_ALERT;
  // The checklist opens its own window over an empty alert: leave the ground.
  if (title.empty() && message.empty() && action.empty()) return;

  const Nb4Alert* known =
      alert ? nb4AlertFor(title.c_str(), message.c_str()) : nullptr;
  const bool canOpen = known && nb4AlertCanOpen(*known);
  const lv_color_t ink = alertInk(alert);

  // Buttons along the bottom edge; they fix how much room the text has.
  const coord_t half = (W - 2 * M - GAP) / 2;
  coord_t bottom = H - M - BTN - GAP;
  auto dismiss = [this]() { closeDialog(); };
  if (!alert) {
    Nb4Ui::action(this, {(coord_t)(M + half + GAP), (coord_t)(H - M - BTN), half, BTN},
                  STR_OK, dismiss, true);
    Nb4Ui::action(this, {M, (coord_t)(H - M - BTN), half, BTN}, STR_CANCEL,
                  [this]() { deleteLater(); });
  } else if (canOpen) {
    char go[64];
    snprintf(go, sizeof(go), STR_NB4_GO_TO, known->label());
    auto open = [this, known]() {
      nb4DeferRoute(known->path);
      closeDialog();
    };
    if (landscape) {
      // Built before the one on its left so it takes the initial focus: in
      // both orientations the highlighted button is the one that opens the
      // setting, and the same key press does the same thing.
      alertButton(this, {(coord_t)(M + half + GAP), (coord_t)(H - M - BTN), half, BTN},
                  go, open, true);
      alertButton(this, {M, (coord_t)(H - M - BTN), half, BTN}, STR_NB4_SKIP_FOR_NOW,
                  dismiss, false);
    } else {
      alertButton(this, {M, (coord_t)(H - M - 2 * BTN - GAP), (coord_t)(W - 2 * M), BTN},
                  go, open, true);
      alertButton(this, {M, (coord_t)(H - M - BTN), (coord_t)(W - 2 * M), BTN},
                  STR_NB4_SKIP_FOR_NOW, dismiss, false);
      bottom = H - M - 2 * BTN - 2 * GAP;
    }
  } else {
    alertButton(this, {M, (coord_t)(H - M - BTN), (coord_t)(W - 2 * M), BTN},
                STR_NB4_GOT_IT, dismiss, true);
  }

  // The icon sits above the text in portrait and beside it in landscape.
  const coord_t iconX = landscape ? M : (W - ICON) / 2;
  const coord_t iconY = landscape ? (coord_t)(M + (bottom - M - ICON) / 2) : M;
  auto mark = new StaticIcon(this, iconX, iconY, ICON_ERROR,
                             COLOR_THEME_WARNING_INDEX);
  if (alert) {
    lv_obj_set_style_img_recolor(mark->getLvObj(), kWhite, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(mark->getLvObj(), LV_OPA_COVER, LV_PART_MAIN);
  }

  const coord_t bodyX = landscape ? (coord_t)(M + ICON + GAP * 2) : M;
  const coord_t bodyY = landscape ? M : (coord_t)(M + ICON + GAP);
  const rect_t bodyRect = {bodyX, bodyY, (coord_t)(W - bodyX - M),
                           (coord_t)(bottom - bodyY)};
  auto body = new Window(this, bodyRect);
  body->setWindowFlag(NO_FOCUS);
  lv_obj_set_style_bg_opa(body->getLvObj(), LV_OPA_TRANSP, LV_PART_MAIN);
  body->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_TINY, bodyRect.w, bodyRect.h);
  lv_obj_set_flex_align(body->getLvObj(), LV_FLEX_ALIGN_CENTER,
                        landscape ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START);
  const lv_text_align_t align =
      landscape ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;

  if (alert) alertText(body, STR_WARNING, FONT(BOLD), ink, align);
  if (!title.empty()) alertText(body, title.c_str(), FONT(XL), ink, align);
  messageLabel = alertText(body, message.c_str(), FONT(L), ink, align);
  // Smaller than the message it explains, and only for warnings we know.
  if (known) alertText(body, known->advice(), FONT(STD), ink, align);
}
#endif

void FullScreenDialog::closeDialog()
{
  if (confirmHandler) confirmHandler();
  deleteLater();
}

bool FullScreenDialog::onLongPress()
{
  closeDialog();
  lv_indev_wait_release(lv_indev_get_act());
  return false;
}

void FullScreenDialog::onEvent(event_t event)
{
  // Buttons other than RTN or ENTER
  if (type == WARNING_TYPE_ALERT) {
    closeDialog();
    killEvents(event);
  }
}

void FullScreenDialog::onCancel() { deleteLater(); }

void FullScreenDialog::checkEvents()
{
  Window::checkEvents();
  if (closeCondition && closeCondition()) {
    deleteLater();
  }
}

void FullScreenDialog::deleteLater(bool detach, bool trash)
{
  if (running) {
    running = false;
  } else {
    Window::deleteLater(detach, trash);
  }
}

void FullScreenDialog::setMessage(const char* text)
{
  if (messageLabel) messageLabel->setText(text);
}

static void run_ui_manually()
{
  checkBacklight();
  WDG_RESET();

  sleep_ms(10);
  LvglWrapper::runNested();
  MainWindow::instance()->run(false);
}

void FullScreenDialog::runForever(bool checkPwr)
{
  running = true;

  // reset input devices to avoid
  // RELEASED/CLICKED to be called in a loop
  lv_indev_reset(nullptr, nullptr);

  while (running) {
    resetBacklightTimeout();

    if (checkPwr) {
      auto check = pwrCheck();
      if (check == e_power_off) {
        boardOff();
#if defined(SIMU)
        return;
#endif
      } else if (check == e_power_press) {
        WDG_RESET();
        sleep_ms(1);
        continue;
      }
    }

    run_ui_manually();
  }

  deleteLater();
}

void raiseAlert(const char* title, const char* msg, const char* action,
                uint8_t sound)
{
  TRACE("raiseAlert('%s')", msg);
  AUDIO_ERROR_MESSAGE(sound);
  LED_ERROR_BEGIN();
  auto dialog = new FullScreenDialog(WARNING_TYPE_ALERT, title ? title : "",
                                     msg ? msg : "", action ? action : "");
  dialog->runForever();
  LED_ERROR_END();
}

// POPUP_CONFIRMATION
bool confirmationDialog(const char* title, const char* msg, bool checkPwr,
                        const std::function<bool(void)>& closeCondition)
{
  bool confirmed = false;
  auto dialog = new FullScreenDialog(WARNING_TYPE_CONFIRM, title ? title : "",
                                     msg ? msg : "", "",
                                     [&confirmed]() { confirmed = true; });
  if (closeCondition) {
    dialog->setCloseCondition([&confirmed, &closeCondition]() {
      if (closeCondition()) {
        confirmed = true;
        return true;
      } else {
        return false;
      }
    });
  }

  dialog->runForever(checkPwr);

  return confirmed;
}
