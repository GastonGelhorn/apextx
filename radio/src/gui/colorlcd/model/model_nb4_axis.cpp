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
      headline = nb4Text("Los dos lados comparten un mismo ajuste",
                         "Both sides share one setting");
      break;
    case Nb4AxisStatus::Dynamic:
      headline = nb4Text("Un valor viene de otra fuente, no de un número",
                         "A value comes from another source, not a number");
      break;
    default:
      headline = nb4Text("Esta configuración no se puede editar aquí",
                         "This setup cannot be edited here");
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
      blocker == Nb4Blocker::Input  ? nb4Text("Abrir Entradas", "Open Inputs")
      : blocker == Nb4Blocker::Mix  ? nb4Text("Abrir Mezclas", "Open Mixes")
                                    : nb4Text("Abrir Salidas", "Open Outputs");

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
                            nb4Text("Desactivar estas funciones",
                                    "Turn these functions off"),
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
             nb4Text("Los dos lados usan la curva %s, y %d ajuste%s más del modelo "
                     "también. Mover sus puntos los mueve todos.",
                     "Both sides use curve %s, and %d more setting%s in the model "
                     "does too. Moving its points moves them all."),
             getCurveString(curveNumber), (int)others, others == 1 ? "" : "s");
  else
    snprintf(msg, sizeof(msg),
             nb4Text("Los dos lados usan la curva %s. Mover sus puntos cambia el "
                     "gas y el freno a la vez; cambiar el selector de un lado, no.",
                     "Both sides use curve %s. Moving its points changes throttle "
                     "and brake together; changing one side's selector does not."),
             getCurveString(curveNumber));

  auto line = form->newLine(grid);
  auto why = new StaticText(line, rect_t{}, msg, COLOR_THEME_PRIMARY3_INDEX);
  lv_label_set_long_mode(why->getLvObj(), LV_LABEL_LONG_WRAP);
  lv_obj_set_style_grid_cell_column_span(why->getLvObj(), 2, LV_PART_MAIN);

  char label[64];
  snprintf(label, sizeof(label),
           nb4Text("Editar los puntos de %s", "Edit the points of %s"),
           getCurveString(curveNumber));

  line = form->newLine(grid);
  auto edit = new TextButton(
      line, {0, 0, LV_PCT(100), 0}, label,
      [curveNumber, source, refreshView, others]() {
        char ask[160];
        if (others > 0)
          snprintf(ask, sizeof(ask),

                   nb4Text("Afecta al gas, al freno y a %d ajuste%s más. Seguir?",
                           "This affects throttle, brake and %d more setting%s. "
                           "Continue?"),
                   (int)others, others == 1 ? "" : "s");
        else

          snprintf(ask, sizeof(ask), "%s",
                   nb4Text("Afecta al gas y al freno a la vez. Seguir?",
                           "This affects throttle and brake at once. Continue?"));
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
        nb4Text("La mezcla de este canal lleva un desplazamiento o una curva, y el "
                "dibujo no los incluye: se deja de dibujar antes que enseñar un "
                "mapa que no es.",
                "This channel's mix carries an offset or a curve that the drawing "
                "does not include, so it is not drawn: better none than a map that "
                "lies."),
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
  const char* leftZone = isThrottle ? nb4Text("Freno", "Brake") : nb4Text("Izquierda", "Left");
  const char* middleZone = isThrottle ? nb4Text("Neutro", "Neutral") : nb4Text("Centro", "Centre");
  const char* rightZone = isThrottle ? nb4Text("Gas", "Throttle") : nb4Text("Derecha", "Right");

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
                 isThrottle ? nb4Text("Curva de gas y freno", "Throttle and brake curve")
                            : nb4Text("Curva de dirección", "Steering curve"),
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
             : nb4Text("Este ajuste no se puede editar mientras no se pueda "
                       "interpretar la cadena de mezclas de este canal.",
                       "This setting cannot be edited while this channel's mixer "
                       "chain cannot be interpreted."),
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
      nb4Text("Recorrido", "Travel"), nb4Text("Curva", "Curve"),
      nb4Text("Centro", "Centre"), nb4Text("Velocidad", "Speed")};
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
                 nb4Text("Izquierda", "Left"), nb4EndpointForStickSide(view, nb4AxisVisual(false, -1)),
                 nb4Text("Derecha", "Right"), nb4EndpointForStickSide(view, nb4AxisVisual(false, +1)));

    nb4ParamRow(window, grid, Nb4Param::ChannelReverse, {view.outputChannel},
                nb4Text("Invertir el canal", "Reverse channel"));
    return;
  }

  if (tab == 2) {

    nb4ParamPair(window, grid, Nb4Param::ChannelSubtrim, {view.outputChannel},
                 nb4Text("Centro", "Centre"), Nb4Param::SteeringTrim, {},
                 nb4Text("Trim", "Trim"));
    return;
  }

  if (view.status == Nb4AxisStatus::Shared) {
    if (view.lineForPositive >= 0)
      nb4ParamPair(window, grid,
                   Nb4Param::InputDualRate, {Nb4ParamCtx::NONE, view.lineForPositive},
                   nb4Text("Dual rate", "Dual rate"),
                   Nb4Param::InputResponse, {Nb4ParamCtx::NONE, view.lineForPositive},
                   nb4Text("Exponencial", "Exponential"));
    return;
  }

  const int8_t leftLine = nb4InputLineForStickSide(view, nb4AxisVisual(false, -1));
  const int8_t rightLine = nb4InputLineForStickSide(view, nb4AxisVisual(false, +1));
  if (leftLine >= 0 && rightLine >= 0) {
    nb4ParamPair(window, grid,
                 Nb4Param::InputDualRate, {Nb4ParamCtx::NONE, leftLine},
                 nb4Text("Dual rate izq.", "Left dual rate"),
                 Nb4Param::InputDualRate, {Nb4ParamCtx::NONE, rightLine},
                 nb4Text("Dual rate der.", "Right dual rate"));
    nb4ParamPair(window, grid,
                 Nb4Param::InputResponse, {Nb4ParamCtx::NONE, leftLine},
                 nb4Text("Expo izquierda", "Left expo"),
                 Nb4Param::InputResponse, {Nb4ParamCtx::NONE, rightLine},
                 nb4Text("Expo derecha", "Right expo"));
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
      BaseDialog(nb4Text("Seguimiento del gas", "Throttle tracking"), true)
  {
    new StaticText(form, {0, 0, LV_PCT(100), 0},
                   nb4Text("Qué canal sigue el cronómetro para contar el tiempo "
                           "de motor.",
                           "Which channel the timer follows to count engine time."),
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
      nb4Text("Recorrido", "Travel"), nb4Text("Curva", "Curve"),
      nb4Text("Freno", "Brake"), nb4Text("Motor", "Engine")};
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
                    nb4Text("Invertir el canal", "Reverse channel"));

        Nb4ParamCtx trigger;
        trigger.afterChange = [this]() { pendingTab = (int8_t)tab; };
        nb4ParamRow(window, grid, Nb4Param::ThrottleReversed, trigger);

        Nb4ParamCtx centre;
        centre.channel = view.outputChannel;
        nb4ParamPair(window, grid, Nb4Param::ChannelSubtrim, centre,
                     nb4Text("Neutro", "Neutral"), Nb4Param::ThrottleTrim, {},
                     nb4Text("Trim", "Trim"));
      } else {
        nb4ParamRow(window, grid, Nb4Param::ThrottleTrim);
      }

      nb4ParamRow(window, grid, Nb4Param::ThrottleTrimIdleOnly);
      nb4ParamRow(window, grid, Nb4Param::ThrottleTrimSource);
      if (!usable) { tabIsEmptyBecause(window, grid, view.reason); break; }

      endpointPair(window, grid, view,
                   sides.known ? nb4Text("Gas", "Throttle")
                               : nb4Text("Lado +", "+ side"),
                   nb4EndpointForStickSide(view, accelStickSide),
                   sides.known ? nb4Text("Freno", "Brake")
                               : nb4Text("Lado -", "- side"),
                   nb4EndpointForStickSide(view, (int8_t)-accelStickSide));
      break;

    case 1: {                                            // Response
      if (!usable) { tabIsEmptyBecause(window, grid, view.reason); break; }
      if (view.status == Nb4AxisStatus::Shared) {

        if (view.lineForPositive >= 0)
          responseRow(window, grid,
                      nb4Text("Respuesta (gas y freno)", "Response (both)"),
                      view.lineForPositive);
        break;
      }
      const int8_t accelLine = nb4InputLineForStickSide(view, accelStickSide);
      const int8_t brakeLine = nb4InputLineForStickSide(view, -accelStickSide);
      if (accelLine >= 0 && brakeLine >= 0)
        nb4ParamPair(window, grid,
                     Nb4Param::InputResponse, {Nb4ParamCtx::NONE, accelLine},
                     sides.known ? nb4Text("Expo de gas", "Throttle expo")
                                 : nb4Text("Expo lado +", "Expo, + side"),
                     Nb4Param::InputResponse, {Nb4ParamCtx::NONE, brakeLine},
                     sides.known ? nb4Text("Expo de freno", "Brake expo")
                                 : nb4Text("Expo lado -", "Expo, - side"));

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
            nb4Text("Este modelo está declarado como eléctrico: no tiene ralentí "
                    "ni corte de motor. El tipo se cambia en Competición.",
                    "This model is declared electric: no idle-up and no engine "
                    "cut. The type is set in Racing."),
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
