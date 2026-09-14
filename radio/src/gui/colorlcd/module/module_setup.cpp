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

#include "module_setup.h"
#if defined(RADIO_NB4)
#include "targets/pl18/nb4_rf_controller.h"
#endif
#if defined(RADIO_NB4_FAMILY) && defined(AFHDS3)
#include "dialog.h"
#include "pulses/afhds3.h"
#include "nb4_car_state.h"   // nb4Text
#include "mainview/nb4_help.h"
#endif

#include "bind_menu_d16.h"
#include "button.h"
#include "channel_range.h"
#include "choice.h"
#include "custom_failsafe.h"
#include "form.h"
#include "mixer_scheduler.h"
#include "edgetx.h"
#include "ppm_settings.h"
#include "storage/modelslist.h"
#include "etx_lv_theme.h"
#include "os/sleep.h"

#if defined(EXTERNAL_ANTENNA)
#include "ext_antenna_settings.h"
#endif

#if defined(PXX2)
#include "access_settings.h"
#endif

#if defined(CROSSFIRE)
#include "crossfire_settings.h"
#include "telemetry/crossfire.h"
#endif

#if defined(AFHDS2)
#include "afhds2a_settings.h"
#endif

#if defined(AFHDS3)
#include "afhds3_settings.h"
#endif

#if defined(AFHDS2)
#include "pulses/flysky.h"
#endif

#if defined(MULTIMODULE)
#include "io/multi_protolist.h"
#include "mpm_settings.h"
#include "multi_rfprotos.h"
#endif

#if defined(DSMP)
#include "dsmp_settings.h"
#endif

#define SET_DIRTY() storageDirty(EE_MODEL)

#define ETX_STATE_UNIQUE_ID_WARN LV_STATE_USER_1

static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(2),
                                     LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

struct FailsafeChoice : public Window {
  FailsafeChoice(Window* parent, uint8_t moduleIdx) :
      Window(parent, rect_t{}), moduleIdx(moduleIdx)
  {
    padAll(PAD_TINY);
    setFlexLayout(LV_FLEX_FLOW_ROW, PAD_SMALL, LV_SIZE_CONTENT);

    auto md = &g_model.moduleData[moduleIdx];
    new Choice(this, rect_t{}, STR_VFAILSAFE, 0, FAILSAFE_LAST,
              GET_DEFAULT(md->failsafeMode), [=](int32_t newValue) {
                md->failsafeMode = newValue;
                optsBtn->show(newValue == FAILSAFE_CUSTOM);
                SET_DIRTY();
              });

    optsBtn = new TextButton(this, rect_t{}, STR_SET, [=]() -> uint8_t {
      new FailSafePage(moduleIdx);
      return 0;
    });
    optsBtn->show(md->failsafeMode == FAILSAFE_CUSTOM);
  }

  void update() const
  {
    optsBtn->show(g_model.moduleData[moduleIdx].failsafeMode == FAILSAFE_CUSTOM);
  }

 private:
  uint8_t moduleIdx;
  TextButton* optsBtn;
};

