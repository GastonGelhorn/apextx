/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "gtests.h"

#if defined(RADIO_NB4_FAMILY) && defined(COLORLCD)

#include "layer.h"
#include "mainwindow.h"
#include "view_main.h"
#include "view_channels.h"
#include "theme_manager.h"
#include "pagegroup.h"
#include "radio_setup.h"
#include "model_nb4_racing.h"
#include "mixes.h"
#include "model_init.h"
#include "quick_menu.h"
#include "nb4_routes.h"
#include "nb4_controls.h"
#include "nb4_assignments.h"
#include "nb4_racing.h"
#include "nb4_axis.h"
#include "nb4_pit.h"
#include "nb4_palettes.h"
#include "bitmaps.h"
#include "nb4_home.h"
#include "nb4_ui.h"
#include "nb4_home_templates.h"
#include "nb4_health.h"
#include "timers.h"
#include "nb4_history.h"
#include "nb4_model_compat.h"
#include "output_edit.h"
#include "throttle_params.h"
#include "model/nb4_params.h"
#include "curveedit.h"
#include "input_edit.h"
#include "module_setup.h"
#include "timer_setup.h"
#include "dialog.h"
#include "menu.h"
#include "textedit.h"
#include "keyboard_base.h"
#include "fullscreen_dialog.h"
#include "table.h"
#include "radio_calibration.h"
#include "location.h"
#include "hal/adc_driver.h"
#if defined(RADIO_NB4) && defined(AFHDS3)
#include "pulses/afhds3.h"
#include "pulses/afhds3_nb4.h"
#include "pulses/afhds3_transport.h"
#include "targets/pl18/nb4_rf_controller.h"
#endif
#include "fonts.h"
#include "storage/modelslist.h"
#if defined(LUA)
#include "lua/lua_api.h"
#endif
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

void guiMain(event_t event);

namespace {
void racingTick(uint8_t tick) { nb4RaceTimerAdvance(tick); nb4RacingTick(tick); }

double channelLuminance(uint8_t value)
{
  const double s = value / 255.0;
  return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}

double colorLuminance(uint32_t rgb)
{
  return 0.2126 * channelLuminance((rgb >> 16) & 0xff) +
         0.7152 * channelLuminance((rgb >> 8) & 0xff) +
         0.0722 * channelLuminance(rgb & 0xff);
}

double contrastRatio(uint32_t a, uint32_t b)
{
  const double first = colorLuminance(a);
  const double second = colorLuminance(b);
  const double light = std::max(first, second);
  const double dark = std::min(first, second);
  return (light + 0.05) / (dark + 0.05);
}

std::vector<lv_color_t> frame;

unsigned long flushedPixels = 0;
unsigned flushedAreas = 0;
lv_area_t flushedBounds = {0, 0, -1, -1};

void resetFlushAccounting()
{
  flushedPixels = 0;
  flushedAreas = 0;
  flushedBounds = {0, 0, -1, -1};
}

void captureFrame(lv_disp_drv_t* d, const lv_area_t* area, lv_color_t* pixels)
{
  const int w = d->hor_res, h = d->ver_res;

  flushedPixels += (unsigned long)(area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
  if (flushedAreas++ == 0) flushedBounds = *area;
  else {
    flushedBounds.x1 = std::min(flushedBounds.x1, area->x1);
    flushedBounds.y1 = std::min(flushedBounds.y1, area->y1);
    flushedBounds.x2 = std::max(flushedBounds.x2, area->x2);
    flushedBounds.y2 = std::max(flushedBounds.y2, area->y2);
  }
  frame.resize(w * h);
  if (d->direct_mode) {
    memcpy(frame.data(), pixels, w * h * sizeof(lv_color_t));
  } else {
    for (int y = area->y1; y <= area->y2; ++y)
      memcpy(frame.data() + y * w + area->x1,
             pixels + (y - area->y1) * (area->x2 - area->x1 + 1),
             (area->x2 - area->x1 + 1) * sizeof(lv_color_t));
  }
  lv_disp_flush_ready(d);
}

void saveFrame(const char* name, int w, int h)
{
  const char* directory = getenv("NB4_SCREENSHOT_DIR");
  if (!directory) return;
  std::string path = std::string(directory) + "/" + name + ".ppm";
  auto file = fopen(path.c_str(), "wb");
  ASSERT_NE(file, nullptr);
  fprintf(file, "P6\n%d %d\n255\n", w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const auto& pixel =
          frame[w > h ? (LCD_PHYS_H - 1 - x) * LCD_PHYS_W + y : y * LCD_PHYS_W + x];
      auto rgb = lv_color_to32(pixel);
      unsigned char data[] = {static_cast<unsigned char>(rgb >> 16),
                              static_cast<unsigned char>(rgb >> 8),
                              static_cast<unsigned char>(rgb)};
      fwrite(data, 1, 3, file);
    }
  fclose(file);
}

void renderIncremental(MainWindow* root)
{
  lv_obj_update_layout(root->getLvObj());
  lv_tick_inc(50);
  lv_timer_handler();
  root->run();
}

void expectGlyphsOf(const char* txt, const lv_font_t* font, const char* where)
{
  for (uint32_t i = 0; txt && txt[i];) {
    uint32_t letter = _lv_txt_encoded_next(txt, &i);
    if (letter < 0x20) continue;
    lv_font_glyph_dsc_t dsc;
    EXPECT_TRUE(lv_font_get_glyph_dsc(font, &dsc, letter, 0));
  }
}

void expectEveryGlyphRenderable(lv_obj_t* obj, const char* where)
{
  if (lv_obj_check_type(obj, &lv_label_class)) {
    expectGlyphsOf(lv_label_get_text(obj),
                   lv_obj_get_style_text_font(obj, LV_PART_MAIN), where);
  }

  if (lv_obj_has_class(obj, &lv_table_class)) {
    auto font = lv_obj_get_style_text_font(obj, LV_PART_ITEMS);
    uint16_t rows = lv_table_get_row_cnt(obj), cols = lv_table_get_col_cnt(obj);
    for (uint16_t r = 0; r < rows; ++r)
      for (uint16_t c = 0; c < cols; ++c)
        expectGlyphsOf(lv_table_get_cell_value(obj, r, c), font, where);
  }
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c)
    expectEveryGlyphRenderable(lv_obj_get_child(obj, c), where);
}

bool openHelpSheet(lv_obj_t* obj)
{
  if (lv_obj_check_type(obj, &lv_label_class)) {
    const char* txt = lv_label_get_text(obj);
    if (txt && txt[0] == '?' && lv_obj_get_parent(obj)) {
      lv_event_send(lv_obj_get_parent(obj), LV_EVENT_CLICKED, nullptr);
      return true;
    }
  }
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c)
    if (openHelpSheet(lv_obj_get_child(obj, c))) return true;
  return false;
}

void render(MainWindow* root)
{
  lv_obj_invalidate(root->getLvObj());
  lv_obj_update_layout(root->getLvObj());
  lv_tick_inc(50);
  lv_timer_handler();
  root->run();
}

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

struct Scene {
  MainWindow* root;
  lv_obj_t* originalScreen;
  rect_t originalRect;
  void (*originalFlush)(lv_disp_drv_t*, const lv_area_t*, lv_color_t*);

  char originalLanguage[sizeof(g_eeGeneral.uiLanguage)];
  const LangStrings* originalStrings;

  Scene()
  {
    SYSTEM_RESET(); MODEL_RESET(); loadCurves(); nb4RacingReset();
    nb4HealthClearPrevious();
    nb4HealthInit();
    nb4VisualDefaults();
    nb4RacingDefaults(g_model.nb4Racing);
    strAppend(g_model.header.name, "Noble RC - Circuito", LEN_MODEL_NAME);

    setDefaultInputs();
    for (uint8_t i = 0; i < 3; ++i) {
      g_model.mixData[i].srcRaw = MIXSRC_FIRST_INPUT + i;
      g_model.mixData[i].destCh = i;
      g_model.mixData[i].weight = makeSourceNumVal(100);
    }
    g_model.timers[0].mode = TMRMODE_ON;
    timersStates[0].val = 127;
    channelOutputs[0] = RESX / 3;
    channelOutputs[1] = -RESX / 2;
    channelOutputs[2] = RESX / 4;
    ex_chans[0] = RESX / 3;
    ex_chans[1] = -RESX / 2;
    ex_chans[2] = RESX / 4;
    calibratedAnalogs[ADC_MAIN_ST] = -RESX / 3;
    calibratedAnalogs[ADC_MAIN_TH] = -RESX / 2;
    g_vbat100mV = 39;  // One 18650 cell, as used by the NB4
    setTrimValue(0, 0, 12);
    setTrimValue(0, 1, -6);
    memcpy(originalLanguage, g_eeGeneral.uiLanguage, sizeof(originalLanguage));
    originalStrings = currentLangStrings;
    root = MainWindow::instance();
    originalScreen = lv_scr_act();
    originalRect = root->getRect();
    auto driver = lv_disp_get_default()->driver;
    originalFlush = driver->flush_cb;
    root->setActiveScreen();
    driver->flush_cb = captureFrame;
    ThemePersistance::instance()->refresh();
  }

  ~Scene()
  {
    memcpy(g_eeGeneral.uiLanguage, originalLanguage, sizeof(originalLanguage));
    currentLangStrings = originalStrings;
    nb4RacingReset();
    lcdSetOrientation(false);
    root->setRect(originalRect);
    lv_scr_load(originalScreen);
    lv_tick_inc(50);
    lv_timer_handler();
    lv_disp_get_default()->driver->flush_cb = originalFlush;
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

}  // namespace

TEST(Nb4CarState, DefaultModelDrivesSteeringAndThrottle)
{
  SYSTEM_RESET();
  setModelDefaults(0);
  unsigned mixes = 0;
  for (unsigned i = 0; i < MAX_MIXERS; ++i)
    if (g_model.mixData[i].srcRaw) ++mixes;
  EXPECT_GE(mixes, 2u);
  const auto& s = nb4ReadCarState();
  EXPECT_TRUE(s.channels[s.steeringChannel].assigned);
  EXPECT_TRUE(s.channels[s.throttleChannel].assigned);
}

TEST(Nb4CarState, DisabledTrimsAreAbsentAndEnabledTrimsKeepTheirValues)
{
  SYSTEM_RESET(); MODEL_RESET();
  ASSERT_TRUE(setTrimValue(0, 0, 12));
  ASSERT_TRUE(setTrimValue(0, 1, -6));
  const auto state = nb4ReadCarState();
  EXPECT_EQ(state.steeringTrim.validity, Nb4Validity::Valid);
  EXPECT_EQ(state.steeringTrim.value, 12);
  EXPECT_EQ(state.throttleTrim.value, -6);
  flightModeAddress(0)->trim[0].mode = TRIM_MODE_NONE;
  flightModeAddress(0)->trim[1].mode = TRIM_MODE_3POS;
  const auto disabled = nb4ReadCarState();
  EXPECT_EQ(disabled.steeringTrim.validity, Nb4Validity::Absent);
  EXPECT_EQ(disabled.throttleTrim.validity, Nb4Validity::Absent);
}

TEST(Nb4CarState, HomeGaugesFollowPhysicalControlsAndSteeringDirection)
{
  SYSTEM_RESET(); MODEL_RESET(); nb4RacingDefaults(g_model.nb4Racing);
  calibratedAnalogs[ADC_MAIN_ST] = RESX / 2;
  calibratedAnalogs[ADC_MAIN_TH] = -RESX / 4;
  ex_chans[0] = -RESX; ex_chans[1] = RESX;  // Must not move the instruments
  const auto state = nb4ReadCarState();
  EXPECT_EQ(state.steeringInput.value, -50);
  EXPECT_EQ(state.throttleInput.value, -25);
  EXPECT_EQ(state.steeringInput.validity, Nb4Validity::Valid);
  EXPECT_EQ(state.throttleInput.validity, Nb4Validity::Valid);
}

TEST(Nb4CarState, HomeTimerIsOptionalAndMirrorsNativeCountdown)
{
  SYSTEM_RESET(); MODEL_RESET(); nb4RacingDefaults(g_model.nb4Racing);
  g_model.timers[0].mode = TMRMODE_THR_START;
  g_model.timers[0].start = 600;
  timersStates[0].val = 527;
  timersStates[0].state = TMR_RUNNING;
  auto state = nb4ReadCarState();
  EXPECT_TRUE(state.homeTimerVisible);
  EXPECT_FALSE(state.homeShowsRace);
  EXPECT_EQ(state.homeTimerIndex, 0);
  EXPECT_TRUE(state.homeTimerCountdown);
  EXPECT_EQ(state.homeTimer.value, 527);
  EXPECT_EQ(state.homeTimerState, TMR_RUNNING);

  g_model.nb4Racing.homeTimer = NB4_HOME_TIMER_2;
  state = nb4ReadCarState();
  EXPECT_TRUE(state.homeTimerVisible);
  EXPECT_EQ(state.homeTimerIndex, 1);
  EXPECT_EQ(state.homeTimer.validity, Nb4Validity::Absent);

  g_model.nb4Racing.homeTimer = NB4_HOME_TIMER_HIDDEN;
  EXPECT_FALSE(nb4ReadCarState().homeTimerVisible);

  g_model.nb4Racing.homeTimer = NB4_HOME_TIMER_AUTO;
  g_model.nb4Racing.lapSw = SWSRC_FIRST;
  state = nb4ReadCarState();
  EXPECT_TRUE(state.homeTimerVisible);
  EXPECT_TRUE(state.homeShowsRace);
}

TEST(Nb4Ux, PalettesAreBuiltinAndAccentOverridesFocus)
{
  ASSERT_GE(nb4PaletteCount(), 7u);
  EXPECT_EQ(nb4PaletteIndexByName("ApexTX Dark"), 0);
  EXPECT_EQ(nb4PaletteIndexByName("ApexTX Light"), 1);
  EXPECT_EQ(nb4PaletteIndexByName("NB4 Dark"), 0);
  EXPECT_EQ(nb4PaletteIndexByName("NB4 Light"), 1);
  EXPECT_EQ(nb4PaletteIndexByName("NB4 Noche"), 0);
  EXPECT_EQ(nb4PaletteIndexByName("NB4 Día"), 1);
  EXPECT_EQ(nb4PaletteIndexByName("Otra"), -1);
  for (unsigned i = 0; i < nb4PaletteCount(); ++i)
    for (unsigned j = i + 1; j < nb4PaletteCount(); ++j)
      EXPECT_STRNE(nb4Palette(i).name, nb4Palette(j).name);

  Scene scene;
  auto themes = ThemePersistance::instance();
  ASSERT_GE(themes->getNames().size(), nb4PaletteCount() + 1);
  // The ApexTX palettes come first and the classic theme sits behind them. Index 0
  // is both the factory default and where loadDefaultTheme() falls back when a
  // stored selectedTheme name no longer resolves, so a track radio must find a
  // dark palette there rather than a light one.
  for (unsigned i = 0; i < nb4PaletteCount(); ++i)
    EXPECT_EQ(themes->getNames()[i], nb4Palette(i).name);
  EXPECT_EQ(themes->getNames()[nb4PaletteCount()], "Classic");
  EXPECT_FALSE(themes->deleteThemeByIndex(0));

  strAppend(g_eeGeneral.selectedTheme, "NB4 Noche", SELECTED_THEME_NAME_LEN);
  themes->loadDefaultTheme();
  EXPECT_STREQ(g_eeGeneral.selectedTheme, "ApexTX Dark");
  EXPECT_EQ(themes->getCurrentTheme()->getName(), "ApexTX Dark");

  g_eeGeneral.nb4Accent = 0;
  scene.applyPalette("ApexTX Green");
  const uint32_t focus = nb4Palette(nb4PaletteIndexByName("ApexTX Green")).colors[COLOR_THEME_FOCUS_INDEX];
  EXPECT_EQ(lcdColorTable[COLOR_THEME_FOCUS_INDEX],
            RGB((focus >> 16) & 255, (focus >> 8) & 255, focus & 255));

  ASSERT_GE(nb4AccentCount(), 9u);
  g_eeGeneral.nb4Accent = 2;
  themes->applyTheme(themes->getThemeIndex());
  const uint32_t accent = nb4AccentRgb(2);
  EXPECT_EQ(lcdColorTable[COLOR_THEME_FOCUS_INDEX],
            RGB((accent >> 16) & 255, (accent >> 8) & 255, accent & 255));
  g_eeGeneral.nb4Accent = 0;
  themes->applyTheme(themes->getThemeIndex());
  EXPECT_EQ(lcdColorTable[COLOR_THEME_FOCUS_INDEX],
            RGB((focus >> 16) & 255, (focus >> 8) & 255, focus & 255));
}

TEST(Nb4Ux, HomeRepaintsNothingWhenNothingMovesAndOnlyTheDialThatDid)
{
  Scene scene;
  auto home = new Nb4HomeScreen(scene.root, {0, 0, LCD_W, LCD_H});
  render(scene.root);

  home->checkEvents(); renderIncremental(scene.root);
  home->checkEvents(); renderIncremental(scene.root);

  resetFlushAccounting();
  home->checkEvents(); renderIncremental(scene.root);
  EXPECT_EQ(flushedPixels, 0u);

  calibratedAnalogs[ADC_MAIN_ST] = RESX / 2;
  resetFlushAccounting();
  home->checkEvents(); renderIncremental(scene.root);

  EXPECT_GT(flushedPixels, 0u);
  EXPECT_LT(flushedPixels, (unsigned long)LCD_W * LCD_H / 3);
  const lv_area_t steerDirty = flushedBounds;

  calibratedAnalogs[ADC_MAIN_TH] = RESX / 3;
  resetFlushAccounting();
  home->checkEvents(); renderIncremental(scene.root);
  EXPECT_GT(flushedPixels, 0u);
  const bool sameArea = flushedBounds.x1 == steerDirty.x1 &&
                        flushedBounds.y1 == steerDirty.y1 &&
                        flushedBounds.x2 == steerDirty.x2 &&
                        flushedBounds.y2 == steerDirty.y2;
  EXPECT_FALSE(sameArea);
  calibratedAnalogs[ADC_MAIN_TH] = 0;

  printf("NB4 Home: idle 0 px; steering motion %lu px across %u areas of %ld (%lu%%)\n",
         flushedPixels, flushedAreas, (long)LCD_W * LCD_H,
         flushedPixels * 100 / ((unsigned long)LCD_W * LCD_H));

  calibratedAnalogs[ADC_MAIN_ST] = -RESX / 3;
  scene.orient(true);
  home->setRect({0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)});
  home->checkEvents(); render(scene.root);
  home->checkEvents(); renderIncremental(scene.root);
  home->checkEvents(); renderIncremental(scene.root);
  resetFlushAccounting();
  home->checkEvents(); renderIncremental(scene.root);
  EXPECT_EQ(flushedPixels, 0u);
  calibratedAnalogs[ADC_MAIN_ST] = RESX / 2;
  resetFlushAccounting();
  home->checkEvents(); renderIncremental(scene.root);
  EXPECT_GT(flushedPixels, 0u);
  EXPECT_LT(flushedPixels, (unsigned long)LCD_W * LCD_H / 3);
  printf("NB4 Home landscape: idle 0 px; steering motion %lu px across %u areas\n",
         flushedPixels, flushedAreas);

  home->deleteLater();
}

TEST(Nb4Ux, HomeHeaderFollowsTheModelNameAndItsLabels)
{
  Scene scene;
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  g_model.header.labels[0] = 0;
  auto home = new Nb4HomeScreen(scene.root, {0, 0, LCD_W, LCD_H});
  render(scene.root);
  auto headerCard = [&] { return lv_obj_get_child(home->getLvObj(), 0); };
  const unsigned bare = lv_obj_get_child_cnt(headerCard());

  strAppend(g_model.header.labels, "pista,electrico", LABELS_LENGTH - 1);
  home->checkEvents();
  render(scene.root);
  EXPECT_EQ(lv_obj_get_child_cnt(headerCard()), bare + 1);
  saveFrame("home-portrait-es-labels-dark", LCD_W, LCD_H);

  strAppend(g_model.header.name, "Corte del Norte", LEN_MODEL_NAME);
  home->checkEvents();
  render(scene.root);
  ASSERT_EQ(lv_mem_test(), LV_RES_OK);
  EXPECT_EQ(lv_obj_get_child_cnt(headerCard()), bare + 1);

  g_model.header.labels[0] = 0;
  home->checkEvents();
  render(scene.root);
  EXPECT_EQ(lv_obj_get_child_cnt(headerCard()), bare);
  home->deleteLater();
  scene.root->run();
}

