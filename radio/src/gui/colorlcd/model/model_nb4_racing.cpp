/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "model_nb4_racing.h"
#include "nb4_params.h"

#include "edgetx.h"
#include "nb4_racing.h"
#include "nb4_car_state.h"
#include "nb4_params.h"
#include "dialog.h"
#include "button.h"
#include "dialog.h"
#include "libui/static.h"
#include "switchchoice.h"
#include "toggleswitch.h"

#if defined(RADIO_NB4_FAMILY)

#define SET_DIRTY() storageDirty(EE_MODEL)

static const lv_coord_t col_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(2),
                                     LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

namespace {

void applyPreset(bool nitro)
{

  uint8_t steeringChannel = g_model.nb4Racing.steeringChannel;
  uint8_t throttleChannel = g_model.nb4Racing.throttleChannel;
  uint8_t homeTimer = g_model.nb4Racing.homeTimer;
  if (nitro)
    nb4RacingPresetNitro(g_model.nb4Racing);
  else
    nb4RacingPresetElectric(g_model.nb4Racing);
  g_model.nb4Racing.steeringChannel = steeringChannel;
  g_model.nb4Racing.throttleChannel = throttleChannel;
  g_model.nb4Racing.homeTimer = homeTimer;
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

  FlexGridLayout grid(col_dsc, row_dsc, PAD_TINY);
  window->setFlexLayout();
  window->padAll(PAD_SMALL);

  /* This page only contains behavior that EdgeTX does not already provide,
   * with one deliberate exception: channel reverse is SHOWN here, next to the
   * channel it applies to, because this is where a driver looks for it. It is
   * the same LimitData field the native editor writes, not a second setting.
   * Everything else -model name, inputs, curves, mixes, travel, subtrim,
   * telemetry- stays in its native, tested editor and tab. */
  auto line = window->newLine(grid);
  new Subtitle(line, STR_NB4_PRESETS);

  nb4ParamRow(window, grid, Nb4Param::VehicleType);

  line = window->newLine(grid);
  new StaticText(line, rect_t{}, STR_NB4_START_POINT);
  auto presets = new Window(line, rect_t{0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT});
  presets->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_TINY);

  new TextButton(presets, rect_t{}, STR_NB4_ELECTRIC, [=]() {
    new ConfirmDialog(STR_NB4_ELECTRIC, STR_NB4_PRESET_ASK, [=]() {
      applyPreset(false);
      SET_DIRTY();
      rebuild(window);
    });
    return 0;
  });

  new TextButton(presets, rect_t{}, STR_NB4_NITRO, [=]() {
    new ConfirmDialog(STR_NB4_NITRO, STR_NB4_PRESET_ASK, [=]() {
      applyPreset(true);
      SET_DIRTY();
      rebuild(window);
    });
    return 0;
  });

  nb4BuildChannelAssignment(window, grid, &channels);

  line = window->newLine(grid);
  new Subtitle(line, STR_NB4_RACE);

  line = window->newLine(grid);
  new StaticText(line, rect_t{}, STR_NB4_LAP_SW);
  new SwitchChoice(line, rect_t{}, SWSRC_FIRST, SWSRC_LAST,
                   GET_SET_DEFAULT(g_model.nb4Racing.lapSw));

  line = window->newLine(grid);
  new StaticText(line, rect_t{}, STR_NB4_ANNOUNCE);
  new ToggleSwitch(line, rect_t{}, GET_SET_DEFAULT(g_model.nb4Racing.lapAnnounce));

  line = window->newLine(grid);
  new StaticText(line, rect_t{}, STR_NB4_LAPS);
  auto laps = new NumberEdit(line, rect_t{}, 0, 99,
                             GET_SET_DEFAULT(g_model.nb4Racing.lapCount));
  laps->setZeroText(STR_NONE);
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

#endif  // RADIO_NB4_FAMILY
