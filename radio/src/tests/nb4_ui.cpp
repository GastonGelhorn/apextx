/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "gtests.h"
#include "nb4_ui_languages.h"

#if defined(RADIO_NB4_FAMILY) && defined(COLORLCD)
#include "layer.h"
#include "mainwindow.h"
#include "theme_manager.h"
#include "pagegroup.h"
#include "radio_theme.h"
#include "radio_setup.h"
#include "model_nb4_racing.h"
#include "nb4_racing.h"
#include "nb4_home.h"
#include "nb4_health.h"
#include "nb4_maintenance.h"
#include "hal/usb_driver.h"
#include "nb4_model_compat.h"
#include "nb4_routes.h"
#include "view_main.h"
#include "layout.h"
#include "location.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace {
std::vector<lv_color_t> nb4Frame;
void captureNb4Presentation(lv_disp_drv_t* d, uint16_t* pixels, const rect_t& area)
{
  EXPECT_EQ(area.x, 0); EXPECT_EQ(area.y, 0);
  EXPECT_EQ(area.w, LCD_PHYS_W); EXPECT_EQ(area.h, LCD_PHYS_H);
  nb4Frame.resize(LCD_PHYS_W * LCD_PHYS_H);
  memcpy(nb4Frame.data(), pixels, nb4Frame.size() * sizeof(lv_color_t));
  lv_disp_flush_ready(d);
}
void saveNb4Frame(const char* directory, const char* name, int w, int h)
{
  if (!directory) return;
  std::string path = std::string(directory) + "/" + name + ".ppm";
  auto file = fopen(path.c_str(), "wb");
  ASSERT_NE(file, nullptr);
  fprintf(file, "P6\n%d %d\n255\n", w, h);
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
    const auto& pixel = nb4Frame[w > h ? (LCD_PHYS_H - 1 - x) * LCD_PHYS_W + y : y * LCD_PHYS_W + x];
    auto rgb = lv_color_to32(pixel);
    unsigned char data[] = {static_cast<unsigned char>(rgb >> 16), static_cast<unsigned char>(rgb >> 8), static_cast<unsigned char>(rgb)};
    fwrite(data, 1, 3, file);
  }
  fclose(file);
}
}