#if defined(RADIO_NB4_FAMILY) && defined(AFHDS3)
// The NB4 progress bar counts completed bind stages, never UART packets or
// elapsed time. Only receiver data plus a confirmed link complete two-way bind.
class Nb4BindDialog : public BaseDialog
{
 public:
  Nb4BindDialog(uint8_t moduleIdx, std::function<void()> onDone) :
      BaseDialog(nb4Text("Enlazar receptor", "Bind receiver"), false),
      moduleIdx(moduleIdx),
      onDone(std::move(onDone))
  {
    // Explicit width lets instructions wrap in both screen orientations.
#if defined(RADIO_NB4)
    new DynamicText(form, rect_t{0, 0, LV_PCT(100), 0}, [moduleIdx]() {
      using afhds3::BindPhase;
      const char* title = "";
      switch (afhds3::getBindPhase(moduleIdx)) {
        case BindPhase::Preparing: title = nb4Text("1/3 Preparando emisora", "1/3 Preparing radio"); break;
        case BindPhase::Searching: title = nb4Text("2/3 Buscando receptor", "2/3 Searching for receiver"); break;
        case BindPhase::Confirming: title = nb4Text("3/3 Confirmando enlace", "3/3 Confirming connection"); break;
        case BindPhase::ManualFinish: title = nb4Text("3/3 Finaliza el enlace", "3/3 Finish binding"); break;
        case BindPhase::Connected: title = nb4Text("Receptor conectado", "Receiver connected"); break;
        case BindPhase::Failed: title = nb4Text("Enlace no completado", "Binding not completed"); break;
      }
      return std::string(title);
    }, COLOR_THEME_PRIMARY1_INDEX, FONT(BOLD) | CENTERED);

    progress = lv_bar_create(form->getLvObj());
    lv_obj_set_size(progress, LV_PCT(100), 8);
    lv_obj_set_style_border_width(progress, 0, 0);
    lv_obj_set_style_anim_time(progress, 0, 0);
    etx_solid_bg(progress, COLOR_THEME_SECONDARY2_INDEX);
    etx_solid_bg(progress, COLOR_THEME_FOCUS_INDEX, LV_PART_INDICATOR);
    lv_bar_set_range(progress, 0, 3);
    lv_bar_set_value(progress, 0, LV_ANIM_OFF);

    new DynamicText(form, rect_t{0, 0, LV_PCT(100), 0}, [moduleIdx]() {
      using afhds3::BindPhase;
      const char* text = "";
      switch (afhds3::getBindPhase(moduleIdx)) {
        case BindPhase::Preparing:
          text = nb4Text("Preparando la comunicación con el receptor.", "Preparing communication with the receiver."); break;
        case BindPhase::Searching:
          text = nb4Text("Enciende el receptor manteniendo pulsado su botón de enlace.",
                         "Power the receiver while holding its bind button."); break;
        case BindPhase::Confirming:
          text = nb4Text("Receptor guardado. Esperando la confirmación de conexión.",
                         "Receiver saved. Waiting for connection confirmation."); break;
        case BindPhase::ManualFinish:
          text = nb4Text("Una vía: pulsa Finalizar cuando el LED parpadee despacio.",
                         "One way: press Finish when the LED flashes slowly."); break;
        case BindPhase::Connected:
          text = nb4Text("Enlace confirmado y guardado en este modelo.",
                         "Connection confirmed and saved in this model."); break;
        case BindPhase::Failed: {
          char msg[64] = "";
          getModuleStatusString(moduleIdx, msg);
          return std::string(msg) + nb4Text(". Cierra y vuelve a enlazar.", ". Close and bind again.");
        }
      }
      return std::string(text);
    }, COLOR_THEME_PRIMARY3_INDEX, CENTERED);
#else
    new StaticText(form, rect_t{0, 0, LV_PCT(100), 0},
                   nb4Text("Enciende el receptor manteniendo pulsado su botón de enlace.",
                           "Power the receiver while holding its bind button."),
                   COLOR_THEME_PRIMARY1_INDEX, CENTERED);
    new StaticText(form, rect_t{0, 0, LV_PCT(100), 0}, nb4Text("ESTADO", "STATUS"),
                   COLOR_THEME_PRIMARY3_INDEX, CENTERED);
    new DynamicText(form, rect_t{0, 0, LV_PCT(100), 0}, [moduleIdx]() {
      char msg[64] = "";
      getModuleStatusString(moduleIdx, msg);
      return std::string(msg);
    }, COLOR_THEME_PRIMARY1_INDEX, FONT(BOLD) | CENTERED);
    if (!g_model.moduleData[moduleIdx].afhds3.telemetry)
      new StaticText(form, rect_t{0, 0, LV_PCT(100), 0},
                     nb4Text("Sin telemetría: finaliza cuando el LED parpadee despacio.",
                             "One way: finish when the LED flashes slowly."),
                     COLOR_THEME_PRIMARY3_INDEX, CENTERED);
#endif
    new TextButton(form, rect_t{0, 0, LV_PCT(100), 0},
                   g_model.moduleData[moduleIdx].afhds3.telemetry ? nb4Text("Cancelar", "Cancel") :
                     nb4Text("Finalizar", "Finish"),
                   [this]() { close(false); return 0; });
  }

  void onCancel() override { close(false); }

  void checkEvents() override
  {
    BaseDialog::checkEvents();
    if (deleted()) return;

#if defined(RADIO_NB4)
    const auto phase = afhds3::getBindPhase(moduleIdx);
    if (phase != lastPhase) {
      using afhds3::BindPhase;
      if (phase != BindPhase::Failed) {
        const int completed = phase == BindPhase::Connected ? 3 :
          (phase == BindPhase::Confirming || phase == BindPhase::ManualFinish) ? 2 :
          phase == BindPhase::Searching ? 1 : 0;
        lv_bar_set_value(progress, completed, LV_ANIM_OFF);
      }
      etx_bg_color(progress, phase == BindPhase::Failed ? COLOR_THEME_WARNING_INDEX :
                   phase == BindPhase::Connected ? COLOR_THEME_EDIT_INDEX : COLOR_THEME_FOCUS_INDEX,
                   LV_PART_INDICATOR);
      lastPhase = phase;
    }
    const bool connected = phase == afhds3::BindPhase::Connected;
#else
    const bool connected = afhds3::isConnected(moduleIdx);
#endif
    if (connected) {
      if (!connectedShown) {
        connectedShown = true;
        connectedSince = get_tmr10ms();
      }
      else if ((tmr10ms_t)(get_tmr10ms() - connectedSince) > 80) close(true);
    } else {
      connectedShown = false;
    }
  }

 private:
  uint8_t moduleIdx;
  std::function<void()> onDone;
  tmr10ms_t connectedSince = 0;
  bool connectedShown = false;
#if defined(RADIO_NB4)
  lv_obj_t* progress = nullptr;
  afhds3::BindPhase lastPhase = afhds3::BindPhase::Preparing;
#endif

  void close(bool bound)
  {

    if (moduleState[moduleIdx].mode == MODULE_MODE_BIND)
      moduleState[moduleIdx].mode = MODULE_MODE_NORMAL;
    if (bound) AUDIO_PLAY(AU_SPECIAL_SOUND_CHEEP);
    if (onDone) onDone();
    deleteLater();
  }
};
#endif

class ModuleWindow : public Window
{
 public:
  ModuleWindow(Window* parent, uint8_t moduleIdx) :
      Window(parent, rect_t{}), moduleIdx(moduleIdx)
  {
    setFlexLayout();
    updateModule();
    lv_obj_add_event_cb(lvobj, ModuleWindow::mw_refresh_cb, LV_EVENT_REFRESH, this);
  }