TEST(Nb4Ux, HomeSettingsMenuFitsWhenTheRadioIsTurnedOrRelabelled)
{
  // Reopening the NB4 settings grid must use the current size and language.
  Scene scene;
  for (unsigned pass = 0; pass < 2; ++pass) {
    for (unsigned landscape = 0; landscape < 2; ++landscape) {
      scene.orient(landscape);
      ViewMain::instance()->openMenu();
      render(scene.root);
      auto menu = Layer::back();
      ASSERT_NE(menu, nullptr);
      auto box = lv_obj_get_child(menu->getLvObj(), 0);
      ASSERT_NE(box, nullptr);
      EXPECT_EQ(dynamic_cast<QuickMenu*>(menu), nullptr);
      EXPECT_LE(lv_obj_get_width(box), LCD_W);
      menu->onCancel();
      render(scene.root);
    }
  }

  scene.orient(false);
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance()->openMenu();
  render(scene.root);
  const auto spanish = frame;
  Layer::back()->onCancel();
  render(scene.root);

  memcpy(g_eeGeneral.uiLanguage, "en", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance()->openMenu();
  render(scene.root);
  bool changed = false;
  ASSERT_EQ(frame.size(), spanish.size());
  for (size_t i = 0; i < frame.size() && !changed; ++i)
    changed = lv_color_to32(frame[i]) != lv_color_to32(spanish[i]);
  EXPECT_TRUE(changed);
  Layer::back()->onCancel();
  render(scene.root);
}

TEST(Nb4Ux, QuickMenuFitsEveryEntryInBothOrientations)
{

  unsigned entries = 0;
  for (int i = 0; qmTopItems[i].icon != EDGETX_ICONS_COUNT; ++i)
    if (qmTopItems[i].pageAction == QM_ACTION) ++entries;
  EXPECT_GT(entries, 0u);

  const unsigned landscape = QuickMenu::QM_MAIN_COLS.l * QuickMenu::QM_MAIN_ROWS.l;
  const unsigned portrait = QuickMenu::QM_MAIN_COLS.p * QuickMenu::QM_MAIN_ROWS.p;
  EXPECT_LE(entries, landscape);
  EXPECT_LE(entries, portrait);

  EXPECT_LE(QuickMenu::QM_H.l, (coord_t)LCD_PHYS_W);
  EXPECT_LE(QuickMenu::QM_W.l, (coord_t)LCD_PHYS_H);
  EXPECT_LE(QuickMenu::QM_H.p, (coord_t)LCD_PHYS_H);
  EXPECT_LE(QuickMenu::QM_W.p, (coord_t)LCD_PHYS_W);
}

TEST(Nb4Ux, BuiltinIconTableIsAlignedWithItsEnum)
{

  const int at = etxBuiltinIconMisalignedAt();
  EXPECT_EQ(at, -1);
}

TEST(Nb4Ux, BatteryPercentFollowsTheOfficialCurveNotAStraightLine)
{

  EXPECT_EQ(nb4BatteryPercent(4150), 100);
  EXPECT_EQ(nb4BatteryPercent(3850), 75);
  EXPECT_EQ(nb4BatteryPercent(3700), 45);
  EXPECT_EQ(nb4BatteryPercent(3500), 15);
  EXPECT_EQ(nb4BatteryPercent(3300), 0);

  EXPECT_EQ(nb4BatteryPercent(4300), 100);
  EXPECT_EQ(nb4BatteryPercent(3000), 0);

  uint8_t previous = 0;
  for (uint16_t mv = 3300; mv <= 4150; ++mv) {
    const uint8_t pc = nb4BatteryPercent(mv);
    ASSERT_GE(pc, previous);
    ASSERT_LE(pc - previous, 1);
    previous = pc;
  }

  for (uint16_t mv = 3400; mv <= 3700; mv += 50) {
    const int line = (int)(((uint32_t)(mv - 3300) * 100 + 425) / 850);
    EXPECT_LE(nb4BatteryPercent(mv), line);
  }
  EXPECT_EQ(nb4BatteryPercent(3600), 30);   // A linear response would produce 35
}

TEST(Nb4Ux, InternalBatteryShowsChargingWhenTheExternalIsFeedingIt)
{

  Scene scene;

  scene.applyPalette("ApexTX Dark");
  auto home = new Nb4HomeScreen(scene.root, {0, 0, LCD_W, LCD_H});
  nb4SimuChargeSource = 0;
  home->checkEvents(); render(scene.root);
  const auto idle = frame;

  nb4SimuChargeSource = 2;  // USB
  home->checkEvents(); render(scene.root);
  ASSERT_EQ(frame.size(), idle.size());
  bool changed = false;
  for (size_t i = 0; i < frame.size() && !changed; ++i)
    changed = lv_color_to32(frame[i]) != lv_color_to32(idle[i]);
  EXPECT_TRUE(changed);
  saveFrame("home-portrait-es-cargando-dark", LCD_W, LCD_H);

  nb4SimuChargeSource = 0;
  home->checkEvents(); render(scene.root);
  bool back = true;
  for (size_t i = 0; i < frame.size() && back; ++i)
    back = lv_color_to32(frame[i]) == lv_color_to32(idle[i]);
  EXPECT_TRUE(back);
  home->deleteLater();
}

TEST(Nb4Ux, PalettePolarityMatchesWhatItsNamePromises)
{
  // Contrast is symmetric: ratio(ink, background) == ratio(background, ink). A
  // contrast-only check therefore approves a palette and its exact inverse
  // equally well, which is how "ApexTX Dark" once shipped with a white
  // background and still passed every assertion below. Polarity has to be
  // declared and then checked against the declaration.
  for (unsigned i = 0; i < nb4PaletteCount(); ++i) {
    const auto& palette = nb4Palette(i);
    const auto& c = palette.colors;
    const double page = colorLuminance(c[COLOR_THEME_SECONDARY3_INDEX]);
    const double control = colorLuminance(c[COLOR_THEME_PRIMARY2_INDEX]);
    const double pageInk = colorLuminance(c[COLOR_THEME_PRIMARY1_INDEX]);
    const double controlInk = colorLuminance(c[COLOR_THEME_SECONDARY1_INDEX]);

    if (palette.dark) {
      EXPECT_LT(page, 0.18);
      EXPECT_LT(control, 0.18);
    } else {
      EXPECT_GT(page, 0.5);
    }

    // Ink sits on the opposite side of its own background, whichever way the
    // palette leans. SECONDARY1 is the role that actually broke: it is both the
    // text of every native EdgeTX control and the knob of the toggle switch, so
    // a dark SECONDARY1 over a dark PRIMARY2 makes controls unreadable.
    EXPECT_EQ(palette.dark, pageInk > control);
    EXPECT_EQ(palette.dark, controlInk > control);
  }

  // Index 0 is both the factory default and where loadDefaultTheme() lands when
  // a stored theme name no longer resolves. On a track radio it must be dark.
  EXPECT_TRUE(nb4Palette(0).dark);
  const int dark = nb4PaletteIndexByName("ApexTX Dark");
  ASSERT_GE(dark, 0);
  EXPECT_TRUE(nb4Palette(dark).dark);
}

TEST(Nb4Ux, DarkCardsStayNeutralAndBarelyAboveBlackInTheRenderedFrame)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  auto card = new Window(scene.root, {20, 20, 80, 80});
  Nb4Ui::card(card->getLvObj());
  render(scene.root);
  unsigned red = 0, green = 0, blue = 0;
  for (unsigned y = 32; y < 64; ++y) {
    for (unsigned x = 32; x < 64; ++x) {
      const uint32_t pixel = lv_color_to32(frame[y * lv_disp_get_hor_res(nullptr) + x]);
      red += (pixel >> 16) & 255;
      green += (pixel >> 8) & 255;
      blue += pixel & 255;
    }
  }
  EXPECT_EQ(red, green);
  EXPECT_EQ(red, blue);
  EXPECT_GT(red, 0u);
  EXPECT_LE(red, 3u * 32 * 32);
  card->deleteLater();
  scene.root->run();
}

TEST(Nb4Ux, MotorsportPalettesKeepNativeEdgeTxControlsReadable)
{
  constexpr double minimumContrast = 4.5;
  for (unsigned i = 0; i < nb4PaletteCount(); ++i) {
    const auto& colors = nb4Palette(i).colors;
    EXPECT_GE(contrastRatio(colors[COLOR_THEME_PRIMARY1_INDEX],
                            colors[COLOR_THEME_SECONDARY3_INDEX]),
              minimumContrast);
    EXPECT_GE(contrastRatio(colors[COLOR_THEME_SECONDARY1_INDEX],
                            colors[COLOR_THEME_PRIMARY2_INDEX]),
              minimumContrast);
    EXPECT_GE(contrastRatio(colors[COLOR_THEME_PRIMARY2_INDEX],
                            colors[COLOR_THEME_PRIMARY3_INDEX]),
              minimumContrast);
    EXPECT_GE(contrastRatio(colors[COLOR_THEME_PRIMARY1_INDEX],
                            colors[COLOR_THEME_ACTIVE_INDEX]),
              minimumContrast);
    EXPECT_GE(contrastRatio(colors[COLOR_THEME_PRIMARY2_INDEX],
                            colors[COLOR_THEME_EDIT_INDEX]),
              minimumContrast);
    EXPECT_GE(contrastRatio(colors[COLOR_THEME_QM_BG_INDEX],
                            colors[COLOR_THEME_QM_FG_INDEX]),
              minimumContrast);
  }
}

TEST(Nb4Ux, TouchMarksLapsUndoRestoresAndDeltaCompares)
{
  SYSTEM_RESET();
  nb4RacingDefaults(g_model.nb4Racing);
  g_model.nb4Racing.lapSw = 0;
  seedLaps();
  EXPECT_EQ(nb4RacingLaps(), 3);
  EXPECT_EQ(nb4RacingLapTime(0), 2534u);
  EXPECT_EQ(nb4RacingLapTime(1), 2487u);
  EXPECT_EQ(nb4RacingLapTime(2), 2512u);
  EXPECT_EQ(nb4RacingBestLap(), 2487u);
  EXPECT_EQ(nb4RacingBestLapIndex(), 2);
  EXPECT_EQ(nb4RacingLapDelta(), 2512 - 2487);
  EXPECT_EQ(nb4RacingCurrentLap(), 1230u);

  ASSERT_TRUE(nb4RacingUndoLap()); nb4RaceProcessCommands();
  EXPECT_EQ(nb4RacingLaps(), 2);
  EXPECT_EQ(nb4RacingCurrentLap(), 1230u + 2512u);
  EXPECT_EQ(nb4RacingLastLap(), 2487u);
  EXPECT_EQ(nb4RacingBestLap(), 2487u);
  EXPECT_EQ(nb4RacingLapDelta(), 2487 - 2534);
  ASSERT_TRUE(nb4RacingUndoLap()); nb4RaceProcessCommands();
  ASSERT_TRUE(nb4RacingUndoLap()); nb4RaceProcessCommands();
  EXPECT_FALSE(nb4RacingUndoLap());
  EXPECT_EQ(nb4RacingLaps(), 0);
  EXPECT_EQ(nb4RacingBestLap(), 0u);
  EXPECT_EQ(nb4RacingLapDelta(), 0);

  nb4RacingMarkLap();
  racingTick(10);
  racingTick(10);
  EXPECT_EQ(nb4RacingLaps(), 1);
  nb4RacingReset();
}

TEST(Nb4Ux, PitCountdownLivesOnTimerTwo)
{
  SYSTEM_RESET();
  nb4RacingDefaults(g_model.nb4Racing);
  nb4RacingSetupRaceTimer(true);
  EXPECT_FALSE(nb4PitEnabled());
  EXPECT_EQ(nb4PitLapsLeft(), -1);

  nb4PitConfigure(8);
  ASSERT_TRUE(nb4PitEnabled());
  EXPECT_EQ(nb4PitMinutes(), 8u);
  EXPECT_EQ((unsigned)g_model.timers[NB4_PIT_TIMER].start, 480u);
  EXPECT_EQ((unsigned)g_model.timers[NB4_PIT_TIMER].mode, (unsigned)g_model.timers[0].mode);
  EXPECT_EQ((unsigned)g_model.timers[NB4_PIT_TIMER].countdownBeep, (unsigned)COUNTDOWN_VOICE);
  EXPECT_EQ((unsigned)g_model.timers[NB4_PIT_TIMER].persistent, (unsigned)g_model.timers[0].persistent);
  EXPECT_EQ(timersStates[NB4_PIT_TIMER].val, 480);
  EXPECT_EQ(nb4PitPercent(), 100);

  timersStates[NB4_PIT_TIMER].val = 120;
  EXPECT_EQ(nb4PitRemaining(), 120);
  EXPECT_EQ(nb4PitPercent(), 25);
  timersStates[NB4_PIT_TIMER].val = -5;
  EXPECT_EQ(nb4PitPercent(), 0);
  EXPECT_EQ(nb4PitLapsLeft(), -1);  // No average is available without laps

  seedLaps();  // Average: 2511 hundredths
  timersStates[NB4_PIT_TIMER].val = 120;
  EXPECT_EQ(nb4PitLapsLeft(), 12000 / 2511);
  timersStates[NB4_PIT_TIMER].val = 0;
  EXPECT_EQ(nb4PitLapsLeft(), 0);

  nb4PitRefuel();
  EXPECT_EQ(timersStates[NB4_PIT_TIMER].val, 480);

  nb4PitConfigure(0);
  EXPECT_FALSE(nb4PitEnabled());
  EXPECT_EQ((unsigned)g_model.timers[NB4_PIT_TIMER].mode, (unsigned)TMRMODE_OFF);
  nb4RacingReset();
}

TEST(Nb4Ux, LegacyVisualFieldsRemainReadable)
{
  SYSTEM_RESET();
  memset(g_eeGeneral.nb4Cards, 0, sizeof(g_eeGeneral.nb4Cards));
  EXPECT_EQ(nb4TemplateSlots(NB4_HOME_CHRONO), 4u);
  EXPECT_EQ(nb4TemplateSlots(NB4_HOME_BENCH), 0u);
  EXPECT_EQ(nb4SlotMetric(NB4_HOME_CHRONO, 0), NB4_METRIC_CURRENT_LAP);
  nb4SetSlotMetric(NB4_HOME_CHRONO, 0, NB4_METRIC_PIT);
  EXPECT_EQ(nb4SlotMetric(NB4_HOME_CHRONO, 0), NB4_METRIC_PIT);
  EXPECT_EQ(nb4SlotMetric(NB4_HOME_PIT, 0), NB4_METRIC_PIT);
  nb4SetSlotMetric(NB4_HOME_CHRONO, 0, 250);                   // Out of range: use the default
  EXPECT_EQ(nb4SlotMetric(NB4_HOME_CHRONO, 0), NB4_METRIC_CURRENT_LAP);
  nb4SetSlotMetric(NB4_HOME_COUNT, 0, NB4_METRIC_PIT);
  for (unsigned i = 0; i < NB4_METRIC_COUNT; ++i) EXPECT_STRNE(nb4MetricName(i), "");
}

TEST(Nb4Ux, RacingHomeRendersAllPalettesInBothOrientationsWithoutLeaking)
{
  Scene scene;
  g_model.nb4Racing.lapSw = 0;
  seedLaps();
  EXPECT_EQ(nb4ReadCarState().steeringTrim.value, 12);
  EXPECT_EQ(nb4ReadCarState().throttleTrim.value, -6);
  nb4PitConfigure(8);
  timersStates[NB4_PIT_TIMER].val = 300;
  memset(g_eeGeneral.nb4Cards, 0, sizeof(g_eeGeneral.nb4Cards));
  const auto racing = g_model.nb4Racing;

  auto& receiverVoltage = g_model.telemetrySensors[0];
  receiverVoltage.id = 0x1000;
  receiverVoltage.type = TELEM_TYPE_CUSTOM;
  receiverVoltage.unit = UNIT_VOLTS;
  receiverVoltage.prec = 1;
  memcpy(receiverVoltage.label, "RxV", 3);
  telemetryItems[0].value = 60;
  telemetryItems[0].timeout = TELEMETRY_SENSOR_TIMEOUT_START;
  telemetryData.rssi.set(83);
  telemetryStreaming = TELEMETRY_TIMEOUT10ms;

  const uint8_t designs[] = {NB4_HOME_INSTRUMENTS};
  const char* names[] = {"racing"};
  const char* palettes[] = {"ApexTX Dark", "ApexTX Light", "ApexTX Sun", "ApexTX Red",
                            "ApexTX Green", "ApexTX Orange", "ApexTX Mono"};
  const char* slugs[] = {"dark", "light", "sun", "red", "green", "orange", "mono"};
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  g_eeGeneral.nb4Home = NB4_HOME_INSTRUMENTS;
  auto home = new Nb4HomeScreen(scene.root, {0, 0, LCD_W, LCD_H});

  scene.orient(false);
  scene.applyPalette("ApexTX Dark");
  g_model.nb4Racing.homeTimer = NB4_HOME_TIMER_1;
  g_model.timers[0].mode = TMRMODE_THR_START;
  g_model.timers[0].start = 600;
  g_model.timers[0].showElapsed = 0;
  timersStates[0].val = 527;
  timersStates[0].state = TMR_RUNNING;
  home->checkEvents(); render(scene.root);
  saveFrame("home-portrait-es-countdown-dark", LCD_W, LCD_H);
  g_model.nb4Racing.homeTimer = NB4_HOME_TIMER_HIDDEN;
  home->checkEvents(); render(scene.root);
  saveFrame("home-portrait-es-no-timer-dark", LCD_W, LCD_H);
  g_model.nb4Racing.homeTimer = racing.homeTimer;
  g_model.timers[0].mode = TMRMODE_ON;
  g_model.timers[0].start = 0;
  timersStates[0].val = 127;
  timersStates[0].state = TMR_OFF;

  for (unsigned orientation = 0; orientation < 2; ++orientation) {
    scene.orient(orientation != 0);
    coord_t w = lv_disp_get_hor_res(nullptr), h = lv_disp_get_ver_res(nullptr);
    home->setRect({0, 0, w, h});

    const unsigned paletteCount = orientation ? 2 : DIM(palettes);
    for (unsigned p = 0; p < paletteCount; ++p) {
      scene.applyPalette(palettes[orientation && p == 1 ? 2 : p]);
      for (unsigned d = 0; d < DIM(designs); ++d) {
        g_eeGeneral.nb4Home = designs[d];
        home->checkEvents();
        render(scene.root);
        char name[64];
        snprintf(name, sizeof(name), "ux-%s-%s-%s", orientation ? "landscape" : "portrait",
                 names[d], slugs[orientation && p == 1 ? 2 : p]);
        saveFrame(name, w, h);
        ASSERT_EQ(lv_mem_test(), LV_RES_OK);
        EXPECT_EQ(timersStates[0].val, 127);
        EXPECT_EQ(channelOutputs[0], RESX / 3);
        EXPECT_EQ(nb4RacingLaps(), 3);
        EXPECT_EQ(memcmp(&g_model.nb4Racing, &racing, sizeof(racing)), 0);
      }
    }

    memcpy(g_eeGeneral.uiLanguage, "en", 2);
    currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
    scene.applyPalette("ApexTX Dark");
    home->checkEvents();
    render(scene.root);
    saveFrame(orientation ? "ux-landscape-racing-dark-en"
                          : "ux-portrait-racing-dark-en",
              w, h);
    memcpy(g_eeGeneral.uiLanguage, "es", 2);
    currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  }

  scene.orient(false);
  home->setRect({0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)});
  scene.applyPalette("ApexTX Dark");
  g_eeGeneral.nb4Home = NB4_HOME_CHRONO;
  const unsigned accents[] = {2, 3, 4, 7};
  const char* accentSlugs[] = {"orange", "green", "red", "magenta"};
  for (unsigned a = 0; a < DIM(accents); ++a) {
    g_eeGeneral.nb4Accent = accents[a];
    auto themes = ThemePersistance::instance();
    themes->applyTheme(themes->getThemeIndex());
    home->checkEvents();
    render(scene.root);
    char name[64];
    snprintf(name, sizeof(name), "ux-accent-%s", accentSlugs[a]);
    saveFrame(name, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  }
  g_eeGeneral.nb4Accent = 0;
  ThemePersistance::instance()->applyTheme(ThemePersistance::instance()->getThemeIndex());

  lv_mem_monitor_t warm, final;
  for (unsigned i = 0; i < 120; ++i) {
    g_eeGeneral.nb4Home = designs[i % DIM(designs)];
    home->checkEvents();
    render(scene.root);
    if (i == 11) lv_mem_monitor(&warm);
    ASSERT_EQ(lv_mem_test(), LV_RES_OK);
  }
  lv_mem_monitor(&final);
  EXPECT_GE(final.free_size + 64u, warm.free_size);
  EXPECT_EQ(final.used_cnt, warm.used_cnt);

  home->deleteLater();
  scene.root->run();
  nb4PitConfigure(0);
  nb4RacingReset();
}

TEST(Nb4Ux, NativePagesRender)
{
  Scene scene;
  g_model.nb4Racing.lapSw = 0;
  seedLaps();
  nb4PitConfigure(8);
  scene.orient(false);
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  g_eeGeneral.nb4Home = NB4_HOME_CHRONO;
  coord_t w = lv_disp_get_hor_res(nullptr), h = lv_disp_get_ver_res(nullptr);
  auto home = new Nb4HomeScreen(scene.root, {0, 0, w, h});
  render(scene.root);

  const struct { Nb4Section section; const char* name; } sections[] = {
      {Nb4Section::Car, "menu-car"}, {Nb4Section::Race, "menu-race"},
      {Nb4Section::System, "menu-system"}, {Nb4Section::Appearance, "menu-appearance"},
      {Nb4Section::Cards, "menu-cards"}, {Nb4Section::Throttle, "menu-throttle"},
      {Nb4Section::Steering, "menu-steering"}, {Nb4Section::Advanced, "menu-advanced"}};
  const char* palettes[] = {"ApexTX Dark", "ApexTX Light"};
  const char* slugs[] = {"dark", "light"};
  for (unsigned p = 0; p < 2; ++p) {
    scene.applyPalette(palettes[p]);
    for (const auto& s : sections) {
      nb4OpenSection(s.section);
      auto page = Layer::back();
      ASSERT_NE(page, nullptr);
      render(scene.root);
      char name[64];
      snprintf(name, sizeof(name), "%s-%s", s.name, slugs[p]);
      saveFrame(name, w, h);
      ASSERT_EQ(lv_mem_test(), LV_RES_OK);
      page->onCancel();
      render(scene.root);
    }
  }
  scene.applyPalette("ApexTX Dark");

  PageDef pages[] = {
      {ICON_RADIO_SETUP, STR_DEF(STR_QM_RADIO_SETTINGS), STR_DEF(STR_MAIN_RADIO_SETTINGS),
       PAGE_CREATE, QM_RADIO_SETUP, [](PageDef& p) { return new RadioSetupPage(p); }},
      {ICON_MODEL_SETUP, STR_DEF(STR_NB4_RACING), STR_DEF(STR_NB4_RACING),
       PAGE_CREATE, QM_MODEL_NB4_RACING, [](PageDef& p) { return new ModelNb4RacingPage(p); }},
      {EDGETX_ICONS_COUNT}};
  auto group = new PageGroup(ICON_RADIO_SETUP, "NB4", pages);
  group->setCloseHandler(nullptr);
  render(scene.root);
  saveFrame("edgetx-radio-settings", w, h);
  group->setCurrentTab(1);
  render(scene.root);
  saveFrame("edgetx-racing", w, h);
  group->onCancel();
  render(scene.root);

  home->deleteLater();
  scene.root->run();
  nb4PitConfigure(0);
  nb4RacingReset();
}

