/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Renders every NB4 car widget in the two zone shapes a user meets most often
// and writes a PNG of each, so a widget can be reviewed as the firmware draws
// it rather than as a mock-up. Set NB4_WIDGET_PREVIEW_DIR to choose where the
// images go; without it they land in the system temp directory.

#include "gtests.h"

#if defined(RADIO_NB4_FAMILY)

#include "mainwindow.h"
#include "theme_manager.h"
#include "model_init.h"
#include "nb4_racing.h"
#include "nb4_car_state.h"
#include "widget.h"
#include "nb4_capture.h"
#include "hal/adc_driver.h"

namespace {

using nb4capture::frame;
using nb4capture::render;
using nb4capture::savePng;

void racingTick(uint8_t tick) { nb4RaceTimerAdvance(tick); nb4RacingTick(tick); }

void seedLaps()
{
  nb4RacingReset();
  const uint32_t laps[] = {2534, 2487, 2512};
  for (uint32_t lap : laps) {
    uint32_t t = 0;
    for (; t + 10 <= lap; t += 10) racingTick(10);
    if (lap - t) racingTick(lap - t);
    nb4RacingMarkLap();
    racingTick(0);
  }
  for (uint32_t t = 0; t < 1230; t += 10) racingTick(10);
}

struct Bench {
  MainWindow* root;
  lv_obj_t* originalScreen;
  rect_t originalRect;
  char originalLanguage[sizeof(g_eeGeneral.uiLanguage)];
  const LangStrings* originalStrings;

  Bench()
  {
    SYSTEM_RESET(); MODEL_RESET(); loadCurves(); nb4RacingReset();
    nb4VisualDefaults();
    nb4RacingDefaults(g_model.nb4Racing);
    strAppend(g_model.header.name, "Noble RC - Circuito", LEN_MODEL_NAME);
    setDefaultInputs();
    for (uint8_t i = 0; i < 3; ++i) {
      g_model.mixData[i].srcRaw = MIXSRC_FIRST_INPUT + i;
      g_model.mixData[i].destCh = i;
      g_model.mixData[i].weight = makeSourceNumVal(100);
    }
    channelOutputs[0] = RESX / 3;
    channelOutputs[1] = -RESX / 2;
    calibratedAnalogs[ADC_MAIN_ST] = -RESX / 3;
    calibratedAnalogs[ADC_MAIN_TH] = -RESX / 2;
    g_vbat100mV = 39;
    setTrimValue(0, ADC_MAIN_ST, 12);
    setTrimValue(0, ADC_MAIN_TH, -6);
    telemetryData.rssi.set(83);
    telemetryStreaming = TELEMETRY_TIMEOUT10ms;
    seedLaps();

    memcpy(originalLanguage, g_eeGeneral.uiLanguage, sizeof(originalLanguage));
    originalStrings = currentLangStrings;
    memcpy(g_eeGeneral.uiLanguage, "es", 2);
    currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];

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
    nb4RacingReset();
    lcdSetOrientation(false);
    root->setRect(originalRect);
    lv_scr_load(originalScreen);
    lv_tick_inc(50);
    lv_timer_handler();
    nb4capture::restore();
  }

  void orient(bool landscape)
  {
    ASSERT_TRUE(lcdSetOrientation(landscape));
    coord_t w = lv_disp_get_hor_res(nullptr), h = lv_disp_get_ver_res(nullptr);
    root->setRect({0, 0, w, h});
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

const char* const kWidgets[] = {
    "NB4Trims", "NB4Lap", "NB4Link", "NB4Battery", "NB4Session", "NB4Sensor",
    "NB4SteerDial", "NB4SteerBar", "NB4ThrColumn", "NB4ThrDial",
};

}  // namespace

TEST(Nb4Widgets, EveryCarWidgetRendersInBothZoneShapes)
{
  Bench bench;
  struct Zone { bool landscape; rect_t rect; const char* tag; };
  const Zone zones[] = {
      {false, {8, 60, 148, 205}, "148x205"},
      {true, {8, 60, 464, 125}, "464x125"},
  };
  const auto dir = nb4capture::outputDirectory("NB4_WIDGET_PREVIEW_DIR",
                                              "nb4-widget-previews");
  printf("NB4 widget previews: %s\n", dir.string().c_str());

  for (const auto& z : zones) {
    bench.orient(z.landscape);
    for (const char* id : kWidgets) {
      auto factory = WidgetFactory::getWidgetFactory(id);
      ASSERT_NE(factory, nullptr) << id;

      auto bg = new Window(bench.root, {0, 0, lv_disp_get_hor_res(nullptr),
                                        lv_disp_get_ver_res(nullptr)});
      etx_solid_bg(bg->getLvObj(), COLOR_THEME_SECONDARY3_INDEX);
      auto widget = factory->create(bg, z.rect, 0, 0);
      ASSERT_NE(widget, nullptr) << id;
      // MainWindow::run only drives the top layer, so a plain host window has
      // to forward checkEvents itself: first draw runs delayedInit, then the
      // widget updates its dynamic content, then it is drawn again.
      for (int f = 0; f < 4; ++f) {
        render(bench.root);
        bg->checkEvents();
      }
      render(bench.root);

      const unsigned colours =
          savePng(dir / (std::string(id) + "-" + z.tag + ".png"), z.rect);
      // A tile, its text and at least one accent: fewer colours means the
      // widget drew nothing worth looking at.
      EXPECT_GE(colours, 4u) << id << " " << z.tag;

      bg->deleteLater();
      render(bench.root);
    }
  }
  EXPECT_EQ(lv_mem_test(), LV_RES_OK);
}

#endif
