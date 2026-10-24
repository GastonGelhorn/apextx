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

#include "hal/abnormal_reboot.h"
#include "inactivity_timer.h"
#include "edgetx.h"
#include "os/sleep.h"
#include "stamp.h"
#include "theme_manager.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_routes.h"
#endif

extern void checkSpeakerVolume();

constexpr const char* strip_leading_hyphen(const char* str) {
    return (str[0] == '-') ? str + 1 : str;
}

#if defined(RADIO_NB4)
const std::string splash_version_str = APEXTX_VERSION;
#elif defined(VERSION_TAG)
const std::string ver_str = "" VERSION_TAG;
const std::string nam_str = "" CODENAME;
#else
const std::string ver_str = "" VERSION_PREFIX VERSION;
const std::string nam_str = strip_leading_hyphen("" VERSION_SUFFIX);
const std::string git_str = "(" GIT_STR ")";
#endif

const uint8_t __bmp_splash_logo[] __FLASH = {
#include "splash_logo.lbm"
};

static Window* splashScreen = nullptr;

void drawSplash()
{
  if (!sdMounted()) sdInit();

  splashScreen = new Window(MainWindow::instance(), {0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)});
  lv_obj_set_parent(splashScreen->getLvObj(), lv_layer_top());

  etx_solid_bg(splashScreen->getLvObj(), COLOR_BLACK_INDEX);

  auto bg = new StaticImage(splashScreen, {0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)},
                            BITMAPS_PATH "/" SPLASH_FILE);
  bg->show(bg->hasImage());

  if (!bg->hasImage()) {
    LZ4Bitmap* logo = (LZ4Bitmap*)__bmp_splash_logo;
    coord_t x = (LANDSCAPE ? lv_disp_get_hor_res(nullptr) / 3 : lv_disp_get_hor_res(nullptr) / 2) - logo->width / 2;
    coord_t y = (LANDSCAPE ? lv_disp_get_ver_res(nullptr) / 2 : lv_disp_get_ver_res(nullptr) * 2 / 5) - logo->height / 2;
    new StaticLZ4Image(splashScreen, x, y, logo);

    coord_t w = LAYOUT_SCALE(200);
    x = (LANDSCAPE ? lv_disp_get_hor_res(nullptr) * 4 / 5 : lv_disp_get_hor_res(nullptr) / 2) - w / 2;
    y = lv_disp_get_ver_res(nullptr) - EdgeTxStyles::STD_FONT_HEIGHT * 4;
#if defined(RADIO_NB4)
    new StaticText(splashScreen,
                   {x, y, w, EdgeTxStyles::STD_FONT_HEIGHT},
                   splash_version_str.c_str(), COLOR_GREY_INDEX, CENTERED);
#else
    new StaticText(splashScreen, {x, y, w, EdgeTxStyles::STD_FONT_HEIGHT}, ver_str.c_str(), COLOR_GREY_INDEX, CENTERED);
    new StaticText(splashScreen, {x, y + EdgeTxStyles::STD_FONT_HEIGHT, w, EdgeTxStyles::STD_FONT_HEIGHT},
                   nam_str.c_str(), COLOR_GREY_INDEX, CENTERED);
#if !defined(VERSION_TAG)
    new StaticText(splashScreen, {x, y + EdgeTxStyles::STD_FONT_HEIGHT * 2, w, EdgeTxStyles::STD_FONT_HEIGHT},
                   git_str.c_str(), COLOR_GREY_INDEX, CENTERED);
#endif
#endif
  }

  MainWindow::instance()->setActiveScreen();
  lv_refr_now(nullptr);
}

static tmr10ms_t splashStartTime = 0;
static bool splashRunning = false;

#if defined(RADIO_NB4_FAMILY)
static lv_obj_t* splashSpinner = nullptr;

static void spinSplashSpinner(void* arc, int32_t angle)
{
  lv_arc_set_rotation((lv_obj_t*)arc, angle);
}