TEST(Nb4Ui, RacingHomeRendersAndPreservesDrivingState)
{
  SYSTEM_RESET(); MODEL_RESET();
  telemetryStreaming = 0;
  auto originalLanguage = currentLangStrings;
  nb4HealthClearPrevious(); nb4HealthInit();
  nb4VisualDefaults();
  nb4RacingDefaults(g_model.nb4Racing);
  strAppend(g_model.header.name, "Noble RC - Circuito", LEN_MODEL_NAME);
  g_model.mixData[0].srcRaw = MIXSRC_FIRST_STICK;
  g_model.mixData[0].destCh = 0;
  g_model.mixData[1].srcRaw = MIXSRC_FIRST_STICK + 1;
  g_model.mixData[1].destCh = 1;
  g_model.timers[0].mode = TMRMODE_ON;
  timersStates[0].val = 127;
  channelOutputs[0] = RESX / 3;
  channelOutputs[1] = -RESX / 2;
  ex_chans[0] = RESX / 3;
  ex_chans[1] = -RESX / 2;
  g_vbat100mV = 78;
  g_eeGeneral.vBatWarn = 70;
  const auto racing = g_model.nb4Racing;
  auto root = MainWindow::instance();
  auto originalScreen = lv_scr_act();
  auto originalRect = root->getRect();
  auto driver = lv_disp_get_default()->driver;
  auto originalFlush = driver->flush_cb;
  root->setActiveScreen();
  lcdSetFlushCb(captureNb4Presentation); // exercise the real rotating compositor
  auto themes = ThemePersistance::instance(); themes->refresh();
  auto home = new Nb4HomeScreen(root, {0, 0, LCD_W, LCD_H});
  for (unsigned orientation = 0; orientation < 2; ++orientation) {
    ASSERT_TRUE(lcdSetOrientation(orientation != 0));
    coord_t w = lv_disp_get_hor_res(nullptr), h = lv_disp_get_ver_res(nullptr);
    root->setRect({0, 0, w, h}); home->setRect({0, 0, w, h});
  for (const char* language : NB4_UI_LANGUAGES) {
    memcpy(g_eeGeneral.uiLanguage, language, 2);
    nb4TestApplyUiLanguage();
    for (unsigned design = 0; design < 1; ++design) {
      g_eeGeneral.nb4Home = design;
      for (unsigned theme = 0; theme < 2; ++theme) {
        themes->applyTheme(theme);
        home->checkEvents();
        lv_obj_invalidate(root->getLvObj());
        lv_obj_update_layout(root->getLvObj());
        lv_tick_inc(50); lv_timer_handler(); root->run();
        char name[64];
        snprintf(name, sizeof(name), "home-%s-%s-%s-%s", orientation ? "landscape" : "portrait", language, "racing", theme ? "light" : "dark");
        saveNb4Frame(getenv("NB4_SCREENSHOT_DIR"), name, w, h);
        ASSERT_EQ(lv_mem_test(), LV_RES_OK);
        EXPECT_EQ(timersStates[0].val, 127);
        EXPECT_EQ(channelOutputs[0], RESX / 3);
        EXPECT_EQ(channelOutputs[1], -RESX / 2);
        EXPECT_EQ(memcmp(&g_model.nb4Racing, &racing, sizeof(racing)), 0);
        for (unsigned scenario = 0; scenario < 2; ++scenario) {
          for (unsigned ch = 2; ch < 8; ++ch) {
            g_model.mixData[ch].srcRaw = MIXSRC_MAX;
            g_model.mixData[ch].destCh = ch;
            channelOutputs[ch] = ch % 2 ? RESX : -RESX;
          }
          auto& sensor = g_model.telemetrySensors[0];
          sensor.id = 0x1000; sensor.type = TELEM_TYPE_CUSTOM;
          sensor.unit = UNIT_VOLTS; sensor.prec = 1;
          telemetryItems[0].value = 64;
          telemetryItems[0].timeout = scenario ? TELEMETRY_SENSOR_TIMEOUT_START : TELEMETRY_SENSOR_TIMEOUT_OLD;
          telemetryStreaming = scenario ? 100 : 0;
          g_vbat100mV = scenario ? 60 : 78;
          nb4RacingReset();
          g_model.nb4Racing.lapSw = SWSRC_ON;
          nb4RacingTick(1);
          for (unsigned lap = 0; lap < 3; ++lap) {
            g_model.nb4Racing.lapSw = 0; nb4RacingTick(130);
            g_model.nb4Racing.lapSw = SWSRC_ON; nb4RacingTick(130);
          }
          home->checkEvents(); lv_obj_update_layout(root->getLvObj());
          lv_tick_inc(50); lv_timer_handler(); root->run();
          std::string variant = std::string(name) + (scenario ? "-alarm-aux" : "-stale-aux");
          saveNb4Frame(getenv("NB4_SCREENSHOT_DIR"), variant.c_str(), w, h);
        }
        for (unsigned ch = 2; ch < 8; ++ch) g_model.mixData[ch].srcRaw = 0;
        g_model.telemetrySensors[0].id = 0;
        g_model.nb4Racing.lapSw = 0; nb4RacingReset();
        g_vbat100mV = 78; telemetryStreaming = 0;
      }
    }
  }
  }
  lv_mem_monitor_t warm, final;
  // The orientation/template/theme cycle repeats every 12 iterations. Compare
  // the same state at 9 and 117; distinct palettes keep different image sizes.
  for (unsigned i = 0; i < 118; ++i) {
    ASSERT_TRUE(lcdSetOrientation(i % 2));
    coord_t w = lv_disp_get_hor_res(nullptr), h = lv_disp_get_ver_res(nullptr);
    root->setRect({0, 0, w, h}); home->setRect({0, 0, w, h});
    g_eeGeneral.nb4Home = (i / 2) % 2;
    themes->applyTheme((i / 3) % 2);
    home->checkEvents();
    lv_obj_update_layout(root->getLvObj());
    lv_tick_inc(50); lv_timer_handler(); root->run();
    if (i == 9) lv_mem_monitor(&warm);
    EXPECT_EQ(timersStates[0].val, 127);
    EXPECT_EQ(channelOutputs[1], -RESX / 2);
    ASSERT_EQ(lv_mem_test(), LV_RES_OK);
  }
  lv_mem_monitor(&final);
  EXPECT_GE(final.free_size + 64u, warm.free_size);
  EXPECT_EQ(final.used_cnt, warm.used_cnt);
  for (unsigned orientation = 0; orientation < 2; ++orientation) {
    ASSERT_TRUE(lcdSetOrientation(orientation));
    coord_t w = lv_disp_get_hor_res(nullptr), h = lv_disp_get_ver_res(nullptr);
    root->setRect({0, 0, w, h}); home->setRect({0, 0, w, h});
    themes->applyTheme(0); memcpy(g_eeGeneral.uiLanguage, "es", 2);
    nb4TestApplyUiLanguage();
    for (unsigned section = 0; section <= unsigned(Nb4Section::Appearance); ++section) {
      nb4OpenSection(static_cast<Nb4Section>(section));
      lv_obj_update_layout(root->getLvObj()); lv_tick_inc(50); lv_timer_handler(); root->run();
      char name[48]; snprintf(name, sizeof(name), "menu-%u-%s", section, orientation ? "landscape" : "portrait");
      saveNb4Frame(getenv("NB4_SCREENSHOT_DIR"), name, w, h);
      Layer::back()->onCancel(); root->run();
    }
  }
  home->deleteLater(); root->run();
  ASSERT_TRUE(lcdSetOrientation(false));
  root->setRect(originalRect);
  lv_scr_load(originalScreen);
  lv_tick_inc(50); lv_timer_handler();
  driver->flush_cb = originalFlush;
  lcdSetFlushCb(nullptr);
  currentLangStrings = originalLanguage;
}