namespace {

std::vector<std::string> stringLiterals(const std::string& source)
{
  std::vector<std::string> out;
  enum { CODE, LINE_COMMENT, BLOCK_COMMENT, STRING, CHAR } state = CODE;
  std::string current;
  bool include = false;
  for (size_t i = 0; i < source.size(); ++i) {
    char c = source[i];
    char n = i + 1 < source.size() ? source[i + 1] : '\0';
    switch (state) {
      case CODE:
        if (c == '/' && n == '/') { state = LINE_COMMENT; ++i; }
        else if (c == '/' && n == '*') { state = BLOCK_COMMENT; ++i; }
        else if (c == '#') {

          include = source.compare(i, 8, "#include") == 0;
        }
        else if (c == '\n') include = false;
        else if (c == '\'') state = CHAR;
        else if (c == '"' && !include) { state = STRING; current.clear(); }
        break;
      case LINE_COMMENT:
        if (c == '\n') { state = CODE; include = false; }
        break;
      case BLOCK_COMMENT:
        if (c == '*' && n == '/') { state = CODE; ++i; }
        break;
      case CHAR:
        if (c == '\\') ++i; else if (c == '\'') state = CODE;
        break;
      case STRING:
        if (c == '\\') { current += c; if (i + 1 < source.size()) current += source[++i]; }
        else if (c == '"') { out.push_back(current); state = CODE; }
        else current += c;
        break;
    }
  }
  return out;
}

std::vector<std::string> nb4SourceFiles()
{
  const std::string root = std::string(TESTS_PATH) + "/..";
  std::vector<std::string> files;

  for (const char* lang : {"/translations/i18n/es.h", "/translations/i18n/en.h"})
    files.push_back(root + lang);
  for (const char* dir : {"/gui/colorlcd", "/pulses"}) {
    std::error_code ec;
    for (auto& entry :
         std::filesystem::recursive_directory_iterator(root + dir, ec)) {
      auto path = entry.path().string();
      if (path.size() > 4 &&
          (path.compare(path.size() - 4, 4, ".cpp") == 0 ||
           path.compare(path.size() - 2, 2, ".h") == 0))
        files.push_back(path);
    }
  }
  for (auto& entry : std::filesystem::directory_iterator(root)) {
    auto name = entry.path().filename().string();
    if (name.rfind("nb4_", 0) == 0) files.push_back(entry.path().string());
  }
  return files;
}

}  // namespace

TEST(Nb4Ux, EveryStringTheFirmwareWritesHasAGlyphInTheCompiledFont)
{
  Scene scene;
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];

  const LcdFlags sizes[] = {FONT(XXS), FONT(XS), FONT(STD), FONT(BOLD)};

  auto files = nb4SourceFiles();
  ASSERT_GT(files.size(), 40u);

  unsigned checked = 0, literals = 0;
  for (const auto& file : files) {
    std::ifstream in(file);
    if (!in) continue;
    std::string source((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
    for (const auto& text : stringLiterals(source)) {
      ++literals;
      for (uint32_t i = 0; i < text.size();) {
        uint32_t letter = _lv_txt_encoded_next(text.c_str(), &i);
        if (letter < 0x80) continue;  // ASCII and the \n and \t escape sequences
        ++checked;
        for (LcdFlags flags : sizes) {
          lv_font_glyph_dsc_t dsc;
          EXPECT_TRUE(lv_font_get_glyph_dsc(getFont(flags), &dsc, letter, 0));
        }
      }
    }
  }
  EXPECT_GT(literals, 2000u);
  EXPECT_GT(checked, 200u);
}

namespace {

void collectLabels(lv_obj_t* obj, std::vector<std::string>& out)
{
  if (lv_obj_check_type(obj, &lv_label_class)) {
    const char* txt = lv_label_get_text(obj);
    if (txt && *txt) out.push_back(txt);
  }

  if (lv_obj_has_class(obj, &lv_table_class)) {
    uint16_t rows = lv_table_get_row_cnt(obj), cols = lv_table_get_col_cnt(obj);
    for (uint16_t r = 0; r < rows; ++r)
      for (uint16_t c = 0; c < cols; ++c) {
        const char* txt = lv_table_get_cell_value(obj, r, c);
        if (txt && *txt) out.push_back(txt);
      }
  }
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c)
    collectLabels(lv_obj_get_child(obj, c), out);
}

lv_obj_t* switchNextTo(lv_obj_t* obj, const char* labelText)
{
  if (lv_obj_check_type(obj, &lv_label_class)) {
    const char* txt = lv_label_get_text(obj);
    if (txt && std::string(txt) == labelText) {
      lv_obj_t* row = lv_obj_get_parent(obj);
      for (uint32_t c = 0; c < lv_obj_get_child_cnt(row); ++c) {
        lv_obj_t* sib = lv_obj_get_child(row, c);
        if (lv_obj_has_class(sib, &lv_switch_class)) return sib;
      }
    }
  }
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c)
    if (lv_obj_t* found = switchNextTo(lv_obj_get_child(obj, c), labelText))
      return found;
  return nullptr;
}

NumberEdit* numberEditShowing(lv_obj_t* obj, const char* text)
{
  if (lv_obj_check_type(obj, &lv_label_class)) {
    const char* txt = lv_label_get_text(obj);
    if (txt && std::string(txt) == text) {
      lv_obj_t* parent = lv_obj_get_parent(obj);
      if (parent && lv_obj_has_class(parent, &lv_btn_class)) {
        if (void* ud = lv_obj_get_user_data(parent))
          return (NumberEdit*)(Window*)ud;
      }
    }
  }
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c)
    if (NumberEdit* found = numberEditShowing(lv_obj_get_child(obj, c), text))
      return found;
  return nullptr;
}

std::string labelInside(lv_obj_t* obj);

NumberEdit* numberEditInRowOf(lv_obj_t* root, const char* rowLabel)
{
  std::vector<lv_obj_t*> order;
  std::function<void(lv_obj_t*)> walk = [&](lv_obj_t* o) {
    order.push_back(o);
    for (uint32_t c = 0; c < lv_obj_get_child_cnt(o); ++c)
      walk(lv_obj_get_child(o, c));
  };
  walk(root);

  size_t start = order.size();
  for (size_t i = 0; i < order.size(); ++i)
    if (lv_obj_check_type(order[i], &lv_label_class)) {
      const char* t = lv_label_get_text(order[i]);
      if (t && std::string(t) == rowLabel) { start = i + 1; break; }
    }

  for (size_t i = start; i < order.size(); ++i) {
    lv_obj_t* o = order[i];
    if (!lv_obj_get_group(o) || !lv_obj_get_user_data(o)) continue;
    if (!lv_obj_has_class(o, &lv_btn_class)) continue;
    const std::string inside = labelInside(o);
    if (inside.find_first_of("0123456789") == std::string::npos) continue;
    return (NumberEdit*)(Window*)lv_obj_get_user_data(o);
  }
  return nullptr;
}

std::string curveShownAfter(const std::vector<std::string>& labels,
                            const char* rowLabel)
{
  size_t start = labels.size();
  for (size_t i = 0; i < labels.size(); ++i)
    if (labels[i] == rowLabel) { start = i + 1; break; }
  for (size_t i = start; i < labels.size(); ++i)
    for (int c = 1; c <= MAX_CURVES; ++c)
      if (labels[i] == getCurveString(c)) return labels[i];
  return "";
}

bool pickTab(lv_obj_t* obj, const char* label)
{

  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return false;
  if (lv_obj_check_type(obj, &lv_label_class)) {
    const char* t = lv_label_get_text(obj);
    if (t && std::string(t) == label) {

      for (lv_obj_t* up = lv_obj_get_parent(obj); up; up = lv_obj_get_parent(up)) {
        if (lv_obj_has_class(up, &lv_btn_class)) {
          lv_event_send(up, LV_EVENT_CLICKED, nullptr);
          return true;
        }
      }
    }
  }
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c)
    if (pickTab(lv_obj_get_child(obj, c), label)) return true;
  return false;
}

const char* const* steeringTabs(unsigned* count);
const char* const* throttleTabs(unsigned* count);

std::vector<std::string> labelsOfPage(MainWindow* root, QMPage page,
                                      const char* capture = nullptr)
{
  auto before = Layer::back();
  QuickMenu::openPage(page);
  for (unsigned i = 0; i < 3; ++i) { lv_obj_update_layout(root->getLvObj()); lv_tick_inc(50); lv_timer_handler(); root->run(); }
  std::vector<std::string> out;
  collectLabels(lv_scr_act(), out);
  if (capture) saveFrame(capture, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  for (unsigned i = 0; i < 16 && Layer::back() != before; ++i) {
    auto p = Layer::back(); p->onCancel(); root->run();
    if (Layer::back() == p) break;
  }
  return out;
}

const char* const* steeringTabs(unsigned* count)
{
  static const char* tabs[4];
  tabs[0] = STR_NB4_TRAVEL;
  tabs[1] = STR_NB4_CURVE;
  tabs[2] = STR_NB4_CENTRE;
  tabs[3] = STR_NB4_SPEED;
  if (count) *count = 4;
  return tabs;
}

const char* const* throttleTabs(unsigned* count)
{
  static const char* tabs[4];
  tabs[0] = STR_NB4_TRAVEL;
  tabs[1] = STR_NB4_CURVE;
  tabs[2] = STR_NB4_BRAKE_30D6;
  tabs[3] = STR_NB4_ENGINE;
  if (count) *count = 4;
  return tabs;
}

}  // namespace

TEST(Nb4Ux, AxisTabsFillTheRowWithSymmetricPaddingAndUncutLabels)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  ViewMain::instance();
  auto base = Layer::back();
  for (unsigned orientation = 0; orientation < 2; ++orientation) {
    scene.orient(orientation);
    for (const char* language : {"es", "en"}) {
      memcpy(g_eeGeneral.uiLanguage, language, 2);
      currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
      for (const QMPage page : {QM_MODEL_NB4_STEERING, QM_MODEL_NB4_THROTTLE}) {
        QuickMenu::openPage(page);
        unsigned count;
        const auto names = page == QM_MODEL_NB4_STEERING ? steeringTabs(&count)
                                                       : throttleTabs(&count);
        for (unsigned picked = 0; picked < count; ++picked) {
          render(scene.root);
          ASSERT_TRUE(pickTab(lv_scr_act(), names[picked]));
          for (unsigned f = 0; f < 3; ++f) render(scene.root);
          SCOPED_TRACE(std::string(language) + "/" + names[picked] +
                       (orientation ? "/landscape" : "/portrait"));
          std::vector<lv_obj_t*> labels;
          lv_obj_t* chartCaption = nullptr;
          std::function<void(lv_obj_t*)> walk = [&](lv_obj_t* o) {
            if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
            if (lv_obj_check_type(o, &lv_label_class)) {
              const char* chartZone = page == QM_MODEL_NB4_STEERING
                  ? STR_NB4_LEFT : STR_NB4_THROTTLE;
              if (strcmp(lv_label_get_text(o), chartZone) == 0)
                chartCaption = o;
              for (unsigned i = 0; i < count; ++i)
                if (strcmp(lv_label_get_text(o), names[i]) == 0 &&
                    lv_obj_has_class(lv_obj_get_parent(o), &lv_btn_class))
                  labels.push_back(o);
            }
            for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); ++i)
              walk(lv_obj_get_child(o, i));
          };
          walk(Layer::back()->getLvObj());
          ASSERT_EQ(labels.size(), count);
          ASSERT_NE(chartCaption, nullptr);
          lv_area_t chartArea;
          lv_obj_get_coords(lv_obj_get_parent(chartCaption), &chartArea);
          EXPECT_GE(chartArea.x1, 0);
          EXPECT_LT(chartArea.x2, lv_disp_get_hor_res(nullptr));
          EXPECT_LT(chartArea.y2, lv_disp_get_ver_res(nullptr));
          if (page == QM_MODEL_NB4_STEERING && picked == 0) {
            auto endpoint = numberEditInRowOf(lv_scr_act(), (std::string(STR_NB4_LEFT) + " " + STR_NB4_PERCENT_UNIT).c_str());
            ASSERT_NE(endpoint, nullptr);
            lv_area_t controlArea;
            lv_obj_get_coords(endpoint->getLvObj(), &controlArea);
            EXPECT_LT(controlArea.y2, lv_disp_get_ver_res(nullptr));
          }
          lv_area_t first{}, last{};
          int previousRight = -1, gap = -1, firstWidth = 0;
          for (unsigned i = 0; i < count; ++i) {
            auto label = labels[i];
            auto button = lv_obj_get_parent(label);
            lv_area_t b, l;
            lv_obj_get_coords(button, &b);
            lv_obj_get_coords(label, &l);
            if (i == 0) { first = b; firstWidth = lv_area_get_width(&b); }
            else {
              const int currentGap = b.x1 - previousRight - 1;
              if (i == 1) gap = currentGap;
              EXPECT_EQ(currentGap, gap);
              EXPECT_LE(abs(lv_area_get_width(&b) - firstWidth), 1);
            }
            previousRight = b.x2; last = b;
            EXPECT_GE(lv_area_get_height(&b), 44);
            EXPECT_GE(l.x1 - b.x1, 4);
            EXPECT_GE(b.x2 - l.x2, 4);
            EXPECT_LE(abs((l.x1 - b.x1) - (b.x2 - l.x2)), 1);
            EXPECT_LE(abs((l.y1 - b.y1) - (b.y2 - l.y2)), 1);
          }
          const int right = lv_disp_get_hor_res(nullptr) - last.x2 - 1;
          EXPECT_EQ(first.x1, right);
          EXPECT_LE(first.x1, 2);
          char file[100];
          snprintf(file, sizeof(file), "layout-%s-%s-%s-tab-%u",
                   orientation ? "landscape" : "portrait", language,
                   page == QM_MODEL_NB4_STEERING ? "steering" : "throttle", picked);
          saveFrame(file, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
        }
        while (Layer::back() != base) { Layer::back()->onCancel(); scene.root->run(); }
      }
    }
  }
}

TEST(Nb4Ux, HomeChronoKeepsFullTimesInsideItsCard)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  for (unsigned orientation = 0; orientation < 2; ++orientation) {
    scene.orient(orientation);
    for (const char* language : {"es", "en"}) {
      memcpy(g_eeGeneral.uiLanguage, language, 2);
      for (bool race : {false, true}) {
        auto state = nb4ReadCarState();
        state.homeShowsRace = race;
        state.homeTimerVisible = true;
        state.homeTimerIndex = 0;
        state.homeTimerState = TMR_RUNNING;
        state.racePhase = Nb4RacePhase::Running;
        state.currentLap.validity = Nb4Validity::Valid;
        state.homeTimer.validity = Nb4Validity::Valid;
        const rect_t box = orientation ? rect_t{0, 0, 202, 56} : rect_t{0, 0, 316, 70};
        auto chrono = new Nb4Chrono(scene.root, box, state, orientation);
        for (int value : {0, 1230, 3600, 359999, -3601}) {
          if (race && value < 0) continue;
          state.currentLap.value = value;
          state.homeTimer.value = value;
          chrono->refresh(state);
          render(scene.root);
          const auto expected = race ? Nb4Ui::timeText(value) : Nb4Ui::timerText(value);
          bool found = false;
          std::function<void(lv_obj_t*)> walk = [&](lv_obj_t* o) {
            if (lv_obj_check_type(o, &lv_label_class) &&
                expected == lv_label_get_text(o)) {
              found = true;
              lv_point_t textSize;
              lv_txt_get_size(&textSize, expected.c_str(), lv_obj_get_style_text_font(o, 0),
                              0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
              EXPECT_LE(textSize.x, lv_obj_get_content_width(o));
              if (value <= 1230 && value >= 0)
                EXPECT_GE(lv_obj_get_style_text_font(o, 0)->line_height,
                          getFontHeight(FONT(L)));
              lv_area_t label, card;
              lv_obj_get_coords(o, &label);
              lv_obj_get_coords(chrono->getLvObj(), &card);
              EXPECT_GE(label.x1, card.x1);
              EXPECT_LE(label.x2, card.x2);
              EXPECT_GE(label.y1, card.y1);
              EXPECT_LE(label.y2, card.y2);
            }
            for (unsigned i = 0; i < lv_obj_get_child_cnt(o); ++i)
              walk(lv_obj_get_child(o, i));
          };
          walk(chrono->getLvObj());
          EXPECT_TRUE(found);
        }
        chrono->deleteLater(); scene.root->run();
      }
    }
  }
}

TEST(Nb4Ux, TheExpoEditorWritesAnEncodedNumberAndNotARawNegative)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  {
    ExpoData* a = expoAddress(1);
    ExpoData* b = expoAddress(2);
    *b = *a;
    a->mode = 2; b->mode = 1;
    a->curve.type = CURVE_REF_EXPO; a->curve.value = makeSourceNumVal(25);
    b->curve.type = CURVE_REF_EXPO; b->curve.value = makeSourceNumVal(0);
  }
  ASSERT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).status, Nb4AxisStatus::Ready);

  auto base = Layer::back();
  QuickMenu::openPage(QM_MODEL_NB4_THROTTLE);
  for (unsigned i = 0; i < 3; ++i) { lv_obj_update_layout(scene.root->getLvObj()); lv_tick_inc(50); lv_timer_handler(); scene.root->run(); }
  ASSERT_TRUE(pickTab(lv_scr_act(), STR_NB4_CURVE));
  for (unsigned i = 0; i < 3; ++i) { lv_obj_update_layout(scene.root->getLvObj()); lv_tick_inc(50); lv_timer_handler(); scene.root->run(); }

  NumberEdit* expo = numberEditShowing(lv_scr_act(), "25%");
  ASSERT_NE(expo, nullptr);

  expo->setValue(-30);
  for (unsigned i = 0; i < 3; ++i) { lv_obj_update_layout(scene.root->getLvObj()); lv_tick_inc(50); lv_timer_handler(); scene.root->run(); }

  SourceNumVal written; written.rawValue = expoAddress(1)->curve.value;
  EXPECT_FALSE(written.isSource);
  EXPECT_EQ(written.value, -30);

  for (unsigned i = 0; i < 16 && Layer::back() != base; ++i) {
    auto p = Layer::back(); p->onCancel(); scene.root->run();
    if (Layer::back() == p) break;
  }
}

namespace { std::string labelInside(lv_obj_t* obj); }

TEST(Nb4Ux, NothingSticksOutOfThePortraitScreenAndMicrosecondsReadTheSame)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  {
    ExpoData* a = expoAddress(1);
    ExpoData* b = expoAddress(2);
    *b = *a;
    a->mode = 2; b->mode = 1;
    a->curve.type = CURVE_REF_CUSTOM; a->curve.value = makeSourceNumVal(2);
    b->curve.type = CURVE_REF_CUSTOM; b->curve.value = makeSourceNumVal(5);
  }
  ASSERT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).status, Nb4AxisStatus::Ready);

  auto widest = [&](QMPage page, const char** worst, const char* tab) {
    auto base = Layer::back();
    QuickMenu::openPage(page);
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    if (tab && pickTab(lv_scr_act(), tab))
      for (unsigned f = 0; f < 3; ++f) render(scene.root);
    const lv_coord_t screen = lv_disp_get_hor_res(nullptr);
    lv_coord_t maxRight = 0;
    std::function<void(lv_obj_t*)> walk = [&](lv_obj_t* obj) {

      if (lv_obj_get_group(obj) && lv_obj_get_user_data(obj)) {
        lv_area_t area;
        lv_obj_get_coords(obj, &area);
        if (area.x2 > maxRight) {
          maxRight = area.x2;
          static std::string label;
          label = labelInside(obj);
          if (label.empty()) label = "(control sin rotulo)";
          if (worst) *worst = label.c_str();
        }
      }
      for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c)
        walk(lv_obj_get_child(obj, c));
    };
    if (Layer::back()) walk(Layer::back()->getLvObj());
    for (unsigned d = 0; d < 16 && Layer::back() != base; ++d) {
      auto pg = Layer::back(); pg->onCancel(); scene.root->run();
      if (Layer::back() == pg) break;
    }
    return std::make_pair(maxRight, screen);
  };

  unsigned steerCount = 0, thrCount = 0;
  const char* const* sTabs = steeringTabs(&steerCount);
  const char* const* tTabs = throttleTabs(&thrCount);
  for (unsigned page = 0; page < 2; page += 1) {
    const QMPage qm = page == 0 ? QM_MODEL_NB4_STEERING : QM_MODEL_NB4_THROTTLE;
    const char* const* names = page == 0 ? sTabs : tTabs;
    const unsigned count = page == 0 ? steerCount : thrCount;
    for (unsigned t = 0; t < count; t += 1) {
      nb4ParamRegistryReset();
      const char* worst = "(ninguno)";
      const auto [right, screen] = widest(qm, &worst, names[t]);
      ASSERT_GT(right, 0);
      EXPECT_LE(right, screen);
    }
  }

  LimitData* out = limitAddress(1);
  out->min = 0; out->max = 0;
  const uint8_t previousUnit = g_eeGeneral.ppmunit;
  g_eeGeneral.ppmunit = PPM_US;

  struct RestoreUnit { uint8_t v; ~RestoreUnit() { g_eeGeneral.ppmunit = v; } }
      restore{previousUnit};

  const auto ours = labelsOfPage(scene.root, QM_MODEL_NB4_THROTTLE);
  auto has = [&](const std::vector<std::string>& v, const char* what) {
    return std::find(v.begin(), v.end(), std::string(what)) != v.end();
  };

  EXPECT_TRUE(has(ours, "-512.0") || has(ours, "512.0"));
  EXPECT_FALSE(has(ours, "-100.0"));
}

