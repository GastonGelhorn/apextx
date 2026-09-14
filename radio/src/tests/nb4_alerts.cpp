/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The full-screen warnings the radio raises at boot, rendered as the firmware
// draws them and written out as PNGs so the design can be reviewed. Also
// checks the part that is not visual: an alert with a setting behind it offers
// to open that setting, and the route is taken by the next frame of the main
// loop rather than from inside the alert's own nested loop.

#include "gtests.h"

#if defined(RADIO_NB4_FAMILY)

#include "fullscreen_dialog.h"
#include "layer.h"
#include "mainwindow.h"
#include "model_init.h"
#include "nb4_capture.h"
#include "nb4_car_state.h"
#include "nb4_routes.h"
#include "theme_manager.h"

namespace {

using nb4capture::render;
using nb4capture::savePng;

struct Bench {
  MainWindow* root;
  lv_obj_t* originalScreen;
  rect_t originalRect;
  char originalLanguage[sizeof(g_eeGeneral.uiLanguage)];
  const LangStrings* originalStrings;

  Bench()
  {
    SYSTEM_RESET();
    MODEL_RESET();
    nb4VisualDefaults();
    strAppend(g_model.header.name, "Noble RC - Circuito", LEN_MODEL_NAME);

    memcpy(originalLanguage, g_eeGeneral.uiLanguage, sizeof(originalLanguage));
    originalStrings = currentLangStrings;

    root = MainWindow::instance();
    originalScreen = lv_scr_act();
    originalRect = root->getRect();
    root->setActiveScreen();
    nb4capture::install();
    ThemePersistance::instance()->refresh();
    applyPalette("ApexTX Dark");
  }

  ~Bench()
  {
    memcpy(g_eeGeneral.uiLanguage, originalLanguage, sizeof(originalLanguage));
    currentLangStrings = originalStrings;
    lcdSetOrientation(false);
    root->setRect(originalRect);
    lv_scr_load(originalScreen);
    lv_tick_inc(50);
    lv_timer_handler();
    nb4capture::restore();
  }

  void language(const char* tag)
  {
    memcpy(g_eeGeneral.uiLanguage, tag, 2);
    currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  }

  void orient(bool landscape)
  {
    ASSERT_TRUE(lcdSetOrientation(landscape));
    root->setRect({0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)});
  }

  void applyPalette(const char* name)
  {
    auto themes = ThemePersistance::instance();
    auto names = themes->getNames();
    for (unsigned i = 0; i < names.size(); ++i)
      if (names[i] == name) {
        themes->setDefaultTheme(i);
        themes->applyTheme(i);
        return;
      }
    FAIL();
  }
};

// Walks the dialog and returns every label it shows.
void collectLabels(lv_obj_t* obj, std::vector<std::string>& out)
{
  if (lv_obj_check_type(obj, &lv_label_class)) {
    const char* txt = lv_label_get_text(obj);
    if (txt && *txt) out.push_back(txt);
  }
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c)
    collectLabels(lv_obj_get_child(obj, c), out);
}

bool shows(lv_obj_t* obj, const char* text)
{
  std::vector<std::string> labels;
  collectLabels(obj, labels);
  return std::find(labels.begin(), labels.end(), std::string(text)) != labels.end();
}

void expectLabelsInsideDialog(lv_obj_t* obj)
{
  if (lv_obj_check_type(obj, &lv_label_class)) {
    SCOPED_TRACE(lv_label_get_text(obj));
    lv_area_t label;
    lv_obj_get_coords(obj, &label);
    for (auto parent = lv_obj_get_parent(obj); parent;
         parent = lv_obj_get_parent(parent)) {
      lv_area_t bounds;
      lv_obj_get_coords(parent, &bounds);
      EXPECT_GE(label.x1, bounds.x1);
      EXPECT_GE(label.y1, bounds.y1);
      EXPECT_LE(label.x2, bounds.x2);
      EXPECT_LE(label.y2, bounds.y2);
    }
  }
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c)
    expectLabelsInsideDialog(lv_obj_get_child(obj, c));
}

