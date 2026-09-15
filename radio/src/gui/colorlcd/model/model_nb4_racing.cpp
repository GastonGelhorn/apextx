/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "model_nb4_racing.h"
#include "nb4_params.h"

#include "edgetx.h"
#include "nb4_racing.h"
#include "nb4_car_state.h"
#include "dialog.h"
#include "button.h"
#include "libui/static.h"
#include "switchchoice.h"
#include "toggleswitch.h"
#include "nb4_routes.h"

#if defined(RADIO_NB4_FAMILY)

#define SET_DIRTY() storageDirty(EE_MODEL)

static const lv_coord_t col_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(2),
                                     LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

namespace {

void applyPreset(bool nitro)
{
  const auto previous = g_model.nb4Racing;
  if (nitro)
    nb4RacingPresetNitro(g_model.nb4Racing);
  else
    nb4RacingPresetElectric(g_model.nb4Racing);
  // Presets affect driving behavior, not the user's channels or race controls.
  g_model.nb4Racing.steeringChannel = previous.steeringChannel;
  g_model.nb4Racing.throttleChannel = previous.throttleChannel;
  g_model.nb4Racing.homeTimer = previous.homeTimer;
  g_model.nb4Racing.lapSw = previous.lapSw;
  g_model.nb4Racing.lapAnnounce = previous.lapAnnounce;
  g_model.nb4Racing.lapCount = previous.lapCount;
  g_model.nb4Racing.pitEnabled = previous.pitEnabled;
}

}  // namespace

void ModelNb4RacingPage::rebuild(Window* window)
{
  auto scroll_y = lv_obj_get_scroll_y(window->getLvObj());
  window->clear();
  build(window);
  lv_obj_scroll_to_y(window->getLvObj(), scroll_y, LV_ANIM_OFF);
}

static Window* nb4ReverseSlot(Window* form, FlexGridLayout& grid, uint8_t channel)
{
  auto line = form->newLine(grid);
  new StaticText(line, rect_t{}, nb4ParamLabel(Nb4Param::ChannelReverse));
  auto holder = new Window(line, rect_t{0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT});
  holder->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_TINY);
  nb4ParamControl(holder, rect_t{}, Nb4Param::ChannelReverse, {channel});
  return holder;
}

static void nb4RefillReverse(Window* holder, uint8_t channel)
{
  if (!holder) return;
  holder->clear();
  nb4ParamControl(holder, rect_t{}, Nb4Param::ChannelReverse, {channel});
}

void nb4BuildChannelAssignment(Window* form, FlexGridLayout& grid,
                               Nb4ChannelSlots* slots)
{
  auto line = form->newLine(grid);
  new Subtitle(line, STR_OUTPUTS);

  line = form->newLine(grid);
  new StaticText(line, rect_t{}, STR_LIMITS_HEADERS_DIRECTION);
  auto steeringChannel = new NumberEdit(
      line, rect_t{}, 1, MAX_OUTPUT_CHANNELS,
      GET_DEFAULT(g_model.nb4Racing.steeringChannel + 1),
      [slots](int32_t value) {
        g_model.nb4Racing.steeringChannel = value - 1;
        SET_DIRTY();

        if (slots) nb4RefillReverse(slots->steeringReverse, value - 1);
      });
  steeringChannel->setPrefix(STR_CH);
  steeringChannel->setAvailableHandler([](int value) {
    return value - 1 != g_model.nb4Racing.throttleChannel;
  });
  auto steeringSlot =
      nb4ReverseSlot(form, grid, g_model.nb4Racing.steeringChannel);

  line = form->newLine(grid);
  new StaticText(line, rect_t{}, STR_THROTTLE_LABEL);
  auto throttleChannel = new NumberEdit(
      line, rect_t{}, 1, MAX_OUTPUT_CHANNELS,
      GET_DEFAULT(g_model.nb4Racing.throttleChannel + 1),
      [slots](int32_t value) {
        g_model.nb4Racing.throttleChannel = value - 1;
        SET_DIRTY();
        if (slots) nb4RefillReverse(slots->throttleReverse, value - 1);
      });
  throttleChannel->setPrefix(STR_CH);
  throttleChannel->setAvailableHandler([](int value) {
    return value - 1 != g_model.nb4Racing.steeringChannel;
  });
  auto throttleSlot =
      nb4ReverseSlot(form, grid, g_model.nb4Racing.throttleChannel);

  if (slots) {
    slots->steeringReverse = steeringSlot;
    slots->throttleReverse = throttleSlot;
  }
}