  void updateModule()
  {
    FlexGridLayout grid(col_dsc, row_dsc, PAD_TINY);
    clear();

    modOpts = nullptr;
    chRange = nullptr;
    rxID = nullptr;
    idUnique = nullptr;
    bindButton = nullptr;
    rangeButton = nullptr;
    registerButton = nullptr;
    fsLine = nullptr;
    fsChoice = nullptr;
    rfPower = nullptr;

    // Module parameters
    ModuleData* md = &g_model.moduleData[moduleIdx];

    if (md->type == MODULE_TYPE_NONE) {
      return;
    }
  #if defined(CROSSFIRE)
    else if (isModuleCrossfire(moduleIdx)) {
      modOpts = new CrossfireSettings(this, grid, moduleIdx);
    }
  #endif
  #if defined(AFHDS2)
    else if (isModuleAFHDS2A(moduleIdx)) {
      modOpts = new AFHDS2ASettings(this, grid, moduleIdx);
    }
  #endif
  #if defined(AFHDS3)
    else if (isModuleAFHDS3(moduleIdx)) {
      modOpts = new AFHDS3Settings(this, grid, moduleIdx);
    }
  #endif
  #if defined(MULTIMODULE)
    else if (isModuleMultimodule(moduleIdx)) {
      modOpts = new MultimoduleSettings(this, grid, moduleIdx);
    }
  #endif
  #if defined(DSMP)
    else if (isModuleDSMP(moduleIdx)) {
      modOpts = new DSMPSettings(this, grid, moduleIdx);
    }
  #endif

  #if defined(EXTERNAL_ANTENNA)
    if (moduleIdx == INTERNAL_MODULE &&
        g_eeGeneral.antennaMode == ANTENNA_MODE_PER_MODEL) {
      bool antennaModuleOk = isModuleXJT(moduleIdx);
    #if defined(INTMODULE_ANTSEL_GPIO)
      antennaModuleOk = true;
    #endif
      if (antennaModuleOk) {
        new ExtAntennaSettings(this, grid, moduleIdx);
      }
    }
  #endif

    // Channel Range
    auto line = newLine(grid);
    new StaticText(line, rect_t{}, STR_CHANNELRANGE);
    chRange = new ModuleChannelRange(line, moduleIdx);

    // Failsafe
    fsLine = newLine(grid);
    new StaticText(fsLine, rect_t{}, STR_FAILSAFE);
    fsChoice = new FailsafeChoice(fsLine, moduleIdx);

    // PPM modules
    if (isModulePPM(moduleIdx)) {
      // PPM frame
      auto line = newLine(grid);
      new StaticText(line, rect_t{}, STR_PPMFRAME);
      auto obj = new PpmFrameSettings<PpmModule>(line, &md->ppm);

      // copy pointer to frame len edit object to channel range
      chRange->setPpmFrameLenEditObject(obj->getPpmFrameLenEditObject());
    }

    // Generic module parameters

    // Bind and Range buttons
    if (!isModuleRFAccess(moduleIdx) && (isModuleModelIndexAvailable(moduleIdx) ||
                                        isModuleBindRangeAvailable(moduleIdx))) {
      // Is Reciever ID Unique
      if (isModuleModelIndexAvailable(moduleIdx)) {
        auto line = newLine(grid);
        new StaticText(line, rect_t{}, "");
        idUnique = new StaticText(line, rect_t{}, "");
        etx_txt_color(idUnique->getLvObj(), COLOR_THEME_WARNING_INDEX,
                      ETX_STATE_UNIQUE_ID_WARN);
        updateIDStaticText(moduleIdx);
      }

      auto line = newLine(grid);
      new StaticText(line, rect_t{}, STR_RECEIVER);

      auto box = new Window(line, rect_t{});
      box->padAll(PAD_TINY);
      box->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_MEDIUM, LV_SIZE_CONTENT);

      // Model index
      auto modelId = &g_model.header.modelId[moduleIdx];
      rxID = new NumberEdit(box, {0, 0, EdgeTxStyles::EDIT_FLD_WIDTH_NARROW, 0}, 0, getMaxRxNum(moduleIdx),
                            GET_DEFAULT(*modelId), [=](int32_t newValue) {
                              if (newValue != *modelId) {
                                *modelId = newValue;
                                modelslist.updateCurrentModelCell();
                                updateIDStaticText(moduleIdx);
  #if defined(CROSSFIRE)
                                if (isModuleCrossfire(moduleIdx)) {
                                  moduleState[moduleIdx].counter =
                                      CRSF_FRAME_MODELID;
                                }
  #endif
                                SET_DIRTY();
                              }
                            });

      if (isModuleBindRangeAvailable(moduleIdx) || isModuleCrossfire(moduleIdx)) {
#if defined(RADIO_NB4_FAMILY)

        bindButton = new TextButton(box, rect_t{}, nb4Text("Enlazar", "Bind"));
#else
        bindButton = new TextButton(box, rect_t{}, STR_MODULE_BIND);
#endif
        bindButton->setPressHandler([=]() -> uint8_t {
          if (moduleState[moduleIdx].mode == MODULE_MODE_RANGECHECK) {
            if (rangeButton) rangeButton->check(false);
          }
          if (moduleState[moduleIdx].mode == MODULE_MODE_BIND) {
            moduleState[moduleIdx].mode = MODULE_MODE_NORMAL;
  #if defined(MULTIMODULE)
            if (isModuleMultimodule(moduleIdx)) {
              setMultiBindStatus(moduleIdx, MULTI_BIND_NONE);
            }
  #endif
  #if defined(AFHDS2)
            if (isModuleAFHDS2A(moduleIdx)) resetPulsesAFHDS2();
  #endif
            if (isModuleDSMP(moduleIdx)) restartModule(moduleIdx);
            return 0;
          } else {
            if (isModuleR9MNonAccess(moduleIdx) || isModuleD16(moduleIdx) ||
                IS_R9_MULTI(moduleIdx)) {
              new BindChoiceMenu(
                  moduleIdx, [=]() { bindButton->check(true); },
                  [=]() { bindButton->check(false); });
              return 0;
            }
  #if defined(MULTIMODULE)
            if (isModuleMultimodule(moduleIdx)) {
              setMultiBindStatus(moduleIdx, MULTI_BIND_INITIATED);
            }
  #endif
#if defined(RADIO_NB4_FAMILY) && defined(AFHDS3)

            if (isModuleAFHDS3(moduleIdx)) {
#if defined(RADIO_NB4)
              if (nb4::Nb4RfController::getFault() != nb4::Nb4RfFault::None)
                restartModuleAsync(moduleIdx, 3);
#endif
              moduleState[moduleIdx].mode = MODULE_MODE_BIND;
              new Nb4BindDialog(moduleIdx, [=]() { bindButton->check(false); });
              return 1;
            }
#endif
            moduleState[moduleIdx].mode = MODULE_MODE_BIND;
            if (isModuleELRS(moduleIdx))
              AUDIO_PLAY(AU_SPECIAL_SOUND_CHEEP); // Since ELRS bind is just one frame, we need to play the sound manually
  #if defined(AFHDS2)
            if (isModuleAFHDS2A(moduleIdx)) {
              resetPulsesAFHDS2();
            }
  #endif
            return 1;
          }
          return 0;
        });
        bindButton->setCheckHandler([=]() {
          if (moduleState[moduleIdx].mode != MODULE_MODE_BIND) {
            if (bindButton->checked()) {
              bindButton->check(false);
            }
          }
  #if defined(MULTIMODULE)
          if (isModuleMultimodule(moduleIdx) &&
              getMultiBindStatus(moduleIdx) == MULTI_BIND_FINISHED) {
            setMultiBindStatus(moduleIdx, MULTI_BIND_NONE);
            moduleState[moduleIdx].mode = MODULE_MODE_NORMAL;
            bindButton->check(false);
          }
  #endif
        });

        if (isModuleRangeAvailable(moduleIdx)) {
#if defined(RADIO_NB4_FAMILY)
          rangeButton = new TextButton(box, rect_t{}, nb4Text("Alcance", "Range"));
#else
          rangeButton = new TextButton(box, rect_t{}, STR_MODULE_RANGE);
#endif
          rangeButton->setPressHandler([=]() -> uint8_t {
            if (moduleState[moduleIdx].mode == MODULE_MODE_BIND) {
              bindButton->check(false);
              moduleState[moduleIdx].mode = MODULE_MODE_NORMAL;
            }
            if (moduleState[moduleIdx].mode == MODULE_MODE_RANGECHECK) {
              moduleState[moduleIdx].mode = MODULE_MODE_NORMAL;
              return 0;
            } else {
              moduleState[moduleIdx].mode = MODULE_MODE_RANGECHECK;
  #if defined(AFHDS2)
              if (isModuleAFHDS2A(moduleIdx)) {
                resetPulsesAFHDS2();
              }
  #endif
              startRSSIDialog([=]() {
  #if defined(AFHDS2)
                if (isModuleAFHDS2A(moduleIdx)) {
                  resetPulsesAFHDS2();
                }
  #endif
              });
              return 1;
            }
          });
        }

  #if defined(PXX2)
        if (isModuleISRM(moduleIdx)) {
          auto options = new TextButton(box, rect_t{}, LV_SYMBOL_SETTINGS);
          options->setPressHandler([=]() {
            new pxx2::ModuleOptions(moduleIdx);
            return 0;
          });
        }
  #endif
      }
    }
  #if defined(PXX2)
    else if (isModuleRFAccess(moduleIdx)) {

      // Register and Range buttons
      auto line = newLine(grid);
      new StaticText(line, rect_t{}, STR_MODULE);

      auto box = new Window(line, rect_t{});
      box->padAll(PAD_TINY);
      box->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_LARGE);

