/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_routes.h"
#include "nb4_assignments.h"

#if defined(RADIO_NB4_FAMILY)

#include <string.h>
#include <vector>

#include "edgetx.h"
#include "hal/abnormal_reboot.h"
#include "bitmaps.h"
#include "button.h"
#include "dialog.h"
#include "sdcard.h"
#include "menu.h"
#include "model_select.h"
#include "module_setup.h"
#include "nb4_car_state.h"
#include "nb4_leds.h"
#include "nb4_home.h"
#include "model_nb4_axis.h"
#include "model_nb4_racing.h"
#include "model_telemetry.h"
#include "preflight_checks.h"
#include "timer_setup.h"
#include "trims_setup.h"
#include "quick_menu.h"
#include "static.h"
#include "toggleswitch.h"
#include "quick_menu_group.h"
#include "hw_inputs.h"
#include "radio_calibration.h"
#include "radio_setup.h"
#include "view_channels.h"
#include "view_main.h"

namespace {

void page(QMPage p) { QuickMenu::openPage(p); }

bool hasBluetooth()
{
#if defined(BLUETOOTH)
  return true;
#else
  return false;
#endif
}
void section(Nb4Section s) { nb4OpenSection(s); }

void openSteering()   { page(QM_MODEL_NB4_STEERING); }
void openThrottle()   { page(QM_MODEL_NB4_THROTTLE); }
void openModule()     { new ModulePage(INTERNAL_MODULE); }

void openPower() { openRadioSetupPowerPage(STR_NB4_POWER); }

void openGeneralPrefs() { openRadioSetupGeneralPage(STR_NB4_GENERAL_PREFERENCES); }
void openUsb() { openRadioSetupUsbPage(STR_NB4_USB); }
void openControlBehaviour() { openRadioSetupControlsPage(STR_NB4_CONTROL_BEHAVIOUR); }
void openBrightness() { openRadioSetupBacklightPage(STR_NB4_BRIGHTNESS); }
void openSound() { openRadioSetupSoundPage(STR_NB4_SOUND); }
void openAlarms() { openRadioSetupAlarmsPage(STR_NB4_ALERTS); }
void openHaptic() { openRadioSetupHapticPage(STR_NB4_HAPTIC); }
void openDateTime()
{
#if defined(RADIO_NB4) && !defined(RTCLOCK)
  openRadioSetupDateTimePage(STR_NB4_LOCATION);
#else
  openRadioSetupDateTimePage(STR_NB4_DATE_LOCATION);
#endif
}
void openHardware()   { page(QM_RADIO_HARDWARE); }

void nb4RequestUpdateMode()
{
  new ConfirmDialog(
      STR_NB4_UPDATE,
      STR_NB4_THE_RADIO_RESTARTS_INTO_UPDATE_MODE,
      [] {
        // Keep power latched and let the ApexTX bootloader own USB and display.
        watchdogSuspend(2000 /* 20 s */);
        pulsesStop();
        pwrOn();
#if defined(RADIO_NB4)
        abnormalRebootRequestDfu();
#else
        abnormalRebootRequestRomDfu();
#endif
#if !defined(SIMU)
        NVIC_SystemReset();
#endif
      });
}
void openTelemetry()  { page(QM_MODEL_TELEMETRY); }
void openScreens()
{
  page((QMPage)(QM_UI_SCREEN1 + ViewMain::instance()->getCurrentMainView()));
}

void openDestination(void (*open)(), uint8_t tab)
{
  nb4SetSteeringTab(open == openSteering ? tab : 0);
  nb4SetThrottleTab(open == openThrottle ? tab : 0);
  open();
}

class Nb4VariablesDialog : public BaseDialog
{
 public:
  Nb4VariablesDialog() :
      BaseDialog(STR_NB4_MODEL_VARIABLES, true,
                 (lv_coord_t)(lv_disp_get_hor_res(nullptr) * 0.92),
                 (lv_coord_t)(lv_disp_get_ver_res(nullptr) * 0.92))
  {
    form->padLeft(PAD_MEDIUM);
    form->padRight(PAD_MEDIUM);

    auto line = new Window(form, {0, 0, LV_PCT(100), 0});
    line->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_MEDIUM, LV_SIZE_CONTENT);
    lv_obj_set_style_flex_cross_place(line->getLvObj(), LV_FLEX_ALIGN_CENTER, 0);
    new StaticText(line, {0, 0, 0, 0}, STR_NB4_USE_VARIABLES,
                   COLOR_THEME_PRIMARY1_INDEX);
    new ToggleSwitch(
        line, {0, 0, 0, 0}, []() { return (uint8_t)!g_eeGeneral.modelGVDisabled; },
        [this](uint8_t on) {
          g_eeGeneral.modelGVDisabled = on ? 0 : 1;
          storageDirty(EE_GENERAL);
          updateEditor();
        });