TEST(Nb4Ux, NitroRowsFollowTheVehicleTypeAndTheTypeIsNotAPreset)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  auto has = [](const std::vector<std::string>& v, const char* what) {
    return std::find(v.begin(), v.end(), std::string(what)) != v.end();
  };

  auto motorTab = [&]() {
    auto before = Layer::back();
    QuickMenu::openPage(QM_MODEL_NB4_THROTTLE);
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    pickTab(lv_scr_act(), STR_NB4_ENGINE);
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    std::vector<std::string> out;
    collectLabels(lv_scr_act(), out);
    for (unsigned d = 0; d < 16 && Layer::back() != before; ++d) {
      auto pg = Layer::back(); pg->onCancel(); scene.root->run();
      if (Layer::back() == pg) break;
    }
    return out;
  };

  g_model.nb4Racing.vehicleType = NB4_VEHICLE_UNSET;
  const auto unset = motorTab();
  EXPECT_TRUE(has(unset, STR_NB4_IDLE_UP));

  /* Nitro settings are visible. */
  g_model.nb4Racing.vehicleType = NB4_VEHICLE_NITRO;
  const auto nitro = motorTab();
  EXPECT_TRUE(has(nitro, STR_NB4_IDLE_UP));
  EXPECT_TRUE(has(nitro, STR_NB4_ENGINE_CUT));
  EXPECT_TRUE(has(nitro, STR_NB4_CUT_POS));

  g_model.nb4Racing.vehicleType = NB4_VEHICLE_ELECTRIC;
  const auto electric = motorTab();
  EXPECT_FALSE(has(electric, STR_NB4_IDLE_UP));
  EXPECT_FALSE(has(electric, STR_NB4_ENGINE_CUT));
  EXPECT_FALSE(has(electric, STR_NB4_CUT_POS));

  {
    auto before = Layer::back();
    QuickMenu::openPage(QM_MODEL_NB4_THROTTLE);
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    pickTab(lv_scr_act(), STR_NB4_BRAKE_30D6);
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    std::vector<std::string> brake;
    collectLabels(lv_scr_act(), brake);
    EXPECT_TRUE(has(brake, STR_NB4_BRAKE_MAX));
    for (unsigned d = 0; d < 16 && Layer::back() != before; ++d) {
      auto pg = Layer::back(); pg->onCancel(); scene.root->run();
      if (Layer::back() == pg) break;
    }
  }

  g_model.nb4Racing.brakeMax = 62;
  g_model.nb4Racing.dragBrake = 7;
  g_model.nb4Racing.idleUp = 21;
  g_model.nb4Racing.vehicleType = NB4_VEHICLE_UNSET;

  const auto racing = labelsOfPage(scene.root, QM_MODEL_NB4_RACING);
  EXPECT_TRUE(has(racing, STR_NB4_VEHICLE_TYPE));

  auto base = Layer::back();
  QuickMenu::openPage(QM_MODEL_NB4_RACING);
  for (unsigned f = 0; f < 3; ++f) render(scene.root);
  lv_obj_t* row = nullptr;
  std::function<void(lv_obj_t*)> findRow = [&](lv_obj_t* o) {
    if (!row && lv_obj_check_type(o, &lv_label_class)) {
      const char* t = lv_label_get_text(o);
      if (t && std::string(t) == STR_NB4_VEHICLE_TYPE)
        row = lv_obj_get_parent(o);
    }
    for (uint32_t c = 0; c < lv_obj_get_child_cnt(o); ++c)
      findRow(lv_obj_get_child(o, c));
  };
  if (Layer::back()) findRow(Layer::back()->getLvObj());
  ASSERT_NE(row, nullptr);

  Choice* choice = nullptr;
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(row); ++c) {
    lv_obj_t* sib = lv_obj_get_child(row, c);
    if (lv_obj_get_group(sib) && lv_obj_get_user_data(sib))
      choice = (Choice*)(Window*)lv_obj_get_user_data(sib);
  }
  ASSERT_NE(choice, nullptr);
  choice->setValue(NB4_VEHICLE_NITRO);
  for (unsigned f = 0; f < 3; ++f) render(scene.root);

  EXPECT_EQ(g_model.nb4Racing.vehicleType, NB4_VEHICLE_NITRO);
  EXPECT_EQ(g_model.nb4Racing.brakeMax, 62);
  EXPECT_EQ(g_model.nb4Racing.dragBrake, 7);
  EXPECT_EQ(g_model.nb4Racing.idleUp, 21);

  for (unsigned d = 0; d < 16 && Layer::back() != base; ++d) {
    auto pg = Layer::back(); pg->onCancel(); scene.root->run();
    if (Layer::back() == pg) break;
  }
}

namespace {
bool clickLabel(lv_obj_t* obj, const char* text)
{
  if (lv_obj_check_type(obj, &lv_label_class)) {
    const char* t = lv_label_get_text(obj);
    if (t && std::string(t) == text) {
      lv_obj_t* up = lv_obj_get_parent(obj);
      for (unsigned d = 0; up && d < 4; ++d) {
        if (lv_obj_has_class(up, &lv_btn_class)) { lv_event_send(up, LV_EVENT_CLICKED, nullptr); return true; }
        up = lv_obj_get_parent(up);
      }
    }
  }
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c)
    if (clickLabel(lv_obj_get_child(obj, c), text)) return true;
  return false;
}

#if defined(RADIO_NB4) && defined(AUDIO)
TEST(Nb4Ux, AudioPageSelectsPlaybackModeAndOffersAPreview)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  for (bool landscape : {false, true}) {
    scene.orient(landscape);
    for (const char* language : {"es", "en"}) {
      memcpy(g_eeGeneral.uiLanguage, language, 2);
      currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
      openRadioSetupSoundPage(STR_NB4_SOUND);
      for (unsigned f = 0; f < 3; ++f) render(scene.root);
      std::vector<Choice*> choices;
      std::function<void(lv_obj_t*)> collect = [&](lv_obj_t* obj) {
        if (auto window = (Window*)lv_obj_get_user_data(obj))
          if (auto choice = dynamic_cast<Choice*>(window)) choices.push_back(choice);
        for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); ++i)
          collect(lv_obj_get_child(obj, i));
      };
      auto page = Layer::back();
      collect(page->getLvObj());
      ASSERT_GE(choices.size(), 2u);
      g_eeGeneral.beepMode = e_mode_all;
      audioQueue.playFile("/SOUNDS/en/SYSTEM/telemko.wav", 0, 42);
      choices[0]->setValue(1);
      EXPECT_EQ(g_eeGeneral.nb4TonesOnly, 1);
      EXPECT_FALSE(audioQueue.isPlaying(42));
      EXPECT_TRUE(clickLabel(page->getLvObj(), STR_NB4_TEST_AUDIO));
      choices[0]->setValue(0);
      EXPECT_EQ(g_eeGeneral.nb4TonesOnly, 0);
      for (unsigned f = 0; f < 3; ++f) render(scene.root);
      expectEveryGlyphRenderable(page->getLvObj(), "Audio playback");
      const std::string name = std::string("audio-") + (landscape ? "landscape-" : "portrait-") + language;
      saveFrame(name.c_str(), lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
      audioQueue.stopAll();
      page->onCancel();
      for (unsigned f = 0; f < 3; ++f) render(scene.root);
    }
  }
}
#endif
}  // namespace

#if defined(RADIO_NB4) && defined(AFHDS3) && (defined(NB4_RF_PROFILE_RECOVERED_LAB) || defined(NB4_RF_PROFILE_QUALIFIED))
TEST(Nb4Ux, BindDialogShowsRealStagesAndFitsBothLanguagesAndOrientations)
{
  using namespace afhds3;
  Scene scene;
  ViewMain::instance();
  for (bool landscape : {false, true}) for (bool english : {false, true}) {
    scene.orient(landscape);
    memcpy(g_eeGeneral.uiLanguage, english ? "en" : "es", 2);
    currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
    scene.applyPalette("ApexTX Dark");
    nb4::Nb4RfController::shutdown(); modulePortInit();
    g_model.moduleData[0].type = MODULE_TYPE_FLYSKY_AFHDS3;
    g_model.moduleData[0].channelsCount = -6;
    g_model.moduleData[0].afhds3.phyMode = CLASSIC_FLCR1_18CH;
    g_model.moduleData[0].afhds3.telemetry = true;
    void* ctx = ProtoDriver.init(0); ASSERT_NE(ctx, nullptr);
    struct Cleanup {
      void* ctx; tmr10ms_t time = get_tmr10ms();
      ~Cleanup() {
        ProtoDriver.deinit(ctx); nb4::Nb4RfController::shutdown();
        setModuleMode(0, MODULE_MODE_NORMAL); g_tmr10ms = time;
        getConfig(0)->others.lastUpdated = time;
      }
    } cleanup{ctx};
    setModuleMode(0, MODULE_MODE_NORMAL);
    auto page = new ModulePage(0);
    for (unsigned i = 0; i < 3; ++i) render(scene.root);
    uint8_t rx[TELEMETRY_RX_PACKET_SIZE]{}, rxCount = 0;
    auto receive = [&](COMMAND cmd, const std::vector<uint8_t>& data, uint8_t seq, FRAME_TYPE type) {
      uint8_t wire[MODULE_BUFFER_SIZE]{}; FrameTransport sender;
      sender.init(wire, 0x14, true, sizeof(wire));
      sender.putFrame(cmd, type, const_cast<uint8_t*>(data.data()), data.size(), seq);
      for (unsigned i = 0; i < sender.getFrameSize(); ++i)
        ProtoDriver.processData(ctx, wire[i], rx, &rxCount);
    };
    auto respond = [&](uint8_t value) {
      ProtoDriver.sendPulses(ctx, nullptr, nullptr, 0);
      uint8_t scratch[MODULE_BUFFER_SIZE]{}, decoded[240]{}, count = 0;
      FrameTransport parser; parser.init(scratch, 0x14, true, sizeof(scratch));
      auto wire = pulsesGetModuleBuffer(0);
      for (unsigned i = 0; i < MODULE_BUFFER_SIZE; ++i) {
        if (!parser.processTelemetryData(wire[i], decoded, count, sizeof(decoded))) continue;
        auto f = reinterpret_cast<AfhdsFrame*>(decoded);
        receive(COMMAND(f->command), {value}, f->frameNumber, RESPONSE_DATA);
        return;
      }
      FAIL();
    };
    auto capture = [&](const char* phase, int completed) {
      for (unsigned i = 0; i < 3; ++i) render(scene.root);
      auto dialog = Layer::back(); ASSERT_NE(dialog, page);
      const int w = lv_disp_get_hor_res(nullptr), h = lv_disp_get_ver_res(nullptr);
      char name[90]; snprintf(name, sizeof(name), "bind-%s-%s-%s", landscape ? "landscape" : "portrait", english ? "en" : "es", phase);
      saveFrame(name, w, h);
      expectEveryGlyphRenderable(dialog->getLvObj(), name);
      unsigned bars = 0;
      std::function<void(lv_obj_t*)> inspect = [&](lv_obj_t* obj) {
        if (lv_obj_check_type(obj, &lv_bar_class)) {
          ++bars; EXPECT_EQ(lv_bar_get_value(obj), completed);
          EXPECT_EQ(lv_obj_get_style_bg_opa(obj, LV_PART_INDICATOR), LV_OPA_COVER);
          EXPECT_GT(lv_obj_get_width(obj), 100); EXPECT_EQ(lv_obj_get_height(obj), 8);
        }
        if (lv_obj_check_type(obj, &lv_label_class)) {
          lv_area_t area; lv_obj_get_coords(obj, &area);
          EXPECT_GE(area.x1, 0); EXPECT_LT(area.x2, w);
          EXPECT_GE(area.y1, 0); EXPECT_LT(area.y2, h);
          lv_point_t size;
          lv_txt_get_size(&size, lv_label_get_text(obj), lv_obj_get_style_text_font(obj, 0),
                          lv_obj_get_style_text_letter_space(obj, 0), lv_obj_get_style_text_line_space(obj, 0),
                          lv_obj_get_content_width(obj), LV_TEXT_FLAG_NONE);
          EXPECT_LE(size.y, lv_obj_get_content_height(obj));
        }
        for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c) inspect(lv_obj_get_child(obj, c));
      };
      inspect(dialog->getLvObj()); EXPECT_EQ(bars, 1u);
    };
    auto search = [&]() {
      respond(2); respond(2);
      for (unsigned i = 0; i < 4; ++i) respond(i == 3 ? 0xff : 0xf8);
      respond(15);
      ASSERT_EQ(getBindPhase(0), BindPhase::Searching);
    };
    ASSERT_TRUE(clickLabel(page->getLvObj(), STR_NB4_BIND));
    capture("preparing", 0);
    search(); capture("searching", 1);
    std::vector<uint8_t> receiver(169);
    std::copy(nb4DefaultReceiver, nb4DefaultReceiver + 168, receiver.begin() + 1);
    receive(MODULE_APPLY_CONFIG, receiver, 80, REQUEST_SET_NO_RESP);
    capture("confirming", 2);
    receive(MODULE_STATE, {4}, 81, REQUEST_SET_NO_RESP);
    capture("connected", 3);
    g_tmr10ms += 82;
    render(scene.root); render(scene.root);
    EXPECT_EQ(Layer::back(), page);
    EXPECT_EQ(getModuleMode(0), MODULE_MODE_NORMAL);
    ProtoDriver.sendPulses(ctx, nullptr, nullptr, 0);
    ASSERT_TRUE(clickLabel(page->getLvObj(), STR_NB4_BIND));
    search(); capture("retry-searching", 1);
    g_tmr10ms += 3001;
    ProtoDriver.sendPulses(ctx, nullptr, nullptr, 0);
    capture("timeout", 1);
    Layer::back()->onCancel(); scene.root->run();
    EXPECT_EQ(getModuleMode(0), MODULE_MODE_NORMAL);
    page->onCancel(); scene.root->run();
  }
}
#endif

TEST(Nb4Ux, TheFactoryResetIsInBackupAndEachButtonKeepsToItsHalf)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  nb4OpenSection(Nb4Section::Backup);
  for (unsigned f = 0; f < 4; ++f) render(scene.root);

  std::vector<std::string> labels;
  collectLabels(lv_scr_act(), labels);
  auto has = [&](const char* what) {
    return std::find(labels.begin(), labels.end(), std::string(what)) != labels.end();
  };
  EXPECT_TRUE(has("Ajustes de la emisora"));
  EXPECT_TRUE(has("Este coche"));
  EXPECT_TRUE(has("Emisora y este coche"));

  bool keeps = false;
  for (const auto& l : labels)
    if (l.find("calibración de los ejes") != std::string::npos) keeps = true;
  EXPECT_TRUE(keeps);

  struct Restore {
    RadioData radio; ModelData model;
    ~Restore() { g_eeGeneral = radio; g_model = model; }
  } restore{g_eeGeneral, g_model};

  g_model.nb4Racing.brakeMax = 37;

  memset(g_eeGeneral.selectedTheme, 0, SELECTED_THEME_NAME_LEN);
  strAppend(g_eeGeneral.selectedTheme, "Otro tema", SELECTED_THEME_NAME_LEN);
  g_eeGeneral.inactivityTimer = 42;

  ASSERT_TRUE(clickLabel(lv_scr_act(), "Este coche"));
  for (unsigned f = 0; f < 4; ++f) render(scene.root);
  ASSERT_TRUE(clickLabel(lv_scr_act(), STR_YES));
  for (unsigned f = 0; f < 4; ++f) render(scene.root);

  Nb4RacingData factory;
  nb4RacingDefaults(factory);
  EXPECT_EQ(g_model.nb4Racing.brakeMax, factory.brakeMax);
  EXPECT_STREQ(g_eeGeneral.selectedTheme, "Otro tema");
  EXPECT_EQ(g_eeGeneral.inactivityTimer, 42);

  for (unsigned d = 0; d < 16; ++d) {
    auto pg = Layer::back(); if (!pg) break;
    pg->onCancel(); scene.root->run();
    if (Layer::back() == pg) break;
  }
}

TEST(Nb4Ux, TheSteeringChartAndTheHomeAgreeOnWhichSideIsLeft)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  calibratedAnalogs[ADC_MAIN_ST] = RESX / 2;

  const int32_t home = nb4ReadCarState().steeringInput.value;
  ASSERT_NE(home, 0);

  QuickMenu::openPage(QM_MODEL_NB4_STEERING);

  for (unsigned f = 0; f < 14; ++f) render(scene.root);

  std::vector<std::string> labels;
  collectLabels(lv_scr_act(), labels);

  int chart = 0;
  bool found = false;
  for (const auto& l : labels) {
    if (l.find(LV_SYMBOL_RIGHT) == std::string::npos) continue;
    chart = atoi(l.c_str());
    found = true;
    break;
  }
  ASSERT_TRUE(found);
  ASSERT_NE(chart, 0);

  EXPECT_EQ(home < 0, chart < 0);

  calibratedAnalogs[ADC_MAIN_ST] = 0;

  for (unsigned d = 0; d < 16; ++d) {
    auto pg = Layer::back(); if (!pg) break;
    pg->onCancel(); scene.root->run();
    if (Layer::back() == pg) break;
  }
}

TEST(Nb4Ux, ADisabledTileSaysWhyInsteadOfDoingNothing)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  unsigned count = 0;
  const Nb4Route* routes = nb4Routes(&count);
  const Nb4Route* pending = nullptr;
  for (unsigned i = 0; i < count; ++i)
    if (routes[i].state == Nb4RouteState::NotBuiltYet &&
        strcmp(routes[i].path, "settings/system/help") == 0)
      pending = &routes[i];
  ASSERT_NE(pending, nullptr);
  ASSERT_NE(pending->reason, nullptr);

  nb4OpenSettingsModal();
  for (unsigned f = 0; f < 4; ++f) render(scene.root);
  ASSERT_TRUE(clickLabel(lv_scr_act(), STR_NB4_SYSTEM));
  for (unsigned f = 0; f < 4; ++f) render(scene.root);

  lv_obj_t* helpTile = nullptr;
  {
    std::function<lv_obj_t*(lv_obj_t*)> findTile = [&](lv_obj_t* o) -> lv_obj_t* {
      if (lv_obj_check_type(o, &lv_label_class)) {
        const char* t = lv_label_get_text(o);
        if (t && std::string(t) == std::string(STR_NB4_HELP)) {
          lv_obj_t* up = lv_obj_get_parent(o);
          for (unsigned d = 0; up && d < 4; ++d) {
            if (lv_obj_has_class(up, &lv_btn_class)) return up;
            up = lv_obj_get_parent(up);
          }
        }
      }
      for (uint32_t c = 0; c < lv_obj_get_child_cnt(o); ++c)
        if (lv_obj_t* f = findTile(lv_obj_get_child(o, c))) return f;
      return nullptr;
    };
    helpTile = findTile(lv_scr_act());
  }
  ASSERT_NE(helpTile, nullptr);
  lv_obj_update_layout(lv_scr_act());
  {
    lv_area_t a; lv_obj_get_coords(helpTile, &a);
    lv_point_t centre = {(lv_coord_t)((a.x1 + a.x2) / 2),
                         (lv_coord_t)((a.y1 + a.y2) / 2)};
    EXPECT_TRUE(lv_obj_hit_test(helpTile, &centre));
  }

  const auto before = Layer::back();
  ASSERT_TRUE(clickLabel(lv_scr_act(), STR_NB4_HELP));
  for (unsigned f = 0; f < 4; ++f) render(scene.root);

  EXPECT_NE(Layer::back(), before);

  std::vector<std::string> labels;
  collectLabels(lv_scr_act(), labels);
  bool saysWhy = false;
  for (const auto& l : labels)
    if (l.find("índice de ayuda") != std::string::npos) saysWhy = true;
  EXPECT_TRUE(saysWhy);

  for (unsigned d = 0; d < 16; ++d) {
    auto pg = Layer::back(); if (!pg) break;
    pg->onCancel(); scene.root->run();
    if (Layer::back() == pg) break;
  }
}

TEST(Nb4Ux, NoTabEverComesUpBlankWithoutSayingWhy)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  const ModelData saved = g_model;
  struct Restore { ModelData m; ~Restore() { g_model = m; } } restore{saved};
  for (unsigned i = 0; i < MAX_EXPOS; ++i) {
    ExpoData* line = expoAddress(i);
    if (!line->srcRaw) continue;
    SourceNumVal v; v.isSource = true; v.value = MIXSRC_FIRST_POT;
    line->weight = v.rawValue;
  }
  const Nb4AxisView view = nb4ResolveAxis(Nb4AxisRole::Steering);
  ASSERT_TRUE(view.status != Nb4AxisStatus::Ready &&
              view.status != Nb4AxisStatus::Shared);

  unsigned count = 0;
  const char* const* tabs = steeringTabs(&count);
  for (unsigned t = 0; t < count; t += 1) {

    if (strcmp(tabs[t], STR_NB4_SPEED) == 0) continue;
    auto before = Layer::back();
    QuickMenu::openPage(QM_MODEL_NB4_STEERING);
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    if (t > 0) {
      ASSERT_TRUE(pickTab(lv_scr_act(), tabs[t]));
      for (unsigned f = 0; f < 3; ++f) render(scene.root);
    }
    std::vector<std::string> labels;
    collectLabels(lv_scr_act(), labels);

    ASSERT_NE(view.reason, nullptr);
    unsigned reasons = 0;
    for (const auto& l : labels)
      if (l == std::string(view.reason)) reasons += 1;
    EXPECT_GE(reasons, 2u);
    for (unsigned d = 0; d < 16 && Layer::back() != before; ++d) {
      auto pg = Layer::back(); pg->onCancel(); scene.root->run();
      if (Layer::back() == pg) break;
    }
  }
}