TEST(Nb4Ui, UpdateComponentsRenderInEveryLanguageAndReturnToTheirCaller)
{
#if defined(RADIO_NB4)
  nb4MaintenanceStopUsb();
  const auto usbMode = getSelectedUsbMode();
#endif
  SYSTEM_RESET(); MODEL_RESET(); nb4AcceptNewCarModel();
  nb4HealthClearPrevious(); nb4HealthInit(); nb4VisualDefaults();
  const auto language = currentLangStrings;
  auto root = MainWindow::instance();
  const auto screen = lv_scr_act();
  const auto rect = root->getRect();
  auto driver = lv_disp_get_default()->driver;
  const auto flush = driver->flush_cb;
  root->setActiveScreen();
  lcdSetFlushCb(captureNb4Presentation);
  ViewMain::instance();
  root->run();
  auto home = new Nb4HomeScreen(root, {0, 0, LCD_W, LCD_H});
  for (unsigned orientation = 0; orientation < 2; ++orientation) {
    ASSERT_TRUE(lcdSetOrientation(orientation));
    const int w = lv_disp_get_hor_res(nullptr), h = lv_disp_get_ver_res(nullptr);
    root->setRect({0, 0, w, h}); home->setRect({0, 0, w, h});
    for (const auto locale : NB4_UI_LANGUAGES) {
      memcpy(g_eeGeneral.uiLanguage, locale, 2);
      nb4TestApplyUiLanguage();
      const auto caller = Layer::back();
      ASSERT_TRUE(nb4OpenRoute("settings/system/update"));
      root->run();
      lv_obj_update_layout(root->getLvObj());
      lv_tick_inc(50); lv_timer_handler(); root->run();
      char name[64];
      snprintf(name, sizeof(name), "update-%s-%s", locale, orientation ? "landscape" : "portrait");
      saveNb4Frame(getenv("NB4_SCREENSHOT_DIR"), name, w, h);
      unsigned firmwareButtons = 0, connectButtons = 0, activeButtons = 0;
      std::function<void(lv_obj_t*)> checkButton = [&](lv_obj_t* object) {
        if (lv_obj_check_type(object, &lv_label_class)) {
          const auto text = lv_label_get_text(object);
          if (!strcmp(text, STR_NB4_UPDATE_USB_INVENTORY)) ++connectButtons;
          if (!strcmp(text, STR_NB4_UPDATE_USB_ACTIVE)) {
            ++activeButtons;
            EXPECT_TRUE(lv_obj_has_state(lv_obj_get_parent(object), LV_STATE_DISABLED));
            lv_area_t area; lv_obj_get_coords(lv_obj_get_parent(object), &area);
            EXPECT_GE(area.y1, 0); EXPECT_LT(area.y2, h);
          }
        }
        if (lv_obj_check_type(object, &lv_label_class) &&
            !strcmp(lv_label_get_text(object), STR_NB4_UPDATE_FIRMWARE)) {
          lv_area_t area;
          lv_obj_get_coords(lv_obj_get_parent(object), &area);
          EXPECT_GE(area.y1, 0);
          EXPECT_LT(area.y2, h);
          ++firmwareButtons;
        }
        for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
          checkButton(lv_obj_get_child(object, i));
      };
      checkButton(Layer::back()->getLvObj());
      EXPECT_EQ(firmwareButtons, 1u);
#if defined(RADIO_NB4)
      EXPECT_EQ(connectButtons, 1u); EXPECT_EQ(activeButtons, 0u);
      ASSERT_TRUE(nb4MaintenanceEnable());
      lv_tick_inc(50); lv_timer_handler(); root->run();
      lv_obj_update_layout(root->getLvObj());
      firmwareButtons = connectButtons = activeButtons = 0;
      checkButton(Layer::back()->getLvObj());
      EXPECT_EQ(connectButtons, 0u); EXPECT_EQ(activeButtons, 1u);
      lv_tick_inc(50); lv_timer_handler(); root->run();
      snprintf(name, sizeof(name), "update-connected-%s-%s", locale, orientation ? "landscape" : "portrait");
      saveNb4Frame(getenv("NB4_SCREENSHOT_DIR"), name, w, h);
      nb4MaintenanceStopUsb();
#endif
      Layer::back()->onCancel(); root->run();
      EXPECT_EQ(Layer::back(), caller);
#if defined(RADIO_NB4)
      nb4OpenRfUpdateNotice(); root->run();
      lv_obj_update_layout(root->getLvObj());
      lv_tick_inc(50); lv_timer_handler(); root->run();
      snprintf(name,sizeof(name),"rf-update-notice-%s-%s",locale,orientation?"landscape":"portrait");
      saveNb4Frame(getenv("NB4_SCREENSHOT_DIR"),name,w,h);
      unsigned noticeActions=0;
      std::function<void(lv_obj_t*)> checkNotice=[&](lv_obj_t* object) {
        if (lv_obj_check_type(object,&lv_label_class) &&
            (!strcmp(lv_label_get_text(object),STR_NB4_UPDATE_REVIEW) || !strcmp(lv_label_get_text(object),STR_NB4_UPDATE_LATER))) {
          lv_area_t area; lv_obj_get_coords(lv_obj_get_parent(object),&area);
          EXPECT_GE(area.x1,0); EXPECT_GE(area.y1,0); EXPECT_LT(area.x2,w); EXPECT_LT(area.y2,h); ++noticeActions;
        }
        for (uint32_t i=0;i<lv_obj_get_child_cnt(object);++i) checkNotice(lv_obj_get_child(object,i));
      };
      checkNotice(Layer::back()->getLvObj()); EXPECT_EQ(noticeActions,2u);
      Layer::back()->deleteLater(); root->run(); EXPECT_EQ(Layer::back(),caller);
#endif
      EXPECT_EQ(lv_mem_test(), LV_RES_OK);
    }
  }
  home->deleteLater(); root->run();
  ASSERT_TRUE(lcdSetOrientation(false));
  root->setRect(rect); lv_scr_load(screen);
  lv_tick_inc(50); lv_timer_handler();
  driver->flush_cb = flush;
  lcdSetFlushCb(nullptr);
  currentLangStrings = language;
#if defined(RADIO_NB4)
  setSelectedUsbMode(usbMode);
#endif
}