    editor = new TextButton(
        form, {0, 0, LV_PCT(100), 0},
        STR_NB4_OPEN_THE_VARIABLE_EDITOR,
        []() { page(QM_MODEL_GVARS); return 0; });
    editor->setWrap();
    updateEditor();

    paragraph(form,
              STR_NB4_A_VARIABLE_IS_A_NAMED_NUMBER);

    paragraph(form,
              STR_NB4_A_TWO_CHANNEL_CAR_RARELY_NEEDS);
  }

  void checkEvents() override
  {
    BaseDialog::checkEvents();
    if (deleted() || settled) return;
    settled = true;
    lv_obj_scroll_to_y(form->getLvObj(), 0, LV_ANIM_OFF);
  }

 private:
  TextButton* editor = nullptr;
  bool settled = false;

  void paragraph(Window* parent, const char* text)
  {
    auto st = new StaticText(parent, {0, 0, LV_PCT(100), 0}, text,
                             COLOR_THEME_PRIMARY3_INDEX);
    lv_obj_set_style_pad_top(st->getLvObj(), PAD_MEDIUM, LV_PART_MAIN);
  }

  void updateEditor()
  {
    if (!editor) return;
    if (modelGVEnabled())
      lv_obj_clear_flag(editor->getLvObj(), LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(editor->getLvObj(), LV_OBJ_FLAG_HIDDEN);
  }
};

class Nb4LedsDialog : public BaseDialog
{
 public:
  Nb4LedsDialog() :
      BaseDialog(STR_NB4_LIGHTS, true,
                 (lv_coord_t)(lv_disp_get_hor_res(nullptr) * 0.92))
  {
    form->padLeft(PAD_MEDIUM);
    form->padRight(PAD_MEDIUM);

    auto modeLine = new Window(form, {0, 0, LV_PCT(100), 0});
    modeLine->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_MEDIUM, LV_SIZE_CONTENT);
    lv_obj_set_style_flex_cross_place(modeLine->getLvObj(), LV_FLEX_ALIGN_CENTER, 0);
    new StaticText(modeLine, {0, 0, 0, 0}, STR_NB4_MODE_5032,
                   COLOR_THEME_PRIMARY1_INDEX);
    auto mode = new Choice(
        modeLine, {0, 0, 0, 0}, NB4_LED_OFF, NB4_LED_MODE_COUNT - 1,
        []() { return (int)g_eeGeneral.nb4LedMode; },
        [this](int v) {
          g_eeGeneral.nb4LedMode = (uint8_t)v;
          storageDirty(EE_GENERAL);
          updateColorRow();
        });
    mode->setTextHandler([](int v) {
      switch (v) {
        case NB4_LED_FIXED:   return std::string(STR_NB4_FIXED_COLOUR);
        case NB4_LED_BREATHE: return std::string(STR_NB4_BREATHING);
        case NB4_LED_BATTERY: return std::string(STR_NB4_BATTERY_STATE);
        default:              return std::string(STR_NB4_OFF);
      }
    });

    colorLine = new Window(form, {0, 0, LV_PCT(100), 0});
    colorLine->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_MEDIUM, LV_SIZE_CONTENT);
    lv_obj_set_style_flex_cross_place(colorLine->getLvObj(), LV_FLEX_ALIGN_CENTER, 0);
    new StaticText(colorLine, {0, 0, 0, 0}, STR_NB4_COLOUR,
                   COLOR_THEME_PRIMARY1_INDEX);
    auto color = new Choice(
        colorLine, {0, 0, 0, 0}, NB4_LED_WHITE, NB4_LED_COLOR_COUNT - 1,
        []() { return (int)g_eeGeneral.nb4LedColor; },
        [](int v) { g_eeGeneral.nb4LedColor = (uint8_t)v; storageDirty(EE_GENERAL); });
    color->setTextHandler([](int v) {
      switch (v) {
        case NB4_LED_RED:     return std::string(STR_NB4_RED);
        case NB4_LED_ORANGE:  return std::string(STR_NB4_ORANGE);
        case NB4_LED_YELLOW:  return std::string(STR_NB4_YELLOW);
        case NB4_LED_GREEN:   return std::string(STR_NB4_GREEN);
        case NB4_LED_CYAN:    return std::string(STR_NB4_CYAN);
        case NB4_LED_BLUE:    return std::string(STR_NB4_BLUE);
        case NB4_LED_MAGENTA: return std::string(STR_NB4_MAGENTA);
        default:              return std::string(STR_NB4_WHITE);
      }
    });
    updateColorRow();