      registerButton = new TextButton(box, rect_t{}, STR_REGISTER);
      registerButton->setPressHandler([=]() -> uint8_t {
        new pxx2::RegisterDialog(moduleIdx);
        return 0;
      });

      rangeButton = new TextButton(box, rect_t{}, STR_MODULE_RANGE);
      rangeButton->setPressHandler([=]() -> uint8_t {
        if (moduleState[moduleIdx].mode == MODULE_MODE_RANGECHECK) {
          moduleState[moduleIdx].mode = MODULE_MODE_NORMAL;
          return 0;
        } else {
          moduleState[moduleIdx].mode = MODULE_MODE_RANGECHECK;
          startRSSIDialog();
          return 1;
        }
      });

      auto options = new TextButton(box, rect_t{}, LV_SYMBOL_SETTINGS);
      options->setPressHandler([=]() {
        new pxx2::ModuleOptions(moduleIdx);
        return 0;
      });

      // Model index
      line = newLine(grid);
      new StaticText(line, rect_t{}, STR_RECEIVER_NUM);
      auto modelId = &g_model.header.modelId[moduleIdx];
      new NumberEdit(line, rect_t{}, 0, getMaxRxNum(moduleIdx),
                    GET_SET_DEFAULT(*modelId));
    }
  #endif

    // R9M Power
    if (isModuleR9MNonAccess(moduleIdx)) {
      auto line = newLine(grid);
      new StaticText(line, rect_t{}, STR_RF_POWER);
      rfPower = new Choice(line, rect_t{}, 0, 0, GET_SET_DEFAULT(md->pxx.power));
      line = newLine(grid);
      new StaticText(line, rect_t{}, STR_MODULE_TELEMETRY);
      new DynamicText(line, rect_t{}, [=]() {
        if (modulePortHasRx(moduleIdx)) {
          return std::string(STR_MODULE_TELEM_ON);
        } else {
          return std::string(STR_DISABLE_INTERNAL);
        }
      });
    }

  #if defined(PXX2)
    // Receivers
    if (isModuleRFAccess(moduleIdx)) {
      for (uint8_t receiverIdx = 0; receiverIdx < PXX2_MAX_RECEIVERS_PER_MODULE;
          receiverIdx++) {
        char label[40];
        char* s = strAppend(label, STR_RECEIVER);
        strAppendUnsigned(s, receiverIdx + 1);

        auto line = newLine(grid);
        new StaticText(line, rect_t{}, label);
        new pxx2::ReceiverButton(line, rect_t{}, moduleIdx, receiverIdx);
      }
    }
  #endif
    // SBUS refresh rate
    if (isModuleSBUS(moduleIdx)) {
      auto line = newLine(grid);
      new StaticText(line, rect_t{}, STR_REFRESHRATE);

      auto box = new Window(line, rect_t{});
      box->padAll(PAD_TINY);
      box->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_SMALL);

      auto edit = new NumberEdit(
          box, rect_t{}, SBUS_MIN_PERIOD, SBUS_MAX_PERIOD,
          GET_DEFAULT((int16_t)md->sbus.refreshRate * SBUS_STEPSIZE +
                      SBUS_DEF_PERIOD),
          SET_VALUE(md->sbus.refreshRate,
                    (newValue - SBUS_DEF_PERIOD) / SBUS_STEPSIZE),
          PREC1);
      edit->setSuffix(STR_MS);
      edit->setStep(SBUS_STEPSIZE);
      new Choice(box, rect_t{}, STR_SBUS_INVERSION_VALUES, 0, 1,
                GET_SET_DEFAULT(md->sbus.noninverted));
  #if defined(RADIO_TX16S)
      new StaticText(this, rect_t{}, STR_WARN_5VOLTS);
  #endif
    }

    if (isModuleGhost(moduleIdx)) {
      auto line = newLine(grid);
      new StaticText(line, rect_t{}, "Raw 12 bits");
      new ToggleSwitch(line, rect_t{}, GET_SET_DEFAULT(md->ghost.raw12bits));
    }

    updateSubType();
  }

  void updateSubType()
  {
    if (modOpts) modOpts->update();
    if (chRange) chRange->update();

    updateRxID();
    updateFailsafe();

    if (rfPower) {
      if (isModuleR9M_LBT(moduleIdx)) {
        rfPower->setMax(R9M_LBT_POWER_MAX);
        rfPower->setValues(STR_R9M_LBT_POWER_VALUES);
      } else {
        rfPower->setMax(R9M_FCC_POWER_MAX);
        rfPower->setValues(STR_R9M_FCC_POWER_VALUES);
      }
      rfPower->update();
    }
  }

  void updateRxID()
  {
    if (rxID) {
      if (isModuleModelIndexAvailable(moduleIdx)) {
        rxID->show();
        rxID->update();
      } else {
        rxID->hide();
      }
    }
  }

  void updateFailsafe()
  {
    if (fsLine) {
      if (isModuleFailsafeAvailable(moduleIdx)) {
        fsLine->show();
        fsChoice->update();
      } else {
        fsLine->hide();
      }
    }
  }

  void updateLayout()
  {
    if (isModuleISRM(moduleIdx))
      updateModule();
    else
      updateSubType();

    pulsesModuleSettingsUpdate(moduleIdx);
  }

  uint8_t getModuleIdx() const { return moduleIdx; }

 protected:
  uint8_t moduleIdx;

  ModuleOptions* modOpts = nullptr;
  ChannelRange* chRange = nullptr;
  NumberEdit* rxID = nullptr;
  TextButton* bindButton = nullptr;
  TextButton* rangeButton = nullptr;
  TextButton* registerButton = nullptr;
  Window* fsLine = nullptr;
  FailsafeChoice* fsChoice = nullptr;
  Choice* rfPower = nullptr;
  StaticText* idUnique = nullptr;

  void startRSSIDialog(std::function<void()> closeHandler = nullptr)
  {
    auto rssiDialog = new DynamicMessageDialog(
        STR_RANGE_TEST,
        [=]() {
          return std::to_string((int)TELEMETRY_RSSI()) + getRxStatLabels()->unit;
        },
        getRxStatLabels()->label, 50,
        COLOR_THEME_SECONDARY1_INDEX, CENTERED | FONT(XL));

    rssiDialog->setCloseHandler([this, closeHandler]() {
      rangeButton->check(false);
      moduleState[moduleIdx].mode = MODULE_MODE_NORMAL;
      if (closeHandler) closeHandler();
    });
  }

  void updateIDStaticText(int mdIdx)
  {
    if (idUnique == nullptr) return;
    char buffer[50];
    std::string idStr = STR_MODELIDUNIQUE;
    if (!modelslist.isModelIdUnique(mdIdx, buffer, sizeof(buffer))) {
      idStr = STR_MODELIDUSED;
      idStr = idStr + buffer;
      lv_obj_add_state(idUnique->getLvObj(), ETX_STATE_UNIQUE_ID_WARN);
    } else {
      lv_obj_clear_state(idUnique->getLvObj(), ETX_STATE_UNIQUE_ID_WARN);
    }
    idUnique->setText(idStr);
  }

  void checkEvents() override
  {
    if (bindButton != nullptr) {
      if (TELEMETRY_STREAMING() && isModuleELRS(moduleIdx))
        bindButton->setText(STR_MODULE_UNBIND);
      else if (isModuleELRS(moduleIdx))
        bindButton->setText(STR_MODULE_BIND);

      bindButton->show(isModuleBindRangeAvailable(moduleIdx));
    }
    Window::checkEvents();
  }

  static void mw_refresh_cb(lv_event_t* e)
  {
    auto mw = (ModuleWindow*)lv_event_get_user_data(e);
    if (mw) {
      mw->updateRxID();
      mw->updateFailsafe();
    }
  }
};