TEST(Nb4Ux, TheSettingsGridFitsWithoutScrolling)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  nb4OpenSettingsModal();
  for (unsigned f = 0; f < 4; ++f) render(scene.root);
  lv_obj_update_layout(lv_scr_act());

  lv_coord_t worst = 0;
  std::function<void(lv_obj_t*)> walk = [&](lv_obj_t* o) {
    const lv_coord_t bottom = lv_obj_get_scroll_bottom(o);
    if (bottom > worst) worst = bottom;
    for (uint32_t c = 0; c < lv_obj_get_child_cnt(o); ++c) walk(lv_obj_get_child(o, c));
  };
  if (Layer::back()) walk(Layer::back()->getLvObj());

  EXPECT_EQ(worst, 0);

  std::vector<std::string> labels;
  collectLabels(lv_scr_act(), labels);
  unsigned sectionCount = 0;
  const Nb4Section2* sections = nb4Sections(&sectionCount);
  for (unsigned i = 0; i < sectionCount; i += 1) {
    const char* name = sections[i].label();
    EXPECT_NE(std::find(labels.begin(), labels.end(), std::string(name)),
              labels.end());
  }

  for (unsigned d = 0; d < 16; ++d) {
    auto pg = Layer::back(); if (!pg) break;
    pg->onCancel(); scene.root->run();
    if (Layer::back() == pg) break;
  }
}

TEST(Nb4Ux, TheAbsShortcutOpensTheBrakeTab)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  auto labelsAfterOpening = [&](const char* path) {
    auto before = Layer::back();
    EXPECT_TRUE(nb4OpenRoute(path));
    for (unsigned f = 0; f < 4; ++f) render(scene.root);
    std::vector<std::string> out;
    collectLabels(lv_scr_act(), out);
    for (unsigned d = 0; d < 16 && Layer::back() != before; ++d) {
      auto pg = Layer::back(); pg->onCancel(); scene.root->run();
      if (Layer::back() == pg) break;
    }
    return out;
  };
  auto has = [](const std::vector<std::string>& v, const char* what) {
    return std::find(v.begin(), v.end(), std::string(what)) != v.end();
  };

  const auto abs = labelsAfterOpening("settings/throttle_brake/brake");
  EXPECT_TRUE(has(abs, "Punto ABS"));

  const auto travel = labelsAfterOpening("settings/throttle_brake/travel");
  EXPECT_FALSE(has(travel, "Punto ABS"));

  const auto section = labelsAfterOpening("settings/throttle_brake");
  EXPECT_FALSE(has(section, "Punto ABS"));
}

TEST(Nb4Ux, HoldingAStepButtonAcceleratesInsteadOfCrawling)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  // Leave room for increases: the physical left editor now correctly uses max.
  limitAddress(0)->max = -1000;
  auto base = Layer::back();
  QuickMenu::openPage(QM_MODEL_NB4_STEERING);
  for (unsigned f = 0; f < 3; ++f) render(scene.root);

  lv_obj_t* plus = nullptr;
  std::function<void(lv_obj_t*)> findPlus = [&](lv_obj_t* o) {
    if (plus) return;
    if (lv_obj_check_type(o, &lv_label_class)) {
      const char* t = lv_label_get_text(o);
      if (t && std::string(t) == std::string(LV_SYMBOL_PLUS)) {
        lv_obj_t* up = lv_obj_get_parent(o);
        if (up && lv_obj_has_class(up, &lv_btn_class)) plus = up;
      }
    }
    for (uint32_t c = 0; c < lv_obj_get_child_cnt(o); ++c) findPlus(lv_obj_get_child(o, c));
  };
  findPlus(lv_scr_act());
  ASSERT_NE(plus, nullptr);

  auto valueNow = [&]() {

    lv_obj_t* row = lv_obj_get_parent(plus);
    int32_t sum = 0;
    std::function<void(lv_obj_t*)> collect = [&](lv_obj_t* o) {
      if (lv_obj_check_type(o, &lv_label_class)) {
        const char* t = lv_label_get_text(o);

        if (t && (isdigit((unsigned char)t[0]) || t[0] == '-'))
          sum += (int32_t)(atof(t) * 10);
      }
      for (uint32_t c = 0; c < lv_obj_get_child_cnt(o); ++c) collect(lv_obj_get_child(o, c));
    };
    collect(row);
    return sum;
  };

  const int32_t start = valueNow();

  /* ONE TAP: one step. */
  lv_event_send(plus, LV_EVENT_CLICKED, nullptr);
  for (unsigned f = 0; f < 2; ++f) render(scene.root);
  const int32_t afterOne = valueNow();
  ASSERT_NE(afterOne, start);
  const int32_t oneStep = afterOne - start;

  lv_event_send(plus, LV_EVENT_LONG_PRESSED, nullptr);
  int32_t before = valueNow();
  lv_event_send(plus, LV_EVENT_LONG_PRESSED_REPEAT, nullptr);
  for (unsigned f = 0; f < 2; ++f) render(scene.root);
  EXPECT_NE(valueNow(), before);

  for (unsigned i = 0; i < 38; ++i) lv_event_send(plus, LV_EVENT_LONG_PRESSED_REPEAT, nullptr);
  for (unsigned f = 0; f < 2; ++f) render(scene.root);
  before = valueNow();
  lv_event_send(plus, LV_EVENT_LONG_PRESSED_REPEAT, nullptr);
  for (unsigned f = 0; f < 2; ++f) render(scene.root);
  const int32_t lateStep = valueNow() - before;

  lv_event_send(plus, LV_EVENT_CLICKED, nullptr);
  for (unsigned f = 0; f < 2; ++f) render(scene.root);

  EXPECT_GT(std::abs(lateStep), std::abs(oneStep));

  for (unsigned d = 0; d < 16 && Layer::back() != base; ++d) {
    auto pg = Layer::back(); pg->onCancel(); scene.root->run();
    if (Layer::back() == pg) break;
  }
}

TEST(Nb4Ux, TheTopIconBarFitsOnScreen)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  auto base = Layer::back();
  QuickMenu::openPage(QM_MODEL_NB4_STEERING);
  for (unsigned f = 0; f < 4; ++f) render(scene.root);
  lv_obj_update_layout(lv_scr_act());

  const lv_coord_t screen = lv_disp_get_hor_res(nullptr);

  const lv_coord_t band = 56;

  lv_coord_t rightmost = 0;
  unsigned icons = 0;
  std::function<void(lv_obj_t*)> walk = [&](lv_obj_t* o) {
    lv_area_t a;
    lv_obj_get_coords(o, &a);

    if (a.y2 <= band && a.y2 > a.y1 && lv_obj_is_visible(o) &&
        lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
      icons += 1;
      if (a.x2 > rightmost) rightmost = a.x2;
    }
    for (uint32_t c = 0; c < lv_obj_get_child_cnt(o); ++c) walk(lv_obj_get_child(o, c));
  };
  if (Layer::back()) walk(Layer::back()->getLvObj());

  ASSERT_GT(icons, 3u);
  EXPECT_LE(rightmost, screen - 1);

  for (unsigned d = 0; d < 16 && Layer::back() != base; ++d) {
    auto pg = Layer::back(); pg->onCancel(); scene.root->run();
    if (Layer::back() == pg) break;
  }
}

TEST(Nb4Ux, AFactoryFreshCarShowsNoWarnings)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  const ModelData saved = g_model;
  struct Restore { ModelData m; ~Restore() { g_model = m; } } restore{saved};
  nb4FactoryReset(Nb4Reset::Model);

  const char* const kWarnings[] = {
      "Los dos lados comparten un mismo ajuste",
      "Un valor viene de otra fuente, no de un número",
      "Esta configuración no se puede editar aquí",
  };

  for (unsigned page = 0; page < 2; page += 1) {
    const QMPage qm = page == 0 ? QM_MODEL_NB4_STEERING : QM_MODEL_NB4_THROTTLE;
    auto before = Layer::back();
    QuickMenu::openPage(qm);
    for (unsigned f = 0; f < 4; ++f) render(scene.root);

    std::vector<std::string> labels;
    collectLabels(lv_scr_act(), labels);
    for (const char* warning : kWarnings)
      EXPECT_EQ(std::find(labels.begin(), labels.end(), std::string(warning)),
                labels.end());

    for (unsigned d = 0; d < 16 && Layer::back() != before; ++d) {
      auto pg = Layer::back(); pg->onCancel(); scene.root->run();
      if (Layer::back() == pg) break;
    }
  }
}

TEST(Nb4Ux, GoingBackReturnsToThePreviousViewNotHome)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  for (unsigned d = 0; d < 24; ++d) {
    auto pg = Layer::back();
    if (!pg || pg == ViewMain::instance()) break;
    pg->onCancel();
    scene.root->run();
    if (Layer::back() == pg) break;
  }
  for (unsigned f = 0; f < 3; ++f) render(scene.root);

  const auto home = Layer::back();

  struct CloseAll {
    Scene& scene;
    ~CloseAll() {
      for (unsigned d = 0; d < 24; ++d) {
        auto pg = Layer::back();
        if (!pg || pg == ViewMain::instance()) break;
        pg->onCancel();
        scene.root->run();
        if (Layer::back() == pg) break;
      }
    }
  } closeAll{scene};

  auto top = []() { return Layer::back()->getLvObj(); };
  auto shows = [&](const char* what) {
    std::vector<std::string> labels;
    collectLabels(top(), labels);
    return std::find(labels.begin(), labels.end(), std::string(what)) != labels.end();
  };
  auto back = [&]() {
    auto pg = Layer::back();
    if (pg) pg->onCancel();
    for (unsigned f = 0; f < 4; ++f) render(scene.root);
  };

  nb4OpenSettingsModal();
  for (unsigned f = 0; f < 4; ++f) render(scene.root);
  ASSERT_TRUE(shows("Sistema"));
  const auto settingsGrid = Layer::back();

  ASSERT_TRUE(clickLabel(top(), "Sistema"));
  for (unsigned f = 0; f < 4; ++f) render(scene.root);
  ASSERT_TRUE(shows("Energía"));
  EXPECT_NE(Layer::back(), settingsGrid);
  const auto systemGrid = Layer::back();

  ASSERT_TRUE(clickLabel(top(), "Energía"));
  for (unsigned f = 0; f < 4; ++f) render(scene.root);
  EXPECT_NE(Layer::back(), systemGrid);

  back();
  EXPECT_NE(Layer::back(), home);
  EXPECT_TRUE(shows("Energía"));

  /* SECOND: the Settings grid. */
  back();
  EXPECT_NE(Layer::back(), home);
  EXPECT_TRUE(shows("Sistema"));

  back();
  EXPECT_EQ(Layer::back(), home);
}

TEST(Nb4Ux, VariablesStayOutOfSightAndExplainThemselvesWhenTheyAppear)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  ASSERT_FALSE(modelGVEnabled());

  auto labelsOfEveryTabOf = [&](QMPage page, const char* const* tabs,
                                unsigned count) {
    std::vector<std::string> all;
    for (unsigned t = 0; t < count; t += 1) {
      auto before = Layer::back();
      QuickMenu::openPage(page);
      for (unsigned f = 0; f < 3; ++f) render(scene.root);
      if (t > 0 && !pickTab(lv_scr_act(), tabs[t])) {
        ADD_FAILURE();
      } else {
        for (unsigned f = 0; f < 3; ++f) render(scene.root);
        collectLabels(lv_scr_act(), all);
      }
      for (unsigned d = 0; d < 16 && Layer::back() != before; ++d) {
        auto pg = Layer::back(); pg->onCancel(); scene.root->run();
        if (Layer::back() == pg) break;
      }
    }
    return all;
  };
  auto has = [](const std::vector<std::string>& v, const char* what) {
    return std::find(v.begin(), v.end(), std::string(what)) != v.end();
  };
  auto hasStartingWith = [](const std::vector<std::string>& v, const char* what) {
    const std::string prefix(what);
    for (const auto& l : v)
      if (l.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
  };

  unsigned sCount = 0, tCount = 0;
  const char* const* sTabs = steeringTabs(&sCount);
  const char* const* tTabs = throttleTabs(&tCount);

  auto steering = labelsOfEveryTabOf(QM_MODEL_NB4_STEERING, sTabs, sCount);
  auto throttle = labelsOfEveryTabOf(QM_MODEL_NB4_THROTTLE, tTabs, tCount);
  EXPECT_FALSE(has(steering, "GV") || has(throttle, "GV"));

  EXPECT_FALSE(hasStartingWith(steering, "Este ajuste toma su valor"));

  const Nb4AxisView view = nb4ResolveAxis(Nb4AxisRole::Steering);
  ASSERT_LT(view.outputChannel, MAX_OUTPUT_CHANNELS);
  limitAddress(view.outputChannel)->min = GV_VALUE_FROM_INDEX(0);

  auto withGvar = labelsOfEveryTabOf(QM_MODEL_NB4_STEERING, sTabs, sCount);
  EXPECT_TRUE(hasStartingWith(withGvar, "Este ajuste toma su valor de una variable"));

  EXPECT_TRUE(hasStartingWith(withGvar,
                              "Este ajuste toma su valor de una variable del "
                              "modelo. Para devolverlo"));
  EXPECT_FALSE(has(withGvar, "GV"));

  g_eeGeneral.modelGVDisabled = 0;
  ASSERT_TRUE(modelGVEnabled());
  auto withVars = labelsOfEveryTabOf(QM_MODEL_NB4_STEERING, sTabs, sCount);
  EXPECT_TRUE(has(withVars, "GV"));
  EXPECT_TRUE(hasStartingWith(withVars,
                              "Este ajuste toma su valor de una variable del "
                              "modelo. El botón GV"));
  g_eeGeneral.modelGVDisabled = 1;

  limitAddress(view.outputChannel)->min = 0;
}

TEST(Nb4Ux, TheChartSurvivesEveryTabChange)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  auto has = [](const std::vector<std::string>& v, const char* what) {
    return std::find(v.begin(), v.end(), std::string(what)) != v.end();
  };

  unsigned count = 0;
  const char* const* tabs = steeringTabs(&count);
  const char* title = STR_NB4_STEERING_CURVE;

  auto base = Layer::back();
  QuickMenu::openPage(QM_MODEL_NB4_STEERING);
  for (unsigned f = 0; f < 3; ++f) render(scene.root);

  {
    std::vector<std::string> first;
    collectLabels(lv_scr_act(), first);
    ASSERT_TRUE(has(first, title));
  }

  for (int pass = 0; pass < 2; ++pass) {
    for (unsigned i = 0; i < count; i += 1) {
      const unsigned t = pass == 0 ? i : count - 1 - i;
      ASSERT_TRUE(pickTab(lv_scr_act(), tabs[t]));
      for (unsigned f = 0; f < 3; ++f) render(scene.root);
      std::vector<std::string> shown;
      collectLabels(lv_scr_act(), shown);
      EXPECT_TRUE(has(shown, title));
      EXPECT_EQ(lv_mem_test(), LV_RES_OK);
    }
  }

  for (unsigned d = 0; d < 16 && Layer::back() != base; ++d) {
    auto pg = Layer::back(); pg->onCancel(); scene.root->run();
    if (Layer::back() == pg) break;
  }
}

static void checkResponseChartCost(bool throttle)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();
  {
    ExpoData* a = expoAddress(1);
    ExpoData* b = expoAddress(2);
    *b = *a;
    a->mode = 2; b->mode = 1;
    a->curve.type = CURVE_REF_EXPO; a->curve.value = makeSourceNumVal(0);
    b->curve.type = CURVE_REF_EXPO; b->curve.value = makeSourceNumVal(0);
  }
  auto base = Layer::back();
  QuickMenu::openPage(throttle ? QM_MODEL_NB4_THROTTLE : QM_MODEL_NB4_STEERING);
  const uint8_t chn = throttle ? inputMappingGetThrottle() :
      (uint8_t)(inputMappingGetThrottle() == 0 ? 1 : 0);
  calibratedAnalogs[chn] = 100;
  for (unsigned f = 0; f < 16; ++f) { scene.root->run(); render(scene.root); }

  /* Stationary and settled: no pixel changes. */
  resetFlushAccounting();
  for (unsigned f = 0; f < 4; ++f) { scene.root->run(); renderIncremental(scene.root); }
  EXPECT_EQ(flushedPixels, 0u);

  unsigned long worst = 0, total = 0;
  for (int step = 1; step <= 12; ++step) {
    calibratedAnalogs[chn] = (int16_t)(100 + step * 3);
    resetFlushAccounting();
    scene.root->run();
    renderIncremental(scene.root);
    total += flushedPixels;
    if (step > 1 && flushedPixels > worst) worst = flushedPixels;
  }
  // Full travel catches repaint work hidden by tiny, nearly stationary moves.
  const int sweepFrames = getenv("NB4_CURVE_FRAMES") ? atoi(getenv("NB4_CURVE_FRAMES")) : 120;
  const auto statCallsBefore = simuFatfsStatCalls();
  const auto sweepStart = std::chrono::steady_clock::now();
  for (int step = 0; step < sweepFrames; ++step) {
    const int ramp = step % 60;
    calibratedAnalogs[chn] = (int16_t)(-RESX + (ramp < 30 ? ramp : 60 - ramp) * 2 * RESX / 30);
    scene.root->run();
    renderIncremental(scene.root);
  }
  printf("NB4 curve full sweep: %lld us for %d frames\n",
      (long long)std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - sweepStart).count(), sweepFrames);
  EXPECT_EQ(simuFatfsStatCalls() - statCallsBefore, 0u);
  calibratedAnalogs[chn] = 512;
  render(scene.root);
  saveFrame(throttle ? "response-curve-throttle" : "response-curve-steering",
            lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  const unsigned long screen = (unsigned long)LCD_W * LCD_H;
  EXPECT_LT(worst, screen / 12);
  printf("NB4 card: idle 0 px; motion peak %lu px and average %lu of %lu\n",
         worst, total / 12, screen);

  for (unsigned d = 0; d < 16 && Layer::back() != base; ++d) {
    auto pg = Layer::back(); pg->onCancel(); scene.root->run();
    if (Layer::back() == pg) break;
  }
}

TEST(Nb4Ux, TheResponseChartIsCheapWhileTheStickMoves)
{
  checkResponseChartCost(true);
}

TEST(Nb4Ux, TheSteeringResponseChartDoesNotReadFilesWhileTheWheelMoves)
{
  checkResponseChartCost(false);
}

TEST(Nb4Ux, HeaderRefreshesNotesOnNavigationAndSettingsEveryFrame)
{
  const auto dir = std::filesystem::temp_directory_path() /
      ("nb4-notes-header-" + std::to_string(getpid()));
  std::filesystem::create_directories(dir / "MODELS");
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() { simuFatfsSetPaths(TESTS_PATH, nullptr); std::filesystem::remove_all(path); }
  } cleanup{dir};
  simuFatfsSetPaths(dir.c_str(), nullptr);
  Scene scene;
  strAppend(g_eeGeneral.currModelFilename, "nb4-note.yml", LEN_MODEL_FILENAME);

  struct Tab : PageGroupItem {
    using PageGroupItem::PageGroupItem;
    void build(Window*) override {}
  };
  struct Header : PageGroupHeaderBase {
    Header(Window* parent) : PageGroupHeaderBase(
        parent, PageGroup::PAGE_GROUP_BODY_Y, ICON_MODEL, "", nullptr) {}
    void chgTab(int) override {}
    using PageGroupHeaderBase::getX;
    using PageGroupHeaderBase::checkEvents;
  };
  bool settingEnabled = true;
  unsigned settingChecks = 0;
  PageDef notes{ICON_MODEL_NOTES, STR_DEF(STR_MAIN_MENU_MODEL_NOTES),
                STR_DEF(STR_MAIN_MENU_MODEL_NOTES), PAGE_CREATE, QM_MODEL_NOTES,
                nullptr, modelHasNotes};
  PageDef settings{ICON_MODEL_CURVES, STR_DEF(STR_QM_CURVES), STR_DEF(STR_MENUCURVES),
                   PAGE_CREATE, QM_MODEL_CURVES, nullptr,
                   [&] { ++settingChecks; return settingEnabled; }};
  auto header = new Header(scene.root);
  header->addTab(new Tab(notes));
  header->addTab(new Tab(settings));
  header->addTab(new Tab("Always visible"));
  header->setCurrentIndex(2);
  header->checkEvents();
  EXPECT_EQ(header->getX(1), 0); // No notes file.
  EXPECT_GT(header->getX(2), 0); // The settings-controlled tab is visible.

  // A settings toggle takes effect in one frame without polling files.
  const auto stats = simuFatfsStatCalls();
  const auto checks = settingChecks;
  settingEnabled = false;
  header->checkEvents();
  EXPECT_EQ(header->getX(2), 0);
  EXPECT_EQ(settingChecks - checks, 1u);
  EXPECT_EQ(simuFatfsStatCalls(), stats);

  // Files and the current model are rechecked when navigating, including
  // changes made through USB storage while a page group remained alive.
  std::ofstream(dir / "MODELS/nb4-note.txt") << "Setup notes\n";
  header->checkEvents();
  EXPECT_EQ(simuFatfsStatCalls(), stats);
  header->setCurrentIndex(2);
  EXPECT_GT(header->getX(1), 0);
  EXPECT_GT(simuFatfsStatCalls(), stats);
  strAppend(g_eeGeneral.currModelFilename, "other.yml", LEN_MODEL_FILENAME);
  header->setCurrentIndex(2);
  EXPECT_EQ(header->getX(1), 0);
  strAppend(g_eeGeneral.currModelFilename, "nb4-note.yml", LEN_MODEL_FILENAME);
  header->setCurrentIndex(2);
  EXPECT_GT(header->getX(1), 0);
  std::filesystem::remove(dir / "MODELS/nb4-note.txt");
  header->setCurrentIndex(2);
  EXPECT_EQ(header->getX(1), 0);
  header->deleteLater();
  scene.root->run();
}