    auto note = new StaticText(
        form, {0, 0, LV_PCT(100), 0},
        STR_NB4_WHILE_THE_RADIO_IS_CHARGING_THE,
        COLOR_THEME_PRIMARY3_INDEX);
    lv_obj_set_style_pad_top(note->getLvObj(), PAD_MEDIUM, LV_PART_MAIN);
  }

 private:
  Window* colorLine = nullptr;

  void updateColorRow()
  {
    if (!colorLine) return;
    const bool used = g_eeGeneral.nb4LedMode == NB4_LED_FIXED ||
                      g_eeGeneral.nb4LedMode == NB4_LED_BREATHE;
    if (used)
      lv_obj_clear_flag(colorLine->getLvObj(), LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(colorLine->getLvObj(), LV_OBJ_FLAG_HIDDEN);
  }
};

void openLeds() { new Nb4LedsDialog(); }

void openVariables() { new Nb4VariablesDialog(); }

void resetMenu()
{
  Menu* m = new Menu();
  m->addLine(STR_RESET_TIMER1, []() { timerReset(0); });
  m->addLine(STR_RESET_TIMER2, []() { timerReset(1); });
  m->addLine(STR_RESET_TIMER3, []() { timerReset(2); });
  m->addLine(STR_RESET_TELEMETRY, []() { telemetryReset(); });
}

// Labels and reasons are NB4_STR(...) accessors so the catalogue follows the
// active language at the moment a tile is drawn.
#define AV(path, label, fn) {path, label, Nb4RouteState::Available, nullptr, fn, nullptr, 0}
#define AVTAB(path, label, fn, tab) {path, label, Nb4RouteState::Available, nullptr, fn, nullptr, tab}
#define AVIF(path, label, fn, guard) {path, label, Nb4RouteState::Available, nullptr, fn, guard, 0}
#define AVIFR(path, label, fn, guard, why) {path, label, Nb4RouteState::Available, why, fn, guard, 0}
#define PEND(path, label, why) {path, label, Nb4RouteState::NotBuiltYet, why, nullptr, nullptr, 0}