bool clickLabel(lv_obj_t* obj, const char* text)
{
  if (lv_obj_check_type(obj, &lv_label_class)) {
    const char* t = lv_label_get_text(obj);
    if (t && std::string(t) == text) {
      lv_obj_t* up = lv_obj_get_parent(obj);
      for (unsigned d = 0; up && d < 4; ++d) {
        if (lv_obj_has_class(up, &lv_btn_class)) {
          lv_event_send(up, LV_EVENT_CLICKED, nullptr);
          return true;
        }
        up = lv_obj_get_parent(up);
      }
    }
  }
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c)
    if (clickLabel(lv_obj_get_child(obj, c), text)) return true;
  return false;
}

std::string goToText(const char* section)
{
  char text[64];
  snprintf(text, sizeof(text), STR_NB4_GO_TO, section);
  return text;
}

}  // namespace

TEST(Nb4Alerts, EveryBootWarningRendersAndOffersItsSettingWhereThereIsOne)
{
  Bench bench;
  const auto dir =
      nb4capture::outputDirectory("NB4_ALERT_PREVIEW_DIR", "nb4-alert-previews");
  printf("NB4 alert previews: %s\n", dir.string().c_str());

  struct Case {
    const char* id;
    uint8_t type;
    const char* title;
    const char* message;
    const char* action;
    const char* route;
  };

  for (bool landscape : {false, true}) {
    bench.orient(landscape);
    const char* shape = landscape ? "480x320" : "320x480";
    for (const char* language : {"en", "es"}) {
      bench.language(language);
      // Resolve STR_* after switching language: the tables are selected at
      // runtime, so pointers captured beforehand would keep the old language.
      const Case cases[] = {
          {"failsafe", WARNING_TYPE_ALERT, STR_FAILSAFEWARN, STR_NO_FAILSAFE,
           STR_PRESS_ANY_KEY_TO_SKIP, "settings/receiver_rf/module"},
          {"throttle", WARNING_TYPE_ALERT, STR_THROTTLE_UPPERCASE,
           STR_THROTTLE_NOT_IDLE, STR_PRESS_ANY_KEY_TO_SKIP, "settings/car/safety"},
          {"switches", WARNING_TYPE_ALERT, STR_SWITCHWARN, "SW1 SW2", "",
           "settings/car/safety"},
          {"storage", WARNING_TYPE_ALERT, STR_SD_CARD, STR_SDCARD_FULL, "",
           "settings/system/storage"},
          {"radio-data", WARNING_TYPE_ALERT, STR_STORAGE_WARNING,
           STR_RADIO_DATA_UNRECOVERABLE, "", "settings/system/storage"},
          {"alarms", WARNING_TYPE_ALERT, STR_ALARMSWARN, STR_ALARMSDISABLED, "",
           "settings/sound_alerts/sound"},
          {"rtc-battery", WARNING_TYPE_ALERT, STR_BATTERY,
           STR_WARN_RTC_BATTERY_LOW, "", "settings/system/hardware"},
          {"keystuck", WARNING_TYPE_ALERT, STR_KEYSTUCK, "SW1", "", nullptr},
          {"confirm", WARNING_TYPE_CONFIRM, STR_NB4_RESET_RACE,
           STR_NB4_RESET_RACE_ASK, "", nullptr},
          {"info", WARNING_TYPE_INFO, STR_NB4_SAVING_RUN, STR_NB4_LOADING, "", nullptr},
      };
      for (const auto& c : cases) {
        SCOPED_TRACE(std::string(c.id) + " " + language + " " + shape);
        auto dialog = new FullScreenDialog(c.type, c.title, c.message, c.action);
        for (int f = 0; f < 4; ++f) render(bench.root);

        lv_obj_t* obj = dialog->getLvObj();
        expectLabelsInsideDialog(obj);
        if (*c.title) EXPECT_TRUE(shows(obj, c.title)) << c.id;
        if (*c.message) EXPECT_TRUE(shows(obj, c.message)) << c.id;

        const Nb4AlertLink* link = c.type == WARNING_TYPE_ALERT
                                       ? nb4AlertLink(c.title, c.message)
                                       : nullptr;
        EXPECT_EQ(link != nullptr, c.route != nullptr) << c.id;
        if (link) {
          EXPECT_STREQ(link->path, c.route) << c.id;
          // The old "press any key" text is gone: the alert now names the
          // setting it can open and offers to skip.
          EXPECT_TRUE(shows(obj, goToText(link->label()).c_str())) << c.id;
          EXPECT_TRUE(shows(obj, STR_NB4_SKIP_FOR_NOW)) << c.id;
          EXPECT_FALSE(shows(obj, STR_PRESS_ANY_KEY_TO_SKIP)) << c.id;
        } else if (c.type == WARNING_TYPE_ALERT) {
          EXPECT_TRUE(shows(obj, STR_NB4_GOT_IT)) << c.id;
        } else if (c.type == WARNING_TYPE_CONFIRM) {
          EXPECT_TRUE(shows(obj, STR_OK)) << c.id;
          EXPECT_TRUE(shows(obj, STR_CANCEL)) << c.id;
        }

        const std::string name =
            std::string(c.id) + "-" + language + "-" + shape + ".png";
        const unsigned colours =
            savePng(dir / name,
                    {0, 0, (coord_t)lv_disp_get_hor_res(nullptr),
                     (coord_t)lv_disp_get_ver_res(nullptr)});
        // Background, badge, title, message and a button: a flat image means
        // the dialog drew nothing.
        EXPECT_GE(colours, 6u) << name;

        dialog->deleteLater();
        render(bench.root);
      }
    }
  }
  EXPECT_EQ(lv_mem_test(), LV_RES_OK);
}

