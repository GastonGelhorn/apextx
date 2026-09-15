/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_routes.h"
#include "nb4_assignments.h"

#if defined(RADIO_NB4_FAMILY)

#include <algorithm>
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
#include "nb4_menu_pages.h"
#include "nb4_help.h"
#include "model_setup.h"

namespace {

void page(QMPage p) { QuickMenu::openPage(p); }

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
void openScreens() { nb4OpenScreens(); }

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
        line, {0, 0, 0, 0}, []() { return (uint8_t)modelGVEnabled(); },
        [this](uint8_t on) {
          g_model.modelGVDisabled = on ? OVERRIDE_ON : OVERRIDE_OFF;
          storageDirty(EE_MODEL);
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


// Labels and reasons are NB4_STR(...) accessors so the catalogue follows the
// active language at the moment a tile is drawn.
#define AV(path, label, fn, access, dest, help, shortcut) {path, label, Nb4RouteState::Available, nullptr, fn, nullptr, 0, Nb4RouteAccess::access, dest, NB4_STR(help), shortcut}
#define AVTAB(path, label, fn, tab, access, dest, help, shortcut) {path, label, Nb4RouteState::Available, nullptr, fn, nullptr, tab, Nb4RouteAccess::access, dest, NB4_STR(help), shortcut}
#define AVIF(path, label, fn, guard, access, dest, help, shortcut) {path, label, Nb4RouteState::Available, nullptr, fn, guard, 0, Nb4RouteAccess::access, dest, NB4_STR(help), shortcut}
#define AVIFR(path, label, fn, guard, why, access, dest, help, shortcut) {path, label, Nb4RouteState::Available, why, fn, guard, 0, Nb4RouteAccess::access, dest, NB4_STR(help), shortcut}
#define IN(path, label, fn, access, dest, help, shortcut, section) {path, label, Nb4RouteState::Available, nullptr, fn, nullptr, 0, Nb4RouteAccess::access, dest, NB4_STR(help), shortcut, section}

const Nb4Route routes[] = {
    // --- Car
    AV("settings/car/general", NB4_STR(UX_CAR_DETAILS), []() { page(QM_MODEL_SETUP); }, ModelData, "car/general", UX_HELP_DETAILS, true),

    AV("settings/car/safety", NB4_STR(SAFETY), []() { new PreflightChecks(); }, ModelData, "car/safety", UX_HELP_SAFETY, true),
    AV("settings/car/presets", NB4_STR(UX_VEHICLE_PRESETS), []() { page(QM_MODEL_NB4_RACING); }, ModelData, "car/presets", UX_HELP_PRESETS, true),
    AVIFR("settings/car/notes",
          NB4_STR(NOTES),
          []() { page(QM_MODEL_NOTES); },
          modelHasNotes,
          NB4_STR(THIS_CAR_HAS_NO_NOTES_YET_THEY_ARE_READ), ModelData, "car/notes", UX_HELP_CAR, true),

    AV("settings/car/advanced", NB4_STR(UX_ADVANCED_SETUP), []() { nb4OpenSettingsSection("advanced"); }, ModelData, "car/advanced", UX_HELP_ADVANCED, true),

    // --- Steering
    AVTAB("settings/steering/travel", NB4_STR(STEERING_2090), openSteering, 0, ModelData, "steering", UX_HELP_STEERING, true),
    AVTAB("settings/steering/curve", NB4_STR(CURVE), openSteering, 1, ModelData, "steering", UX_HELP_STEERING, false),
    AVTAB("settings/steering/centre", NB4_STR(CENTRE), openSteering, 2, ModelData, "steering", UX_HELP_STEERING, false),
    AVTAB("settings/steering/speed", NB4_STR(SPEED), openSteering, 3, ModelData, "steering", UX_HELP_STEERING, false),

    // --- Throttle and brake
    AVTAB("settings/throttle_brake/travel", NB4_STR(THROTTLE_BRAKE), openThrottle, 0, ModelData, "throttle_brake", UX_HELP_THROTTLE, true),
    AVTAB("settings/throttle_brake/curve", NB4_STR(CURVE), openThrottle, 1, ModelData, "throttle_brake", UX_HELP_THROTTLE, false),
    AVTAB("settings/throttle_brake/brake", NB4_STR(BRAKE_ABS), openThrottle, 2, ModelData, "throttle_brake", UX_HELP_THROTTLE, false),
    AVTAB("settings/throttle_brake/engine", NB4_STR(ENGINE), openThrottle, 3, ModelData, "throttle_brake", UX_HELP_THROTTLE, false),

    // --- Receiver and RF
    // One page holds the RF module, the receiver and failsafe together, so it
    // gets one entry rather than three labels for the same destination.
    AV("settings/receiver_rf/module", NB4_STR(RECEIVER), openModule, ModelData, "receiver_rf/module", UX_HELP_RECEIVER, true),

    // --- Channels and controls
    // Per-model actions and navigation share one physical-control editor.
    // Hardware naming remains reachable from its Other controls menu.
    AV("settings/controls/trims", NB4_STR(TRIMS_LABEL), []() { new TrimsSetup(); }, ModelData, "controls/trims", UX_HELP_TRIMS, true),
    AV("settings/controls/assignments", NB4_STR(ASSIGNMENTS), nb4OpenAssignments, ModelData, "controls/assignments", UX_HELP_ASSIGNMENTS, true),

    AV("settings/controls/channels", NB4_STR(CHANNELS), nb4OpenChannelsDialog, ModelData, "controls/channels", UX_HELP_CHANNELS, true),
    AV("settings/controls/general", NB4_STR(CONTROL_BEHAVIOUR), openControlBehaviour, RadioOnly, "controls/general", UX_HELP_BEHAVIOUR, true),
    AV("settings/controls/monitor", NB4_STR(MONITOR), []() { new ChannelsViewMenu(); }, Recovery, "controls/monitor", UX_HELP_MONITOR, true),

    // --- Telemetry
    AV("settings/telemetry/track_view",
       NB4_STR(TRACK_VIEW),
       []() { section(Nb4Section::Telemetry); }, ModelData, "telemetry/track_view", UX_HELP_TELEMETRY, true),
    AVIFR("settings/telemetry/sensors",
          NB4_STR(SENSORS),
          openTelemetry,
          modelTelemetryEnabled,
          NB4_STR(TELEMETRY_IS_SWITCHED_OFF_FOR_THIS_CAR_T), ModelData, "telemetry/sensors", UX_HELP_TELEMETRY, true),

    AVIFR("settings/telemetry/alerts",
          NB4_STR(ALERTS),
          openTelemetryAlarmsPage,
          modelTelemetryEnabled,
          NB4_STR(TELEMETRY_IS_SWITCHED_OFF_FOR_THIS_CAR_T), ModelData, "telemetry/alerts", UX_HELP_TELEMETRY, true),

    // --- Race
    AV("settings/race/timer_laps", NB4_STR(TIMERS_LAPS), []() { section(Nb4Section::Chrono); }, ModelData, "race/timer_laps", UX_HELP_LAPS, true),
    AV("settings/race/pit", NB4_STR(PIT), []() { section(Nb4Section::Pit); }, ModelData, "race/pit", UX_HELP_PIT, true),
    AV("settings/race/history", NB4_STR(HISTORY), []() { section(Nb4Section::History); }, ModelData, "race/history", UX_HELP_HISTORY, true),
    AV("settings/race/statistics", NB4_STR(STATISTICS), []() { page(QM_TOOLS_STATS); }, ModelData, "race/statistics", UX_HELP_STATS, true),
    AV("settings/race/setup", NB4_STR(UX_RACE_SETUP), nb4OpenRaceSetup, ModelData, "race/setup", UX_HELP_RACE_SETUP, true),

    AV("settings/race/timers", NB4_STR(TIMERS_85E8), nb4OpenTimers, ModelData, "race/timers", UX_HELP_TIMERS, true),
    AV("settings/race/resets", NB4_STR(SESSION_RESETS), nb4OpenSessionResets, ModelData, "race/resets", UX_HELP_RESETS, false),

    // --- Models
    AV("settings/models/management", NB4_STR(MANAGE), []() { new ModelLabelsWindow(); }, Recovery, "models/management", UX_HELP_MODELS, true),
    AV("settings/models/templates", NB4_STR(TEMPLATES), nb4OpenTemplates, Recovery, "models/templates", UX_HELP_TEMPLATES, true),

    // --- Display and appearance
    AV("settings/display/brightness", NB4_STR(BRIGHTNESS), openBrightness, RadioOnly, "display/brightness", UX_HELP_BRIGHTNESS, true),
    AV("settings/display/appearance", NB4_STR(UX_APPEARANCE), nb4OpenAppearance, RadioOnly, "display/appearance", UX_HELP_APPEARANCE, true),
    AV("settings/display/screens", NB4_STR(SCREENS), openScreens, ModelData, "display/screens", UX_HELP_SCREENS, true),
    AV("settings/display/top_bar", NB4_STR(TOP_BAR), []() { page(QM_UI_SETUP); }, ModelData, "display/top_bar", UX_HELP_TOPBAR, true),
    IN("settings/controls/shortcuts", NB4_STR(KEYS_AND_NAVIGATION), nb4OpenNavigationAssignments, ModelData, "controls/shortcuts", UX_HELP_NAVIGATION, true, "display"),
    IN("settings/controls/quick_access", NB4_STR(CONFIGURE_QUICK_ACCESS), nb4OpenQuickAccessSetup, RadioOnly, "controls/quick_access", UX_HELP_QUICK, false, "display"),
    IN("settings/sound_alerts/lights", NB4_STR(LIGHTS), openLeds, RadioOnly, "sound_alerts/lights", UX_HELP_LIGHTS, true, "display"),

    // --- Sound and alerts
    AV("settings/sound_alerts/alerts", NB4_STR(ALERTS), openAlarms, RadioOnly, "sound_alerts/alerts", UX_HELP_SOUND, true),
    AV("settings/sound_alerts/sound", NB4_STR(SOUND), openSound, RadioOnly, "sound_alerts/sound", UX_HELP_SOUND, true),
    AV("settings/sound_alerts/haptic", NB4_STR(HAPTIC), openHaptic, RadioOnly, "sound_alerts/haptic", UX_HELP_SOUND, true),

    // --- System (legacy connectivity IDs are preserved for saved shortcuts)
    IN("settings/connectivity/usb", NB4_STR(USB), openUsb, RadioOnly, "connectivity/usb", UX_HELP_CONNECTION, true, "system"),

#if defined(BLUETOOTH)
    IN("settings/connectivity/bluetooth", NB4_STR(BLUETOOTH), nb4OpenBluetooth, RadioOnly, "connectivity/bluetooth", UX_HELP_CONNECTION, true, "system"),
#endif

    // --- System
    AV("settings/system/backup_restore", NB4_STR(BACKUP_RESTORE), []() { section(Nb4Section::Backup); }, Recovery, "system/backup_restore", UX_HELP_BACKUP, true),
    AV("settings/system/reset", NB4_STR(UX_RESET_SETTINGS), []() { section(Nb4Section::Reset); }, Recovery, "system/reset", UX_HELP_RESET_SETTINGS, false),
    AV("settings/system/general", NB4_STR(GENERAL_PREFERENCES), openGeneralPrefs, RadioOnly, "system/general", UX_HELP_SYSTEM, true),
    AV("settings/system/power", NB4_STR(POWER), openPower, RadioOnly, "system/power", UX_HELP_SYSTEM, true),
    AV("settings/system/hardware", NB4_STR(HARDWARE), openHardware, RadioOnly, "system/hardware", UX_HELP_SYSTEM, true),
    AV("settings/system/calibration",
       NB4_STR(CALIBRATION),
       []() { new RadioCalibrationPage(); }, Recovery, "system/calibration", UX_HELP_SYSTEM, true),

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
    }, Recovery, "system/storage", UX_HELP_SYSTEM, true),