const Nb4Route routes[] = {
    // --- Car
    AV("settings/car/general", NB4_STR(GENERAL), []() { page(QM_MODEL_SETUP); }),

    AV("settings/car/safety", NB4_STR(SAFETY), []() { new PreflightChecks(); }),
    AV("settings/car/presets", NB4_STR(START_POINT), []() { page(QM_MODEL_NB4_RACING); }),
    AVIFR("settings/car/notes",
          NB4_STR(NOTES),
          []() { page(QM_MODEL_NOTES); },
          modelHasNotes,
          NB4_STR(THIS_CAR_HAS_NO_NOTES_YET_THEY_ARE_READ)),

    // --- Steering
    AVTAB("settings/steering/travel", NB4_STR(TRAVEL), openSteering, 0),
    AVTAB("settings/steering/curve", NB4_STR(CURVE), openSteering, 1),
    AVTAB("settings/steering/centre", NB4_STR(CENTRE), openSteering, 2),
    AVTAB("settings/steering/speed", NB4_STR(SPEED), openSteering, 3),

    // --- Throttle and brake
    AVTAB("settings/throttle_brake/travel", NB4_STR(TRAVEL), openThrottle, 0),
    AVTAB("settings/throttle_brake/curve", NB4_STR(CURVE), openThrottle, 1),
    AVTAB("settings/throttle_brake/brake", NB4_STR(BRAKE_ABS), openThrottle, 2),
    AVTAB("settings/throttle_brake/engine", NB4_STR(ENGINE), openThrottle, 3),

    // --- Receiver and RF
    // One page holds the RF module, the receiver and failsafe together, so it
    // gets one entry rather than three labels for the same destination.
    AV("settings/receiver_rf/module", NB4_STR(RF_MODULE_RECEIVER_FAILSAFE), openModule),

    // --- Channels and controls
    // Per-model actions and navigation share one physical-control editor.
    // Hardware naming remains reachable from its Other controls menu.
    AV("settings/controls/assignments", NB4_STR(ASSIGNMENTS), nb4OpenAssignments),

    AV("settings/controls/channels", NB4_STR(CHANNELS), nb4OpenChannelsDialog),
    AV("settings/controls/trims", NB4_STR(TRIMS_LABEL), []() { new TrimsSetup(); }),
    AV("settings/controls/general", NB4_STR(CONTROL_BEHAVIOUR), openControlBehaviour),
    AV("settings/controls/shortcuts",
       NB4_STR(KEYS_AND_NAVIGATION),
       nb4OpenNavigationAssignments),
    PEND("settings/controls/quick_access",
         NB4_STR(CONFIGURE_QUICK_ACCESS),
         NB4_STR(CHOOSING_WHAT_GOES_INTO_QUICK_ACCESS_IS)),
    AV("settings/controls/monitor", NB4_STR(MONITOR), []() { new ChannelsViewMenu(); }),

    // --- Telemetry
    AVIFR("settings/telemetry/sensors",
          NB4_STR(SENSORS),
          openTelemetry,
          modelTelemetryEnabled,
          NB4_STR(TELEMETRY_IS_SWITCHED_OFF_FOR_THIS_CAR_T)),

    AVIFR("settings/telemetry/alerts",
          NB4_STR(ALERTS),
          openTelemetryAlarmsPage,
          modelTelemetryEnabled,
          NB4_STR(TELEMETRY_IS_SWITCHED_OFF_FOR_THIS_CAR_T)),
    AV("settings/telemetry/track_view",
       NB4_STR(TRACK_VIEW),
       []() { section(Nb4Section::Telemetry); }),

    // --- Race

    AV("settings/race/timers", NB4_STR(TIMERS_85E8), []() {
      Menu* m = new Menu();
      m->setTitle(STR_NB4_TIMERS_85E8);
      for (uint8_t t = 0; t < MAX_TIMERS && t < 3; t += 1) {
        char label[24];
        snprintf(label, sizeof(label), "%s %u", STR_NB4_TIMER_BF94, t + 1);
        m->addLine(label, [t]() { new TimerWindow(t); });
      }

      m->addLine(STR_NB4_THROTTLE_TRACKING,
                 []() { nb4OpenThrottleTraceDialog(); });
    }),
    AV("settings/race/timer_laps", NB4_STR(TIMERS_LAPS), []() { section(Nb4Section::Chrono); }),
    AV("settings/race/statistics", NB4_STR(STATISTICS), []() { page(QM_TOOLS_STATS); }),
    AV("settings/race/pit", NB4_STR(PIT), []() { section(Nb4Section::Pit); }),
    AV("settings/race/history", NB4_STR(HISTORY), []() { section(Nb4Section::History); }),
    PEND("settings/race/race_summary",
         NB4_STR(RUN_SUMMARY),
         NB4_STR(THE_RUN_SUMMARY_IS_NOT_BUILT_YET_IN_THE)),
    AV("settings/race/resets", NB4_STR(SESSION_RESETS), []() { resetMenu(); }),

    // --- Models
    AV("settings/models/management", NB4_STR(MANAGE), []() { new ModelLabelsWindow(); }),
    PEND("settings/models/templates",
         NB4_STR(TEMPLATES),
         NB4_STR(TEMPLATES_HAVE_NO_PAGE_OF_THEIR_OWN_YET)),

    // --- Display and appearance
    AV("settings/display/brightness", NB4_STR(BRIGHTNESS), openBrightness),
    AV("settings/display/top_bar", NB4_STR(TOP_BAR), []() { page(QM_UI_SETUP); }),
    AV("settings/display/screens", NB4_STR(SCREENS), openScreens),

    AVIFR("settings/display/theme",
          NB4_STR(THEME),
          []() { page(QM_UI_THEMES); },
          radioThemesEnabled,
          NB4_STR(EXTERNAL_THEMES_ARE_SWITCHED_OFF_TURN_TH)),
    AV("settings/display/home", NB4_STR(HOME), []() { section(Nb4Section::Appearance); }),

    // --- Sound and alerts
    AV("settings/sound_alerts/alerts", NB4_STR(ALERTS), openAlarms),
    AV("settings/sound_alerts/sound", NB4_STR(SOUND), openSound),
    AV("settings/sound_alerts/haptic", NB4_STR(HAPTIC), openHaptic),

    AV("settings/sound_alerts/lights", NB4_STR(LIGHTS), openLeds),

    // --- Connectivity
    AV("settings/connectivity/usb", NB4_STR(USB), openUsb),

    AVIFR("settings/connectivity/bluetooth",
          NB4_STR(BLUETOOTH),
          openHardware,
          hasBluetooth,
          NB4_STR(THIS_RADIO_HAS_NO_BLUETOOTH_IT_IS_NOT_A)),

    // --- System
    AV("settings/system/general", NB4_STR(GENERAL_PREFERENCES), openGeneralPrefs),
    AV("settings/system/power", NB4_STR(POWER), openPower),
    AV("settings/system/hardware", NB4_STR(HARDWARE), openHardware),
    AV("settings/system/calibration",
       NB4_STR(CALIBRATION),
       []() { new RadioCalibrationPage(); }),

    AV("settings/system/storage", NB4_STR(STORAGE), []() {
      if (!nb4MountFailureIsMissingFilesystem(nb4StorageMountResult())) {
        page(QM_TOOLS_STORAGE);
        return;
      }
      Menu* m = new Menu();
      m->setTitle(STR_NB4_STORAGE);
      m->addLine(STR_NB4_OPEN_BROWSER,
                 []() { page(QM_TOOLS_STORAGE); });
      m->addLine(STR_NB4_CREATE_FILESYSTEM, []() {
        new ConfirmDialog(
            STR_NB4_CREATE_FILESYSTEM,
            STR_NB4_NO_FILESYSTEM_FOUND_CREATING_ONE_ERASES,
            []() { nb4RequestFilesystemCreation(); });
      });
    }),
    AV("settings/system/backup_restore",
       NB4_STR(BACKUP_RESTORE),
       []() { section(Nb4Section::Backup); }),

    AV("settings/system/update", NB4_STR(UPDATE), nb4RequestUpdateMode),
#if defined(RADIO_NB4) && !defined(RTCLOCK)
    AV("settings/system/date_time_location", NB4_STR(LOCATION), openDateTime),
#else
    AV("settings/system/date_time_location", NB4_STR(DATE_TIME_LOCATION), openDateTime),
#endif
    AV("settings/system/diagnostics", NB4_STR(DIAGNOSTICS), []() { page(QM_TOOLS_DEBUG); }),
    AV("settings/system/about", NB4_STR(ABOUT), []() { page(QM_RADIO_VERSION); }),
    PEND("settings/system/help",
         NB4_STR(HELP),
         NB4_STR(THERE_IS_NO_HELP_INDEX_YET_THE_ONE_SHEET)),

    // --- Advanced
    AV("settings/advanced/inputs", NB4_STR(INPUTS), []() { page(QM_MODEL_INPUTS); }),
    AV("settings/advanced/mixes", NB4_STR(MIXES_60C8), []() { page(QM_MODEL_MIXES); }),
    AV("settings/advanced/outputs", NB4_STR(OUTPUTS), []() { page(QM_MODEL_OUTPUTS); }),
    AVIFR("settings/advanced/curves",
          NB4_STR(CURVES),
          []() { page(QM_MODEL_CURVES); },
          modelCurvesEnabled,
          NB4_STR(POINT_CURVES_ARE_SWITCHED_OFF_TURN_THEM)),
    AVIFR("settings/advanced/logic",
          NB4_STR(LOGIC),
          []() { page(QM_MODEL_LS); },
          modelLSEnabled,
          NB4_STR(LOGICAL_SWITCHES_ARE_SWITCHED_OFF_TURN_T)),

    AVIFR("settings/advanced/automation",
          NB4_STR(MODEL_SPECIAL_FUNCTIONS),
          []() { page(QM_MODEL_SF); },
          modelSFEnabled,
          NB4_STR(SPECIAL_FUNCTIONS_ARE_SWITCHED_OFF_TURN)),

    AV("settings/advanced/variables", NB4_STR(MODEL_VARIABLES_GVAR), openVariables),
    AVIFR("settings/advanced/scripts",
          NB4_STR(SCRIPTS),
          []() { page(QM_MODEL_SCRIPTS); },
          modelCustomScriptsEnabled,
          NB4_STR(SCRIPTS_ARE_SWITCHED_OFF_TURN_THEM_ON_IN)),
};