TEST(Nb4Alerts, GoToOpensTheSettingOnceTheMainLoopOwnsTheScreen)
{
  Bench bench;
  bench.orient(false);
  bench.language("en");

  const Nb4AlertLink* link = nb4AlertLink(STR_FAILSAFEWARN, STR_NO_FAILSAFE);
  ASSERT_NE(link, nullptr);
  EXPECT_STREQ(link->path, "settings/receiver_rf/module");

  auto base = Layer::back();
  auto dialog = new FullScreenDialog(WARNING_TYPE_ALERT, STR_FAILSAFEWARN,
                                     STR_NO_FAILSAFE, STR_PRESS_ANY_KEY_TO_SKIP);
  for (int f = 0; f < 4; ++f) render(bench.root);

  // Pressing "Go to Receiver" only closes the alert: nothing may be opened
  // from inside the alert's nested loop.
  ASSERT_TRUE(clickLabel(dialog->getLvObj(), goToText(link->label()).c_str()));
  for (int f = 0; f < 4; ++f) render(bench.root);
  EXPECT_EQ(Layer::back(), base);

  // The main loop then takes the route, once.
  EXPECT_TRUE(nb4RunDeferredRoute());
  for (int f = 0; f < 4; ++f) render(bench.root);
  EXPECT_NE(Layer::back(), base);
  EXPECT_FALSE(nb4RunDeferredRoute());

  while (Layer::back() != base) {
    Layer::back()->deleteLater();
    render(bench.root);
  }
}

TEST(Nb4Alerts, SkipForNowLeavesEverythingClosed)
{
  Bench bench;
  bench.orient(false);
  bench.language("es");

  auto base = Layer::back();
  auto dialog = new FullScreenDialog(WARNING_TYPE_ALERT, STR_FAILSAFEWARN,
                                     STR_NO_FAILSAFE, STR_PRESS_ANY_KEY_TO_SKIP);
  for (int f = 0; f < 4; ++f) render(bench.root);

  ASSERT_TRUE(clickLabel(dialog->getLvObj(), STR_NB4_SKIP_FOR_NOW));
  for (int f = 0; f < 4; ++f) render(bench.root);

  EXPECT_EQ(Layer::back(), base);
  EXPECT_FALSE(nb4RunDeferredRoute());
}

#endif  // RADIO_NB4_FAMILY