TEST(Nb4Ux, TheResponseChartFollowsTheNumbersAndIncludesTheRacingStage)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  {
    ExpoData* a = expoAddress(1);
    ExpoData* b = expoAddress(2);
    *b = *a;
    a->mode = 2; b->mode = 1;
    a->curve.type = CURVE_REF_EXPO; a->curve.value = makeSourceNumVal(0);
    b->curve.type = CURVE_REF_EXPO; b->curve.value = makeSourceNumVal(0);
  }
  const auto view = nb4ResolveAxis(Nb4AxisRole::Throttle);
  ASSERT_EQ(view.status, Nb4AxisStatus::Ready);
  ASSERT_TRUE(nb4AxisMapIsDrawable(view));
  g_model.nb4Racing.brakeMax = 100;

  auto base = Layer::back();
  QuickMenu::openPage(QM_MODEL_NB4_THROTTLE);
  for (unsigned f = 0; f < 3; ++f) render(scene.root);

  auto snapshot = [&]() {
    std::vector<uint8_t> px;

    return px;
  };
  (void)snapshot;

  const int16_t fullBrake = nb4AxisDrawnOutput(view, /*isThrottle=*/true, -1024);
  g_model.nb4Racing.brakeMax = 50;
  for (unsigned f = 0; f < 4; ++f) render(scene.root);
  const int16_t cutBrake = nb4AxisDrawnOutput(view, /*isThrottle=*/true, -1024);

  EXPECT_NEAR(abs(cutBrake), abs(fullBrake) / 2, 8);

  bool graphFound = false, graphRefreshed = false;
  std::function<void(lv_obj_t*)> walk = [&](lv_obj_t* o) {
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const lv_coord_t w = a.x2 - a.x1, h = a.y2 - a.y1;
    if (w > 200 && h > 100 && h < lv_disp_get_ver_res(nullptr) / 2 &&
        lv_obj_get_child_cnt(o) > 0) {
      graphFound = true;

      for (uint32_t c = 0; c < lv_obj_get_child_cnt(o); ++c)
        if (lv_obj_has_class(lv_obj_get_child(o, c), &lv_line_class))
          graphRefreshed = true;
    }
    for (uint32_t c = 0; c < lv_obj_get_child_cnt(o); ++c)
      walk(lv_obj_get_child(o, c));
  };
  if (Layer::back()) walk(Layer::back()->getLvObj());
  EXPECT_TRUE(graphFound);
  EXPECT_TRUE(graphRefreshed);

  g_model.nb4Racing.brakeMax = 100;
  for (unsigned d = 0; d < 16 && Layer::back() != base; ++d) {
    auto pg = Layer::back(); pg->onCancel(); scene.root->run();
    if (Layer::back() == pg) break;
  }
}

TEST(Nb4Ux, SteeringEditsBothLinesWhenThereAreTwoAndOneWhenThereIsOne)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  auto has = [](const std::vector<std::string>& v, const char* what) {
    return std::find(v.begin(), v.end(), std::string(what)) != v.end();
  };

  auto responseTab = [&]() {
    auto base = Layer::back();
    QuickMenu::openPage(QM_MODEL_NB4_STEERING);
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    EXPECT_TRUE(pickTab(lv_scr_act(), STR_NB4_CURVE));
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    std::vector<std::string> out;
    collectLabels(lv_scr_act(), out);
    for (unsigned d = 0; d < 16 && Layer::back() != base; ++d) {
      auto pg = Layer::back(); pg->onCancel(); scene.root->run();
      if (Layer::back() == pg) break;
    }
    return out;
  };

  ASSERT_EQ(nb4ResolveAxis(Nb4AxisRole::Steering).status, Nb4AxisStatus::Shared);
  const auto one = responseTab();
  EXPECT_TRUE(has(one, STR_NB4_DUAL_RATE));
  EXPECT_FALSE(has(one, STR_NB4_LEFT_DUAL_RATE));

  {
    ExpoData* a = expoAddress(0);
    ExpoData* b = nullptr;
    for (uint8_t i = 0; i < MAX_EXPOS && !b; ++i)
      if (!expoAddress(i)->srcRaw) b = expoAddress(i);
    ASSERT_NE(b, nullptr);
    *b = *a;
    a->mode = 2; b->mode = 1;
    a->curve.type = CURVE_REF_EXPO; a->curve.value = makeSourceNumVal(0);
    b->curve.type = CURVE_REF_EXPO; b->curve.value = makeSourceNumVal(0);
  }
  const auto view = nb4ResolveAxis(Nb4AxisRole::Steering);
  ASSERT_EQ(view.status, Nb4AxisStatus::Ready);

  const auto two = responseTab();

  EXPECT_TRUE(has(two, STR_NB4_LEFT_DUAL_RATE));
  EXPECT_TRUE(has(two, STR_NB4_RIGHT_DUAL_RATE));
  EXPECT_TRUE(has(two, STR_NB4_LEFT_EXPO));
  EXPECT_TRUE(has(two, STR_NB4_RIGHT_EXPO));
  EXPECT_FALSE(has(two, STR_NB4_DUAL_RATE));

  nb4ParamRegistryReset();
  responseTab();
  EXPECT_EQ(nb4ParamBuilt(Nb4Param::InputDualRate, (uint8_t)view.lineForPositive), 1u);
  EXPECT_EQ(nb4ParamBuilt(Nb4Param::InputDualRate, (uint8_t)view.lineForNegative), 1u);
  EXPECT_NE(view.lineForPositive, view.lineForNegative);
}

TEST(Nb4Ux, TheAxisTrimIsEditableAndWritesWhereTheButtonsWrite)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  auto has = [](const std::vector<std::string>& v, const char* what) {
    return std::find(v.begin(), v.end(), std::string(what)) != v.end();
  };

  const uint8_t throttleIdx = inputMappingGetThrottle();
  const uint8_t steeringIdx = throttleIdx == 0 ? 1 : 0;
  for (auto& fm : g_model.flightModeData) memclear(&fm.trim, sizeof(fm.trim));

  auto base = Layer::back();
  QuickMenu::openPage(QM_MODEL_NB4_STEERING);
  for (unsigned f = 0; f < 3; ++f) render(scene.root);
  ASSERT_TRUE(pickTab(lv_scr_act(), STR_NB4_CENTRE));
  for (unsigned f = 0; f < 3; ++f) render(scene.root);

  std::vector<std::string> steering;
  collectLabels(lv_scr_act(), steering);

  ASSERT_TRUE(has(steering, STR_NB4_TRIM));

  NumberEdit* trim = numberEditInRowOf(lv_scr_act(), STR_NB4_TRIM);
  ASSERT_NE(trim, nullptr);

  trim->setValue(37);
  for (unsigned f = 0; f < 3; ++f) render(scene.root);

  EXPECT_EQ(getTrimValue(0, steeringIdx), 37);
  EXPECT_EQ(getTrimValue(0, throttleIdx), 0);

  for (unsigned d = 0; d < 16 && Layer::back() != base; ++d) {
    auto pg = Layer::back(); pg->onCancel(); scene.root->run();
    if (Layer::back() == pg) break;
  }

  {
    auto b2 = Layer::back();
    QuickMenu::openPage(QM_MODEL_NB4_THROTTLE);
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    std::vector<std::string> throttle;
    collectLabels(lv_scr_act(), throttle);

    EXPECT_TRUE(has(throttle, STR_NB4_TRIM));
    for (unsigned d = 0; d < 16 && Layer::back() != b2; ++d) {
      auto pg = Layer::back(); pg->onCancel(); scene.root->run();
      if (Layer::back() == pg) break;
    }
  }

  for (auto& fm : g_model.flightModeData) memclear(&fm.trim, sizeof(fm.trim));
}

/* Input reversal changes the physical acceleration half before expo selection.
 * The gas curve therefore stays attached to the same logical input line. The
 * physical-side test above checks this against actual mixer output. */
TEST(Nb4Ux, ReversingTheTriggerPreservesTheGasCurveOnItsNewPhysicalSide)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  {
    ExpoData* a = expoAddress(1);
    ExpoData* b = expoAddress(2);
    *b = *a;
    a->mode = 2; b->mode = 1;
    a->curve.type = CURVE_REF_CUSTOM; a->curve.value = makeSourceNumVal(2);
    b->curve.type = CURVE_REF_CUSTOM; b->curve.value = makeSourceNumVal(5);
  }
  g_model.throttleReversed = 0;
  const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
  ASSERT_EQ(v.status, Nb4AxisStatus::Ready);
  ASSERT_EQ(v.lineForPositive, 1);
  ASSERT_EQ(v.lineForNegative, 2);

  auto before = Layer::back();
  QuickMenu::openPage(QM_MODEL_NB4_THROTTLE);
  for (unsigned i = 0; i < 3; ++i) { lv_obj_update_layout(scene.root->getLvObj()); lv_tick_inc(50); lv_timer_handler(); scene.root->run(); }

  ASSERT_TRUE(pickTab(lv_scr_act(), STR_NB4_CURVE));
  for (unsigned i = 0; i < 3; ++i) { lv_obj_update_layout(scene.root->getLvObj()); lv_tick_inc(50); lv_timer_handler(); scene.root->run(); }

  std::vector<std::string> shown;
  collectLabels(lv_scr_act(), shown);
  const std::string throttleCurveBefore = curveShownAfter(shown, "Expo de gas");
  const std::string brakeCurveBefore = curveShownAfter(shown, "Expo de freno");
  ASSERT_EQ(throttleCurveBefore, getCurveString(2));
  ASSERT_EQ(brakeCurveBefore, getCurveString(5));
  ASSERT_FALSE(throttleCurveBefore.empty());

  ASSERT_TRUE(pickTab(lv_scr_act(), STR_NB4_TRAVEL));
  for (unsigned i = 0; i < 3; ++i) { lv_obj_update_layout(scene.root->getLvObj()); lv_tick_inc(50); lv_timer_handler(); scene.root->run(); }
  lv_obj_t* sw = switchNextTo(lv_scr_act(), "Invertir el gatillo");
  ASSERT_NE(sw, nullptr);
  lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_event_send(sw, LV_EVENT_VALUE_CHANGED, nullptr);
  for (unsigned i = 0; i < 3; ++i) { lv_obj_update_layout(scene.root->getLvObj()); lv_tick_inc(50); lv_timer_handler(); scene.root->run(); }

  ASSERT_EQ(g_model.throttleReversed, 1);

  ASSERT_TRUE(pickTab(lv_scr_act(), STR_NB4_CURVE));
  for (unsigned i = 0; i < 3; ++i) { lv_obj_update_layout(scene.root->getLvObj()); lv_tick_inc(50); lv_timer_handler(); scene.root->run(); }

  std::vector<std::string> after;
  collectLabels(lv_scr_act(), after);
  const std::string throttleCurveAfter = curveShownAfter(after, "Expo de gas");
  const std::string brakeCurveAfter = curveShownAfter(after, "Expo de freno");

  EXPECT_EQ(throttleCurveAfter, throttleCurveBefore);
  EXPECT_EQ(brakeCurveAfter, brakeCurveBefore);

  ASSERT_TRUE(pickTab(lv_scr_act(), STR_NB4_TRAVEL));
  for (unsigned i = 0; i < 3; ++i) { lv_obj_update_layout(scene.root->getLvObj()); lv_tick_inc(50); lv_timer_handler(); scene.root->run(); }
  std::vector<std::string> travel;
  collectLabels(lv_scr_act(), travel);
  EXPECT_FALSE(travel.empty());

  for (unsigned i = 0; i < 16 && Layer::back() != before; ++i) {
    auto p = Layer::back(); p->onCancel(); scene.root->run();
    if (Layer::back() == p) break;
  }
}

TEST(Nb4Ux, SharingOneCurveBetweenBothSidesIsSaidOnScreen)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  auto has = [](const std::vector<std::string>& v, const char* what) {
    return std::find(v.begin(), v.end(), std::string(what)) != v.end();
  };
  auto mentions = [](const std::vector<std::string>& v, const char* what) {
    for (const auto& s : v)
      if (s.find(what) != std::string::npos) return true;
    return false;
  };

  {
    ExpoData* a = expoAddress(1);
    ExpoData* b = expoAddress(2);
    *b = *a;
    a->mode = 2; b->mode = 1;
    a->curve.type = CURVE_REF_CUSTOM; a->curve.value = makeSourceNumVal(2);
    b->curve.type = CURVE_REF_CUSTOM; b->curve.value = makeSourceNumVal(5);
  }
  ASSERT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).status, Nb4AxisStatus::Ready);
  auto responseTab = [&](const char* capture) {
    auto base = Layer::back();
    QuickMenu::openPage(QM_MODEL_NB4_THROTTLE);
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    EXPECT_TRUE(pickTab(lv_scr_act(), STR_NB4_CURVE));
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    std::vector<std::string> out;
    collectLabels(lv_scr_act(), out);
    if (capture)
      saveFrame(capture, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
    for (unsigned d = 0; d < 16 && Layer::back() != base; ++d) {
      auto pg = Layer::back(); pg->onCancel(); scene.root->run();
      if (Layer::back() == pg) break;
    }
    return out;
  };

  const auto apart = responseTab(nullptr);
  EXPECT_FALSE(mentions(apart, "Mover sus puntos"));

  expoAddress(2)->curve.value = makeSourceNumVal(2);
  const auto v = nb4ResolveAxis(Nb4AxisRole::Throttle);
  ASSERT_EQ(v.status, Nb4AxisStatus::Ready);
  ASSERT_EQ(nb4AxisSharedCurveResource(v), 2u);

  const auto sharedPage = responseTab("shared-curve-throttle");
  EXPECT_TRUE(mentions(sharedPage, "Mover sus puntos"));
  EXPECT_TRUE(mentions(sharedPage, getCurveString(2)));

  char button[64];
  snprintf(button, sizeof(button), "Editar los puntos de %s", getCurveString(2));
  EXPECT_TRUE(has(sharedPage, button));

  EXPECT_TRUE(has(sharedPage, STR_NB4_THROTTLE_EXPO) ||
              has(sharedPage, STR_NB4_EXPO_SIDE));
  EXPECT_TRUE(has(sharedPage, STR_NB4_BRAKE_EXPO) ||
              has(sharedPage, STR_NB4_EXPO_SIDE_F0FC));
}

namespace {

std::string labelInside(lv_obj_t* obj)
{
  if (lv_obj_check_type(obj, &lv_label_class)) {
    const char* t = lv_label_get_text(obj);
    if (t && *t) return t;
  }
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c) {
    const std::string found = labelInside(lv_obj_get_child(obj, c));
    if (!found.empty()) return found;
  }
  return "";
}

bool collectStrayControls(lv_obj_t* obj, std::vector<std::string>& out)
{
  bool controlBelow = false;
  for (uint32_t c = 0; c < lv_obj_get_child_cnt(obj); ++c)
    if (collectStrayControls(lv_obj_get_child(obj, c), out)) controlBelow = true;

  const bool isControl = lv_obj_get_group(obj) != nullptr &&
                         lv_obj_get_user_data(obj) != nullptr;
  if (!isControl) return controlBelow;
  if (controlBelow) return true;
  if (nb4ParamOwnsObject(obj)) return true;

  std::string label;
  if (lv_obj_t* row = lv_obj_get_parent(obj)) {
    for (uint32_t c = 0; c < lv_obj_get_child_cnt(row); ++c) {
      lv_obj_t* sib = lv_obj_get_child(row, c);
      if (sib == obj) continue;
      if (lv_obj_check_type(sib, &lv_label_class)) {
        const char* t = lv_label_get_text(sib);
        if (t && *t) { label = t; break; }
      }
    }
  }
  if (label.empty()) label = labelInside(obj);
  if (label.empty()) label = "(sin rotulo)";
  out.push_back(label);
  return true;
}

}  // namespace

TEST(Nb4Ux, EveryDrivingParameterIsShownInExactlyOnePlace)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  setDefaultInputs();
  for (uint8_t i = 0; i < 2; ++i) {
    MixData* mix = mixAddress(i);
    mix->destCh = i;
    mix->srcRaw = MIXSRC_FIRST_INPUT + i;
    mix->weight = makeSourceNumVal(100);
    mix->swtch = 0;
  }
  g_model.nb4Racing.steeringChannel = 0;
  g_model.nb4Racing.throttleChannel = 1;
  {
    ExpoData* a = expoAddress(1);
    ExpoData* b = expoAddress(2);
    *b = *a;
    a->mode = 2; b->mode = 1;
    a->curve.type = CURVE_REF_EXPO; a->curve.value = makeSourceNumVal(0);
    b->curve.type = CURVE_REF_EXPO; b->curve.value = makeSourceNumVal(0);
  }
  ASSERT_EQ(nb4ResolveAxis(Nb4AxisRole::Throttle).status, Nb4AxisStatus::Ready);
  ASSERT_NE(nb4ResolveAxis(Nb4AxisRole::Steering).lineForPositive,
            nb4ResolveAxis(Nb4AxisRole::Throttle).lineForPositive);

  struct Seen { Nb4Param param; uint8_t ctx; };
  std::vector<Seen> seen;
  std::vector<std::string> steering, throttle;

  unsigned steerCount = 0, thrCount = 0;
  const char* const* sTabs = steeringTabs(&steerCount);
  const char* const* tTabs = throttleTabs(&thrCount);

  for (unsigned page = 0; page < 2; page += 1) {
    const QMPage qm = page == 0 ? QM_MODEL_NB4_STEERING : QM_MODEL_NB4_THROTTLE;
    const char* const* names = page == 0 ? sTabs : tTabs;
    const unsigned count = page == 0 ? steerCount : thrCount;
    for (unsigned t = 0; t < count; t += 1) {
      nb4ParamRegistryReset();

      auto before = Layer::back();
      QuickMenu::openPage(qm);
      for (unsigned f = 0; f < 3; ++f) render(scene.root);
      if (t > 0) {
        nb4ParamRegistryReset();
        ASSERT_TRUE(pickTab(lv_scr_act(), names[t]));
        for (unsigned f = 0; f < 3; ++f) render(scene.root);
      }
      collectLabels(lv_scr_act(), page == 0 ? steering : throttle);

      for (unsigned i = 0; i < nb4ParamRegistrySize(); i += 1) {
        Nb4Param param; uint8_t ctx; unsigned n;
        nb4ParamRegistryEntry(i, &param, &ctx, &n);
        EXPECT_EQ(n, 1u);
        seen.push_back({param, ctx});
      }

      for (unsigned d = 0; d < 16 && Layer::back() != before; ++d) {
        auto pg = Layer::back(); pg->onCancel(); scene.root->run();
        if (Layer::back() == pg) break;
      }
    }
  }

  for (size_t i = 0; i < seen.size(); ++i)
    for (size_t j = i + 1; j < seen.size(); ++j)
      if (seen[i].param == seen[j].param && seen[i].ctx == seen[j].ctx)
        ADD_FAILURE();

  ASSERT_GT(seen.size(), 15u);

  nb4ParamRegistryReset();
  {
    auto base = Layer::back();
    new ThrottleParams();
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    EXPECT_EQ(nb4ParamBuilt(Nb4Param::ThrottleReversed), 1u);
    EXPECT_EQ(nb4ParamBuilt(Nb4Param::ThrottleTraceSource), 1u);
    EXPECT_EQ(nb4ParamBuilt(Nb4Param::ThrottleTrimIdleOnly), 1u);
    EXPECT_EQ(nb4ParamBuilt(Nb4Param::ThrottleTrimSource), 1u);
    for (unsigned d = 0; d < 8 && Layer::back() != base; ++d) {
      auto pg = Layer::back(); pg->onCancel(); scene.root->run();
      if (Layer::back() == pg) break;
    }
  }

  nb4ParamRegistryReset();
  {
    auto base = Layer::back();
    new OutputEditWindow(1);
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    EXPECT_EQ(nb4ParamBuilt(Nb4Param::ChannelTravelMin, 1), 1u);
    EXPECT_EQ(nb4ParamBuilt(Nb4Param::ChannelTravelMax, 1), 1u);
    EXPECT_EQ(nb4ParamBuilt(Nb4Param::ChannelSubtrim, 1), 1u);
    EXPECT_EQ(nb4ParamBuilt(Nb4Param::ChannelReverse, 1), 1u);
    for (unsigned d = 0; d < 8 && Layer::back() != base; ++d) {
      auto pg = Layer::back(); pg->onCancel(); scene.root->run();
      if (Layer::back() == pg) break;
    }
  }

  for (unsigned view = 0; view < 2; view += 1) {
    auto base = Layer::back();
    if (view == 0)
      QuickMenu::openPage(QM_MODEL_NB4_RACING);
    else
      nb4OpenChannelsDialog();
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    nb4ParamRegistryReset();

    std::vector<std::string> shown;
    collectLabels(lv_scr_act(), shown);
    const bool hasReverse =
        std::find(shown.begin(), shown.end(),
                  std::string(nb4ParamLabel(Nb4Param::ChannelReverse))) !=
        shown.end();
    EXPECT_TRUE(hasReverse);
    for (unsigned d = 0; d < 8 && Layer::back() != base; ++d) {
      auto pg = Layer::back(); pg->onCancel(); scene.root->run();
      if (Layer::back() == pg) break;
    }
  }

  const char* allowed[] = {

      "Recorrido", "Curva", "Centro", "Velocidad", "Freno", "Motor",
      "Travel", "Curve", "Centre", "Speed", "Brake", "Engine",
      "Abrir Entradas", "Abrir Mezclas", "Abrir Salidas",
      "Desactivar estas funciones",
      "Ver la traza del gas",
      "Punto de partida", "Eléctrico", "Nitro",
  };
  auto isAllowed = [&](const std::string& label) {
    if (label.rfind("Editar los puntos de", 0) == 0) return true;
    if (label.rfind("?", 0) == 0) return true;   // Help row
    for (const char* a : allowed) if (label == a) return true;
    return false;
  };

  for (QMPage page : {QM_MODEL_NB4_STEERING, QM_MODEL_NB4_THROTTLE}) {
    nb4ParamRegistryReset();
    auto base = Layer::back();
    QuickMenu::openPage(page);
    for (unsigned f = 0; f < 3; ++f) render(scene.root);

    std::vector<std::string> stray;
    ASSERT_NE(Layer::back(), nullptr);
    collectStrayControls(Layer::back()->getLvObj(), stray);
    for (const auto& label : stray)
      EXPECT_TRUE(isAllowed(label));
    for (unsigned d = 0; d < 16 && Layer::back() != base; ++d) {
      auto pg = Layer::back(); pg->onCancel(); scene.root->run();
      if (Layer::back() == pg) break;
    }
  }

  const char* moved[] = {
      STR_NB4_BRAKE_MAX, STR_NB4_DRAG_BRAKE, STR_NB4_ABS_POINT, STR_NB4_ABS_RATE,
      STR_NB4_ABS_RELEASE, STR_NB4_STEER_TURN, STR_NB4_STEER_RETURN,
      STR_NB4_IDLE_UP, STR_NB4_IDLE_UP_SW, STR_NB4_ENGINE_CUT, STR_NB4_CUT_POS,
      STR_TTRIM, STR_TTRIM_SW,
  };
  const auto racing = labelsOfPage(scene.root, QM_MODEL_NB4_RACING);
  auto has = [](const std::vector<std::string>& v, const char* what) {
    return std::find(v.begin(), v.end(), std::string(what)) != v.end();
  };
  for (const char* label : moved) {
    EXPECT_FALSE(has(racing, label));
    EXPECT_TRUE(has(steering, label) || has(throttle, label));
  }

  EXPECT_GT(steering.size(), 6u);
  EXPECT_GT(throttle.size(), 10u);
}