#undef AV
#undef AVTAB
#undef AVIF
#undef AVIFR
#undef PEND

const Nb4Section2 sections[] = {
    {"car", NB4_STR(CAR), ICON_NB4_MODEL_SETUP},
    {"steering", NB4_STR(STEERING_2090), ICON_NB4_STEERING},
    {"throttle_brake", NB4_STR(THROTTLE_BRAKE), ICON_NB4_THROTTLE},
    {"receiver_rf", NB4_STR(RECEIVER), ICON_RADIO},
    {"controls", NB4_STR(CONTROLS), ICON_NB4_OUTPUTS},
    {"telemetry", NB4_STR(TELEMETRY), ICON_MODEL_TELEMETRY},
    {"race", NB4_STR(RACE_5527), ICON_STATS_TIMERS},
    {"models", NB4_STR(MODELS), ICON_MODEL_SELECT},
    {"display", NB4_STR(DISPLAY), ICON_THEME},
    {"sound_alerts", NB4_STR(SOUND), ICON_RADIO_SETUP},
    {"connectivity", NB4_STR(CONNECTION), ICON_MODEL_USB},
    {"system", NB4_STR(SYSTEM), ICON_RADIO_HARDWARE},
    {"advanced", NB4_STR(ADVANCED), ICON_MODEL_MIXER},
};

}  // namespace