TEST(Nb4Ui, RotationClosesEditorsAndPreservesPersistedScreens)
{
  SYSTEM_RESET(); nb4AcceptNewCarModel();
  nb4HealthClearPrevious(); nb4HealthInit(); nb4VisualDefaults();
  nb4RacingDefaults(g_model.nb4Racing);
  g_model.timers[0].mode = TMRMODE_ON; timersStates[0].val = 356;
  channelOutputs[1] = -631;
  g_model.limitData[1].min = 231;
  g_model.setScreenLayoutId(0, "Layout1x1");
  // Retain additional screen data even when its factory is unavailable.
  g_model.setScreenLayoutId(1, "UserExtra");
  auto root = MainWindow::instance(); auto originalScreen = lv_scr_act();
  root->setActiveScreen();
  lcdSetFlushCb(captureNb4Presentation);
  auto main = ViewMain::instance();
  LayoutFactory::loadCustomScreens();
  lv_mem_monitor_t warm, final;
  for (unsigned i = 0; i < 110; ++i) {
    nb4OpenSection(Nb4Section::Steering);
    auto selector = new Menu(); selector->addLine("Selection", [] {});
    nb4RequestOrientation(i % 2);
    nb4ProcessOrientation();
    root->run();
    ASSERT_EQ(lv_disp_get_hor_res(nullptr), i % 2 ? 480 : 320);
    EXPECT_EQ(g_eeGeneral.nb4Orientation, i % 2);
    auto appearance = Layer::back(); ASSERT_NE(appearance, main);
    appearance->onCancel(); root->run();
    ASSERT_EQ(Layer::back(), main);
    lv_obj_update_layout(root->getLvObj()); lv_tick_inc(50); lv_timer_handler();
    EXPECT_EQ(g_model.limitData[1].min, 231);
    EXPECT_EQ(timersStates[0].val, 356);
    EXPECT_EQ(channelOutputs[1], -631);
    EXPECT_STREQ(g_model.getScreenLayoutId(0), "Layout1x1");
    EXPECT_STREQ(g_model.getScreenLayoutId(1), "UserExtra");
    ASSERT_EQ(lv_mem_test(), LV_RES_OK);
    if (i == 9) lv_mem_monitor(&warm);
  }
  lv_mem_monitor(&final);
  EXPECT_EQ(final.used_cnt, warm.used_cnt);
  EXPECT_GE(final.free_size + 64u, warm.free_size);
  LayoutFactory::deleteCustomScreens();
  static_cast<Window*>(main)->deleteLater(); root->run();
  ASSERT_TRUE(lcdSetOrientation(false)); root->setRect({0, 0, 320, 480});
  lv_scr_load(originalScreen); lv_tick_inc(50); lv_timer_handler();
  lcdSetFlushCb(nullptr);
}