    AV("settings/system/update", NB4_STR(UPDATE), nb4RequestUpdateMode, Recovery, "system/update", UX_HELP_UPDATE, false),
#if defined(RADIO_NB4) && !defined(RTCLOCK)
    AV("settings/system/date_time_location", NB4_STR(LOCATION), openDateTime, RadioOnly, "system/date_time_location", UX_HELP_SYSTEM, true),
#else
    AV("settings/system/date_time_location", NB4_STR(DATE_TIME_LOCATION), openDateTime, RadioOnly, "system/date_time_location", UX_HELP_SYSTEM, true),
#endif
    AV("settings/system/diagnostics", NB4_STR(DIAGNOSTICS), []() { page(QM_TOOLS_DEBUG); }, Recovery, "system/diagnostics", UX_HELP_SYSTEM, true),
    AV("settings/system/about", NB4_STR(ABOUT), []() { page(QM_RADIO_VERSION); }, Recovery, "system/about", UX_HELP_SYSTEM, true),
    IN("settings/system/help", NB4_STR(HELP), []() { nb4OpenHelp(); }, Recovery, "system/help", UX_HELP_INDEX, false, "help"),

    // --- Advanced
    AV("settings/advanced/features", NB4_STR(UX_ENABLED_FEATURES), openNb4ModelFeatures, ModelData, "advanced/features", UX_HELP_FEATURES, true),
    AV("settings/advanced/input_preferences", NB4_STR(UX_INPUT_PREFERENCES), openNb4InputPreferences, ModelData, "advanced/input_preferences", UX_HELP_INPUT_PREFS, true),
    AV("settings/advanced/inputs", NB4_STR(INPUTS), []() { page(QM_MODEL_INPUTS); }, ModelData, "advanced/inputs", UX_HELP_INPUTS, true),
    AV("settings/advanced/mixes", NB4_STR(MIXES_60C8), []() { page(QM_MODEL_MIXES); }, ModelData, "advanced/mixes", UX_HELP_MIXES, true),
    AV("settings/advanced/outputs", NB4_STR(OUTPUTS), []() { page(QM_MODEL_OUTPUTS); }, ModelData, "advanced/outputs", UX_HELP_OUTPUTS, true),
    AVIFR("settings/advanced/logic",
          NB4_STR(LOGIC),
          []() { page(QM_MODEL_LS); },
          modelLSEnabled,
          NB4_STR(LOGICAL_SWITCHES_ARE_SWITCHED_OFF_TURN_T), ModelData, "advanced/logic", UX_HELP_LOGIC, true),

