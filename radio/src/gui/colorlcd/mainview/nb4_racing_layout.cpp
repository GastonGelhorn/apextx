/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#if defined(RADIO_NB4_FAMILY)
#include "layout.h"
#include "nb4_home.h"
#include "nb4_routes.h"
#include "nb4_ui.h"
#include "nb4_model_compat.h"
#include "nb4_palettes.h"
#include "view_main.h"
#include "timer_setup.h"
#include "sdcard.h"

namespace {
uint8_t racingMap[] = {0,0,36,40, 36,0,24,40, 0,40,60,12, 0,52,60,8};
const char* widgets[] = {"ApexSteering", "ApexThrottle", "ApexTimer", "ApexStats"};

bool sameFileContents(const char* source, const char* backup) {
  FIL first, second;
  if (f_open(&first, source, FA_READ) != FR_OK) return false;
  if (f_open(&second, backup, FA_READ) != FR_OK) { f_close(&first); return false; }
  bool same = true;
  char a[128], b[128];
  UINT na = 0, nb = 0;
  do {
    if (f_read(&first, a, sizeof(a), &na) != FR_OK ||
        f_read(&second, b, sizeof(b), &nb) != FR_OK ||
        na != nb || memcmp(a, b, na)) { same = false; break; }
  } while (na);
  if (f_close(&first) != FR_OK) same = false;
  if (f_close(&second) != FR_OK) same = false;
  return same;
}

class RacingLayout : public Layout {
 public:
  RacingLayout(Window* parent, const LayoutFactory* factory, int screen,
               uint8_t count, uint8_t* map) : Layout(parent, factory, screen, count, map) {
    decoration.reset();
    chrome = new Nb4HomeScreen(this, getRect(), false);
    lv_obj_clear_flag(getLvObj(), LV_OBJ_FLAG_SCROLLABLE);
  }
  bool hasTopbar() const override { return false; }
  bool hasFlightMode() const override { return false; }
  bool hasSliders() const override { return false; }
  bool hasTrims() const override { return false; }
  bool isMirrored() const override { return false; }
  void show(bool visible = true) override { Window::show(visible); }
  rect_t getZone(unsigned index) const override {
    const bool wide = width() > height();
    const coord_t full = width() - 4;
    const coord_t top = wide ? 52 : 110;
    const coord_t footer = height() - 2 - (wide ? 58 : 122);
    const coord_t dial = (full - 2) * 3 / 5;
    switch (index) {
      case 0: return {2, top, dial, coord_t(footer - 2 - top)};
      case 1: return {coord_t(4 + dial), top, coord_t(full - 2 - dial), coord_t(footer - 2 - top)};
      case 2: return wide ? rect_t{2, footer, 202, 58} : rect_t{2, footer, full, 70};
      case 3: return wide ? rect_t{206, footer, coord_t(full - 204), 58}
                          : rect_t{2, coord_t(footer + 72), full, 50};
      default: return {};
    }
  }
  void checkEvents() override {
    Window::checkEvents();
    if (deleted()) return;
    if (chrome->width() != width() || chrome->height() != height()) {
      chrome->setRect({0, 0, width(), height()});
      updateZones();
    }
  }
 private:
  Nb4HomeScreen* chrome;
};
const LayoutOption racingOptions[] = {LAYOUT_OPTIONS_END};
BaseLayoutFactory<RacingLayout> racingLayout("ApexTXRacing", "ApexTX Racing",
                                            racingOptions, 4, racingMap);

class RacingInstrument : public Widget {
 public:
  RacingInstrument(const WidgetFactory* factory, Window* parent, const rect_t& rect,
                   int screen, int zone) : Widget(factory, parent, rect, screen, zone) {
    for (unsigned i = 0; i < 4; ++i) if (!strcmp(factory->getName(), widgets[i])) kind = i;
    build();
    setPressHandler([this] {
      if (kind == 0) nb4OpenRoute("settings/steering/travel");
      else if (kind == 1) nb4OpenRoute("settings/throttle_brake/travel");
      else if (kind == 2) {
        const auto& state = nb4ReadCarState();
        if (!state.homeShowsRace && state.homeTimerIndex < MAX_TIMERS)
          new TimerWindow(state.homeTimerIndex);
        else nb4OpenRoute("settings/race/timer_laps");
      } else nb4OpenRoute("settings/race/history");
      return 0;
    });
  }
  void checkEvents() override {
    Widget::checkEvents();
    if (deleted()) return;
    const auto& state = nb4ReadCarState();
    const unsigned panel = state.homeShowsRace ? 1 : 2 + state.homeTimerIndex;
    if (builtW != width() || builtH != height() || theme != nb4ThemeKey() ||
        builtPanel != panel || language != languageKey()) build();
    if (chrono) chrono->refresh(state);
    if (stats) stats->refresh(state);
  }
 private:
  unsigned kind = 0, builtPanel = 0;
  coord_t builtW = 0, builtH = 0;
  uint32_t theme = 0;
  uint16_t language = 0;
  static uint16_t languageKey() {
    return uint8_t(g_eeGeneral.uiLanguage[0]) |
           (uint16_t(uint8_t(g_eeGeneral.uiLanguage[1])) << 8);
  }
  Nb4Chrono* chrono = nullptr;
  Nb4Stats* stats = nullptr;
  void build() {
    const bool focusEnabled = lv_obj_get_group(getLvObj()) != nullptr;
    if (focusEnabled) enableFocus(false);
    clear(); chrono = nullptr; stats = nullptr;
    builtW = width(); builtH = height(); theme = nb4ThemeKey();
    language = languageKey();
    const auto& state = nb4ReadCarState();
    builtPanel = state.homeShowsRace ? 1 : 2 + state.homeTimerIndex;
    const bool wide = lv_disp_get_hor_res(nullptr) > lv_disp_get_ver_res(nullptr);
    const rect_t bounds{0, 0, width(), height()};
    Window* instrument = nullptr;
    switch (kind) {
      case 0: instrument = new Nb4Dial(this, bounds, wide); break;
      case 1: instrument = new Nb4Column(this, bounds, wide); break;
      case 2: instrument = chrono = new Nb4Chrono(this, bounds, state, wide); break;
      case 3: instrument = stats = new Nb4Stats(this, bounds, wide); break;
    }
    if (instrument) Nb4Ui::passThrough(instrument->getLvObj());
    if (focusEnabled) enableFocus(true);
  }
};
class RacingWidgetFactory : public BaseWidgetFactory<RacingInstrument> {
 public:
  RacingWidgetFactory(const char* id, Nb4Str label) :
    BaseWidgetFactory<RacingInstrument>(id, nullptr), label(label) {}
  const char* getDisplayName() const override { return label(); }
 private:
  Nb4Str label;
};
RacingWidgetFactory steering("ApexSteering", NB4_STR(STEERING_2090));
RacingWidgetFactory throttle("ApexThrottle", NB4_STR(THROTTLE_BRAKE));
RacingWidgetFactory timer("ApexTimer", NB4_STR(CHRONO));
RacingWidgetFactory stats("ApexStats", NB4_STR(STATISTICS));
}

