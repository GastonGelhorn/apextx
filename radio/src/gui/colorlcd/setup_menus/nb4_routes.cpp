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
void openRadioSetup() { page(QM_RADIO_SETUP); }

void openPower() { openRadioSetupPowerPage(nb4Text("Energía", "Power")); }

void openSound() { openRadioSetupSoundPage(nb4Text("Sonido", "Sound")); }
void openAlarms() { openRadioSetupAlarmsPage(nb4Text("Alertas", "Alerts")); }
void openHaptic() { openRadioSetupHapticPage(nb4Text("Vibración", "Haptic")); }
void openDateTime()
{
#if defined(RADIO_NB4) && !defined(RTCLOCK)
  openRadioSetupDateTimePage(nb4Text("Ubicación", "Location"));
#else
  openRadioSetupDateTimePage(nb4Text("Fecha y ubicación", "Date & location"));
#endif
}
void openHardware()   { page(QM_RADIO_HARDWARE); }
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
      BaseDialog(nb4Text("Variables del modelo", "Model variables"), true,
                 (lv_coord_t)(lv_disp_get_hor_res(nullptr) * 0.92),
                 (lv_coord_t)(lv_disp_get_ver_res(nullptr) * 0.92))
  {
    form->padLeft(PAD_MEDIUM);
    form->padRight(PAD_MEDIUM);

    auto line = new Window(form, {0, 0, LV_PCT(100), 0});
    line->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_MEDIUM, LV_SIZE_CONTENT);
    lv_obj_set_style_flex_cross_place(line->getLvObj(), LV_FLEX_ALIGN_CENTER, 0);
    new StaticText(line, {0, 0, 0, 0}, nb4Text("Usar variables", "Use variables"),
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
        nb4Text("Abrir el editor de variables", "Open the variable editor"),
        []() { page(QM_MODEL_GVARS); return 0; });
    editor->setWrap();
    updateEditor();

    paragraph(form,
              nb4Text("Una variable es un número con nombre que vive en el "
                      "modelo. Varios ajustes pueden tomar su valor de ella en "
                      "vez de llevar su propia cifra: cambias la variable y "
                      "cambian todos a la vez.",
                      "A variable is a named number stored in the model. "
                      "Several settings can take their value from it instead of "
                      "carrying their own figure: change the variable and they "
                      "all change."));

    paragraph(form,
              nb4Text("En un coche de dos canales casi nunca hace falta, y "
                      "encendidas ponen un botón \"GV\" junto a cada número: "
                      "pulsarlo hace que ese ajuste deje de ser una cifra. Por "
                      "eso vienen apagadas.",
                      "A two-channel car rarely needs them, and switched on they "
                      "put a \"GV\" button next to every number: pressing it "
                      "stops that setting being a figure. That is why they ship "
                      "switched off."));
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
      BaseDialog(nb4Text("Luces", "Lights"), true,
                 (lv_coord_t)(lv_disp_get_hor_res(nullptr) * 0.92))
  {
    form->padLeft(PAD_MEDIUM);
    form->padRight(PAD_MEDIUM);

    auto modeLine = new Window(form, {0, 0, LV_PCT(100), 0});
    modeLine->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_MEDIUM, LV_SIZE_CONTENT);
    lv_obj_set_style_flex_cross_place(modeLine->getLvObj(), LV_FLEX_ALIGN_CENTER, 0);
    new StaticText(modeLine, {0, 0, 0, 0}, nb4Text("Modo", "Mode"),
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
        case NB4_LED_FIXED:   return std::string(nb4Text("Color fijo", "Fixed colour"));
        case NB4_LED_BREATHE: return std::string(nb4Text("Latido", "Breathing"));
        case NB4_LED_BATTERY: return std::string(nb4Text("Estado de la batería", "Battery state"));
        default:              return std::string(nb4Text("Apagado", "Off"));
      }
    });

    colorLine = new Window(form, {0, 0, LV_PCT(100), 0});
    colorLine->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_MEDIUM, LV_SIZE_CONTENT);
    lv_obj_set_style_flex_cross_place(colorLine->getLvObj(), LV_FLEX_ALIGN_CENTER, 0);
    new StaticText(colorLine, {0, 0, 0, 0}, nb4Text("Color", "Colour"),
                   COLOR_THEME_PRIMARY1_INDEX);
    auto color = new Choice(
        colorLine, {0, 0, 0, 0}, NB4_LED_WHITE, NB4_LED_COLOR_COUNT - 1,
        []() { return (int)g_eeGeneral.nb4LedColor; },
        [](int v) { g_eeGeneral.nb4LedColor = (uint8_t)v; storageDirty(EE_GENERAL); });
    color->setTextHandler([](int v) {
      switch (v) {
        case NB4_LED_RED:     return std::string(nb4Text("Rojo", "Red"));
        case NB4_LED_ORANGE:  return std::string(nb4Text("Naranja", "Orange"));
        case NB4_LED_YELLOW:  return std::string(nb4Text("Amarillo", "Yellow"));
        case NB4_LED_GREEN:   return std::string(nb4Text("Verde", "Green"));
        case NB4_LED_CYAN:    return std::string(nb4Text("Cian", "Cyan"));
        case NB4_LED_BLUE:    return std::string(nb4Text("Azul", "Blue"));
        case NB4_LED_MAGENTA: return std::string(nb4Text("Magenta", "Magenta"));
        default:              return std::string(nb4Text("Blanco", "White"));
      }
    });
    updateColorRow();

    auto note = new StaticText(
        form, {0, 0, LV_PCT(100), 0},
        nb4Text("Mientras la emisora carga, el LED enseña la carga en verde "
                "-latiendo mientras sube y fijo al llenarse- sea cual sea el "
                "modo. Sólo \"Apagado\" manda sobre eso: si apagas la luz, no se "
                "enciende sola.",
                "While the radio is charging the LED shows the charge in green "
                "-breathing while it fills, steady when full- whatever the mode. "
                "Only \"Off\" overrides that: if you switch the light off, it "
                "stays off."),
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

#define AV(path, es, en, fn) {path, es, en, Nb4RouteState::Available, nullptr, nullptr, fn, nullptr, 0}

#define AVTAB(path, es, en, fn, tab) {path, es, en, Nb4RouteState::Available, nullptr, nullptr, fn, nullptr, tab}

#define AVIF(path, es, en, fn, guard) {path, es, en, Nb4RouteState::Available, nullptr, nullptr, fn, guard, 0}

#define AVIFR(path, es, en, fn, guard, whyEs, whyEn) {path, es, en, Nb4RouteState::Available, whyEs, whyEn, fn, guard, 0}
#define PEND(path, es, en, whyEs, whyEn) {path, es, en, Nb4RouteState::NotBuiltYet, whyEs, whyEn, nullptr, nullptr, 0}

const Nb4Route routes[] = {
    // --- Car
    AV("settings/car/general", "General", "General", []() { page(QM_MODEL_SETUP); }),

    AV("settings/car/safety", "Seguridad", "Safety",
       []() { new PreflightChecks(); }),
    AV("settings/car/presets", "Configuración inicial", "Starting point",
       []() { page(QM_MODEL_NB4_RACING); }),
    AVIFR("settings/car/notes", "Notas", "Notes", []() { page(QM_MODEL_NOTES); },
          modelHasNotes,
          "Este coche todavia no tiene notas. Se leen de un fichero de "
                  "texto con su mismo nombre dentro de la carpeta MODELS de la "
                  "tarjeta: crealo por USB y aparecera aqui.",
                  "This car has no notes yet. They are read from a text file "
                  "named after it inside the card's MODELS folder: create it "
                  "over USB and it will show up here."),

    // --- Steering

    AVTAB("settings/steering/general", "General", "General",
          openSteering, 2),
    AVTAB("settings/steering/travel", "Recorridos", "Travel",
          openSteering, 0),
    AVTAB("settings/steering/response", "Respuesta", "Response",
          openSteering, 1),

    // --- Throttle and brake

    AVTAB("settings/throttle_brake/general", "General", "General",
          openThrottle, 0),
    AVTAB("settings/throttle_brake/throttle", "Gas", "Throttle",
          openThrottle, 0),
    AVTAB("settings/throttle_brake/brake", "Freno", "Brake",
          openThrottle, 2),

    AVTAB("settings/throttle_brake/abs", "ABS", "ABS",
          openThrottle, 2),

    AVTAB("settings/throttle_brake/trims", "Comportamiento del trim", "Trim behaviour",
          openThrottle, 0),
    AVTAB("settings/throttle_brake/nitro_engine", "Motor nitro", "Nitro engine",
          openThrottle, 3),

    AVTAB("settings/throttle_brake/advanced_mixing", "Mezcla avanzada",
          "Advanced mixing", openThrottle, 0),

    // --- Receiver and RF
    AV("settings/receiver_rf/rf", "RF", "RF",
       openModule),
    AV("settings/receiver_rf/receiver", "Receptor", "Receiver",
       openModule),
    AV("settings/receiver_rf/failsafe", "Failsafe", "Failsafe",
       openModule),

    // --- Channels and controls
    // Per-model actions and navigation share one physical-control editor.
    // Hardware naming remains reachable from its Other controls menu.
    AV("settings/controls/assignments", "Asignaciones", "Assignments",
       nb4OpenAssignments),

    AV("settings/controls/channels", "Canales", "Channels",
       nb4OpenChannelsDialog),
    AV("settings/controls/trims", "Trims", "Trims",
       []() { new TrimsSetup(); }),
    AV("settings/controls/general", "Comportamiento de los controles",
       "Control behaviour", openRadioSetup),
    AV("settings/controls/shortcuts", "Teclas y navegación", "Keys and navigation",
       nb4OpenNavigationAssignments),
    PEND("settings/controls/quick_access", "Configurar Acceso rápido",
         "Configure quick access",
         "Todavía no se puede elegir qué va en el Acceso rápido: las ocho "
                 "entradas son fijas por ahora. El Acceso rápido en sí funciona, "
                 "es la llave inglesa de la pantalla de inicio.",
                 "Choosing what goes into Quick access is not built yet: the "
                 "eight entries are fixed for now. Quick access itself works: "
                 "it is the spanner on the home screen."),
    AV("settings/controls/monitor", "Monitor", "Monitor",
       []() { new ChannelsViewMenu(); }),

    // --- Telemetry
    AVIFR("settings/telemetry/sensors", "Sensores", "Sensors",
          openTelemetry, modelTelemetryEnabled,
          "La telemetria esta apagada para este coche. Se enciende en "
                  "las opciones de vista de los ajustes de la emisora.",
                  "Telemetry is switched off for this car. Turn it on in the "
                  "radio settings' view options."),

    AVIFR("settings/telemetry/alerts", "Alertas", "Alerts",
          openTelemetryAlarmsPage, modelTelemetryEnabled,
          "La telemetria esta apagada para este coche. Se enciende en "
                  "las opciones de vista de los ajustes de la emisora.",
                  "Telemetry is switched off for this car. Turn it on in the "
                  "radio settings' view options."),
    AV("settings/telemetry/track_view", "Vista de pista", "Track view",
       []() { section(Nb4Section::Telemetry); }),

    // --- Race

    AV("settings/race/timers", "Cronómetros", "Timers", []() {
      Menu* m = new Menu();
      m->setTitle(nb4Text("Cronómetros", "Timers"));
      for (uint8_t t = 0; t < MAX_TIMERS && t < 3; t += 1) {
        char label[24];
        snprintf(label, sizeof(label), "%s %u", nb4Text("Crono", "Timer"), t + 1);
        m->addLine(label, [t]() { new TimerWindow(t); });
      }

      m->addLine(nb4Text("Seguimiento del gas", "Throttle tracking"),
                 []() { nb4OpenThrottleTraceDialog(); });
    }),
    AV("settings/race/timer_laps", "Crono y vueltas", "Timers & laps",
       []() { section(Nb4Section::Chrono); }),
    AV("settings/race/statistics", "Estadísticas", "Statistics",
       []() { page(QM_TOOLS_STATS); }),
    AV("settings/race/pit", "Boxes", "Pit", []() { section(Nb4Section::Pit); }),
    AV("settings/race/history", "Historial", "History",
       []() { section(Nb4Section::History); }),
    PEND("settings/race/race_summary", "Resumen de manga", "Run summary",
         "El resumen de manga todavía no está. Mientras tanto, cada manga "
                 "guardada se abre entera en Carrera > Historial.",
                 "The run summary is not built yet. In the meantime every saved "
                 "run opens in full under Race > History."),
    AV("settings/race/resets", "Reinicios de sesión", "Session resets",
       []() { resetMenu(); }),

    // --- Models
    AV("settings/models/management", "Gestión", "Manage",
       []() { new ModelLabelsWindow(); }),
    PEND("settings/models/templates", "Plantillas", "Templates",
         "Las plantillas todavía no tienen vista propia. Se eligen al "
                 "crear un coche nuevo, desde Modelos > Gestión.",
                 "Templates have no page of their own yet. You pick one when "
                 "creating a new car, from Models > Manage."),

    // --- Display and appearance
    AV("settings/display/interface", "Interfaz", "Interface",
       []() { page(QM_UI_SETUP); }),
    AV("settings/display/screens", "Pantallas", "Screens", openScreens),

    PEND("settings/display/widgets", "Widgets", "Widgets",
         "La pantalla de inicio de la NB4 no se monta con widgets, así que aquí "
                 "no hay nada que configurar. Si quieres una "
                 "pantalla de widgets, añádela en Pantalla > Pantallas.",
                 "The NB4 home screen is not built from widgets, so there is "
                 "nothing to configure here. For a widget screen, add one "
                 "under Screen > Screens."),
    AVIFR("settings/display/theme", "Tema", "Theme", []() { page(QM_UI_THEMES); },
          radioThemesEnabled,
          "Los temas externos estan apagados. Se encienden en las "
                  "opciones de vista de los ajustes de la emisora. La paleta y "
                  "el acento de la NB4 se cambian en Pantalla > Inicio, que no "
                  "depende de esto.",
                  "External themes are switched off. Turn them on in the radio "
                  "settings' view options. The NB4 palette and accent live in "
                  "Screen > Home, which does not depend on this."),
    AV("settings/display/home", "Inicio", "Home",
       []() { section(Nb4Section::Appearance); }),

    // --- Sound and alerts
    AV("settings/sound_alerts/alerts", "Alertas", "Alerts", openAlarms),
    AV("settings/sound_alerts/sound", "Sonido", "Sound", openSound),
    AV("settings/sound_alerts/haptic", "Vibración", "Haptic", openHaptic),

    AV("settings/sound_alerts/lights", "Luces", "Lights", openLeds),

    // --- Connectivity
    AV("settings/connectivity/usb", "USB", "USB", openRadioSetup),

    AVIFR("settings/connectivity/bluetooth", "Bluetooth", "Bluetooth",
          openHardware, hasBluetooth,
          "Esta emisora no lleva Bluetooth. No es que falte "
                  "configurarlo: el firmware de la NB4 se compila sin el porque "
                  "la placa no trae el modulo.",
                  "This radio has no Bluetooth. It is not a missing setting: "
                  "the NB4 firmware is built without it because the board has "
                  "no module."),
    AV("settings/connectivity/serial_port", "Puerto serie", "Serial port",
       openHardware),

    // --- System
    AV("settings/system/general", "Preferencias generales", "General preferences",
       openRadioSetup),
    AV("settings/system/power", "Energía", "Power", openPower),
    AV("settings/system/hardware", "Hardware", "Hardware",
       openHardware),
    AV("settings/system/calibration", "Calibración", "Calibration",
       []() { new RadioCalibrationPage(); }),

    AV("settings/system/storage", "Almacenamiento", "Storage", []() {
      if (!nb4MountFailureIsMissingFilesystem(nb4StorageMountResult())) {
        page(QM_TOOLS_STORAGE);
        return;
      }
      Menu* m = new Menu();
      m->setTitle(nb4Text("Almacenamiento", "Storage"));
      m->addLine(nb4Text("Abrir el explorador", "Open browser"),
                 []() { page(QM_TOOLS_STORAGE); });
      m->addLine(nb4Text("Crear sistema de archivos", "Create filesystem"), []() {
        new ConfirmDialog(
            nb4Text("Crear sistema de archivos", "Create filesystem"),
            nb4Text("No se encuentra un sistema de archivos. Crear uno BORRA todo lo "
                    "que hubiera: coches, ajustes y registros. Si crees que había "
                    "datos, haz antes una copia por USB.",
                    "No filesystem found. Creating one ERASES everything on it: cars, "
                    "settings and logs. If you think there was data, back it up over "
                    "USB first."),
            []() { nb4RequestFilesystemCreation(); });
      });
    }),
    AV("settings/system/backup_restore", "Copias y restauración",
       "Backup & restore", []() { section(Nb4Section::Backup); }),

    PEND("settings/system/firmware", "Firmware y bootloader", "Firmware & bootloader",
         "La emisora no se actualiza desde aquí: el firmware se graba por "
                 "USB en modo DFU, o desde un fichero en la tarjeta con el "
                 "cargador de arranque. La versión que llevas puesta está en "
                 "Sistema > Acerca de.",
                 "The radio is not updated from here: firmware is flashed over "
                 "USB in DFU mode, or from a file on the card using the "
                 "bootloader. The version you are running is under System > "
                 "About."),
#if defined(RADIO_NB4) && !defined(RTCLOCK)
    AV("settings/system/date_time_location", "Ubicación", "Location", openDateTime),
#else
    AV("settings/system/date_time_location", "Fecha, hora y ubicación",
       "Date, time & location", openDateTime),
#endif
    AV("settings/system/diagnostics", "Diagnóstico", "Diagnostics",
       []() { page(QM_TOOLS_DEBUG); }),
    AV("settings/system/about", "Acerca de", "About",
       []() { page(QM_RADIO_VERSION); }),
    PEND("settings/system/help", "Ayuda", "Help",
         "Todavía no hay un índice de ayuda. La única hoja escrita está "
                 "dentro de Receptor > RF, en el botón \"Qué hace cada ajuste\".",
                 "There is no help index yet. The one sheet written so far is "
                 "inside Receiver > RF, under the \"What each setting does\" "
                 "button."),

    // --- Advanced
    AV("settings/advanced/inputs", "Entradas", "Inputs",
       []() { page(QM_MODEL_INPUTS); }),
    AV("settings/advanced/mixes", "Mezclas", "Mixes",
       []() { page(QM_MODEL_MIXES); }),
    AV("settings/advanced/outputs", "Salidas", "Outputs",
       []() { page(QM_MODEL_OUTPUTS); }),
    AVIFR("settings/advanced/curves", "Curvas", "Curves",
          []() { page(QM_MODEL_CURVES); }, modelCurvesEnabled,
          "Las curvas de puntos estan apagadas. Se encienden en las "
                  "opciones de vista de los ajustes de la emisora. La curva de "
                  "gas y la de direccion no dependen de esto: viven en sus "
                  "propias paginas.",
                  "Point curves are switched off. Turn them on in the radio "
                  "settings' view options. The throttle and steering curves do "
                  "not depend on this: they live on their own pages."),
    AVIFR("settings/advanced/logic", "Lógica", "Logic", []() { page(QM_MODEL_LS); },
          modelLSEnabled,
          "Los interruptores logicos estan apagados. Se encienden en las "
                  "opciones de vista de los ajustes de la emisora.",
                  "Logical switches are switched off. Turn them on in the radio "
                  "settings' view options."),

    AVIFR("settings/advanced/automation", "Funciones especiales del modelo",
          "Model special functions", []() { page(QM_MODEL_SF); }, modelSFEnabled,
          "Las funciones especiales estan apagadas. Se encienden en las "
                  "opciones de vista de los ajustes de la emisora.",
                  "Special functions are switched off. Turn them on in the "
                  "radio settings' view options."),

    AV("settings/advanced/variables", "Variables del modelo (GVAR)",
       "Model variables (GVAR)", openVariables),
    AVIFR("settings/advanced/scripts", "Scripts", "Scripts",
          []() { page(QM_MODEL_SCRIPTS); }, modelCustomScriptsEnabled,
          "Los scripts estan apagados. Se encienden en las opciones de "
                  "vista de los ajustes de la emisora.",
                  "Scripts are switched off. Turn them on in the radio "
                  "settings' view options."),
};

#undef AV
#undef AVTAB
#undef AVIF
#undef PEND

const Nb4Section2 sections[] = {
    {"car", "Coche", "Car", ICON_NB4_MODEL_SETUP},
    {"steering", "Dirección", "Steering", ICON_NB4_STEERING},
    {"throttle_brake", "Gas / Freno", "Throttle / Brake", ICON_NB4_THROTTLE},
    {"receiver_rf", "Receptor", "Receiver", ICON_RADIO},
    {"controls", "Controles", "Controls", ICON_NB4_OUTPUTS},
    {"telemetry", "Telemetría", "Telemetry", ICON_MODEL_TELEMETRY},
    {"race", "Carrera", "Race", ICON_STATS_TIMERS},
    {"models", "Modelos", "Models", ICON_MODEL_SELECT},
    {"display", "Pantalla", "Screen", ICON_THEME},
    {"sound_alerts", "Sonido", "Sound", ICON_RADIO_SETUP},
    {"connectivity", "Conexión", "Connection", ICON_MODEL_USB},
    {"system", "Sistema", "System", ICON_RADIO_HARDWARE},
    {"advanced", "Avanzado", "Advanced", ICON_MODEL_MIXER},
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
                    : nb4Text("No está disponible con la configuración actual de "
                              "la emisora o de este coche.",
                              "Not available with the current radio or car "
                              "settings."));
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
    {"settings/steering", "Dirección", "Steering", ICON_NB4_STEERING},
    {"settings/throttle_brake", "Gas / Freno", "Throttle / Brake", ICON_NB4_THROTTLE},

    {"settings/throttle_brake/abs", "ABS", "ABS", ICON_MODEL_CURVES},
    {"settings/controls/trims", "Trims", "Trims", ICON_NB4_OUTPUTS},
    {"settings/race/timer_laps", "Vueltas", "Laps", ICON_STATS_TIMERS},
    {"settings/telemetry/track_view", "Telemetría", "Telemetry", ICON_MODEL_TELEMETRY},
    {"settings/receiver_rf", "Receptor", "Receiver", ICON_RADIO},
    {"settings/controls/monitor", "Monitor", "Monitor", ICON_MONITOR},
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
  auto modal = new Nb4GridModal(nb4Text("Acceso rápido", "Quick access"),
                                count, true);
  for (unsigned i = 0; i < count; i += 1) {
    const Nb4QuickEntry* e = &entries[i];
    modal->tile(e->icon, nb4Text(e->labelEs, e->labelEn), pathIsOpenable(e->path),
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

  auto modal = new Nb4GridModal(nb4Text("Ajustes", "Settings"),
                                sectionCount, true);

  for (unsigned i = 0; i < sectionCount; i += 1) {
    const Nb4Section2* sec = &list[i];
    modal->tile(sec->icon, nb4Text(sec->labelEs, sec->labelEn),
                sectionHasSomethingOpenable(sec->id), [sec]() {

                  if (auto only = nb4SingleDestinationOf(sec->id)) {

                    openDestination(only, 0);
                    return;
                  }
                  const Nb4Route* views[32];
                  const unsigned n = nb4RoutesOfSection(sec->id, views, 32);
                  auto sub = new Nb4GridModal(
                      nb4Text(sec->labelEs, sec->labelEn), n);
                  for (unsigned v = 0; v < n && v < 32; v += 1) {
                    const Nb4Route* route = views[v];
                    sub->tile(sec->icon, nb4Text(route->labelEs, route->labelEn),
                              nb4RouteIsOpenable(*route),
                              [route]() { nb4OpenRoute(route->path); },
                              nb4Text(route->reasonEs, route->reasonEn));
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
      "settings/connectivity/serial_port",
      "settings/controls/general",
      "settings/display/interface",
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