    AVIFR("settings/advanced/automation",
          NB4_STR(MODEL_SPECIAL_FUNCTIONS),
          []() { page(QM_MODEL_SF); },
          modelSFEnabled,
          NB4_STR(SPECIAL_FUNCTIONS_ARE_SWITCHED_OFF_TURN), ModelData, "advanced/automation", UX_HELP_FUNCTIONS, true),

    AV("settings/advanced/variables", NB4_STR(MODEL_VARIABLES_GVAR), openVariables, ModelData, "advanced/variables", UX_HELP_VARIABLES, true),
    AVIFR("settings/advanced/scripts",
          NB4_STR(SCRIPTS),
          []() { page(QM_MODEL_SCRIPTS); },
          modelCustomScriptsEnabled,
          NB4_STR(SCRIPTS_ARE_SWITCHED_OFF_TURN_THEM_ON_IN), ModelData, "advanced/scripts", UX_HELP_SCRIPTS, true),
};

#undef AV
#undef AVTAB
#undef AVIF
#undef AVIFR
#undef IN

const Nb4Section2 sections[] = {
    {"steering", NB4_STR(STEERING_2090), ICON_NB4_STEERING},
    {"throttle_brake", NB4_STR(THROTTLE_BRAKE), ICON_NB4_THROTTLE},
    {"car", NB4_STR(CAR), ICON_NB4_MODEL_SETUP},
    {"controls", NB4_STR(CONTROLS), ICON_NB4_OUTPUTS},
    {"receiver_rf", NB4_STR(RECEIVER), ICON_RADIO},
    {"race", NB4_STR(RACE_5527), ICON_STATS_TIMERS},
    {"telemetry", NB4_STR(TELEMETRY), ICON_MODEL_TELEMETRY},
    {"models", NB4_STR(MODELS), ICON_MODEL_SELECT},
    {"display", NB4_STR(DISPLAY), ICON_THEME},
    {"sound_alerts", NB4_STR(UX_SOUND_ALERTS), ICON_RADIO_SETUP},
    {"system", NB4_STR(SYSTEM), ICON_RADIO_HARDWARE},
    {"help", NB4_STR(HELP), ICON_RADIO_VERSION},
    {"advanced", NB4_STR(UX_ADVANCED_SETUP), ICON_MODEL_MIXER, "car"},
};

}  // namespace