namespace {

constexpr int GRID_COLS = 4;

constexpr lv_coord_t TILE_H = 70;

class Nb4GridModal : public BaseDialog
{
 public:
  Nb4GridModal(const char* title, unsigned tiles, bool branded = false) :
      BaseDialog(title, true, gridWidth(), gridHeight(tiles))
  {
    if (branded)
      useBrandHeader();
    else
      useSectionHeader();
    form->setFlexLayout(LV_FLEX_FLOW_ROW_WRAP, PAD_SMALL, gridWidth(),
                        LV_SIZE_CONTENT);
    form->padAll(PAD_SMALL);

    etx_solid_bg(form->getLvObj(), COLOR_THEME_QM_BG_INDEX);
    if (lv_obj_t* content = lv_obj_get_parent(form->getLvObj()))
      etx_solid_bg(content, COLOR_THEME_QM_BG_INDEX);
  }

  void tile(uint8_t icon, const char* label, bool openable,
            std::function<void()> action, const char* reason = nullptr)
  {
    QuickMenuButton* btn = new QuickMenuButton(
        form, (EdgeTxIcon)icon, label,
        [this, action, openable, label, reason]() {
          if (openable) {

            action();
          } else {
            new MessageDialog(
                label,
                reason && *reason
                    ? reason
                    : STR_NB4_NOT_AVAILABLE_WITH_THE_CURRENT_RADIO);
          }
          return 0;
        },
        nullptr);

    if (!openable) btn->setDisabled();

    lv_obj_set_height(btn->getLvObj(), TILE_H);
    tiles.push_back(btn);
  }

  void checkEvents() override
  {
    BaseDialog::checkEvents();
    if (deleted()) return;
    for (auto* btn : tiles) {
      if (!btn->getLvObj()) continue;
      if (lv_obj_has_state(btn->getLvObj(), LV_STATE_FOCUSED))
        btn->setFocused();
      else
        btn->setDeFocused();
    }
  }

 private:
  std::vector<QuickMenuButton*> tiles;

  static lv_coord_t gridWidth()
  {

    return GRID_COLS * QuickMenuGroup::QM_BUTTON_WIDTH +
           (GRID_COLS - 1) * PAD_SMALL + 4 * PAD_SMALL;
  }

  static lv_coord_t gridHeight(unsigned)
  {
    return (lv_coord_t)(lv_disp_get_ver_res(nullptr) * 0.96);
  }
};

void (*singleDestinationImpl(const char* id))()
{
  const Nb4Route* views[32];
  const unsigned n = nb4RoutesOfSection(id, views, 32);
  void (*only)() = nullptr;
  for (unsigned v = 0; v < n && v < 32; v += 1) {
    if (views[v]->state == Nb4RouteState::NotBuiltYet) return nullptr;
    if (!nb4RouteIsOpenable(*views[v])) continue;
    if (!only) only = views[v]->open;
    else if (only != views[v]->open) return nullptr;
  }
  return only;
}

bool sectionHasSomethingOpenable(const char* id)
{
  const Nb4Route* views[32];
  const unsigned n = nb4RoutesOfSection(id, views, 32);
  for (unsigned v = 0; v < n && v < 32; v += 1)
    if (nb4RouteIsOpenable(*views[v])) return true;
  return false;
}

bool pathIsOpenable(const char* path)
{
  const char* slash = strchr(path, '/');
  if (slash && !strchr(slash + 1, '/')) return sectionHasSomethingOpenable(slash + 1);
  unsigned rc = 0;
  const Nb4Route* all = nb4Routes(&rc);
  for (unsigned r = 0; r < rc; r += 1)
    if (strcmp(all[r].path, path) == 0) return nb4RouteIsOpenable(all[r]);
  return false;
}

}  // namespace