// A turning arc above the version line while the splash prepares the menus.
static void showSplashSpinner()
{
  if (!splashScreen || splashSpinner) return;
  constexpr coord_t SIZE = 32;
  splashSpinner = lv_arc_create(splashScreen->getLvObj());
  lv_obj_set_size(splashSpinner, SIZE, SIZE);
  lv_obj_set_pos(splashSpinner, (lv_disp_get_hor_res(nullptr) - SIZE) / 2,
                 lv_disp_get_ver_res(nullptr) -
                     EdgeTxStyles::STD_FONT_HEIGHT * 4 - SIZE - PAD_LARGE);
  lv_obj_remove_style(splashSpinner, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(splashSpinner, LV_OBJ_FLAG_CLICKABLE);
  lv_arc_set_bg_angles(splashSpinner, 0, 360);
  lv_arc_set_angles(splashSpinner, 0, 75);
  lv_obj_set_style_arc_width(splashSpinner, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(splashSpinner, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(splashSpinner, makeLvColor(COLOR_GREY), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(splashSpinner, LV_OPA_30, LV_PART_MAIN);
  lv_obj_set_style_arc_color(splashSpinner, makeLvColor(COLOR_WHITE), LV_PART_INDICATOR);

  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, splashSpinner);
  lv_anim_set_exec_cb(&anim, spinSplashSpinner);
  lv_anim_set_values(&anim, 0, 360);
  lv_anim_set_time(&anim, 900);
  lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&anim);
}

static void hideSplashSpinner()
{
  if (!splashSpinner) return;
  lv_anim_del(splashSpinner, nullptr);
  lv_obj_del(splashSpinner);
  splashSpinner = nullptr;
}
#endif

void startSplash()
{
  if (!UNEXPECTED_SHUTDOWN()) {
    splashStartTime = get_tmr10ms();
    splashRunning = true;
    drawSplash();
  }
}

void cancelSplash()
{
#if defined(RADIO_NB4_FAMILY)
  hideSplashSpinner();
#endif
  if (splashScreen) {
    splashScreen->deleteLater();
    splashScreen = nullptr;
    MainWindow::instance()->setActiveScreen();
    splashStartTime = 0;
  }
}

void waitSplash()
{
  // Handle color splash screen
  if (splashRunning) {
    inactivityCheckInputs();
    splashStartTime += SPLASH_TIMEOUT;

#if defined(PWR_BUTTON_DUAL) || defined(RADIO_NB4_FAMILY)
    // Wait for the power button to be released before the main loop can arm
    // shutdown detection. pwrCheck() starts the shutdown timer as soon as it
    // sees the button held, so a startup press that is still held here would
    // shut the radio down again right after the splash. The NB4 startup hold
    // is two seconds, and an externally powered start takes longer to qualify,
    // which makes that overlap the normal case rather than the exception.
    while (pwrPressed()) {
      WDG_RESET();
      sleep_ms(10);
    }
    // Kill SYS and/or MDL events
    killAllEvents();
    MainWindow::instance()->run();
#endif

#if defined(RADIO_NB4_FAMILY)
    // Build the navigation grids while the splash is up, so even the first
    // tap on each opens a ready one. The splash waits for them, but never
    // more than WARM_LIMIT beyond its own time; anything left is finished
    // once home is idle.
    constexpr tmr10ms_t WARM_LIMIT = 500;
    const tmr10ms_t warmDeadline = get_tmr10ms() + WARM_LIMIT;
    bool warming = true;
    showSplashSpinner();
    LvglWrapper::instance()->run();
#endif

    while (splashStartTime >= get_tmr10ms()
#if defined(RADIO_NB4_FAMILY)
           || (warming && warmDeadline >= get_tmr10ms())
#endif
    ) {
#if defined(RADIO_NB4_FAMILY)
      if (warming && !(warming = nb4WarmNextGrid())) hideSplashSpinner();
#endif
      LvglWrapper::instance()->run();
      MainWindow::instance()->run();
      WDG_RESET();
      checkSpeakerVolume();
      checkBacklight();
      sleep_ms(10);
      auto evt = getEvent();
      if (evt || inactivityCheckInputs()) {
        if (evt) killEvents(evt);
        break;
      }
#if defined(SIMU)
      // Allow simulator to exit if closed while splash showing
      uint32_t pwr_check = pwrCheck();
      if (pwr_check == e_power_off) {
        break;
      }
#endif  // defined(SIMU)
    }

    // Reset timer so special/global functions set to !1x don't get triggered
    START_SILENCE_PERIOD();
  }

  cancelSplash();
}

static LAYOUT_VAL_SCALED(SHUTDOWN_CIRCLE_RADIUS, 75)

const int8_t bmp_shutdown_xo[] = {0, 0, -SHUTDOWN_CIRCLE_RADIUS,
                                  -SHUTDOWN_CIRCLE_RADIUS};
const int8_t bmp_shutdown_yo[] = {-SHUTDOWN_CIRCLE_RADIUS, 0, 0,
                                  -SHUTDOWN_CIRCLE_RADIUS};

static Window* shutdownWindow = nullptr;
static StaticIcon* shutdownAnim[4] = {nullptr};
static BitmapBuffer* shutdownSplashImg = nullptr;

void drawSleepBitmap()
{
  if (shutdownWindow) {
    shutdownWindow->clear();
  } else {
    shutdownWindow =
        new Window(MainWindow::instance(), {0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)});
    shutdownWindow->setWindowFlag(OPAQUE);
    etx_solid_bg(shutdownWindow->getLvObj(), COLOR_THEME_PRIMARY1_INDEX);
  }

  (new StaticIcon(shutdownWindow, 0, 0, ICON_SHUTDOWN, COLOR_THEME_PRIMARY2_INDEX))
      ->center(lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));

  LvglWrapper::instance()->run();
}