class ModuleSubTypeChoice : public Choice
{
 public:
  ModuleSubTypeChoice(Window* parent, uint8_t moduleIdx) :
      Choice(parent, rect_t{}, 0, 0,
            [=]() { return getSubTypeValue(); },
            [=](int32_t newValue) { setSubTypeValue(newValue); }),
      moduleIdx(moduleIdx)
  {
  }

  int getSubTypeValue()
  {
    if (isModuleXJT(moduleIdx) || isModuleDSM2(moduleIdx) ||
        isModuleR9MNonAccess(moduleIdx) || isModuleSBUS(moduleIdx)
#if defined(PPM)
        || isModulePPM(moduleIdx)
#endif
#if defined(PXX2)
        || isModuleISRM(moduleIdx)
#endif
    ) {
      return g_model.moduleData[moduleIdx].subType;
    } else {
      return g_model.moduleData[moduleIdx].multi.rfProtocol;
    }
  }

  void setSubTypeValue(int32_t newValue)
  {
    if (isModuleXJT(moduleIdx) || isModuleDSM2(moduleIdx) ||
        isModuleR9MNonAccess(moduleIdx) || isModuleSBUS(moduleIdx)
#if defined(PPM)
        || isModulePPM(moduleIdx)
#endif
#if defined(PXX2)
        || isModuleISRM(moduleIdx)
#endif
    ) {
      if (isModuleXJT(moduleIdx)) {
        g_model.moduleData[moduleIdx].channelsStart = 0;
        g_model.moduleData[moduleIdx].channelsCount = defaultModuleChannels_M8(moduleIdx);
      }
      g_model.moduleData[moduleIdx].subType = newValue;
      SET_DIRTY();
    } else {
#if defined(MULTIMODULE)
      g_model.moduleData[moduleIdx].multi.rfProtocol = newValue;
      g_model.moduleData[moduleIdx].subType = 0;
      resetMultiProtocolsOptions(moduleIdx);

      MultiModuleStatus& status = getMultiModuleStatus(moduleIdx);
      status.invalidate();

      uint32_t startUpdate = time_get_ms();
      while (!status.isValid() && (time_get_ms() - startUpdate < 250))
        sleep_ms(1);

      SET_DIRTY();
#endif
    }

    if (moduleWindow)
      moduleWindow->updateLayout();
  }