void ModelNb4RacingPage::build(Window* window)
{
  if (nb4RacingMigrate(g_model.nb4Racing)) SET_DIRTY();

  window->setFlexLayout();
  window->padAll(PAD_SMALL);

  auto note = new StaticText(window, {0, 0, LV_PCT(100), LV_SIZE_CONTENT},
    STR_NB4_UX_HELP_PRESETS, COLOR_THEME_PRIMARY3_INDEX);
  lv_label_set_long_mode(note->getLvObj(), LV_LABEL_LONG_WRAP);

  for (bool nitro : {false, true}) {
    auto heading = new StaticText(window, {0, 0, LV_PCT(100), LV_SIZE_CONTENT},
      nitro ? STR_NB4_NITRO : STR_NB4_ELECTRIC, COLOR_THEME_PRIMARY1_INDEX, FONT(BOLD));
    heading->padTop(PAD_LARGE);
    auto description = new StaticText(window, {0, 0, LV_PCT(100), LV_SIZE_CONTENT},
      nitro ? STR_NB4_UX_PRESET_NITRO : STR_NB4_UX_PRESET_ELECTRIC,
      COLOR_THEME_PRIMARY3_INDEX);
    lv_label_set_long_mode(description->getLvObj(), LV_LABEL_LONG_WRAP);
    auto button = new TextButton(window, {0, 0, LV_PCT(100), 44},
      nitro ? STR_NB4_UX_APPLY_NITRO : STR_NB4_UX_APPLY_ELECTRIC, [=]() {
        new ConfirmDialog(STR_NB4_UX_VEHICLE_PRESETS, STR_NB4_UX_PRESET_CONFIRM, [=]() {
          applyPreset(nitro);
          SET_DIRTY();
          rebuild(window);
        });
        return 0;
      });
    button->setWrap();
  }
}

namespace {

class Nb4ChannelsDialog : public BaseDialog
{
 public:
  Nb4ChannelsDialog() :
      BaseDialog(STR_NB4_CHANNELS, true,
                 (lv_coord_t)(lv_disp_get_hor_res(nullptr) * 0.92))
  {

    if (nb4RacingMigrate(g_model.nb4Racing)) SET_DIRTY();

    form->padLeft(PAD_MEDIUM);
    form->padRight(PAD_MEDIUM);
    new StaticText(form, {0, 0, LV_PCT(100), 0},
                   STR_NB4_WHICH_RECEIVER_OUTPUT_EACH_CONTROL_DRIVE,
                   COLOR_THEME_PRIMARY3_INDEX);

    nb4BuildChannelAssignment(form, grid, &slots);
  }

 private:
  FlexGridLayout grid{col_dsc, row_dsc, PAD_TINY};
  Nb4ChannelSlots slots;
};

}  // namespace

void nb4OpenChannelsDialog() { new Nb4ChannelsDialog(); }

void nb4OpenRaceSetup()
{
  class RaceSetup : public BaseDialog {
   public:
    RaceSetup() : BaseDialog(STR_NB4_UX_RACE_SETUP, true,
      lv_disp_get_hor_res(nullptr) - 8, lv_disp_get_ver_res(nullptr) - 12) {
      useSectionHeader();
      setHelpHandler([] { nb4OpenHelp("settings/race/setup", true); });
      form->padAll(PAD_MEDIUM);
      auto note = new StaticText(form, {0, 0, LV_PCT(100), LV_SIZE_CONTENT},
        STR_NB4_UX_HELP_RACE_SETUP, COLOR_THEME_PRIMARY3_INDEX);
      lv_label_set_long_mode(note->getLvObj(), LV_LABEL_LONG_WRAP);
      FlexGridLayout grid(col_dsc, row_dsc, PAD_SMALL);
      auto line = form->newLine(grid);
      new StaticText(line, rect_t{}, STR_NB4_LAP_SW);
      new SwitchChoice(line, rect_t{}, SWSRC_FIRST, SWSRC_LAST,
                       GET_SET_DEFAULT(g_model.nb4Racing.lapSw));
      line = form->newLine(grid);
      new StaticText(line, rect_t{}, STR_NB4_ANNOUNCE);
      new ToggleSwitch(line, rect_t{}, GET_SET_DEFAULT(g_model.nb4Racing.lapAnnounce));
      line = form->newLine(grid);
      new StaticText(line, rect_t{}, STR_NB4_LAPS);
      new NumberEdit(line, rect_t{}, 0, NB4_MAX_LAPS,
        GET_SET_DEFAULT(g_model.nb4Racing.lapCount));
    }
  };
  new RaceSetup();
}

#endif  // RADIO_NB4_FAMILY