TEST(Nb4Ux, PhysicalAxisEditorsChangeOnlyTheirLabelledSide)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "en", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();
  for (bool throttle : {false, true}) for (int bits = 0; bits < 16; ++bits) {
    SCOPED_TRACE(::testing::Message() << "throttle=" << throttle << " wiring=" << bits);
    setDefaultInputs(); MIXER_RESET();
    const unsigned channel = throttle ? 1 : 0;
    const int sourceSign = bits & 1 ? -1 : 1;
    const int weightSign = bits & 2 ? -1 : 1;
    g_model.throttleReversed = bits & 4 ? 1 : 0;
    auto* out = limitAddress(channel);
    *out = {};
    out->revert = bits & 8 ? 1 : 0;
    auto* a = expoAddress(channel);
    if (!throttle) *expoAddress(2) = *expoAddress(1);
    auto* b = expoAddress(channel + 1);
    *b = *a;
    a->mode = 2; b->mode = 1;
    for (auto* e : {a, b}) {
      e->srcRaw = sourceSign * abs(e->srcRaw);
      e->weight = makeSourceNumVal(100 * weightSign);
      e->curve.type = CURVE_REF_EXPO;
      e->curve.value = makeSourceNumVal(0);
      e->trimSource = TRIM_OFF;
    }
    // NB4 raw wheel left is positive. The throttle's configured acceleration
    // half follows source/input reversal, independently of servo reversal.
    const int side = throttle ? sourceSign * (g_model.throttleReversed ? -1 : 1) : 1;
    const uint8_t stick = abs(a->srcRaw) - MIXSRC_FIRST_STICK;
    auto output = [&](int raw) {
      anaSetFiltered(inputMappingConvertMode(stick), raw);
      evalMixes(1);
      return channelOutputs[channel];
    };
    const int full = abs(output(side * 1024)), other = abs(output(-side * 1024));
    ASSERT_GT(full, 900); ASSERT_GT(other, 900);
    auto base = Layer::back();
    QuickMenu::openPage(throttle ? QM_MODEL_NB4_THROTTLE : QM_MODEL_NB4_STEERING);
    for (unsigned i = 0; i < 3; ++i) render(scene.root);
    auto* travel = numberEditInRowOf(lv_scr_act(), throttle ? "Throttle (%)" : "Left (%)");
    ASSERT_NE(travel, nullptr);
    travel->setValue(travel->getValue() < 0 ? -500 : 500);
    EXPECT_NEAR(abs(output(side * 1024)), full / 2, 8);
    EXPECT_NEAR(abs(output(-side * 1024)), other, 8);
    auto* row = lv_obj_get_parent(travel->getLvObj());
    lv_event_send(lv_obj_get_child(row, -1), LV_EVENT_CLICKED, nullptr);
    for (unsigned i = 0; i < 3; ++i) render(scene.root);
    EXPECT_EQ(travel->getValue(), 505);
    out->min = out->max = 0;
    ASSERT_TRUE(pickTab(lv_scr_act(), "Curve"));
    for (unsigned i = 0; i < 3; ++i) render(scene.root);
    const int before = abs(output(side * 512)), beforeOther = abs(output(-side * 512));
    auto* expo = numberEditInRowOf(lv_scr_act(), throttle ? "Throttle expo" : "Left expo");
    ASSERT_NE(expo, nullptr);
    expo->setValue(50);
    EXPECT_LT(abs(output(side * 512)), before - 100);
    EXPECT_NEAR(abs(output(-side * 512)), beforeOther, 8);
    if (!throttle) {
      a->curve.value = b->curve.value = makeSourceNumVal(0);
      auto* rate = numberEditInRowOf(lv_scr_act(), "Left dual rate");
      ASSERT_NE(rate, nullptr);
      rate->setValue(50 * weightSign);
      EXPECT_NEAR(abs(output(side * 1024)), full / 2, 8);
      EXPECT_NEAR(abs(output(-side * 1024)), other, 8);
    }
    for (unsigned i = 0; i < 16 && Layer::back() != base; ++i) {
      auto p = Layer::back(); p->onCancel(); scene.root->run();
      if (p == Layer::back()) break;
    }
  }
}

TEST(Nb4Ux, TravelFieldsShowTheRealPercentageNotTheStoredOffset)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();

  setDefaultInputs();
  for (uint8_t i = 0; i < 2; ++i) {
    MixData* mix = mixAddress(i);
    mix->destCh = i; mix->srcRaw = MIXSRC_FIRST_INPUT + i;
    mix->weight = makeSourceNumVal(100); mix->swtch = 0;
  }
  g_model.nb4Racing.steeringChannel = 0;
  g_model.nb4Racing.throttleChannel = 1;
  g_model.extendedLimits = 0;
  LimitData* out = limitAddress(0);
  out->min = 0; out->max = 0;

  ASSERT_EQ(LIMIT_MIN(out), -LIMIT_STD_MAX);
  ASSERT_EQ(LIMIT_MAX(out), +LIMIT_STD_MAX);

  const auto labels = labelsOfPage(scene.root, QM_MODEL_NB4_STEERING);
  auto has = [&](const char* what) {
    return std::find(labels.begin(), labels.end(), std::string(what)) != labels.end();
  };

  unsigned hundreds = 0;
  for (const auto& l : labels) if (l == "100.0") hundreds += 1;
  EXPECT_GE(hundreds, 2u);
  EXPECT_FALSE(has("-100.0"));

  EXPECT_TRUE(has("Izquierda (%)"));

  unsigned zeros = 0;
  for (const auto& l : labels) if (l == "0.0") zeros += 1;
  EXPECT_LE(zeros, 1u);

  auto before = Layer::back();
  QuickMenu::openPage(QM_MODEL_NB4_STEERING);
  for (unsigned i = 0; i < 3; ++i) { lv_obj_update_layout(scene.root->getLvObj()); lv_tick_inc(50); lv_timer_handler(); scene.root->run(); }

  const uint8_t steerChn = inputMappingGetThrottle() == 0 ? 1 : 0;
  anaSetFiltered(inputMappingConvertMode(steerChn), 1024);
  evalMixes(1);
  const int16_t fullTravel = channelOutputs[0];
  ASSERT_NE(fullTravel, 0);

  NumberEdit* left = numberEditInRowOf(lv_scr_act(), "Izquierda (%)");
  ASSERT_NE(left, nullptr);
  left->setValue(500);
  for (unsigned i = 0; i < 3; ++i) { lv_obj_update_layout(scene.root->getLvObj()); lv_tick_inc(50); lv_timer_handler(); scene.root->run(); }

  EXPECT_EQ(LIMIT_MAX(out), 500);

  anaSetFiltered(inputMappingConvertMode(steerChn), 1024);
  evalMixes(1);
  const int16_t halvedTravel = channelOutputs[0];
  EXPECT_NEAR(abs(halvedTravel), abs(fullTravel) / 2, 8);

  for (unsigned i = 0; i < 16 && Layer::back() != before; ++i) {
    auto p = Layer::back(); p->onCancel(); scene.root->run();
    if (Layer::back() == p) break;
  }
}

namespace {

struct RouteDestination { const char* path; const char* mustShow; };

const RouteDestination kDestinations[] = {
    {"settings/car/general", "Nombre modelo"},
    {"settings/car/safety", "Seguridad al encender"},
    {"settings/car/presets", "Punto de partida"},
    {"settings/receiver_rf/module", "RF interna"},
    {"settings/steering/travel", "Centro"},
    {"settings/steering/curve", "Centro"},
    {"settings/steering/centre", "Centro"},
    {"settings/steering/speed", "Centro"},
    {"settings/throttle_brake/travel", "Curva de gas y freno"},
    {"settings/throttle_brake/curve", "Curva de gas y freno"},
    {"settings/throttle_brake/brake", "Curva de gas y freno"},
    {"settings/throttle_brake/engine", "Curva de gas y freno"},

    {"settings/controls/assignments", "Asignar pulsando"},

    {"settings/controls/channels", "Canales"},
    {"settings/controls/trims", "Paso trim"},
    {"settings/controls/general", "Atraso switches"},
    {"settings/controls/shortcuts", "Asignar pulsando"},
    {"settings/controls/monitor", "MONITOR CANALES 1/8"},
    {"settings/telemetry/sensors", "Sensores"},

    {"settings/telemetry/alerts", "Alarmas"},
    {"settings/telemetry/track_view", "Telemetría"},
    {"settings/race/timers", "Cronómetros"},
    {"settings/race/timer_laps", "TIEMPO DE CARRERA"},
    {"settings/race/statistics", "Battery"},
    {"settings/race/pit", "DEPÓSITO / PACK"},
    {"settings/race/history", "Registro de mangas"},
    {"settings/models/management", "Modelos"},
    {"settings/display/top_bar", "Config. widgets"},
    {"settings/display/screens", "PALETA"},
    {"settings/display/theme", "TEMAS"},

    {"settings/display/home", "PALETA"},

    {"settings/sound_alerts/alerts", "Batería baja"},
    {"settings/sound_alerts/sound", "Volumen"},
    {"settings/sound_alerts/haptic", "Modo"},
    {"settings/sound_alerts/lights", "Luces"},
    {"settings/connectivity/usb", "Modo USB"},
    {"settings/system/general", "Unidades"},

    {"settings/system/power", "Atraso apagado"},
    {"settings/system/hardware", "Calibración batería"},
    {"settings/system/calibration", "CALIBRACIÓN"},
    {"settings/system/storage", "TARJETA SD"},
    {"settings/system/backup_restore", "Copias y restauración"},
    {"settings/system/update", "Actualizar"},
    {"settings/display/brightness", "Brillo"},

    {"settings/system/date_time_location", "Zona horaria"},
    {"settings/system/diagnostics", "Tmix máx"},
    {"settings/system/about", "https://github.com/GastonGelhorn/apextx"},
    {"settings/advanced/inputs", "ENTRADAS"},
    {"settings/advanced/mixes", "MEZCLAS"},
    {"settings/advanced/outputs", "Ampliar límites"},
    {"settings/advanced/curves", "CURVAS"},
    {"settings/advanced/logic", "INTERRUPTORES LÓGICOS"},
    {"settings/advanced/automation", "FUNCIONES ESPECIALES"},

    {"settings/advanced/variables", "Variables del modelo"},

    {"settings/race/resets", "Reset Reloj 1"},
};

}  // namespace

TEST(Nb4Ux, EveryAvailableRouteActuallyOpensSomethingAndComesBack)
{
  Scene scene;
  const auto dir = std::filesystem::temp_directory_path() / ("nb4-routes-" + std::to_string(getpid()));
  for (const char* folder : {"RADIO", "MODELS", "THEMES", "SCRIPTS/TOOLS", "LOGS"})
    std::filesystem::create_directories(dir / folder);
  struct Cleanup { std::filesystem::path path; ~Cleanup() { simuFatfsSetPaths(TESTS_PATH, nullptr); std::filesystem::remove_all(path); } } cleanup{dir};
  simuFatfsSetPaths(dir.c_str(), nullptr);
  strAppend(g_eeGeneral.currModelFilename, "routes-test.yml", LEN_MODEL_FILENAME);
  auto model = modelslist.getCurrentModel();
  if (!model) model = modelslist.addModel("routes-test.yml", false);
  modelslist.setCurrentModel(model);
  nb4AcceptNewCarModel();
  scene.applyPalette("ApexTX Dark");
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  ViewMain::instance();
  auto base = Layer::back();
  ASSERT_NE(base, nullptr);

  unsigned count = 0, opened = 0, guarded = 0;
  unsigned checkedDestination = 0, menuDestination = 0;
  const Nb4Route* routes = nb4Routes(&count);

  for (unsigned i = 0; i < count; ++i) {
    const Nb4Route& r = routes[i];
    if (r.state != Nb4RouteState::Available) continue;

    if (!nb4RouteIsOpenable(r)) { ++guarded; continue; }

    ASSERT_TRUE(nb4OpenRoute(r.path));
    for (unsigned f = 0; f < 3; ++f) render(scene.root);
    EXPECT_NE(Layer::back(), base);
    opened += 1;

    expectEveryGlyphRenderable(lv_scr_act(), r.path);

    const char* mustShow = nullptr;
    bool declared = false;
    for (const auto& d : kDestinations)
      if (strcmp(d.path, r.path) == 0) { mustShow = d.mustShow; declared = true; break; }
    EXPECT_TRUE(declared);
    if (declared && mustShow && *mustShow) {
      std::vector<std::string> ls;
      collectLabels(lv_scr_act(), ls);
#if defined(RADIO_NB4) && !defined(RTCLOCK)
      if (!strcmp(r.path, "settings/system/general") ||
          !strcmp(r.path, "settings/system/date_time_location")) {
        EXPECT_EQ(std::find(ls.begin(), ls.end(), "Fecha"), ls.end());
        EXPECT_EQ(std::find(ls.begin(), ls.end(), "Ajustar RTC"), ls.end());
      }
#endif
      const bool found =
          std::find(ls.begin(), ls.end(), std::string(mustShow)) != ls.end();
      EXPECT_TRUE(found);
      if (found) ++checkedDestination;
    } else if (declared) {
      ++menuDestination;
    }

    Keyboard::hide(false);
    for (unsigned depth = 0; depth < 24 && Layer::back() != base; ++depth) {
      auto page = Layer::back();
      page->onCancel();
      scene.root->run();
      if (Layer::back() == page) break;
    }
    EXPECT_EQ(Layer::back(), base);
    EXPECT_EQ(lv_mem_test(), LV_RES_OK);
  }

  EXPECT_GT(opened, 45u);
  EXPECT_LT(guarded, 8u);
  EXPECT_GT(checkedDestination, 45u);
  EXPECT_EQ(menuDestination, 0u);
  nb4RacingReset();
}