  void updateLayout()
  {
    if (isModuleXJT(moduleIdx)) {
      setMin(MODULE_SUBTYPE_PXX1_ACCST_D16);
      setMax(MODULE_SUBTYPE_PXX1_LAST);
      setValues(STR_XJT_ACCST_RF_PROTOCOLS);
      setTextHandler(nullptr);
    } else if (isModuleDSM2(moduleIdx)) {
      setMin(DSM2_PROTO_LP45);
      setMax(DSM2_PROTO_DSMX);
      setValues(STR_DSM_PROTOCOLS);
      setTextHandler(nullptr);
    }
    else if (isModuleSBUS(moduleIdx)) {
      setMin(SBUS_PROTO_TLM_NONE);
      setMax(SBUS_PROTO_TLM_SPORT);
      setValues(STR_SBUS_PROTOCOLS);
      setTextHandler(nullptr);
    }
#if defined(PPM)
    else if (isModulePPM(moduleIdx)) {
      setMin(PPM_PROTO_TLM_NONE);
      setMax(PPM_PROTO_TLM_SPORT);
      setValues(STR_PPM_PROTOCOLS);
      setTextHandler(nullptr);
    }
#endif
    else if (isModuleR9MNonAccess(moduleIdx)) {
      setMin(MODULE_SUBTYPE_R9M_FCC);
      setMax(MODULE_SUBTYPE_R9M_LAST);
      setValues(STR_R9M_REGION);
      setTextHandler(nullptr);
    }
#if defined(PXX2)
    else if (isModuleISRM(moduleIdx)) {
      setMin(MODULE_SUBTYPE_ISRM_PXX2_ACCESS);
      setMax(MODULE_SUBTYPE_ISRM_PXX2_ACCST_D16);
      setValues(STR_ISRM_RF_PROTOCOLS);
      setTextHandler(nullptr);
    }
#endif
#if defined(MULTIMODULE)
    else if (isModuleMultimodule(moduleIdx)) {
      setMin(0);
      setMax(0);
      values.clear();

      auto protos = MultiRfProtocols::instance(moduleIdx);
      protos->triggerScan();

      if (protos->isScanning()) {
        new RfScanDialog(protos, [=]() { updateLayout(); });
      } else {
        TRACE("!protos->isScanning()");
      }

      setTextHandler([=](int value) { return protos->getProtoLabel(value); });
    }
#endif
    else {
      hide();
      return;
    }

    update();
    show();
  }