TEST(Nb4Ui, RemovingAnUnderlyingLayerPreservesTheActiveLayer)
{
  Window first(MainWindow::instance(), {0, 0, 80, 80});
  Window middle(MainWindow::instance(), {0, 0, 80, 80});
  Window last(MainWindow::instance(), {0, 0, 80, 80});
  auto original = Layer::back();
  Layer::push(&first);
  auto firstGroup = lv_group_get_default();
  Layer::push(&middle);
  Layer::push(&last);
  auto lastGroup = lv_group_get_default();
  Layer::pop(&middle);
  EXPECT_EQ(Layer::back(), &last);
  EXPECT_EQ(lv_group_get_default(), lastGroup);
  Layer::pop(&last);
  EXPECT_EQ(Layer::back(), &first);
  EXPECT_EQ(lv_group_get_default(), firstGroup);
  Layer::pop(&middle); // also exercise removal of an already removed layer
  Layer::pop(&first);
  EXPECT_EQ(Layer::back(), original);
  last.deleteLater(true, false);
  middle.deleteLater(true, false);
  first.deleteLater(true, false);
}

TEST(Nb4Ui, TenThousandLayerOperationsDoNotLeakLvglMemory)
{
  auto root = MainWindow::instance();
  const auto original = Layer::back();
  // Warm allocator buckets before measuring sustained usage.
  { Window warm(root, {0, 0, 80, 80}); Layer::push(&warm); Layer::pop(&warm); warm.deleteLater(true, false); }
  lv_mem_monitor_t before, after;
  lv_mem_monitor(&before);
  for (unsigned i = 0; i < 2000; ++i) {
    Window page(root, {0, 0, 80, 80});
    Window dialog(root, {0, 0, 64, 64});
    Layer::push(&page);
    Layer::push(&dialog);
    const auto active = lv_group_get_default();
    Layer::pop(&page);
    ASSERT_EQ(Layer::back(), &dialog);
    ASSERT_EQ(lv_group_get_default(), active);
    Layer::pop(&dialog);
    Layer::pop(&dialog);
    ASSERT_EQ(Layer::back(), original);
    dialog.deleteLater(true, false);
    page.deleteLater(true, false);
  }
  lv_mem_monitor(&after);
  EXPECT_EQ(after.free_size, before.free_size);
  EXPECT_EQ(lv_mem_test(), LV_RES_OK);
}