static const Nb4QuickEntry quickDefaults[] = {
    {"settings/steering", NB4_STR(STEERING_2090), ICON_NB4_STEERING},
    {"settings/throttle_brake", NB4_STR(THROTTLE_BRAKE), ICON_NB4_THROTTLE},

    {"settings/throttle_brake/brake", NB4_STR(ABS), ICON_MODEL_CURVES},
    {"settings/controls/trims", NB4_STR(TRIMS_LABEL), ICON_NB4_OUTPUTS},
    {"settings/race/timer_laps", NB4_STR(LAPS), ICON_STATS_TIMERS},
    {"settings/telemetry/track_view", NB4_STR(TELEMETRY), ICON_MODEL_TELEMETRY},
    {"settings/receiver_rf", NB4_STR(RECEIVER), ICON_RADIO},
    {"settings/controls/monitor", NB4_STR(MONITOR), ICON_MONITOR},
};

const Nb4QuickEntry* nb4QuickAccessDefaults(unsigned* count)
{
  if (count) *count = sizeof(quickDefaults) / sizeof(quickDefaults[0]);
  return quickDefaults;
}

void nb4OpenQuickAccessModal()
{
  unsigned count = 0;
  const Nb4QuickEntry* entries = nb4QuickAccessDefaults(&count);
  auto modal = new Nb4GridModal(STR_NB4_QUICK_ACCESS,
                                count, true);
  for (unsigned i = 0; i < count; i += 1) {
    const Nb4QuickEntry* e = &entries[i];
    modal->tile(e->icon, e->label(), pathIsOpenable(e->path),
                [e]() { nb4OpenRoute(e->path); });
  }
}

const Nb4Route* nb4Routes(unsigned* count)
{
  if (count) *count = sizeof(routes) / sizeof(routes[0]);
  return routes;
}

const Nb4Section2* nb4Sections(unsigned* count)
{
  if (count) *count = sizeof(sections) / sizeof(sections[0]);
  return sections;
}

void (*nb4SingleDestinationOf(const char* sectionId))()
{
  return singleDestinationImpl(sectionId);
}

unsigned nb4RoutesOfSection(const char* sectionId, const Nb4Route** out,
                            unsigned max)
{
  if (!sectionId) return 0;
  char prefix[64];
  snprintf(prefix, sizeof(prefix), "settings/%s/", sectionId);
  const size_t len = strlen(prefix);
  unsigned n = 0;
  for (const auto& r : routes) {
    if (strncmp(r.path, prefix, len) != 0) continue;
    if (n < max && out) out[n] = &r;
    n += 1;
  }
  return n;
}

void nb4OpenSettingsModal()
{
  unsigned sectionCount = 0;
  const Nb4Section2* list = nb4Sections(&sectionCount);

  auto modal = new Nb4GridModal(STR_NB4_SETTINGS,
                                sectionCount, true);

  for (unsigned i = 0; i < sectionCount; i += 1) {
    const Nb4Section2* sec = &list[i];
    modal->tile(sec->icon, sec->label(),
                sectionHasSomethingOpenable(sec->id), [sec]() {

                  if (auto only = nb4SingleDestinationOf(sec->id)) {

                    openDestination(only, 0);
                    return;
                  }
                  const Nb4Route* views[32];
                  const unsigned n = nb4RoutesOfSection(sec->id, views, 32);
                  auto sub = new Nb4GridModal(sec->label(), n);
                  for (unsigned v = 0; v < n && v < 32; v += 1) {
                    const Nb4Route* route = views[v];
                    sub->tile(sec->icon, route->label(),
                              nb4RouteIsOpenable(*route),
                              [route]() { nb4OpenRoute(route->path); },
                              nb4StrOrNull(route->reason));
                  }
                });
  }
}

Nb4RouteAccess nb4RouteAccessOf(const char* path)
{
  if (!path) return Nb4RouteAccess::ModelData;

  static const char* const kRecovery[] = {
      "settings/system/calibration",
      "settings/system/storage",
      "settings/system/backup_restore",
      "settings/system/diagnostics",
      "settings/system/about",
      "settings/controls/monitor",
      "settings/models/management",
  };
  for (const char* r : kRecovery)
    if (strcmp(path, r) == 0) return Nb4RouteAccess::Recovery;

  static const char* const kRadioOnly[] = {
      "settings/system/general",
      "settings/system/power",
      "settings/system/hardware",
      "settings/system/date_time_location",
      "settings/sound_alerts/alerts",
      "settings/sound_alerts/sound",
      "settings/sound_alerts/haptic",
      "settings/connectivity/usb",
      "settings/connectivity/bluetooth",
      "settings/controls/general",
      "settings/display/top_bar",
      "settings/display/theme",
      "settings/display/home",
  };
  for (const char* r : kRadioOnly)
    if (strcmp(path, r) == 0) return Nb4RouteAccess::RadioOnly;

  return Nb4RouteAccess::ModelData;
}