  void openMenu() override
  {
#if defined(MULTIMODULE)
    if (isModuleMultimodule(moduleIdx)) {
      auto menu = new Menu();

      if (menuTitle) menu->setTitle(menuTitle);
      menu->setCloseHandler([=]() { setEditMode(false); });

      setEditMode(true);

      auto protos = MultiRfProtocols::instance(moduleIdx);
      protos->fillList([=](const MultiRfProtocols::RfProto& p) {
        addValue(p.label.c_str());
        menu->addLine(p.label.c_str(), [=]() {
          setValue(p.proto);
        });
      });

      ModuleData* md = &g_model.moduleData[moduleIdx];
      int idx = protos->getIndex(md->multi.rfProtocol);
      if (idx >= 0) menu->select(idx);
    } else
#endif
    {
      Choice::openMenu();
    }
  }

  void setModuleWindow(ModuleWindow* w) { moduleWindow = w; }

 protected:
  uint8_t moduleIdx;
  ModuleWindow* moduleWindow = nullptr;
};

#if defined(RADIO_NB4_FAMILY)

static const Nb4HelpEntry _rf_help[] = {
    {"Modo", "Mode",
     "El idioma con el que la radio habla con el receptor. El módulo interno de "
     "la NB4 es AFHDS3 de FlySky: es el que entienden los receptores que vienen "
     "con la emisora. Puesto en \"Apagado\" la radio deja de emitir y el coche "
     "no responde.",
     "The protocol the radio uses to talk to the receiver. The NB4 internal "
     "module is FlySky AFHDS3, which is what the bundled receivers speak. Set to "
     "Off, the radio stops transmitting and the car will not respond."},

    {"Estado módulo", "Module status",
     "Lo que está pasando ahora mismo con el enlace. \"Conectado\": el receptor "
     "responde. \"Desconectado\": no hay receptor encendido, o no está enlazado. "
     "\"Vinculando\": la radio espera a que el receptor se empareje.",
     "What the link is doing right now. \"Connected\": the receiver answers. "
     "\"Disconnected\": no receiver powered, or not bound. \"Binding\": the "
     "radio is waiting for the receiver to pair."},

    {"Tipo", "Type",
     "Selecciona la familia que admite tu receptor y vuelve a enlazar si la "
     "cambias. Classic: FGr4, FGr4S, FGr4P, FTr4, FTr10 y FTr16S. Enhanced: "
     "FGr4B, FGr8B, FGr12B, FTr8B, FTr12B, GMr y TMr. Los canales de salida "
     "se eligen en Canales. Se conserva la configuración regional del módulo.",
     "Choose the family supported by your receiver and bind again after "
     "changing it. Classic: FGr4, FGr4S, FGr4P, FTr4, FTr10 and FTr16S. "
     "Enhanced: FGr4B, FGr8B, FGr12B, FTr8B, FTr12B, GMr and TMr. Set the "
     "output count in Channels. The module's regional configuration is preserved."},

    {"Opciones módulo", "Module options",
     "Cómo salen las señales por los pines del receptor: PWM (un servo por pin), "
     "PPM, bus serie (SBUS, i-BUS) y la frecuencia del servo. \"Servo 50HZ\" es "
     "para servos analógicos y \"Servo333HZ\" para digitales; poner 333 Hz a un "
     "servo analógico lo puede quemar. Si no sabes cuál llevas, déjalo en 50HZ.",
     "How the receiver drives its pins: PWM (one servo per pin), PPM, serial bus "
     "(SBUS, i-BUS), and the servo frame rate. 50 Hz is for analogue servos and "
     "333 Hz for digital ones; feeding 333 Hz to an analogue servo can burn it. "
     "If you are not sure which you have, leave it at 50 Hz."},

    {"Sensores", "Sensors",
     "Los sensores de telemetría que manda el receptor: tensión de la batería "
     "del coche, temperatura, RPM. Lo que aparezca aquí es lo que puedes poner "
     "en la pantalla principal y usar en las alarmas.",
     "The telemetry the receiver sends back: pack voltage, temperature, RPM. "
     "Whatever shows up here is what you can put on the home screen and use for "
     "alarms."},

    {"Canales", "Channels",
     "Qué canales de la radio se envían al receptor. En un coche lo normal es "
     "dejarlo tal cual: dirección y gas son los dos primeros, y los siguientes "
     "sólo se usan si el coche lleva algo más (marchas, luces, bloqueos).",
     "Which of the radio's channels are sent to the receiver. On a car you "
     "normally leave this alone: steering and throttle are the first two, and "
     "the rest only matter if the car has extras (gears, lights, lockers)."},

    {"Failsafe", "Failsafe",
     "Qué hace el coche si se pierde la señal. \"Mantener\" deja los servos "
     "donde estaban: si iba acelerando, sigue acelerando. \"Personalizado\" te "
     "deja fijar la posición de cada canal, y es la única opción segura en un "
     "coche: gas a cero, o incluso algo de freno. \"No pulsos\" corta la señal a "
     "los servos. \"Receptor\" usa lo que el receptor tenga guardado.",
     "What the car does when the signal is lost. \"Hold\" leaves the servos "
     "where they were: if it was accelerating, it keeps accelerating. \"Custom\" "
     "lets you set every channel, and is the only safe choice on a car: throttle "
     "at zero, or even a little brake. \"No pulses\" stops driving the servos. "
     "\"Receiver\" uses whatever the receiver has stored."},

#if defined(RADIO_NB4)
    {"Receptor", "Receiver",
     "Los datos del receptor se detectan durante el enlace y se guardan con este "
     "modelo. No necesitas introducir un ID.",
     "Receiver data is detected during binding and saved with this model. "
     "You do not need to enter an ID."},

    {"Enlazar", "Bind",
     "Empareja radio y receptor. El diálogo muestra preparación, búsqueda y "
     "confirmación. En dos vías se cierra al confirmar la conexión; en una vía "
     "pulsa Finalizar cuando el LED parpadee despacio. Vuelve a enlazar si cambias "
     "Classic/Enhanced o una/dos vías.",
     "Pairs radio and receiver. The dialog shows preparation, search and "
     "confirmation. Two-way binding closes after connection is confirmed; "
     "for one way, press Finish when the LED flashes slowly. Bind again after "
     "changing Classic/Enhanced or one/two way."},
#else
    {"Receptor", "Receiver",
     "El número identifica a ESTE coche dentro de la radio. Si dos modelos "
     "comparten número la radio avisa, porque el receptor podría responder al "
     "modelo equivocado.",
     "The number identifies THIS model inside the radio. If two models share a "
     "number the radio warns you, because the receiver could answer to the wrong "
     "one."},

    {"Enlazar", "Bind",
     "Empareja radio y receptor. Se abre un aviso con el estado del enlace: "
     "enciende el receptor manteniendo pulsado su botón de enlace y espera a que "
     "diga \"Conectado\". Hay que repetirlo si cambias el Tipo o la región.",
     "Pairs radio and receiver. A dialog opens showing the link state: power the "
     "receiver while holding its bind button and wait for \"Connected\". You "
     "have to repeat it if you change Type or region."},
#endif

    {"Alcance", "Range",
     "Baja la potencia a propósito para ver a qué distancia se pierde el enlace. "
     "Se hace antes de rodar, con el coche en el suelo y el motor desconectado.",
     "Deliberately drops the power so you can see at what distance the link "
     "breaks. Do it before running, car on the ground and motor disconnected."},
};
#endif