void nb4SetRacingHomeData(unsigned index) {
  if (index >= MAX_CUSTOM_SCREENS) return;
  auto data = g_model.getScreenLayoutData(index);
  data->clear();
  g_model.setScreenLayoutId(index, "ApexTXRacing");
  for (unsigned i = 0; i < 4; ++i) data->setWidgetName(i, widgets[i]);
  if (!index) g_model.nb4ScreenVersion = 1;
}

bool nb4MigrateHome(const char* modelPath) {
  if (g_model.nb4ScreenVersion) return g_model.nb4ScreenVersion == 1;
  if (nb4ModelBlocked() || !modelPath || !*modelPath) return false;
  const std::string backup = std::string(modelPath) + ".pre-apextx-home";
  FILINFO info;
  const auto exists = f_stat(backup.c_str(), &info);
  // A colliding or stale backup is not proof that the current hidden screen
  // was preserved. Keep the model unchanged until the conflict is resolved.
  if (exists == FR_OK && !sameFileContents(modelPath, backup.c_str())) return false;
  if (exists != FR_OK) {
    if (exists != FR_NO_FILE) return false;
    const auto temporary = backup + ".tmp";
    if (sdCopyFile(modelPath, temporary.c_str())) return false;
    if (f_rename(temporary.c_str(), backup.c_str()) != FR_OK) return false;
  }
  nb4SetRacingHomeData(0);
  storageDirty(EE_MODEL);
  return true;
}
#endif