void cancelShutdownAnimation()
{
  if (shutdownWindow) {
    shutdownWindow->deleteLater();
    shutdownWindow = nullptr;
    for (int i = 0; i < 4; i += 1) shutdownAnim[i] = nullptr;
  }
}

void drawShutdownAnimation(uint32_t duration, uint32_t totalDuration,
                           const char* message)
{
  if (totalDuration == 0) return;

  if (shutdownWindow == nullptr) {
    shutdownWindow =
        new Window(MainWindow::instance(), {0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)});
    shutdownWindow->setWindowFlag(OPAQUE);
    etx_solid_bg(shutdownWindow->getLvObj(), COLOR_THEME_PRIMARY1_INDEX);

    if (sdMounted() && !shutdownSplashImg) {
      shutdownSplashImg = BitmapBuffer::loadBitmap(
          BITMAPS_PATH "/" SHUTDOWN_SPLASH_FILE, BMP_RGB565);
      if (shutdownSplashImg)
        shutdownSplashImg->addCanvas(shutdownWindow);
    }

    (new StaticIcon(shutdownWindow, 0, 0, ICON_SHUTDOWN, COLOR_THEME_PRIMARY2_INDEX))
        ->center(lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));

    for (int i = 0; i < 4; i += 1) {
      shutdownAnim[i] = new StaticIcon(
          shutdownWindow, lv_disp_get_hor_res(nullptr) / 2 + bmp_shutdown_xo[i],
          lv_disp_get_ver_res(nullptr) / 2 + bmp_shutdown_yo[i],
          (EdgeTxIcon)(ICON_SHUTDOWN_CIRCLE0 + i), COLOR_THEME_PRIMARY2_INDEX);
    }
  }

  int quarter = 4 - (duration * 5) / totalDuration;
  if (quarter < 0) quarter = 0;
  for (int i = 3; i >= quarter; i -= 1) shutdownAnim[i]->hide();

  LvglWrapper::instance()->run();
}

void drawFatalErrorScreen(const char* message)
{
  static Window* fatalErrorWindow = nullptr;

  if (!fatalErrorWindow) {
    fatalErrorWindow =
        new Window(MainWindow::instance(), {0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)});
    fatalErrorWindow->setWindowFlag(OPAQUE);
    etx_solid_bg(fatalErrorWindow->getLvObj(), COLOR_BLACK_INDEX);

    new StaticText(fatalErrorWindow, rect_t{0, lv_disp_get_ver_res(nullptr) / 2 - EdgeTxStyles::STD_FONT_HEIGHT, lv_disp_get_hor_res(nullptr), EdgeTxStyles::STD_FONT_HEIGHT * 2},
                   message, COLOR_WHITE_INDEX, FONT(XL) | CENTERED);
  }

  backlightEnable(100);
  LvglWrapper::instance()->run();
}

void runFatalErrorScreen(const char* message)
{
  lcdInitDisplayDriver();

  drawFatalErrorScreen(message);

  // On startup wait for power button to be released
  while (pwrPressed()) {
    WDG_RESET();
  }

  while (true) {
    drawFatalErrorScreen(message);
    WDG_RESET();

    // loop as long as PWR button is pressed
    while (true) {
      uint32_t pwr_check = pwrCheck();
      if (pwr_check == e_power_off) {
        boardOff();
        return;  // only happens in SIMU, required for proper shutdown
      } else if (pwr_check == e_power_on) {
        break;
      }
      WDG_RESET();
    }
  }
}