ModulePage::ModulePage(uint8_t moduleIdx) : Page(ICON_MODEL_SETUP)
{
  const char* title2 =
      moduleIdx == INTERNAL_MODULE ? STR_INTERNALRF : STR_EXTERNALRF;
  header->setTitle(STR_MAIN_MENU_MODEL_SETTINGS);
  header->setTitle2(title2);

  body->setFlexLayout();

  FlexGridLayout grid(col_dsc, row_dsc, PAD_TINY);

  // Module Type
  auto line = body->newLine(grid);
  new StaticText(line, rect_t{}, STR_MODE);

  auto box = new Window(line, rect_t{});
  box->padAll(PAD_TINY);
  box->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_SMALL, LV_SIZE_CONTENT);

  ModuleData* md = &g_model.moduleData[moduleIdx];
  auto moduleChoice =
      new Choice(box, rect_t{}, STR_MODULE_PROTOCOLS, MODULE_TYPE_NONE,
                 MODULE_TYPE_COUNT - 1, GET_DEFAULT(md->type));

  moduleChoice->setAvailableHandler([=](int8_t moduleType) {
    if (moduleType == MODULE_TYPE_NONE) return true;
    return moduleIdx == INTERNAL_MODULE ? isInternalModuleAvailable(moduleType)
                                        : isExternalModuleAvailable(moduleType);
  });

  auto subTypeChoice = new ModuleSubTypeChoice(box, moduleIdx);
  auto moduleWindow = new ModuleWindow(body, moduleIdx);

  subTypeChoice->setModuleWindow(moduleWindow);

  // This needs to be after moduleWindow has been created
  moduleChoice->setSetValueHandler([=](int32_t newValue) {
    setModuleType(moduleIdx, newValue);

    moduleWindow->updateModule();
    subTypeChoice->updateLayout();

    SET_DIRTY();
  });

#if defined(RADIO_NB4_FAMILY)

  nb4AddHelp(body, "RF y receptor", "RF and receiver", _rf_help,
             sizeof(_rf_help) / sizeof(_rf_help[0]) -
                 (isModuleRangeAvailable(moduleIdx) ? 0 : 1));
#endif

  // Call this last in case it opens the 'Scanning' popup.
  subTypeChoice->updateLayout();
}
