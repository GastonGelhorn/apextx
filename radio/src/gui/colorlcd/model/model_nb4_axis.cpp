/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "model_nb4_axis.h"

#if defined(RADIO_NB4_FAMILY)

#include "button.h"
#include "curve_param.h"
#include "controls/curve.h"
#include "controls/nb4_response_chart.h"
#include "mainview/nb4_ui.h"
#include "input_edit.h"
#include "edgetx.h"
#include "dialog.h"
#include "etx_lv_theme.h"
#include "libui/static.h"
#include "mixes.h"
#include "model_curves.h"
#include "nb4_axis.h"
#include "nb4_racing.h"
#include "nb4_params.h"
#include "nb4_car_state.h"
#include "numberedit.h"
#include "output_edit.h"
#include "quick_menu.h"
#include "gvar_numberedit.h"
#include "source_numberedit.h"
#include "sourcechoice.h"
#include "switchchoice.h"
#include "timers.h"
#include "gui_common.h"
#include "toggleswitch.h"

#define SET_DIRTY() storageDirty(EE_MODEL)

static const lv_coord_t col_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(2),
                                     LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

namespace {

void statusBanner(Window* form, FlexGridLayout& grid, const Nb4AxisView& view,
                  uint8_t channelForNativeEditor, bool sharedIsNormal = false)
{
  if (view.status == Nb4AxisStatus::Ready) return;
  if (sharedIsNormal && view.status == Nb4AxisStatus::Shared) return;

  const char* headline = nullptr;
  switch (view.status) {
    case Nb4AxisStatus::Shared:
      headline = STR_NB4_BOTH_SIDES_SHARE_ONE_SETTING;
      break;
    case Nb4AxisStatus::Dynamic:
      headline = STR_NB4_A_VALUE_COMES_FROM_ANOTHER_SOURCE;
      break;
    default:
      headline = STR_NB4_THIS_SETUP_CANNOT_BE_EDITED_HERE;
      break;
  }

  auto line = form->newLine(grid);
  auto title = new StaticText(line, rect_t{}, headline, COLOR_THEME_WARNING_INDEX,
                              FONT(BOLD));
  lv_obj_set_style_grid_cell_column_span(title->getLvObj(), 2, LV_PART_MAIN);

  if (view.reason) {
    line = form->newLine(grid);
    auto why = new StaticText(line, rect_t{}, view.reason, COLOR_THEME_PRIMARY3_INDEX);
    lv_label_set_long_mode(why->getLvObj(), LV_LABEL_LONG_WRAP);
    lv_obj_set_style_grid_cell_column_span(why->getLvObj(), 2, LV_PART_MAIN);
  }

  const Nb4Blocker blocker = view.blocker;
  const char* openLabel =
      blocker == Nb4Blocker::Input  ? STR_NB4_OPEN_INPUTS
      : blocker == Nb4Blocker::Mix  ? STR_NB4_OPEN_MIXES
                                    : STR_NB4_OPEN_OUTPUTS;

  line = form->newLine(grid);
  auto open = new TextButton(line, {0, 0, LV_PCT(100), 0}, openLabel,
                 [channelForNativeEditor, blocker]() {
                   if (blocker == Nb4Blocker::Input) {
                     QuickMenu::openPage(QM_MODEL_INPUTS);
                   } else if (blocker == Nb4Blocker::Mix) {
                     QuickMenu::openPage(QM_MODEL_MIXES);
                   } else if (channelForNativeEditor < MAX_OUTPUT_CHANNELS) {
                     new OutputEditWindow(channelForNativeEditor);
                   } else {
                     QuickMenu::openPage(QM_MODEL_OUTPUTS);
                   }
                   return 0;
                 });
  lv_obj_set_style_grid_cell_column_span(open->getLvObj(), 2, LV_PART_MAIN);
  open->setWrap();
}

void signGateNotice(Window* form, FlexGridLayout& grid, const char* reason)
{
  auto line = form->newLine(grid);
  auto why = new StaticText(line, rect_t{}, reason, COLOR_THEME_WARNING_INDEX);
  lv_label_set_long_mode(why->getLvObj(), LV_LABEL_LONG_WRAP);
  lv_obj_set_style_grid_cell_column_span(why->getLvObj(), 2, LV_PART_MAIN);

  line = form->newLine(grid);
  auto off = new TextButton(line, {0, 0, LV_PCT(100), 0},
                            STR_NB4_TURN_THESE_FUNCTIONS_OFF,
                            []() {
                              nb4RacingNeutraliseSignDependent();
                              SET_DIRTY();
                              return 0;
                            });
  lv_obj_set_style_grid_cell_column_span(off->getLvObj(), 2, LV_PART_MAIN);
  off->setWrap();
}

void responseRow(Window* form, FlexGridLayout& grid, const char* label,
                 int8_t line)
{

  nb4ParamRow(form, grid, Nb4Param::InputResponse, {Nb4ParamCtx::NONE, line}, label);
}

void sharedCurveRow(Window* form, FlexGridLayout& grid, const Nb4AxisView& view,
                    uint8_t curveNumber, mixsrc_t source,
                    std::function<void()> refreshView)
{
  const uint8_t others = nb4CurveOtherUsers(curveNumber, view);

  char msg[128];
  if (others > 0)
    snprintf(msg, sizeof(msg),
             STR_NB4_BOTH_SIDES_USE_CURVE_S_AND,
             getCurveString(curveNumber), (int)others, others == 1 ? "" : "s");
  else
    snprintf(msg, sizeof(msg),
             STR_NB4_BOTH_SIDES_USE_CURVE_S_MOVING,
             getCurveString(curveNumber));

  auto line = form->newLine(grid);
  auto why = new StaticText(line, rect_t{}, msg, COLOR_THEME_PRIMARY3_INDEX);
  lv_label_set_long_mode(why->getLvObj(), LV_LABEL_LONG_WRAP);
  lv_obj_set_style_grid_cell_column_span(why->getLvObj(), 2, LV_PART_MAIN);

  char label[64];
  snprintf(label, sizeof(label),
           STR_NB4_EDIT_THE_POINTS_OF_S,
           getCurveString(curveNumber));

  line = form->newLine(grid);
  auto edit = new TextButton(
      line, {0, 0, LV_PCT(100), 0}, label,
      [curveNumber, source, refreshView, others]() {
        char ask[160];
        if (others > 0)
          snprintf(ask, sizeof(ask),

                   STR_NB4_THIS_AFFECTS_THROTTLE_BRAKE_AND_D,
                   (int)others, others == 1 ? "" : "s");
        else

          snprintf(ask, sizeof(ask), "%s",
                   STR_NB4_THIS_AFFECTS_THROTTLE_AND_BRAKE_AT);
        new ConfirmDialog(getCurveString(curveNumber), ask,
                          [curveNumber, source, refreshView]() {
                            ModelCurvesPage::pushEditCurve(curveNumber - 1,
                                                           refreshView, source);
                          });
        return 0;
      });
  lv_obj_set_style_grid_cell_column_span(edit->getLvObj(), 2, LV_PART_MAIN);
  edit->setWrap();
}

void responseGraph(Window* form, FlexGridLayout& grid, const Nb4AxisView& view,
                   bool isThrottle)
{

  const Nb4ThrottleSides sides = nb4ResolveThrottleSides();
  const bool accelIsLeft = isThrottle && sides.known && sides.accelSign < 0;

  if (!nb4AxisMapIsDrawable(view)) {
    auto why = new StaticText(
        form->newLine(grid), rect_t{},
        STR_NB4_THIS_CHANNEL_S_MIX_CARRIES_AN,
        COLOR_THEME_PRIMARY3_INDEX);
    lv_label_set_long_mode(why->getLvObj(), LV_LABEL_LONG_WRAP);
    lv_obj_set_style_grid_cell_column_span(why->getLvObj(), 2, LV_PART_MAIN);
    return;
  }

  const int8_t line =
      view.lineForPositive >= 0 ? view.lineForPositive : view.lineForNegative;
  const int16_t srcRaw = expoAddress(line)->srcRaw;
  const Nb4AxisView snapshot = view;

  lv_obj_update_layout(form->getLvObj());
  const coord_t width = lv_obj_get_content_width(form->getLvObj());
  const coord_t innerWidth = width - 2;
  const coord_t chartH = Nb4ResponseChart::heightFor(innerWidth);
  const coord_t headerH = EdgeTxStyles::STD_FONT_HEIGHT + PAD_TINY;
  const char* leftZone = isThrottle ? STR_NB4_BRAKE_30D6 : STR_NB4_LEFT;
  const char* middleZone = isThrottle ? STR_NB4_NEUTRAL : STR_NB4_CENTRE;
  const char* rightZone = isThrottle ? STR_NB4_THROTTLE : STR_NB4_RIGHT;

  auto row = form->newLine(grid);
  auto card = new Window(row, rect_t{0, 0, width, (coord_t)(chartH + headerH + PAD_MEDIUM)});
  lv_obj_set_style_grid_cell_column_span(card->getLvObj(), 2, LV_PART_MAIN);
  lv_obj_set_style_radius(card->getLvObj(), PAD_MEDIUM, LV_PART_MAIN);
  etx_bg_color(card->getLvObj(), COLOR_THEME_PRIMARY2_INDEX, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card->getLvObj(), LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_border_width(card->getLvObj(), 1, LV_PART_MAIN);
  etx_border_color(card->getLvObj(), COLOR_THEME_PRIMARY2_INDEX, LV_PART_MAIN);
  lv_obj_set_style_border_opa(card->getLvObj(), LV_OPA_30, LV_PART_MAIN);
  lv_obj_clear_flag(card->getLvObj(), LV_OBJ_FLAG_SCROLLABLE);

  const coord_t readoutWidth = 96;
  const coord_t titleWidth = innerWidth - readoutWidth - PAD_MEDIUM * 2;
  new StaticText(card, rect_t{PAD_MEDIUM, PAD_TINY, titleWidth, headerH},
                 isThrottle ? STR_NB4_THROTTLE_AND_BRAKE_CURVE
                            : STR_NB4_STEERING_CURVE,
                 COLOR_THEME_PRIMARY1_INDEX, FONT(BOLD));

  auto readout = new StaticText(
      card,
      rect_t{(coord_t)(innerWidth - readoutWidth - PAD_MEDIUM), PAD_TINY,
             readoutWidth, headerH},
      "", COLOR_THEME_PRIMARY3_INDEX, FONT(XS) | RIGHT);

  auto chart = new Nb4ResponseChart(
      card, rect_t{0, headerH, innerWidth, chartH},
      [snapshot, isThrottle](int x) -> int {
        return nb4AxisDrawnOutput(snapshot, isThrottle, x);
      },

      [srcRaw, isThrottle, reversed = snapshot.inputReversalApplies]() -> int {
        const int v = getValue(srcRaw < 0 ? -srcRaw : srcRaw);
        return (int)nb4AxisVisual(isThrottle, reversed ? -v : v);
      },
      leftZone, middleZone, rightZone,

      isThrottle ? (accelIsLeft ? Nb4Ui::throttleColor() : Nb4Ui::brakeColor())
                 : Nb4Ui::steeringColor(),
      isThrottle ? (accelIsLeft ? Nb4Ui::brakeColor() : Nb4Ui::throttleColor())
                 : Nb4Ui::steeringColor());

  chart->setReadoutHandler([readout](int in, int out) {
    char text[48];

    snprintf(text, sizeof(text), "%d%%  %s  %d%%", in, LV_SYMBOL_RIGHT, out);
    readout->setText(text);
  });

}

void tabsRow(Window* form, FlexGridLayout& grid, const char* const* labels,
             unsigned count, uint8_t current, std::function<void(uint8_t)> onPick)
{
  auto line = form->newLine(grid);
  auto bar = new Window(line, rect_t{});
  lv_obj_set_style_grid_cell_column_span(bar->getLvObj(), 2, LV_PART_MAIN);
  bar->setFlexLayout(LV_FLEX_FLOW_ROW, 1, LV_PCT(100),
                    EdgeTxStyles::UI_ELEMENT_HEIGHT);
  lv_obj_clear_flag(bar->getLvObj(), LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(bar->getLvObj(), 0, LV_PART_MAIN);

  for (unsigned i = 0; i < count; i += 1) {
    auto button = new TextButton(bar, rect_t{0, 0, 0, EdgeTxStyles::UI_ELEMENT_HEIGHT}, labels[i],
                                 [onPick, i]() { onPick((uint8_t)i); return 0; });
    lv_obj_set_width(button->getLvObj(), 0);
    lv_obj_set_flex_grow(button->getLvObj(), 1);

    etx_font(button->getLvObj(), FONT_XS_INDEX, LV_PART_MAIN);
    lv_obj_set_style_pad_left(button->getLvObj(), PAD_SMALL, LV_PART_MAIN);
    lv_obj_set_style_pad_right(button->getLvObj(), PAD_SMALL, LV_PART_MAIN);
    if (i == current) button->check(true);
  }
}

void endpointPair(Window* form, FlexGridLayout& grid, const Nb4AxisView& view,
                  const char* labelA, Nb4Endpoint endpointA, const char* labelB,
                  Nb4Endpoint endpointB)
{
  char textA[40], textB[40];
  snprintf(textA, sizeof(textA), "%s %s", labelA, nb4ParamUnit());
  snprintf(textB, sizeof(textB), "%s %s", labelB, nb4ParamUnit());

  Nb4ParamCtx ctx;
  ctx.channel = view.outputChannel;
  ctx.magnitude = true;

  nb4ParamPair(form, grid,
               endpointA == Nb4Endpoint::Min ? Nb4Param::ChannelTravelMin
                                             : Nb4Param::ChannelTravelMax,
               ctx, textA,
               endpointB == Nb4Endpoint::Min ? Nb4Param::ChannelTravelMin
                                             : Nb4Param::ChannelTravelMax,
               ctx, textB);
}

void tabIsEmptyBecause(Window* form, FlexGridLayout& grid, const char* reason)
{
  auto line = form->newLine(grid);
  auto why = new StaticText(
      line, rect_t{},
      reason ? reason
             : STR_NB4_THIS_SETTING_CANNOT_BE_EDITED_WHILE,
      COLOR_THEME_PRIMARY3_INDEX);
  lv_label_set_long_mode(why->getLvObj(), LV_LABEL_LONG_WRAP);
  lv_obj_set_style_grid_cell_column_span(why->getLvObj(), 2, LV_PART_MAIN);
}

void subtitleRow(Window* form, FlexGridLayout& grid, const char* text)
{
  auto line = form->newLine(grid);
  new Subtitle(line, text);
}

}  // namespace

// ---------------------------------------------------------------------------
// TAB SELECTION HINT
// ---------------------------------------------------------------------------

static uint8_t pendingSteeringTab = 0;
static uint8_t pendingThrottleTab = 0;

void nb4SetSteeringTab(uint8_t tab) { pendingSteeringTab = tab < 4 ? tab : 0; }
void nb4SetThrottleTab(uint8_t tab) { pendingThrottleTab = tab < 4 ? tab : 0; }

uint8_t nb4TakeSteeringTab()
{
  const uint8_t t = pendingSteeringTab;
  pendingSteeringTab = 0;
  return t;
}

uint8_t nb4TakeThrottleTab()
{
  const uint8_t t = pendingThrottleTab;
  pendingThrottleTab = 0;
  return t;
}

// ---------------------------------------------------------------------------
// STEERING
// ---------------------------------------------------------------------------

void ModelNb4SteeringPage::rebuild(Window* window)
{
  window->clear();
  build(window);

  lv_obj_scroll_to_y(window->getLvObj(), 0, LV_ANIM_OFF);
}

void ModelNb4SteeringPage::checkEvents()
{
  if (pendingTab >= 0 && body) {
    tab = (uint8_t)pendingTab;
    pendingTab = -1;
    rebuild(body);
  }
  PageGroupItem::checkEvents();
}

void ModelNb4SteeringPage::build(Window* window)
{
  FlexGridLayout grid(col_dsc, row_dsc, PAD_ZERO);
  window->setFlexLayout();
  window->padAll(PAD_TINY);
  window->padLeft(PAD_ZERO);
  window->padRight(PAD_ZERO);

  const Nb4AxisView view = nb4ResolveAxis(Nb4AxisRole::Steering);
  const bool usable = view.status == Nb4AxisStatus::Ready ||
                      view.status == Nb4AxisStatus::Shared;

  statusBanner(window, grid, view, view.outputChannel, /*sharedIsNormal=*/true);

  const char* const tabs[] = {
      STR_NB4_TRAVEL, STR_NB4_CURVE,
      STR_NB4_CENTRE, STR_NB4_SPEED};
  if (tab >= 4) tab = 0;

  body = window;
  pendingTab = -1;
  tabsRow(window, grid, tabs, 4, tab,
          [this](uint8_t picked) { pendingTab = (int8_t)picked; });

  responseGraph(window, grid, view, /*isThrottle=*/false);

  if (tab == 3) {

    nb4ParamPair(window, grid, Nb4Param::SteerSpeedTurn, {}, STR_NB4_STEER_TURN,
                 Nb4Param::SteerSpeedReturn, {}, STR_NB4_STEER_RETURN);
    return;
  }

  if (!usable) {
    tabIsEmptyBecause(window, grid, view.reason);
    return;
  }

  if (tab == 0) {
    // Convert the displayed left/right direction to the physical wheel sign,
    // exactly as the home and response graph do. Left is positive on NB4.
    endpointPair(window, grid, view,
                 STR_NB4_LEFT, nb4EndpointForStickSide(view, nb4AxisVisual(false, -1)),
                 STR_NB4_RIGHT, nb4EndpointForStickSide(view, nb4AxisVisual(false, +1)));

    nb4ParamRow(window, grid, Nb4Param::ChannelReverse, {view.outputChannel},
                STR_NB4_REVERSE_CHANNEL);
    return;
  }

  if (tab == 2) {

    nb4ParamPair(window, grid, Nb4Param::ChannelSubtrim, {view.outputChannel},
                 STR_NB4_CENTRE, Nb4Param::SteeringTrim, {},
                 STR_NB4_TRIM);
    return;
  }

  if (view.status == Nb4AxisStatus::Shared) {
    if (view.lineForPositive >= 0)
      nb4ParamPair(window, grid,
                   Nb4Param::InputDualRate, {Nb4ParamCtx::NONE, view.lineForPositive},
                   STR_NB4_DUAL_RATE,
                   Nb4Param::InputResponse, {Nb4ParamCtx::NONE, view.lineForPositive},
                   STR_NB4_EXPONENTIAL);
    return;
  }

  const int8_t leftLine = nb4InputLineForStickSide(view, nb4AxisVisual(false, -1));
  const int8_t rightLine = nb4InputLineForStickSide(view, nb4AxisVisual(false, +1));
  if (leftLine >= 0 && rightLine >= 0) {
    nb4ParamPair(window, grid,
                 Nb4Param::InputDualRate, {Nb4ParamCtx::NONE, leftLine},
                 STR_NB4_LEFT_DUAL_RATE,
                 Nb4Param::InputDualRate, {Nb4ParamCtx::NONE, rightLine},
                 STR_NB4_RIGHT_DUAL_RATE);
    nb4ParamPair(window, grid,
                 Nb4Param::InputResponse, {Nb4ParamCtx::NONE, leftLine},
                 STR_NB4_LEFT_EXPO,
                 Nb4Param::InputResponse, {Nb4ParamCtx::NONE, rightLine},
                 STR_NB4_RIGHT_EXPO);
  }

  if (const uint8_t sharedCurve = nb4AxisSharedCurveResource(view))
    sharedCurveRow(window, grid, view, sharedCurve,
                   expoAddress(view.lineForPositive)->srcRaw, nullptr);
}

// ---------------------------------------------------------------------------
// THROTTLE AND BRAKE
// ---------------------------------------------------------------------------

namespace {

class Nb4ThrottleTraceDialog : public BaseDialog
{
 public:
  Nb4ThrottleTraceDialog() :
      BaseDialog(STR_NB4_THROTTLE_TRACKING, true)
  {
    new StaticText(form, {0, 0, LV_PCT(100), 0},
                   STR_NB4_WHICH_CHANNEL_THE_TIMER_FOLLOWS_TO,
                   COLOR_THEME_PRIMARY3_INDEX);
    auto sc = new SourceChoice(
        form, {0, 0, LV_PCT(100), 0}, 0, MIXSRC_LAST_CH,
        []() { return throttleSource2Source(g_model.thrTraceSrc); },
        [](int16_t src) {
          const int16_t val = source2ThrottleSource(src);
          if (val >= 0) { g_model.thrTraceSrc = val; SET_DIRTY(); }
        });
    sc->setAvailableHandler(isThrottleSourceAvailable);
  }
};
}  // namespace

void nb4OpenThrottleTraceDialog() { new Nb4ThrottleTraceDialog(); }

void ModelNb4ThrottlePage::rebuild(Window* window)
{
  window->clear();
  build(window);

  lv_obj_scroll_to_y(window->getLvObj(), 0, LV_ANIM_OFF);
}

void ModelNb4ThrottlePage::checkEvents()
{
  if (pendingTab >= 0 && body) {
    tab = (uint8_t)pendingTab;
    pendingTab = -1;
    rebuild(body);
  }
  PageGroupItem::checkEvents();
}

void ModelNb4ThrottlePage::build(Window* window)
{
  FlexGridLayout grid(col_dsc, row_dsc, PAD_ZERO);
  window->setFlexLayout();
  window->padAll(PAD_TINY);
  window->padLeft(PAD_ZERO);
  window->padRight(PAD_ZERO);

  const Nb4AxisView view = nb4ResolveAxis(Nb4AxisRole::Throttle);
  const Nb4ThrottleSides sides = nb4ResolveThrottleSides();
  const bool usable = view.status == Nb4AxisStatus::Ready ||
                      view.status == Nb4AxisStatus::Shared;
  const char* signGate = nb4RacingSignGateReason(view, sides);

  statusBanner(window, grid, view, view.outputChannel, /*sharedIsNormal=*/true);

  const char* const tabs[] = {
      STR_NB4_TRAVEL, STR_NB4_CURVE,
      STR_NB4_BRAKE_30D6, STR_NB4_ENGINE};
  if (tab >= 4) tab = 0;
  body = window;
  pendingTab = -1;
  tabsRow(window, grid, tabs, 4, tab,
          [this](uint8_t picked) { pendingTab = (int8_t)picked; });

  responseGraph(window, grid, view, /*isThrottle=*/true);

  const bool accelIsPositive = sides.known ? sides.accelSign > 0 : true;
  const int8_t accelStickSide = accelIsPositive ? (int8_t)+1 : (int8_t)-1;

  switch (tab) {
    case 0:
      if (usable) {
        nb4ParamRow(window, grid, Nb4Param::ChannelReverse, {view.outputChannel},
                    STR_NB4_REVERSE_CHANNEL);

        Nb4ParamCtx trigger;
        trigger.afterChange = [this]() { pendingTab = (int8_t)tab; };
        nb4ParamRow(window, grid, Nb4Param::ThrottleReversed, trigger);

        Nb4ParamCtx centre;
        centre.channel = view.outputChannel;
        nb4ParamPair(window, grid, Nb4Param::ChannelSubtrim, centre,
                     STR_NB4_NEUTRAL, Nb4Param::ThrottleTrim, {},
                     STR_NB4_TRIM);
      } else {
        nb4ParamRow(window, grid, Nb4Param::ThrottleTrim);
      }

      nb4ParamRow(window, grid, Nb4Param::ThrottleTrimIdleOnly);
      nb4ParamRow(window, grid, Nb4Param::ThrottleTrimSource);
      if (!usable) { tabIsEmptyBecause(window, grid, view.reason); break; }

      endpointPair(window, grid, view,
                   sides.known ? STR_NB4_THROTTLE
                               : STR_NB4_SIDE,
                   nb4EndpointForStickSide(view, accelStickSide),
                   sides.known ? STR_NB4_BRAKE_30D6
                               : STR_NB4_SIDE_1C91,
                   nb4EndpointForStickSide(view, (int8_t)-accelStickSide));
      break;

    case 1: {                                            // Response
      if (!usable) { tabIsEmptyBecause(window, grid, view.reason); break; }
      if (view.status == Nb4AxisStatus::Shared) {

        if (view.lineForPositive >= 0)
          responseRow(window, grid,
                      STR_NB4_RESPONSE_BOTH,
                      view.lineForPositive);
        break;
      }
      const int8_t accelLine = nb4InputLineForStickSide(view, accelStickSide);
      const int8_t brakeLine = nb4InputLineForStickSide(view, -accelStickSide);
      if (accelLine >= 0 && brakeLine >= 0)
        nb4ParamPair(window, grid,
                     Nb4Param::InputResponse, {Nb4ParamCtx::NONE, accelLine},
                     sides.known ? STR_NB4_THROTTLE_EXPO
                                 : STR_NB4_EXPO_SIDE,
                     Nb4Param::InputResponse, {Nb4ParamCtx::NONE, brakeLine},
                     sides.known ? STR_NB4_BRAKE_EXPO
                                 : STR_NB4_EXPO_SIDE_F0FC);

      if (const uint8_t sharedCurve = nb4AxisSharedCurveResource(view))
        sharedCurveRow(window, grid, view, sharedCurve,
                       expoAddress(view.lineForPositive)->srcRaw, nullptr);
      break;
    }

    case 2:                                              // Brake and ABS
      if (signGate) {
        signGateNotice(window, grid, signGate);
        break;
      }
      nb4ParamPair(window, grid, Nb4Param::BrakeMax, {}, STR_NB4_BRAKE_MAX,
                   Nb4Param::DragBrake, {}, STR_NB4_DRAG_BRAKE);
      subtitleRow(window, grid, STR_NB4_ABS);
      nb4ParamRow(window, grid, Nb4Param::AbsEnable);
      nb4ParamPair(window, grid, Nb4Param::AbsPoint, {}, STR_NB4_ABS_POINT,
                   Nb4Param::AbsRate, {}, STR_NB4_ABS_RATE);
      nb4ParamRow(window, grid, Nb4Param::AbsRelease);
      break;

    case 3:                                              // Engine

      if (g_model.nb4Racing.vehicleType == NB4_VEHICLE_ELECTRIC) {
        auto why = new StaticText(
            window->newLine(grid), rect_t{},
            STR_NB4_THIS_MODEL_IS_DECLARED_ELECTRIC_NO,
            COLOR_THEME_PRIMARY3_INDEX);
        lv_label_set_long_mode(why->getLvObj(), LV_LABEL_LONG_WRAP);
        lv_obj_set_style_grid_cell_column_span(why->getLvObj(), 2, LV_PART_MAIN);
        break;
      }
      if (signGate) {

        signGateNotice(window, grid, signGate);
      } else {
        nb4ParamRow(window, grid, Nb4Param::IdleUp);
        nb4ParamRow(window, grid, Nb4Param::IdleUpSwitch);
      }
      nb4ParamRow(window, grid, Nb4Param::EngineCutSwitch);
      nb4ParamRow(window, grid, Nb4Param::EngineCutPos);
      break;

    default:
      break;
  }
}

#endif  // RADIO_NB4_FAMILY
