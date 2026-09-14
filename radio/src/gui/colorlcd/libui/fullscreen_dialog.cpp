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
  etx_solid_bg(lvobj, type == WARNING_TYPE_INFO ? COLOR_THEME_SECONDARY1_INDEX
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
// NB4: a dark page with a coloured badge, the title and message, and real
// buttons. An alert that has a setting behind it offers to open that setting;
// the route is opened by the main loop once the alert is gone, because alerts
// run in a nested loop of their own.
void FullScreenDialog::buildNb4()
{
  const coord_t W = lv_disp_get_hor_res(nullptr);
  const coord_t H = lv_disp_get_ver_res(nullptr);
  const bool landscape = W > H;
  constexpr coord_t M = 20, GAP = 10, BTN = 48, BADGE = 128, ICON = 96;

  const bool alert = type == WARNING_TYPE_ALERT;
  const bool info = type == WARNING_TYPE_INFO;
  // The checklist opens its own window over an empty alert: keep a backdrop.
  if (title.empty() && message.empty() && action.empty()) return;

  const LcdColorIndex tone = alert  ? COLOR_THEME_WARNING_INDEX
                             : info ? COLOR_THEME_PRIMARY3_INDEX
                                    : COLOR_THEME_FOCUS_INDEX;
  const Nb4AlertLink* link =
      alert ? nb4AlertLink(title.c_str(), message.c_str()) : nullptr;

  // Buttons along the bottom edge; they fix how much room the text has.
  coord_t bottom = H - M;
  if (!info) {
    const coord_t half = (W - 2 * M - GAP) / 2;
    auto skip = [this]() { closeDialog(); };
    if (type == WARNING_TYPE_CONFIRM) {
      Nb4Ui::action(this, {M, H - M - BTN, half, BTN}, STR_CANCEL,
                    [this]() { deleteLater(); });
      Nb4Ui::action(this, {M + half + GAP, H - M - BTN, half, BTN}, STR_OK,
                    skip, true);
      bottom = H - M - BTN - GAP;
    } else if (link) {
      char go[64];
      snprintf(go, sizeof(go), STR_NB4_GO_TO, link->label());
      auto open = [this, link]() {
        nb4DeferRoute(link->path);
        closeDialog();
      };
      if (landscape) {
        // Built before the one on its left so it takes the initial focus:
        // in both orientations the highlighted button is the one that opens
        // the setting, and the same key press does the same thing.
        Nb4Ui::action(this, {M + half + GAP, H - M - BTN, half, BTN}, go, open,
                      true);
        Nb4Ui::action(this, {M, H - M - BTN, half, BTN}, STR_NB4_SKIP_FOR_NOW,
                      skip);
        bottom = H - M - BTN - GAP;
      } else {
        Nb4Ui::action(this, {M, H - M - 2 * BTN - GAP, W - 2 * M, BTN}, go,
                      open, true);
        Nb4Ui::action(this, {M, H - M - BTN, W - 2 * M, BTN},
                      STR_NB4_SKIP_FOR_NOW, skip);
        bottom = H - M - 2 * BTN - 2 * GAP;
      }
    } else {
      Nb4Ui::action(this, {M, H - M - BTN, W - 2 * M, BTN}, STR_NB4_GOT_IT,
                    skip, true);
      bottom = H - M - BTN - GAP;
    }
  }

  // Badge: a soft disc with the icon in the alert's colour.
  const coord_t badgeX = landscape ? M : (W - BADGE) / 2;
  const coord_t badgeY = landscape ? M + (bottom - M - BADGE) / 2 : M;
  auto disc = new Window(this, {badgeX, badgeY, BADGE, BADGE});
  disc->setWindowFlag(NO_FOCUS);
  etx_solid_bg(disc->getLvObj(), COLOR_THEME_SECONDARY2_INDEX);
  lv_obj_set_style_radius(disc->getLvObj(), LV_RADIUS_CIRCLE, 0);
  new StaticIcon(this, badgeX + (BADGE - ICON) / 2, badgeY + (BADGE - ICON) / 2,
                 info ? ICON_BUSY : ICON_ERROR, tone);

  // Text block: caption, title and message stacked, centred under the badge
  // in portrait and beside it in landscape.
  const coord_t bodyX = landscape ? M + BADGE + 2 * GAP : M;
  const coord_t bodyY = landscape ? M : badgeY + BADGE + GAP;
  const rect_t bodyRect = {bodyX, bodyY, W - bodyX - M, bottom - bodyY};
  auto body = new Window(this, bodyRect);
  body->setWindowFlag(NO_FOCUS);
  lv_obj_set_style_bg_opa(body->getLvObj(), LV_OPA_TRANSP, 0);
  body->setFlexLayout(LV_FLEX_FLOW_COLUMN, GAP / 2, bodyRect.w, bodyRect.h);
  lv_obj_set_flex_align(body->getLvObj(),
                        landscape ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START,
                        landscape ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START);
  const lv_text_align_t align =
      landscape ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;
  auto text = [&](const char* value, LcdColorIndex color, LcdFlags font) {
    auto label = new StaticText(body, {0, 0, LV_PCT(100), 0}, value, color, font);
    lv_label_set_long_mode(label->getLvObj(), LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label->getLvObj(), align, 0);
    return label;
  };
  if (alert) text(STR_WARNING, tone, FONT(XS));
  if (!title.empty()) text(title.c_str(), COLOR_THEME_PRIMARY1_INDEX, FONT(L));
  messageLabel = text(message.c_str(), COLOR_THEME_PRIMARY1_INDEX, FONT(STD));
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