bool nb4RouteIsOpenable(const Nb4Route& route)
{
  if (route.state != Nb4RouteState::Available || !route.open) return false;
  if (nb4ModelBlocked() && nb4RouteAccessOf(route.path) == Nb4RouteAccess::ModelData)
    return false;
  return !route.available || route.available();
}

const Nb4Route* nb4RouteByPath(const char* path)
{
  if (!path) return nullptr;
  for (const auto& r : routes)
    if (strcmp(r.path, path) == 0) return &r;
  return nullptr;
}

namespace {

const char* pendingRoute = nullptr;

bool sameText(const char* a, const char* b) { return a && b && strcmp(a, b) == 0; }

}  // namespace

const Nb4Alert* nb4AlertFor(const char* title, const char* message)
{
  // The radio raises each of these from a different place, and none of them
  // carries an identifier, so they are recognised by the title they were
  // given, and by the message where one title serves two causes.
  static const Nb4Alert failsafe = {"settings/receiver_rf/module", NB4_STR(RECEIVER),
                                    NB4_STR(ADVICE_FAILSAFE)};
  static const Nb4Alert throttle = {"settings/car/safety", NB4_STR(SAFETY),
                                    NB4_STR(ADVICE_THROTTLE)};
  static const Nb4Alert controls = {"settings/car/safety", NB4_STR(SAFETY),
                                    NB4_STR(ADVICE_CONTROLS)};
  static const Nb4Alert storageFull = {"settings/system/storage", NB4_STR(STORAGE),
                                       NB4_STR(ADVICE_STORAGE_FULL)};
  static const Nb4Alert radioData = {"settings/system/storage", NB4_STR(STORAGE),
                                     NB4_STR(ADVICE_RADIO_DATA)};
  static const Nb4Alert sound = {"settings/sound_alerts/sound", NB4_STR(SOUND),
                                 NB4_STR(ADVICE_SOUND_OFF)};
  static const Nb4Alert theme = {"settings/display/theme", NB4_STR(THEME),
                                 NB4_STR(ADVICE_THEME)};
  // Nothing to open: the fix is to free the control, not to change a setting.
  static const Nb4Alert keyStuck = {nullptr, nullptr, NB4_STR(ADVICE_KEY_STUCK)};

  if (sameText(title, STR_FAILSAFEWARN)) return &failsafe;
  if (sameText(title, STR_THROTTLE_UPPERCASE)) return &throttle;
  if (sameText(title, STR_SWITCHWARN)) return &controls;
  if (sameText(title, STR_SD_CARD)) return &storageFull;
  if (sameText(title, STR_STORAGE_WARNING)) return &radioData;
  if (sameText(title, STR_ALARMSWARN)) return &sound;
  if (sameText(title, STR_KEYSTUCK)) return &keyStuck;
  if (sameText(title, STR_WARNING)) return &theme;
  return nullptr;
}

bool nb4AlertCanOpen(const Nb4Alert& alert)
{
  if (!alert.path) return false;
  const Nb4Route* route = nb4RouteByPath(alert.path);
  return route && nb4RouteIsOpenable(*route);
}

void nb4DeferRoute(const char* path) { pendingRoute = path; }

bool nb4RunDeferredRoute()
{
  const char* path = pendingRoute;
  if (!path) return false;
  pendingRoute = nullptr;
  return nb4OpenRoute(path);
}

bool nb4OpenRoute(const char* path)
{
  if (!path) return false;

  static const char kPrefix[] = "settings/";
  if (strncmp(path, kPrefix, sizeof(kPrefix) - 1) != 0) return false;

  const char* firstSlash = path + sizeof(kPrefix) - 2;  // Prefix bar
  if (!strchr(firstSlash + 1, '/')) {
    const Nb4Route* views[32];
    const unsigned n = nb4RoutesOfSection(firstSlash + 1, views, 32);
    for (unsigned i = 0; i < n && i < 32; i += 1)

      if (nb4RouteIsOpenable(*views[i])) {
        openDestination(views[i]->open, 0);
        return true;
      }
    return false;
  }
  for (const auto& r : routes) {
    if (strcmp(r.path, path) != 0) continue;
    if (!nb4RouteIsOpenable(r)) return false;
    openDestination(r.open, r.tab);
    return true;
  }
  return false;
}

#endif  // RADIO_NB4_FAMILY