TEST(Nb4RacingUi, AllDestinationsAndEditorsInBothLanguagesAndOrientations)
{
  Scene scene;
  const auto directory = std::filesystem::temp_directory_path() / ("nb4-pages-ui-" + std::to_string(getpid()));
  for (const char* folder : {"RADIO", "MODELS", "THEMES", "SCRIPTS/TOOLS", "LOGS"}) std::filesystem::create_directories(directory / folder);
  struct Cleanup { std::filesystem::path path; ~Cleanup() { simuFatfsSetPaths(TESTS_PATH, nullptr); std::filesystem::remove_all(path); } } cleanup{directory};
  simuFatfsSetPaths(directory.c_str(), nullptr);
  strAppend(g_eeGeneral.currModelFilename, "racing-test.yml", LEN_MODEL_FILENAME);
  auto model = modelslist.getCurrentModel();
  if (!model) model = modelslist.addModel("racing-test.yml", false);
  model->setModelName(g_model.header.name); modelslist.setCurrentModel(model);
  nb4AcceptNewCarModel(); seedLaps();
  const auto originalLang = currentLangStrings;
  auto& sensor = g_model.telemetrySensors[0];
  sensor.id = 0x1000; sensor.type = TELEM_TYPE_CUSTOM; sensor.unit = UNIT_VOLTS; sensor.prec = 1;
  memcpy(sensor.label, "RxV", 3);
  telemetryItems[0].value = 60; telemetryItems[0].timeout = TELEMETRY_SENSOR_TIMEOUT_START;
  telemetryStreaming = 100;
  g_model.moduleData[0].type = MODULE_TYPE_FLYSKY_AFHDS3;
  const auto outputs = std::array<int16_t, 2>{channelOutputs[0], channelOutputs[1]};
  const struct { Nb4Section section; const char* name; } sections[] = {
    {Nb4Section::Car, "car"}, {Nb4Section::Steering, "steering"}, {Nb4Section::Throttle, "throttle"},
    {Nb4Section::Auxiliary, "aux"}, {Nb4Section::Advanced, "advanced"},
    {Nb4Section::System, "system"}, {Nb4Section::Appearance, "appearance"},
    {Nb4Section::Race, "race-setup"}, {Nb4Section::Chrono, "chrono"},
    {Nb4Section::Pit, "pit"}, {Nb4Section::Telemetry, "telemetry"}, {Nb4Section::History, "history"}, {Nb4Section::Backup, "backup"}};
  ViewMain::instance();
  auto originalLayer = Layer::back();
  for (unsigned orientation = 0; orientation < 2; ++orientation) {
    scene.orient(orientation);
    for (unsigned language = 0; language < 2; ++language) {
      memcpy(g_eeGeneral.uiLanguage, language ? "en" : "es", 2);
      currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
      scene.applyPalette("ApexTX Dark");
      auto capture = [&](const char* name) {
        nb4StorageProcess();
        for (unsigned frame = 0; frame < 3; ++frame) render(scene.root);
        char file[100]; snprintf(file, sizeof(file), "racing-%s-%s-%s", orientation ? "landscape" : "portrait", language ? "en" : "es", name);
        saveFrame(file, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
        expectEveryGlyphRenderable(lv_scr_act(), file);
        ASSERT_EQ(lv_mem_test(), LV_RES_OK);
        Keyboard::hide(false);
        for (unsigned depth = 0; depth < 16 && Layer::back() != originalLayer; ++depth) {
          auto page = Layer::back(); ASSERT_NE(page, nullptr); page->onCancel(); scene.root->run();
          ASSERT_NE(Layer::back(), page);
        }
        ASSERT_EQ(Layer::back(), originalLayer);
      };

      ViewMain::instance()->openMenu();
      capture("quick-menu");

      nb4OpenSettingsModal();
      capture("settings-modal");
      nb4OpenQuickAccessModal();
      capture("quick-access");

      for (const auto& entry : sections) {
        nb4OpenSection(entry.section);
        if (entry.section == Nb4Section::Telemetry) {
          for (unsigned sample = 0; sample < 60; ++sample) {
            telemetryItems[0].value = 60 + (sample % 11) - 5;
            lv_tick_inc(500); render(scene.root);
          }
        }
        capture(entry.name);
      }
      new OutputEditWindow(0); capture("output-editor");
      new CurveEditWindow(0); capture("curve-editor");
      new InputEditWindow(0, 0); capture("input-editor");
      new TimerWindow(0); capture("timer-editor");
      const std::pair<QMPage, const char*> pages[] = {
        {QM_MODEL_SETUP, "model-profile"},
        {QM_MODEL_NB4_STEERING, "steering-setup"}, {QM_MODEL_NB4_THROTTLE, "throttle-setup"},
        {QM_MODEL_INPUTS, "inputs"}, {QM_MODEL_MIXES, "mixes"},
        {QM_MODEL_CURVES, "curves"}, {QM_MODEL_GVARS, "variables"}, {QM_MODEL_LS, "logical-switches"},
        {QM_MODEL_SF, "safety"}, {QM_MODEL_TELEMETRY, "sensor-setup"},
        {QM_RADIO_SETUP, "radio-setup"}, {QM_RADIO_HARDWARE, "controls"},
        {QM_RADIO_GF, "global-functions"}, {QM_TOOLS_STATS, "statistics"},
        {QM_TOOLS_DEBUG, "diagnostics"}, {QM_RADIO_VERSION, "version"},
        {QM_TOOLS_STORAGE, "files"}, {QM_TOOLS_APPS, "lua"}, {QM_UI_THEMES, "themes"}};
      for (const auto& page : pages) { QuickMenu::openPage(page.first); capture(page.second); }
      new ChannelsViewMenu(); capture("channel-monitor");
      new RadioCalibrationPage(); capture("calibration");
      QuickMenu::openPage(QM_MANAGE_MODELS); capture("models");
      auto keyboardPage = new SubPage(ICON_MODEL_SETUP, "Car name", "");
      auto text = new ModelTextEdit(keyboardPage, {8, 60, lv_disp_get_hor_res(nullptr) - 16, 44}, g_model.header.name, LEN_MODEL_NAME);
      text->onPress(); capture("keyboard");
      new FullScreenDialog(WARNING_TYPE_ALERT, "Low battery", "Check transmitter power.", STR_OK); capture("warning");
      new ModulePage(INTERNAL_MODULE);

      capture("receiver");
      new ModulePage(INTERNAL_MODULE);
      render(scene.root);
      EXPECT_TRUE(openHelpSheet(lv_scr_act()));
      capture("receiver-help");
      new ConfirmDialog("Finish run", "Keep the result of this run.", [] {}); capture("dialog");
      auto menu = new Menu(); menu->addLine("AFHDS3", [] {}); menu->addLine("Channels & failsafe", [] {}); capture("selector");
      EXPECT_EQ(channelOutputs[0], outputs[0]); EXPECT_EQ(channelOutputs[1], outputs[1]);
    }
  }
  currentLangStrings = originalLang;
  nb4RacingReset();
}

TEST(Nb4RacingUi, SavedHistoryListAndDetailUseDurableNativeRecords)
{
  Scene scene;
  const auto dir = std::filesystem::temp_directory_path() / ("nb4-history-ui-" + std::to_string(getpid()));
  std::filesystem::create_directories(dir);
  struct Cleanup { std::filesystem::path path; ~Cleanup() { simuFatfsSetPaths(TESTS_PATH, nullptr); std::filesystem::remove_all(path); } } cleanup{dir};
  simuFatfsSetPaths(dir.c_str(), nullptr); nb4StorageResume();
  nb4AcceptNewCarModel(); seedLaps(); nb4RaceFinish(); nb4RaceProcessCommands(); nb4StorageProcess();
  auto token = nb4HistoryRequest(); nb4StorageProcess(); Nb4HistoryView view{};
  ASSERT_TRUE(nb4HistoryPoll(token, view)); ASSERT_GT(view.count, 0);
  auto layer = Layer::back();
  for (unsigned orientation = 0; orientation < 2; ++orientation) {
    scene.orient(orientation);
    for (unsigned language = 0; language < 2; ++language) {
      memcpy(g_eeGeneral.uiLanguage, language ? "en" : "es", 2); scene.applyPalette("ApexTX Dark");
      for (unsigned detail = 0; detail < 2; ++detail) {
        if (detail) nb4OpenRaceRecord(view.records[0].id); else nb4OpenSection(Nb4Section::History);
        for (unsigned frame = 0; frame < 4; ++frame) { render(scene.root); nb4StorageProcess(); }
        char file[80]; snprintf(file, sizeof(file), "racing-%s-%s-history-%s", orientation ? "landscape" : "portrait", language ? "en" : "es", detail ? "detail" : "saved");
        saveFrame(file, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
        Layer::back()->onCancel(); scene.root->run(); EXPECT_EQ(Layer::back(), layer);
      }
    }
  }
}

TEST(Nb4RacingUi, EmptyAndSparseTablesCanShrinkAndReceiveNavigation)
{
  Scene scene;
  auto table = new TableField(scene.root, {8, 60, 304, 300});
  table->setRowCount(0); table->select(0, 0); table->adjustScroll();
  table->setRowCount(20); lv_table_set_col_cnt(table->getLvObj(), 3);
  lv_table_set_cell_value(table->getLvObj(), 8, 1, "AFHDS3");
  lv_table_set_col_cnt(table->getLvObj(), 1); table->setRowCount(0);
  table->selectNext(1); table->adjustScroll();
  static_cast<Window*>(table)->deleteLater(); scene.root->run();
  ASSERT_EQ(lv_mem_test(), LV_RES_OK);
}

TEST(Nb4RacingUi, HomeInstrumentTapOpensOneCorrespondingPage)
{
  Scene scene; nb4AcceptNewCarModel(); auto main = ViewMain::instance();
  auto home = new Nb4HomeScreen(scene.root, {0, 0, 320, 480});

  lv_obj_t* gauge = nullptr;
  lv_obj_t* throttle = nullptr;
  auto obj = home->getLvObj();
  auto hasChildOfType = [](lv_obj_t* parent, const lv_obj_class_t* type) {
    for (unsigned j = 0; j < lv_obj_get_child_cnt(parent); ++j)
      if (lv_obj_check_type(lv_obj_get_child(parent, j), type)) return true;
    return false;
  };
  for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i) {
    auto child = lv_obj_get_child(obj, i);
    if (!gauge && hasChildOfType(child, &lv_arc_class)) { gauge = child; continue; }

    if (gauge && hasChildOfType(child, &lv_canvas_class)) { throttle = child; break; }
  }
  ASSERT_NE(gauge, nullptr);
  ASSERT_NE(throttle, nullptr);
  auto countLayers = [] { unsigned n = 0; Layer::walk([&](Window*) { ++n; return false; }); return n; };
  auto before = countLayers(); auto output = channelOutputs[0];
  lv_event_send(gauge, LV_EVENT_CLICKED, nullptr);
  EXPECT_EQ(countLayers(), before + 1);
  auto page = Layer::back(); ASSERT_NE(page, main);
  page->onCancel(); scene.root->run(); EXPECT_EQ(Layer::back(), main);
  EXPECT_EQ(channelOutputs[0], output);

  before = countLayers(); auto throttleOutput = channelOutputs[1];
  lv_event_send(throttle, LV_EVENT_CLICKED, nullptr);
  EXPECT_EQ(countLayers(), before + 1);
  page = Layer::back(); ASSERT_NE(page, main);
  page->onCancel(); scene.root->run(); EXPECT_EQ(Layer::back(), main);
  EXPECT_EQ(channelOutputs[1], throttleOutput);
  home->deleteLater(); scene.root->run();
}

TEST(Nb4RacingUi, TenThousandRealNavigationOperationsRetainMemoryAndControl)
{
  Scene scene;
  nb4AcceptNewCarModel(); scene.applyPalette("ApexTX Dark");
  auto rootLayer = Layer::back();
  const Nb4Section sections[] = {Nb4Section::Car, Nb4Section::Steering, Nb4Section::Throttle,
    Nb4Section::Auxiliary, Nb4Section::Advanced, Nb4Section::System, Nb4Section::Appearance,
    Nb4Section::Race, Nb4Section::Chrono, Nb4Section::Pit, Nb4Section::Telemetry, Nb4Section::History};
  const auto nativeTimer = timersStates[0].val;
  const auto output = channelOutputs[1];
  lv_mem_monitor_t warm{}, final{};
  // 120 warm-up operations, then 10,000 measured open/selector/back/back actions.
  for (unsigned i = 0; i < 2530; ++i) {
    nb4OpenSection(sections[i % DIM(sections)]);
    auto page = Layer::back(); ASSERT_NE(page, rootLayer);
    auto selector = new Menu(); selector->addLine("AFHDS3", [] {});
    selector->onCancel(); page->onCancel(); scene.root->run();
    ASSERT_EQ(Layer::back(), rootLayer);
    // Complete asynchronous reads without keeping deleted UI callbacks.
    nb4StorageProcess();
    if (i == 29) lv_mem_monitor(&warm);
    if (i % 100 == 0) {
      ASSERT_EQ(lv_mem_test(), LV_RES_OK);
      EXPECT_EQ(timersStates[0].val, nativeTimer);
      EXPECT_EQ(channelOutputs[1], output);
    }
  }
  lv_mem_monitor(&final);
  EXPECT_EQ(final.used_cnt, warm.used_cnt);
  EXPECT_GE(final.free_size + 64u, warm.free_size);
  fprintf(stderr, "ApexTX: 10000 real navigation operations; free %u -> %u, blocks %u -> %u\n",
    unsigned(warm.free_size), unsigned(final.free_size), unsigned(warm.used_cnt), unsigned(final.used_cnt));
}

TEST(Nb4Ux, AssignmentsAreReadableAndSaveOnlyWhenRequested)
{
  Scene scene;
  nb4AcceptNewCarModel(); scene.applyPalette("ApexTX Dark");
  ViewMain::instance();
  const auto base = Layer::back();
  auto home = new Nb4HomeScreen(scene.root, {0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)});
  auto settle = [&]() { for (unsigned i = 0; i < 3; ++i) render(scene.root); };
  auto close = [&]() { Layer::back()->onCancel(); settle(); };
  auto verify = [&](const char* shot) {
    settle();
    auto layer = Layer::back()->getLvObj();
    expectEveryGlyphRenderable(layer, shot);
    lv_area_t box; lv_obj_get_coords(lv_obj_get_child(layer, 0), &box);
    EXPECT_GE(box.x1, 0); EXPECT_LT(box.x2, lv_disp_get_hor_res(nullptr));
    EXPECT_GE(box.y1, 0); EXPECT_LT(box.y2, lv_disp_get_ver_res(nullptr));
    std::function<void(lv_obj_t*)> measure = [&](lv_obj_t* obj) {
      if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
      if (lv_obj_check_type(obj, &lv_label_class)) {
        const char* text = lv_label_get_text(obj);
        if (!text || !*text) return;
        lv_point_t size;
        lv_txt_get_size(&size, text, lv_obj_get_style_text_font(obj, 0),
                       lv_obj_get_style_text_letter_space(obj, 0), lv_obj_get_style_text_line_space(obj, 0),
                       lv_obj_get_content_width(obj), LV_TEXT_FLAG_NONE);
        EXPECT_LE(size.y, lv_obj_get_content_height(obj))
            << text;
      }
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); ++i) measure(lv_obj_get_child(obj, i));
    };
    measure(layer);
    saveFrame(shot, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr));
  };
  for (unsigned landscape = 0; landscape < 2; ++landscape) {
    scene.orient(landscape);
    home->setRect({0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)});
    home->checkEvents();
    for (const char* language : {"es", "en"}) {
      memcpy(g_eeGeneral.uiLanguage, language, 2);
      currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
      nb4ControlSetBinding(2, NB4_CONTROL_RUN_PAUSE);
      nb4ControlSetBinding(3, NB4_CONTROL_LAP);
      nb4ControlSetBinding(8, NB4_CONTROL_PREVIOUS);
      nb4ControlSetBinding(9, NB4_CONTROL_NEXT);
      const std::string suffix = std::string(landscape ? "landscape-" : "portrait-") + language;
      nb4OpenAssignmentsList(); verify(("assignments-" + suffix).c_str());
      if (landscape) {
        std::vector<lv_obj_t*> rows;
        std::function<void(lv_obj_t*)> findRows = [&](lv_obj_t* obj) {
          if (lv_obj_check_type(obj, &lv_label_class) &&
              (!strncmp(lv_label_get_text(obj), "SW2  ", 5) || !strncmp(lv_label_get_text(obj), "SW3  ", 5))) rows.push_back(lv_obj_get_parent(obj));
          for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); ++i) findRows(lv_obj_get_child(obj, i));
        };
        findRows(Layer::back()->getLvObj()); ASSERT_EQ(rows.size(), 2u);
        lv_area_t a, b; lv_obj_get_coords(rows[0], &a); lv_obj_get_coords(rows[1], &b);
        EXPECT_EQ(a.y1, b.y1);
        EXPECT_GT(b.x1, a.x2);
      }
      close();
      nb4OpenAssignments(); verify(("assign-function-" + suffix).c_str());
      ASSERT_TRUE(clickLabel(Layer::back()->getLvObj(), STR_NB4_ASSIGN_BY_PRESSING));
      verify(("learn-control-" + suffix).c_str());
      EXPECT_TRUE(nb4ControlsLearning());
      close(); EXPECT_FALSE(nb4ControlsLearning());
      close();
      nb4OpenNavigationAssignments(); verify(("navigation-" + suffix).c_str()); close();
      for (unsigned key : {2u, 8u, 11u}) {
        if (key == 11) nb4ControlSetBinding(11, NB4_CONTROL_RESET);
        nb4OpenControlAssignment(key);
        verify(("assignment-" + std::to_string(key) + "-" + suffix).c_str()); close();
      }
      ViewMain::instance()->openMenu(); verify(("settings-header-" + suffix).c_str()); close();
    }
  }
  scene.orient(false);
  memcpy(g_eeGeneral.uiLanguage, "es", 2);
  currentLangStrings = langStrings[getLanguageId(g_eeGeneral.uiLanguage)];
  nb4ControlSetBinding(2, NB4_CONTROL_DEFAULT);
  auto chooseGroup = [&]() {
    std::vector<Choice*> choices;
    std::function<void(lv_obj_t*)> collect = [&](lv_obj_t* obj) {
      if (auto window = (Window*)lv_obj_get_user_data(obj))
        if (auto choice = dynamic_cast<Choice*>(window)) choices.push_back(choice);
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); ++i) collect(lv_obj_get_child(obj, i));
    };
    collect(Layer::back()->getLvObj());
    ASSERT_FALSE(choices.empty());
    choices[0]->onClicked(); settle();
    auto menu = dynamic_cast<Menu*>(Layer::back()); ASSERT_NE(menu, nullptr);
    std::function<TableField*(lv_obj_t*)> find = [&](lv_obj_t* obj) -> TableField* {
      if (lv_obj_has_class(obj, &lv_table_class)) return dynamic_cast<TableField*>((Window*)lv_obj_get_user_data(obj));
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); ++i) if (auto table = find(lv_obj_get_child(obj, i))) return table;
      return nullptr;
    };
    auto table = find(menu->getLvObj()); ASSERT_NE(table, nullptr);
    table->onPress(2, 0); settle(); // Timer/laps: first action is Start/pause.
  };
  nb4OpenControlAssignment(2); settle(); chooseGroup();
  EXPECT_EQ(nb4ControlBinding(2), NB4_CONTROL_DEFAULT);
  close(); // Cancel leaves persisted binding unchanged.
  EXPECT_EQ(nb4ControlBinding(2), NB4_CONTROL_DEFAULT);
  nb4OpenControlAssignment(2); settle(); chooseGroup();
  ASSERT_TRUE(clickLabel(Layer::back()->getLvObj(), "Guardar asignación")); settle();
  EXPECT_EQ(nb4ControlBinding(2), NB4_CONTROL_RUN_PAUSE);
  EXPECT_EQ(Layer::back(), base);
  home->deleteLater(); scene.root->run();
}

// Exercise the real GUI loop after the deterministic screenshot suite: it
// initialises the hardware input drivers and their focus groups.

namespace {
unsigned motionImageDraws;
decltype(lv_draw_ctx_t::draw_img_decoded) motionDrawImage;
void countMotionImage(lv_draw_ctx_t* ctx, const lv_draw_img_dsc_t* dsc,
                     const lv_area_t* area, const uint8_t* pixels, lv_img_cf_t format)
{
  ++motionImageDraws;
  motionDrawImage(ctx, dsc, area, pixels, format);
}
}

TEST(Nb4Performance, HomeContinuousMotionRenderCost)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
  for (bool landscape : {false, true}) {
    scene.orient(landscape);
    const rect_t bounds{0, 0, lcdWidth, lcdHeight};
    auto page = new NavWindow(scene.root, bounds);
    page->pushLayer();
    auto home = new Nb4HomeScreen(page, bounds);
    render(scene.root);
    auto ctx = lv_disp_get_default()->driver->draw_ctx;
    motionDrawImage = ctx->draw_img_decoded;
    ctx->draw_img_decoded = countMotionImage;
    std::vector<double> duration;
    duration.reserve(240);
    unsigned images = 0, maxImages = 0;
    unsigned long pixels = 0;
    for (int i = 0; i < 240; ++i) {
      calibratedAnalogs[ADC_MAIN_ST] = (i % 80 - 40) * RESX / 40;
      calibratedAnalogs[ADC_MAIN_TH] = ((i * 3) % 80 - 40) * RESX / 40;
      motionImageDraws = 0;
      resetFlushAccounting();
      const auto start = std::chrono::steady_clock::now();
      home->checkEvents();
      renderIncremental(scene.root);
      duration.push_back(std::chrono::duration<double, std::micro>(
          std::chrono::steady_clock::now() - start).count());
      images += motionImageDraws;
      maxImages = std::max(maxImages, motionImageDraws);
      pixels += flushedPixels;
    }
    ctx->draw_img_decoded = motionDrawImage;
    EXPECT_LT(maxImages, 40u);
    std::sort(duration.begin(), duration.end());
    printf("NB4 motion %s: median %.0f us, p95 %.0f us, max %.0f us; images/frame %u max %u; pixels/frame %lu\n",
           landscape ? "landscape" : "portrait", duration[120], duration[228], duration.back(),
           images / 240, maxImages, pixels / 240);
    page->deleteLater(); scene.root->run();
  }
}

TEST(Nb4Performance, HomeGuiCyclePresentsTheNewestControlPosition)
{
  Scene scene;
  scene.applyPalette("ApexTX Dark");
#if defined(LUA)
  // Navigation tests may leave a script reload pending. This fixture has no
  // Lua runtime or SD card; exercise the GUI loop with script execution paused.
  struct PauseLua {
    uint8_t previous = luaState;
    PauseLua() { luaState = INTERPRETER_PAUSED; }
    ~PauseLua() { luaState = previous; }
  } pauseLua;
#endif
  for (bool landscape : {false, true}) {
    scene.orient(landscape);
    const rect_t bounds{0, 0, lcdWidth, lcdHeight};
    auto page = new NavWindow(scene.root, bounds);
    page->pushLayer();
    new Nb4HomeScreen(page, bounds);
    render(scene.root);
    for (int value : {-RESX, RESX, 0, -RESX / 2, RESX / 2}) {
      calibratedAnalogs[ADC_MAIN_ST] = value;
      calibratedAnalogs[ADC_MAIN_TH] = -value;
      lv_tick_inc(20);
      guiMain(0);
      const auto shown = frame;

      render(scene.root);
      EXPECT_EQ(memcmp(shown.data(), frame.data(), frame.size() * sizeof(lv_color_t)), 0);
    }
    page->deleteLater(); scene.root->run();
  }
}

TEST(Nb4Performance, ChildEventSnapshotSurvivesSiblingRemovalAndInsertion)
{
  Scene scene;
  struct Child : Window {
    unsigned calls = 0;
    std::function<void()> action;
    explicit Child(Window* parent) : Window(parent, {0, 0, 10, 10}) {}
    void checkEvents() override {
      ++calls;
      if (action) action();
      Window::checkEvents();
    }
  };
  // Exercise both the stack snapshot and the larger-list fallback.
  for (unsigned count : {8u, 20u}) {
    auto parent = new Window(scene.root, {0, 0, 40, 40});
    std::vector<Child*> original;
    for (unsigned i = 0; i < count; ++i) original.push_back(new Child(parent));
    Child* added = nullptr;
    original.front()->action = [&] {
      if (added) return;
      original[1]->deleteLater();
      added = new Child(parent);
    };
    parent->checkEvents();
    EXPECT_EQ(original[1]->calls, 0u);
    EXPECT_EQ(added->calls, 0u);
    EXPECT_EQ(original.back()->calls, 1u);
    parent->checkEvents();
    EXPECT_EQ(added->calls, 1u);
    EXPECT_EQ(original.back()->calls, 2u);
    parent->deleteLater(); scene.root->run();
  }
}

TEST(Nb4Performance, RemappedGripKeysNavigateSelectAndReturnThroughLvgl)
{
  Scene scene;
  nb4AcceptNewCarModel(); scene.applyPalette("ApexTX Dark");
  ViewMain::instance();
  for (unsigned i = 0; i < MAX_KEYS; ++i) simuSetKey(i, false);
  nb4RacingReset();
  nb4ControlSetBinding(0, NB4_CONTROL_NEXT);
  nb4ControlSetBinding(1, NB4_CONTROL_ENTER);
  auto cycle = [&]() {
    keysPollingCycle();
    scene.root->run();
    lv_tick_inc(20);
    LvglWrapper::instance()->run();
  };
  for (unsigned i = 0; i < 10; ++i) cycle();
  auto press = [&](EnumKeys key) {
    simuSetKey(key, true);
    for (unsigned i = 0; i < 8; ++i) cycle();
    simuSetKey(key, false);
    for (unsigned i = 0; i < 12; ++i) cycle();
  };
  const auto base = Layer::back();
  nb4OpenAssignmentsList();
  for (unsigned i = 0; i < 3; ++i) cycle();
  auto list = Layer::back(); ASSERT_NE(list, base);
  auto first = lv_group_get_focused(lv_group_get_default());
  ASSERT_NE(first, nullptr);
  press(KEY_EXIT); // physical SW1-L is now Next.
  auto next = lv_group_get_focused(lv_group_get_default());
  ASSERT_NE(next, first);
  press(KEY_ENTER);
  ASSERT_NE(Layer::back(), list);
  std::vector<std::string> labels;
  collectLabels(Layer::back()->getLvObj(), labels);
  EXPECT_NE(std::find(labels.begin(), labels.end(), "SW3"), labels.end());
  nb4ControlSetBinding(0, NB4_CONTROL_BACK);
  for (unsigned i = 0; i < 10; ++i) cycle();
  press(KEY_EXIT);
  EXPECT_EQ(Layer::back(), list);
  press(KEY_EXIT);
  EXPECT_EQ(Layer::back(), base);

  // Function-first learning goes through the actual key driver and LVGL loop.
  g_model.timers[0].mode = TMRMODE_OFF;
  nb4RacingReset();
  nb4OpenAssignments();
  for (unsigned i = 0; i < 3; ++i) cycle();
  auto picker = Layer::back();
  ASSERT_TRUE(clickLabel(picker->getLvObj(), STR_NB4_ASSIGN_BY_PRESSING));
  for (unsigned i = 0; i < 3; ++i) cycle();
  EXPECT_TRUE(nb4ControlsLearning());
  press(KEY_EXIT); // Learn SW1-L, which was Back. Must not dismiss the picker.
  EXPECT_EQ(Layer::back(), picker);
  EXPECT_EQ(nb4ControlBinding(0), NB4_CONTROL_RUN_PAUSE);
  EXPECT_FALSE(nb4ControlsLearning());
  nb4ControlsProcessCommands(); nb4RaceProcessCommands();
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Ready);
  labels.clear(); collectLabels(picker->getLvObj(), labels);
  EXPECT_TRUE(std::any_of(labels.begin(), labels.end(), [](const std::string& text) {
    return text.find("SW1-L /") != std::string::npos;
  }));
  press(KEY_EXIT);
  nb4ControlsProcessCommands(); nb4RaceProcessCommands();
  EXPECT_EQ(nb4RacePhase(), Nb4RacePhase::Running);

  const auto saved = nb4ControlBinding(0);
  ASSERT_TRUE(clickLabel(picker->getLvObj(), STR_NB4_ASSIGN_BY_PRESSING));
  auto clockBefore = g_tmr10ms;
  g_tmr10ms += 1500;
  cycle();
  g_tmr10ms = clockBefore;
  EXPECT_EQ(Layer::back(), picker);
  EXPECT_FALSE(nb4ControlsLearning());
  EXPECT_EQ(nb4ControlBinding(0), saved);
  picker->onCancel(); scene.root->run();
  EXPECT_EQ(Layer::back(), base);
}

#endif