namespace {

constexpr unsigned GRID_COLS = 4;
constexpr unsigned GRID_ROWS = 3;
constexpr coord_t TILE_MAX = 76;

struct AppTileColors {
  lv_color_t base;
  lv_color_t detail;
};

struct AppTileLabel {
  const char* key;
  const char* en;
  const char* es;
};

const char* appTileLabel(const char* key, const char* fallback)
{
  // Grid labels are deliberately compact: the complete translated title is
  // retained by the destination page and by unavailable-item explanations.
  // ApexTX NB4 ships EN/ES, so both supported UI languages are explicit here.
  static constexpr AppTileLabel labels[] = {
      {"steering", "Steering", "Dirección"},
      {"throttle_brake", "Throttle", "Gas/freno"},
      {"car", "Car", "Coche"},
      {"controls", "Controls", "Mandos"},
      {"receiver_rf", "Receiver", "Receptor"},
      {"race", "Race", "Carrera"},
      {"telemetry", "Telemetry", "Telemetría"},
      {"models", "Cars", "Coches"},
      {"display", "Display", "Pantalla"},
      {"sound_alerts", "Alerts", "Avisos"},
      {"system", "System", "Sistema"},
      {"help", "Help", "Ayuda"},
      {"quick-access/edit", "Edit", "Editar"},
      {"settings/car/general", "Details", "Datos"},
      {"settings/car/safety", "Startup", "Arranque"},
      {"settings/car/presets", "Presets", "Preajuste"},
      {"settings/car/notes", "Notes", "Notas"},
      {"settings/car/advanced", "Advanced", "Avanzado"},
      {"settings/steering/travel", "Steering", "Dirección"},
      {"settings/throttle_brake/travel", "Throttle", "Gas/freno"},
      {"settings/receiver_rf/module", "Receiver", "Receptor"},
      {"settings/controls/trims", "Trims", "Trims"},
      {"settings/controls/assignments", "Assign", "Asignar"},
      {"settings/controls/channels", "Channels", "Canales"},
      {"settings/controls/general", "Response", "Respuesta"},
      {"settings/controls/monitor", "Monitor", "Monitor"},
      {"settings/telemetry/track_view", "Live", "Vista"},
      {"settings/telemetry/sensors", "Sensors", "Sensores"},
      {"settings/telemetry/alerts", "Alerts", "Alertas"},
      {"settings/race/timer_laps", "Chrono", "Crono"},
      {"settings/race/pit", "Pit", "Boxes"},
      {"settings/race/history", "History", "Historial"},
      {"settings/race/statistics", "Stats", "Datos"},
      {"settings/race/setup", "Setup", "Ajustes"},
      {"settings/race/timers", "Timers", "Tiempos"},
      {"settings/race/resets", "Resets", "Reinicios"},
      {"settings/models/management", "Cars", "Coches"},
      {"settings/models/templates", "Templates", "Plantillas"},
      {"settings/display/brightness", "Bright", "Brillo"},
      {"settings/display/appearance", "Style", "Aspecto"},
      {"settings/display/screens", "Screens", "Pantallas"},
      {"settings/display/top_bar", "Top bar", "Barra"},
      {"settings/controls/shortcuts", "Keys", "Teclas"},
      {"settings/controls/quick_access", "Shortcuts", "Accesos"},
      {"settings/sound_alerts/lights", "Lights", "Luces"},
      {"settings/sound_alerts/alerts", "Alerts", "Avisos"},
      {"settings/sound_alerts/sound", "Sound", "Sonido"},
      {"settings/sound_alerts/haptic", "Haptic", "Vibración"},
      {"settings/connectivity/usb", "USB", "USB"},
      {"settings/connectivity/bluetooth", "Bluetooth", "Bluetooth"},
      {"settings/system/backup_restore", "Backup", "Copia"},
      {"settings/system/reset", "Restore", "Restaurar"},
      {"settings/system/general", "General", "General"},
      {"settings/system/power", "Power", "Energía"},
      {"settings/system/hardware", "Hardware", "Hardware"},
      {"settings/system/calibration", "Calibrate", "Calibrar"},
      {"settings/system/storage", "Storage", "Archivos"},
      {"settings/system/update", "Update", "Actualizar"},
      {"settings/system/date_time_location", "Location", "Ubicación"},
      {"settings/system/diagnostics", "Tests", "Pruebas"},
      {"settings/system/about", "About", "Acerca"},
      {"settings/system/help", "Help", "Ayuda"},
      {"settings/advanced/features", "Features", "Funciones"},
      {"settings/advanced/input_preferences", "Input cfg", "Entrada"},
      {"settings/advanced/inputs", "Inputs", "Entradas"},
      {"settings/advanced/mixes", "Mixes", "Mezclas"},
      {"settings/advanced/outputs", "Outputs", "Salidas"},
      {"settings/advanced/logic", "Logic", "Lógica"},
      {"settings/advanced/automation", "Actions", "Acciones"},
      {"settings/advanced/variables", "Variables", "Variables"},
      {"settings/advanced/scripts", "Scripts", "Scripts"},
  };
  if (key) {
    const bool spanish = g_eeGeneral.uiLanguage[0] == 'e' &&
                         g_eeGeneral.uiLanguage[1] == 's';
    for (const auto& label : labels)
      if (!strcmp(label.key, key)) return spanish ? label.es : label.en;
  }
  return fallback;
}

AppTileColors appTileColors(const char* key)
{
  // Dark-mode variants of the familiar iOS app colours.  Keeping a colour
  // family per destination makes an item recognisable in both its canonical
  // menu and Quick access.
  if (!key) return {lv_color_hex(0x636366), lv_color_hex(0x64D2FF)};
  if (strstr(key, "car/safety"))
    return {lv_color_hex(0xFF453A), lv_color_hex(0xFFD60A)};
  if (strstr(key, "car/presets"))
    return {lv_color_hex(0x0A84FF), lv_color_hex(0x30D158)};
  if (strstr(key, "car/notes"))
    return {lv_color_hex(0xFFD60A), lv_color_hex(0xFF9F0A)};
  if (strstr(key, "car/advanced"))
    return {lv_color_hex(0xBF5AF2), lv_color_hex(0x64D2FF)};
  if (strstr(key, "controls/assignments"))
    return {lv_color_hex(0x5E5CE6), lv_color_hex(0x64D2FF)};
  if (strstr(key, "controls/channels"))
    return {lv_color_hex(0x32ADE6), lv_color_hex(0x30D158)};
  if (strstr(key, "controls/general"))
    return {lv_color_hex(0x636366), lv_color_hex(0xBF5AF2)};
  if (strstr(key, "controls/monitor"))
    return {lv_color_hex(0x0A84FF), lv_color_hex(0xBF5AF2)};
  if (strstr(key, "telemetry/sensors"))
    return {lv_color_hex(0x30D158), lv_color_hex(0xFFD60A)};
  if (strstr(key, "telemetry/alerts"))
    return {lv_color_hex(0xFF453A), lv_color_hex(0x30D158)};
  if (strstr(key, "race/history"))
    return {lv_color_hex(0xFF453A), lv_color_hex(0xFFD60A)};
  if (strstr(key, "race/pit"))
    return {lv_color_hex(0xFF9F0A), lv_color_hex(0xFF453A)};
  if (strstr(key, "race/statistics"))
    return {lv_color_hex(0x30D158), lv_color_hex(0x64D2FF)};
  if (strstr(key, "race/setup"))
    return {lv_color_hex(0x5E5CE6), lv_color_hex(0xFF375F)};
  if (strstr(key, "race/resets"))
    return {lv_color_hex(0xFF453A), lv_color_hex(0xFF9F0A)};
  if (strstr(key, "models/templates"))
    return {lv_color_hex(0xBF5AF2), lv_color_hex(0xFF9F0A)};
  if (strstr(key, "display/brightness") || strstr(key, "sound_alerts/lights"))
    return {lv_color_hex(0xFFD60A), lv_color_hex(0xFF9F0A)};
  if (strstr(key, "display/appearance"))
    return {lv_color_hex(0xBF5AF2), lv_color_hex(0xFF375F)};
  if (strstr(key, "display/screens"))
    return {lv_color_hex(0x0A84FF), lv_color_hex(0x64D2FF)};
  if (strstr(key, "display/top_bar"))
    return {lv_color_hex(0x32ADE6), lv_color_hex(0x30D158)};
  if (strstr(key, "controls/shortcuts"))
    return {lv_color_hex(0x5E5CE6), lv_color_hex(0x64D2FF)};
  if (strstr(key, "controls/quick_access"))
    return {lv_color_hex(0x0A84FF), lv_color_hex(0xBF5AF2)};
  if (strstr(key, "sound_alerts/alerts"))
    return {lv_color_hex(0xFF453A), lv_color_hex(0xFFD60A)};
  if (strstr(key, "sound_alerts/sound"))
    return {lv_color_hex(0xBF5AF2), lv_color_hex(0x64D2FF)};
  if (strstr(key, "sound_alerts/haptic"))
    return {lv_color_hex(0x30D158), lv_color_hex(0x64D2FF)};
  if (strstr(key, "connectivity/usb"))
    return {lv_color_hex(0x0A84FF), lv_color_hex(0x30D158)};
  if (strstr(key, "system/backup_restore"))
    return {lv_color_hex(0x30D158), lv_color_hex(0x64D2FF)};
  if (strstr(key, "system/reset"))
    return {lv_color_hex(0xFF453A), lv_color_hex(0xFFD60A)};
  if (strstr(key, "system/general"))
    return {lv_color_hex(0x636366), lv_color_hex(0x64D2FF)};
  if (strstr(key, "system/power"))
    return {lv_color_hex(0xFF9F0A), lv_color_hex(0xFFD60A)};
  if (strstr(key, "system/hardware"))
    return {lv_color_hex(0x5E5CE6), lv_color_hex(0xBF5AF2)};
  if (strstr(key, "system/calibration"))
    return {lv_color_hex(0x0A84FF), lv_color_hex(0x64D2FF)};
  if (strstr(key, "system/storage"))
    return {lv_color_hex(0x32ADE6), lv_color_hex(0x30D158)};
  if (strstr(key, "system/update"))
    return {lv_color_hex(0x30D158), lv_color_hex(0x64D2FF)};
  if (strstr(key, "system/date_time_location"))
    return {lv_color_hex(0xFF9F0A), lv_color_hex(0x64D2FF)};
  if (strstr(key, "system/diagnostics"))
    return {lv_color_hex(0x636366), lv_color_hex(0x30D158)};
  if (strstr(key, "system/about"))
    return {lv_color_hex(0x32ADE6), lv_color_hex(0x64D2FF)};
  if (strstr(key, "steering"))
    return {lv_color_hex(0x0A84FF), lv_color_hex(0x64D2FF)};
  if (strstr(key, "throttle_brake"))
    return {lv_color_hex(0xFF9F0A), lv_color_hex(0xFF453A)};
  if (strstr(key, "receiver_rf"))
    return {lv_color_hex(0xBF5AF2), lv_color_hex(0xFF375F)};
  if (strstr(key, "telemetry"))
    return {lv_color_hex(0x30D158), lv_color_hex(0x64D2FF)};
  if (strstr(key, "race"))
    return {lv_color_hex(0xFF453A), lv_color_hex(0xFF9F0A)};
  if (strstr(key, "controls"))
    return {lv_color_hex(0x32ADE6), lv_color_hex(0x5E5CE6)};
  if (strstr(key, "models"))
    return {lv_color_hex(0xFF375F), lv_color_hex(0xFF9F0A)};
  if (strstr(key, "display"))
    return {lv_color_hex(0x64D2FF), lv_color_hex(0x5E5CE6)};
  if (strstr(key, "sound_alerts"))
    return {lv_color_hex(0xAF52DE), lv_color_hex(0xFF9F0A)};
  if (strstr(key, "reset"))
    return {lv_color_hex(0xFF453A), lv_color_hex(0xFF9F0A)};
  if (strstr(key, "help"))
    return {lv_color_hex(0x32ADE6), lv_color_hex(0x30D158)};
  if (strstr(key, "system"))
    return {lv_color_hex(0x636366), lv_color_hex(0x0A84FF)};
  if (strstr(key, "advanced"))
    return {lv_color_hex(0x5E5CE6), lv_color_hex(0xBF5AF2)};
  if (strstr(key, "car"))
    return {lv_color_hex(0x5E5CE6), lv_color_hex(0xFF375F)};
  return {lv_color_hex(0x0A84FF), lv_color_hex(0x30D158)};
}

uint8_t routeIcon(const Nb4Route& route, uint8_t fallback)
{
  if (!strcmp(route.destination, "steering")) return ICON_NB4_STEERING;
  if (!strcmp(route.destination, "throttle_brake")) return ICON_NB4_THROTTLE;
  if (!strcmp(route.path, "settings/car/general")) return ICON_NB4_MODEL_SETUP;
  if (!strcmp(route.path, "settings/car/safety")) return ICON_MODEL_SPECIAL_FUNCTIONS;
  if (!strcmp(route.path, "settings/car/presets")) return ICON_MODEL_FLIGHT_MODES;
  if (!strcmp(route.path, "settings/car/notes")) return ICON_MODEL_NOTES;
  if (!strcmp(route.path, "settings/car/advanced")) return ICON_MODEL_MIXER;
  if (!strcmp(route.path, "settings/controls/trims")) return ICON_NB4_OUTPUTS;
  if (!strcmp(route.path, "settings/controls/assignments")) return ICON_MODEL_INPUTS;
  if (!strcmp(route.path, "settings/controls/channels")) return ICON_MODEL_OUTPUTS;
  if (!strcmp(route.path, "settings/controls/general")) return ICON_RADIO_HARDWARE;
  if (!strcmp(route.path, "settings/controls/monitor")) return ICON_MONITOR;
  if (!strcmp(route.path, "settings/telemetry/track_view")) return ICON_MODEL_TELEMETRY;
  if (!strcmp(route.path, "settings/telemetry/sensors")) return ICON_STATS_ANALOGS;
  if (!strcmp(route.path, "settings/telemetry/alerts")) return ICON_RADIO_GLOBAL_FUNCTIONS;
  if (!strcmp(route.path, "settings/race/history")) return ICON_STATS;
  if (!strcmp(route.path, "settings/race/timer_laps")) return ICON_STATS_TIMERS;
  if (!strcmp(route.path, "settings/race/pit")) return ICON_RADIO_TOOLS;
  if (!strcmp(route.path, "settings/race/statistics")) return ICON_STATS_ANALOGS;
  if (!strcmp(route.path, "settings/race/setup")) return ICON_MODEL_SETUP;
  if (!strcmp(route.path, "settings/race/timers")) return ICON_STATS_TIMERS;
  if (!strcmp(route.path, "settings/race/resets")) return ICON_TOOLS_RESET;
  if (!strcmp(route.path, "settings/models/management")) return ICON_MODEL_SELECT;
  if (!strcmp(route.path, "settings/models/templates")) return ICON_TOOLS_APPS;
  if (!strcmp(route.path, "settings/display/brightness")) return ICON_THEME_VIEW1;
  if (!strcmp(route.path, "settings/display/appearance")) return ICON_RADIO_EDIT_THEME;
  if (!strcmp(route.path, "settings/display/screens")) return ICON_THEME;
  if (!strcmp(route.path, "settings/display/top_bar")) return ICON_THEME_SETUP;
  if (!strcmp(route.path, "settings/controls/shortcuts")) return ICON_RADIO_HARDWARE;
  if (!strcmp(route.path, "settings/controls/quick_access")) return ICON_QM_FAVORITES;
  if (!strcmp(route.path, "settings/sound_alerts/lights")) return ICON_THEME_VIEW2;
  if (!strcmp(route.path, "settings/sound_alerts/alerts")) return ICON_RADIO_GLOBAL_FUNCTIONS;
  if (!strcmp(route.path, "settings/sound_alerts/sound")) return ICON_RADIO_SETUP;
  if (!strcmp(route.path, "settings/sound_alerts/haptic")) return ICON_RADIO_TRAINER;
  if (!strcmp(route.path, "settings/connectivity/usb")) return ICON_MODEL_USB;
  if (!strcmp(route.path, "settings/connectivity/bluetooth")) return ICON_RADIO_TRAINER;
  if (!strcmp(route.path, "settings/system/backup_restore")) return ICON_MODEL_NOTES;
  if (!strcmp(route.path, "settings/system/reset")) return ICON_TOOLS_RESET;
  if (!strcmp(route.path, "settings/system/general")) return ICON_RADIO_SETUP;
  if (!strcmp(route.path, "settings/system/power")) return ICON_STATS_ANALOGS;
  if (!strcmp(route.path, "settings/system/hardware")) return ICON_RADIO_HARDWARE;
  if (!strcmp(route.path, "settings/system/calibration")) return ICON_RADIO_CALIBRATION;
  if (!strcmp(route.path, "settings/system/storage")) return ICON_RADIO_SD_MANAGER;
  if (!strcmp(route.path, "settings/system/update")) return ICON_RADIO_TOOLS;
  if (!strcmp(route.path, "settings/system/date_time_location")) return ICON_STATS_TIMERS;
  if (!strcmp(route.path, "settings/system/diagnostics")) return ICON_STATS_DEBUG;
  if (!strcmp(route.path, "settings/system/about")) return ICON_RADIO_VERSION;
  if (!strcmp(route.path, "settings/system/help")) return ICON_RADIO_VERSION;
  if (!strcmp(route.path, "settings/advanced/features")) return ICON_MODEL_SETUP;
  if (!strcmp(route.path, "settings/advanced/input_preferences")) return ICON_MODEL_INPUTS;
  if (!strcmp(route.path, "settings/advanced/inputs")) return ICON_MODEL_INPUTS;
  if (!strcmp(route.path, "settings/advanced/mixes")) return ICON_MODEL_MIXER;
  if (!strcmp(route.path, "settings/advanced/outputs")) return ICON_MODEL_OUTPUTS;
  if (!strcmp(route.path, "settings/advanced/logic")) return ICON_MODEL_LOGICAL_SWITCHES;
  if (!strcmp(route.path, "settings/advanced/automation")) return ICON_MODEL_SPECIAL_FUNCTIONS;
  if (!strcmp(route.path, "settings/advanced/variables")) return ICON_MODEL_GVARS;
  if (!strcmp(route.path, "settings/advanced/scripts")) return ICON_MODEL_LUA_SCRIPTS;
  return fallback;
}

class Nb4GridModal : public BaseDialog
{
 public:
  Nb4GridModal(const char* title, unsigned tiles) :
      BaseDialog(title, true, gridWidth(), gridHeight(tiles))
  {
    setScopeText(""); // Category grids contain destinations with different scopes.
    useBrandHeader();
    form->setFlexLayout(LV_FLEX_FLOW_ROW_WRAP, PAD_SMALL, gridWidth(),
                        LV_SIZE_CONTENT);
    form->padAll(PAD_SMALL);
    lv_obj_set_flex_align(form->getLvObj(), LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(form->getLvObj(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(form->getLvObj(), LV_SCROLLBAR_MODE_OFF);

    etx_solid_bg(form->getLvObj(), COLOR_THEME_QM_BG_INDEX);
    if (lv_obj_t* content = lv_obj_get_parent(form->getLvObj()))
      etx_solid_bg(content, COLOR_THEME_QM_BG_INDEX);
  }

  // Navigation grids only choose a destination. Help belongs to the editor,
  // not to a second category index; also reject route-level help injection.
  bool setHelpHandler(std::function<void()>) override { return false; }

  void tile(uint8_t icon, const char* label, bool openable,
            std::function<void()> action, const char* reason = nullptr,
            const char* colorKey = nullptr)
  {
    const coord_t size = tileSize();
    const coord_t column = columnWidth();
    auto slot = new Window(form, {0, 0, column, size});
    slot->padAll(PAD_ZERO);
    lv_obj_clear_flag(slot->getLvObj(),
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(slot->getLvObj(), LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(slot->getLvObj(), 0, LV_PART_MAIN);
    const char* compactLabel = appTileLabel(colorKey, label);
    QuickMenuButton* btn = new QuickMenuButton(
        slot, (EdgeTxIcon)icon, compactLabel,
        [this, action, openable, title = std::string(label), reason]() {
          if (openable) {

            action();
          } else {
            new MessageDialog(
                title.c_str(),
                reason && *reason
                    ? reason
                    : STR_NB4_NOT_AVAILABLE_WITH_THE_CURRENT_RADIO);
          }
          return 0;
        },
        nullptr);

    // Keep unavailable options focusable so their explanation can be read.
    if (!openable) lv_obj_set_style_opa(btn->getLvObj(), LV_OPA_50, 0);

    const auto colors = appTileColors(colorKey ? colorKey : label);
    btn->useAppTile(size, colors.base, colors.detail);
    btn->setPos((column - size) / 2, 0);
    tiles.push_back(btn);
  }

  void checkEvents() override
  {
    BaseDialog::checkEvents();
    if (deleted()) return;
    uint32_t current = g_eeGeneral.nb4QuickAccessVersion;
    for (unsigned i = 0; i < NB4_QUICK_ACCESS_COUNT; ++i)
      current = current * 31 + g_eeGeneral.nb4QuickAccess[i];
    if (watchQuickAccess && rebuild && current != quickSignature) {
      quickSignature = current;
      tiles.clear(); form->clear(); rebuild();
    }
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

 public:
  void populate(std::function<void()> action, bool watch = false) {
    watchQuickAccess = watch;
    rebuild = std::move(action);
    quickSignature = g_eeGeneral.nb4QuickAccessVersion;
    for (unsigned i = 0; i < NB4_QUICK_ACCESS_COUNT; ++i)
      quickSignature = quickSignature * 31 + g_eeGeneral.nb4QuickAccess[i];
    rebuild();
  }
  void configure(std::function<void()> action) {
    tile(ICON_QM_FAVORITES, STR_NB4_CONFIGURE_QUICK_ACCESS, true,
         std::move(action), nullptr, "quick-access/edit");
  }

 private:
  std::function<void()> rebuild;
  uint32_t quickSignature = 0;
  bool watchQuickAccess = false;

  static lv_coord_t gridWidth()
  {
    // Use the complete screen width. Four square columns then retain enough
    // height for the larger badge, its label gap, and the fixed label font in
    // portrait, while the dialog itself still provides its inner padding.
    return lv_disp_get_hor_res(nullptr);
  }

  static lv_coord_t gridHeight(unsigned)
  {
    return (lv_coord_t)(lv_disp_get_ver_res(nullptr) * 0.96);
  }

  coord_t tileSize() const
  {
    // Four columns by three rows is the complete viewport contract.  Derive
    // one square size from both axes so rotation cannot introduce scrolling.
    lv_obj_update_layout(form->getLvObj());
    const coord_t gap = lv_obj_get_style_pad_column(form->getLvObj(), 0);
    const coord_t availableWidth = lv_obj_get_content_width(form->getLvObj());
    const coord_t byWidth =
        (availableWidth - (GRID_COLS - 1) * gap) / GRID_COLS;
    const coord_t availableHeight =
        gridHeight(0) - EdgeTxStyles::UI_ELEMENT_HEIGHT - 2 * PAD_SMALL;
    const coord_t byHeight =
        (availableHeight - (GRID_ROWS - 1) * gap) / GRID_ROWS;
    return std::max<coord_t>(EdgeTxStyles::UI_ELEMENT_HEIGHT,
                             std::min({TILE_MAX, byWidth, byHeight}));
  }

  coord_t columnWidth() const
  {
    lv_obj_update_layout(form->getLvObj());
    const coord_t gap = lv_obj_get_style_pad_column(form->getLvObj(), 0);
    const coord_t availableWidth = lv_obj_get_content_width(form->getLvObj());
    return (availableWidth - (GRID_COLS - 1) * gap) / GRID_COLS;
  }
};

void (*singleDestinationImpl(const char* id))()
{
  const Nb4Route* views[32];
  const unsigned n = nb4RoutesOfSection(id, views, 32);
  const Nb4Route* first = nullptr;
  for (unsigned v = 0; v < n && v < 32; ++v) {
    if (!nb4RouteIsOpenable(*views[v])) continue;
    if (!first) first = views[v];
    else if (!first->destination || !views[v]->destination ||
             strcmp(first->destination, views[v]->destination)) return nullptr;
  }
  return first ? first->open : nullptr;
}

bool sectionHasSomethingOpenable(const char* id)
{
  const Nb4Route* views[32];
  const unsigned n = nb4RoutesOfSection(id, views, 32);
  for (unsigned v = 0; v < n && v < 32; v += 1)
    if (nb4RouteIsOpenable(*views[v])) return true;
  return false;
}

}  // namespace

const char* nb4AppTileLabel(const char* key, const char* fallback)
{
  return appTileLabel(key, fallback);
}

static const Nb4QuickEntry quickDefaults[] = {
    {"settings/steering", NB4_STR(STEERING_2090), ICON_NB4_STEERING},
    {"settings/throttle_brake", NB4_STR(THROTTLE_BRAKE), ICON_NB4_THROTTLE},

    {"settings/race/history", NB4_STR(HISTORY), ICON_STATS_TIMERS},
    {"settings/controls/trims", NB4_STR(TRIMS_LABEL), ICON_NB4_OUTPUTS},
    {"settings/race/timer_laps", NB4_STR(LAPS), ICON_STATS_TIMERS},
    {"settings/telemetry/track_view", NB4_STR(TELEMETRY), ICON_MODEL_TELEMETRY},
    {"settings/race/pit", NB4_STR(PIT), ICON_STATS_TIMERS},
    {"settings/controls/monitor", NB4_STR(MONITOR), ICON_MONITOR},
};

const Nb4QuickEntry* nb4QuickAccessDefaults(unsigned* count)
{
  if (count) *count = sizeof(quickDefaults) / sizeof(quickDefaults[0]);
  return quickDefaults;
}

void nb4OpenQuickAccessModal()
{
  nb4QuickAccessNormalize();
  auto modal = new Nb4GridModal(STR_NB4_QUICK_ACCESS, NB4_QUICK_ACCESS_COUNT);
  modal->populate([modal] {
  for (unsigned i = 0; i < NB4_QUICK_ACCESS_COUNT; ++i) {
    const Nb4Route* route = nb4RouteById(g_eeGeneral.nb4QuickAccess[i]);
    if (!route) continue;
    uint8_t icon = ICON_RADIO;
    for (const auto& section : sections) {
      if (nb4RouteInSection(*route, section.id)) icon = section.icon;
    }
    icon = routeIcon(*route, icon);
    modal->tile(icon, nb4QuickAccessLabel(*route).c_str(), nb4RouteIsOpenable(*route),
                [route] { nb4OpenRoute(route->path); },
                nb4StrOrNull(route->reason), route->path);
  }
  modal->configure([] { nb4OpenRoute("settings/controls/quick_access"); });
  }, true);
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
  unsigned n = 0;
  for (const auto& r : routes) {
    if (!nb4RouteInSection(r, sectionId)) continue;
    if (n < max && out) out[n] = &r;
    n += 1;
  }
  return n;
}

bool nb4RouteInSection(const Nb4Route& route, const char* sectionId)
{
  if (!sectionId) return false;
  if (route.section) return !strcmp(route.section, sectionId);
  const auto prefix = std::string("settings/") + sectionId + "/";
  return !strncmp(route.path, prefix.c_str(), prefix.size());
}

void nb4OpenSettingsSection(const char* id)
{
  nb4QuickAccessNormalize();
  const Nb4Section2* section = nullptr;
  for (const auto& item : sections) if (!strcmp(item.id, id)) section = &item;
  if (!section) return;
  // These categories already have one editor with its own tabs, not submenus.
  const char* direct = !strcmp(id, "steering") ? "settings/steering/travel" :
    !strcmp(id, "throttle_brake") ? "settings/throttle_brake/travel" :
    !strcmp(id, "receiver_rf") ? "settings/receiver_rf/module" :
    !strcmp(id, "help") ? "settings/system/help" : nullptr;
  if (direct) { nb4OpenRoute(direct); return; }
  const Nb4Route* views[32];
  const unsigned n = nb4RoutesOfSection(id, views, 32);
  auto modal = new Nb4GridModal(section->label(), n);
  modal->populate([modal, section] {
  const Nb4Route* views[32];
  const unsigned n = nb4RoutesOfSection(section->id, views, 32);
  for (unsigned v = 0; v < n && v < 32; ++v) {
    const auto route = views[v];
    if (!nb4RouteInSettings(*route)) continue;
    modal->tile(routeIcon(*route, section->icon), route->label(),
                nb4RouteIsOpenable(*route),
                [route] { nb4OpenRoute(route->path); },
                nb4StrOrNull(route->reason), route->path);
  }
  });
}

void nb4OpenSettingsModal()
{
  nb4QuickAccessNormalize();
  auto modal = new Nb4GridModal(STR_NB4_SETTINGS, 12);
  modal->populate([modal] {
  for (const auto& section : sections) {
    if (section.parent) continue;
    const Nb4Route* views[32];
    const auto n = nb4RoutesOfSection(section.id, views, 32);
    bool visible = false;
    for (unsigned i = 0; i < n && i < 32; ++i)
      if (nb4RouteInSettings(*views[i])) visible = true;
    if (!visible) continue;
    const auto sec = &section;
    modal->tile(sec->icon, sec->label(), sectionHasSomethingOpenable(sec->id),
                [sec] { nb4OpenSettingsSection(sec->id); }, nullptr, sec->id);
  }
  });
}

bool nb4RouteInQuickAccess(const Nb4Route& route)
{
  for (unsigned i = 0; i < NB4_QUICK_ACCESS_COUNT; ++i) {
    const auto pinned = nb4RouteById(g_eeGeneral.nb4QuickAccess[i]);
    if (pinned && pinned->shortcut && !strcmp(pinned->destination, route.destination)) return true;
  }
  return false;
}

bool nb4RouteInSettings(const Nb4Route& route)
{
  if ((!strcmp(route.destination, "steering") || !strcmp(route.destination, "throttle_brake")) && route.tab)
    return false;
  return true;
}

Nb4RouteAccess nb4RouteAccessOf(const char* path)
{
  const auto route = nb4RouteByPath(path);
  return route ? route->access : Nb4RouteAccess::ModelData;
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
  if (!strcmp(path, "settings/display/home") || !strcmp(path, "settings/display/theme"))
    path = "settings/display/appearance";
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
  if (!path || strncmp(path, "settings/", 9)) return false;
  if (!strchr(path + 9, '/')) {
    if (!sectionHasSomethingOpenable(path + 9)) return false;
    nb4OpenSettingsSection(path + 9);
    return true;
  }
  const auto route = nb4RouteByPath(path);
  if (!route || !nb4RouteIsOpenable(*route)) return false;
  openDestination(route->open, route->tab);
  if (auto page = Layer::back()) {
    if (!page->isHelpPage()) page->setScopeText(nb4RouteScope(*route));
    page->setRouteTitle(route->label());
    // Receiver keeps its richer, field-by-field help registered by ModulePage.
    if (!page->isHelpPage() && strcmp(route->destination, "receiver_rf/module"))
      page->setHelpHandler([route] { nb4OpenHelp(route->path, true); });
  }
  return true;
}

uint32_t nb4RouteId(const char* path)
{
  if (!path) return 0;
  uint32_t hash = 2166136261u;
  while (*path) hash = (hash ^ uint8_t(*path++)) * 16777619u;
  return hash;
}

const Nb4Route* nb4RouteById(uint32_t id)
{
  if (!id) return nullptr;
  for (const auto& route : routes)
    if (nb4RouteId(route.path) == id) return &route;
  return nullptr;
}

void nb4QuickAccessReset()
{
  static const char* defaults[] = {
    "settings/steering/travel", "settings/throttle_brake/travel",
    "settings/race/history", "settings/controls/trims",
    "settings/race/timer_laps", "settings/telemetry/track_view",
    "settings/race/pit", "settings/controls/monitor"
  };
  for (unsigned i = 0; i < NB4_QUICK_ACCESS_COUNT; ++i)
    g_eeGeneral.nb4QuickAccess[i] = nb4RouteId(defaults[i]);
  g_eeGeneral.nb4QuickAccessVersion = 2;
  storageDirty(EE_GENERAL);
}

void nb4QuickAccessNormalize()
{
  if (!g_eeGeneral.nb4QuickAccessVersion) { nb4QuickAccessReset(); return; }
  bool changed = g_eeGeneral.nb4QuickAccessVersion < 2;
  for (unsigned i = 0; i < NB4_QUICK_ACCESS_COUNT; ++i) {
    auto id = g_eeGeneral.nb4QuickAccess[i];
    if (!id) continue;
    auto route = nb4RouteById(id);
    if (route && route->tab && (!strcmp(route->destination, "steering") || !strcmp(route->destination, "throttle_brake"))) {
      const std::string path = std::string("settings/") + route->destination + "/travel";
      route = nb4RouteByPath(path.c_str());
      id = nb4RouteId(route->path);
      g_eeGeneral.nb4QuickAccess[i] = id;
      changed = true;
    }
    bool valid = route && route->shortcut;
    for (unsigned j = 0; j < i; ++j)
      if (id == g_eeGeneral.nb4QuickAccess[j]) valid = false;
    if (!valid) { g_eeGeneral.nb4QuickAccess[i] = 0; changed = true; }
  }
  if (changed) {
    g_eeGeneral.nb4QuickAccessVersion = 2;
    storageDirty(EE_GENERAL);
  }
}

bool nb4QuickAccessSet(unsigned slot, uint32_t id)
{
  if (slot >= NB4_QUICK_ACCESS_COUNT) return false;
  const auto route = nb4RouteById(id);
  if (id && (!route || !route->shortcut)) return false;
  for (unsigned i = 0; id && i < NB4_QUICK_ACCESS_COUNT; ++i)
    if (i != slot && g_eeGeneral.nb4QuickAccess[i] == id) return false;
  g_eeGeneral.nb4QuickAccess[slot] = id;
  g_eeGeneral.nb4QuickAccessVersion = 2;
  storageDirty(EE_GENERAL);
  return true;
}

void nb4QuickAccessMove(unsigned slot, int direction)
{
  const int target = int(slot) + direction;
  if (slot >= NB4_QUICK_ACCESS_COUNT || target < 0 || target >= int(NB4_QUICK_ACCESS_COUNT)) return;
  const uint32_t previous = g_eeGeneral.nb4QuickAccess[slot];
  g_eeGeneral.nb4QuickAccess[slot] = g_eeGeneral.nb4QuickAccess[target];
  g_eeGeneral.nb4QuickAccess[target] = previous;
  storageDirty(EE_GENERAL);
}

const char* nb4RouteHelp(const Nb4Route& route)
{
  return route.help ? route.help() : STR_NB4_NOT_AVAILABLE_WITH_THE_CURRENT_RADIO;
}

std::string nb4RouteScope(const Nb4Route& route)
{
  if (!strcmp(route.path, "settings/race/statistics") ||
      !strcmp(route.path, "settings/system/backup_restore") ||
      !strcmp(route.path, "settings/system/reset")) return STR_NB4_UX_SCOPE_MIXED;
  if (nb4RouteInSection(route, "models")) return STR_NB4_UX_SCOPE_CARS;
  if (route.access != Nb4RouteAccess::ModelData &&
      strcmp(route.path, "settings/controls/monitor")) return STR_NB4_UX_SCOPE_RADIO;
  return std::string(STR_NB4_UX_SCOPE_CAR) +
    std::string(g_model.header.name, strnlen(g_model.header.name, LEN_MODEL_NAME));
}

std::string nb4QuickAccessLabel(const Nb4Route& route)
{
  if (!strcmp(route.path, "settings/receiver_rf/module")) return STR_NB4_RECEIVER;
  if (!strcmp(route.destination, "steering")) {
    if (!route.tab) return STR_NB4_STEERING_2090;
    return std::string(STR_NB4_STEERING_2090) + " / " + route.label();
  }
  if (!strcmp(route.destination, "throttle_brake")) {
    if (!route.tab) return STR_NB4_THROTTLE_BRAKE;
    if (route.tab == 2) return STR_NB4_BRAKE_ABS;
    return std::string(STR_NB4_THROTTLE_BRAKE) + " / " + route.label();
  }
  return route.label();
}

#endif  // RADIO_NB4_FAMILY