TEST(Nb4Ui, BuiltinThemesSurviveRefreshAndRejectInvalidIndices)
{
  auto themes = ThemePersistance::instance();
  themes->refresh();
  ASSERT_GE(themes->getNames().size(), 2u);
  EXPECT_EQ(themes->getThemeByIndex(-1), nullptr);
  themes->setThemeIndex(-1);
  EXPECT_EQ(themes->getCurrentTheme(), nullptr);
  themes->setThemeIndex(1);
  auto name = themes->getCurrentTheme()->getName();
  const auto count = themes->getNames().size();
  for (unsigned i = 0; i < 100; ++i) {
    themes->refresh();
    EXPECT_EQ(themes->getNames().size(), count);
    ASSERT_NE(themes->getCurrentTheme(), nullptr);
    EXPECT_EQ(themes->getCurrentTheme()->getName(), name);
    EXPECT_FALSE(themes->deleteThemeByIndex(0));
    EXPECT_FALSE(themes->deleteThemeByIndex(1));
  }
}

#if !defined(SIMU_DISKIO)
TEST(Nb4Ui, InvalidAndSlowExternalThemesDoNotOpenDialogsOrLoseBuiltins)
{
  auto root = std::filesystem::temp_directory_path() /
              ("nb4-ui-theme-test-" + std::to_string(getpid()));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  auto rootPath = root.string();
  simuFatfsSetPaths(rootPath.c_str(), nullptr);
  auto folder = std::filesystem::path(simuFatfsGetRealPath(THEMES_PATH "/nb4-ui-theme-test"));
  ASSERT_FALSE(std::filesystem::exists(folder));
  std::filesystem::create_directories(folder);
  struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
      simuFatfsSetFaults(0);
      simuFatfsSetPaths(TESTS_PATH, nullptr);
      std::filesystem::remove_all(root);
      ThemePersistance::instance()->refresh();
    }
  } cleanup{root};
  auto file = folder / "theme.yml";
  auto themes = ThemePersistance::instance(); themes->refresh();
  auto count = themes->getNames().size();
  auto layer = Layer::back();
  simuFatfsSetFaults(2);
  for (unsigned i = 0; i < 100; ++i) {
    { std::ofstream out(file); out << (i % 2 ? std::string(17000, 'x') : "unknown: invalid theme\n"); }
    themes->refresh();
    EXPECT_EQ(themes->getNames().size(), count);
    themes->applyTheme(i % 2);
    EXPECT_EQ(Layer::back(), layer);
    ASSERT_EQ(lv_mem_test(), LV_RES_OK);
  }
}
#endif

TEST(Nb4Ui, RepeatedSettingsNavigationReleasesControls)
{
  SYSTEM_RESET();
  nb4RacingDefaults(g_model.nb4Racing);
  auto root = MainWindow::instance();
  auto originalScreen = lv_scr_act();
  root->setActiveScreen();
  // There is no SDL consumer thread in gtests. Render normally but complete
  // display transfers synchronously, just as a headless framebuffer driver.
  auto driver = lv_disp_get_default()->driver;
  auto originalFlush = driver->flush_cb;
  driver->flush_cb = [](lv_disp_drv_t* d, const lv_area_t*, lv_color_t*) {
    lv_disp_flush_ready(d);
  };
  ThemePersistance::instance()->refresh();
  PageDef pages[] = {
    {ICON_RADIO_EDIT_THEME, STR_DEF(STR_QM_THEMES), STR_DEF(STR_MAIN_MENU_THEMES),
     PAGE_CREATE, QM_UI_THEMES, [](PageDef& p) { return new ThemeSetupPage(p); }},
    {ICON_RADIO_SETUP, STR_DEF(STR_QM_RADIO_SETTINGS), STR_DEF(STR_MAIN_RADIO_SETTINGS),
     PAGE_CREATE, QM_RADIO_SETUP, [](PageDef& p) { return new RadioSetupPage(p); }},
    {ICON_MODEL_SETUP, STR_DEF(STR_NB4_RACING), STR_DEF(STR_NB4_RACING),
     PAGE_CREATE, QM_MODEL_NB4_RACING, [](PageDef& p) { return new ModelNb4RacingPage(p); }},
    {EDGETX_ICONS_COUNT}
  };
  // Inject LVGL pressure without changing the device-equivalent 2 MiB pool.
  // Lua runs alongside navigation under its real shared 1 MiB allocator cap.
  auto pressure = lv_mem_alloc(1024 * 1024);
#if defined(LUA)
  extern ::testing::AssertionResult __luaExecStr(const char*);
  EXPECT_TRUE(__luaExecStr("nb4Stress={} for i=1,100 do nb4Stress[i]=string.rep('x',256) end"));
#endif
  lv_mem_monitor_t warm, final;
  for (unsigned cycle = 0; cycle <= 100; ++cycle) {
    auto group = new PageGroup(ICON_RADIO_SETUP, "NB4", pages);
    group->setCloseHandler(nullptr); // tests must never save radio/model files
    for (unsigned index = 0; index < 100; ++index) {
      group->setCurrentTab(index % 3);
      auto current = group->getCurrentTab();
      group->setCurrentTab(1000); // reject before narrowing to uint8_t
      ASSERT_EQ(group->getCurrentTab(), current);
      lv_obj_update_layout(group->getLvObj());
      lv_tick_inc(50);
      lv_timer_handler();
      root->run();
#if defined(LUA)
      EXPECT_TRUE(__luaExecStr("local s=getCarState(); assert(s.version==1 and #s.channels==8)"));
#endif
      if (index % 10 == 0) {
        auto selector = new Menu();
        selector->addLine("Direccion / Steering", [] {});
        selector->addLine("Gas / Throttle", [] {});
        lv_obj_update_layout(selector->getLvObj());
        // Simulate a hardware tab shortcut while a selector is still open.
        group->setCurrentTab((index + 1) % 3);
        EXPECT_TRUE(selector->deleted());
        root->run();
        ASSERT_EQ(Layer::back(), group);
      }
    }
    group->onCancel();
    root->run();
    lv_mem_monitor_t measurement;
    lv_mem_monitor(&measurement);
    std::function<unsigned(lv_obj_t*)> count = [&](lv_obj_t* obj) {
      unsigned n = 1;
      for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i) n += count(lv_obj_get_child(obj, i));
      return n;
    };
    unsigned objects = 0;
    auto display = lv_disp_get_default();
    for (unsigned i = 0; i < display->screen_cnt; ++i) objects += count(display->screens[i]);
    if (cycle == 0 || cycle == 100)
      fprintf(stderr, "NB4 navigation cycle=%u free=%u largest=%u blocks=%u objects=%u\n", cycle, measurement.free_size, measurement.free_biggest_size, measurement.used_cnt, objects);
    ASSERT_EQ(lv_mem_test(), LV_RES_OK);
    if (cycle == 0) lv_mem_monitor(&warm);
  }
  lv_mem_monitor(&final);
  // TLSF may use one extra alignment word for a differently split block.
  // Bound that variance and require a stable allocation count: per-navigation
  // leaks grow well beyond this allowance over 10,000 operations.
  EXPECT_GE(final.free_size + 64u, warm.free_size);
  EXPECT_EQ(final.used_cnt, warm.used_cnt);
  lv_mem_free(pressure);
#if defined(LUA)
  EXPECT_TRUE(__luaExecStr("nb4Stress=nil collectgarbage('collect')"));
#endif
  lv_scr_load(originalScreen);
  lv_timer_handler();
  driver->flush_cb = originalFlush;
}

TEST(Nb4Ui, LvglAllocationExhaustionUsesTheFaultHandler)
{
  EXPECT_EXIT(lv_mem_alloc(LV_MEM_SIZE * 2), ::testing::KilledBySignal(SIGABRT), "");
  EXPECT_EQ(lv_mem_test(), LV_RES_OK);
}
#endif
