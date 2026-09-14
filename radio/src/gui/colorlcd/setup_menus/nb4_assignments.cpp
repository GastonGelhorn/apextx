/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "nb4_assignments.h"
#if defined(RADIO_NB4_FAMILY)
#include "edgetx.h"
#include "nb4_controls.h"
#include "nb4_i18n.h"
#include "nb4_routes.h"
#include "nb4_model_compat.h"
#include "nb4_ui.h"
#include "dialog.h"
#include "choice.h"
#include "button.h"
#include "static.h"
#include "menu.h"
#include "hw_inputs.h"

namespace {
coord_t displayWidth() { return lv_disp_get_hor_res(nullptr); }
coord_t displayHeight() { return lv_disp_get_ver_res(nullptr); }
bool wide() { return displayWidth() > displayHeight(); }
const char* names[NB4_CONTROL_COUNT] = {
  "SW1-L", "SW1-R", "SW2", "SW3",
  "TR1-FB -", "TR1-FB +", "TR2-FB -", "TR2-FB +",
  "TR1-LR -", "TR1-LR +", "TR2-LR -", "TR2-LR +"
};
const char* location(unsigned i)
{
  if (i < 2) return i ? STR_NB4_RIGHT_GRIP : STR_NB4_LEFT_GRIP;
  if (i < 4) return i == 2 ? STR_NB4_LEFT_OF_STEERING_WHEEL : STR_NB4_RIGHT_OF_STEERING_WHEEL;
  return i < 8 ? STR_NB4_FORWARD_BACK_TRIM : STR_NB4_LEFT_RIGHT_TRIM;
}
const char* actionName(unsigned action)
{
  static const Nb4Str names[] = {NB4_STR(ORIGINAL_FUNCTION), NB4_STR(NO_ACTION),
      NB4_STR(PREVIOUS), NB4_STR(NEXT), NB4_STR(SELECT), NB4_STR(BACK),
      NB4_STR(OPEN_SETTINGS), NB4_STR(QUICK_ACCESS), NB4_STR(START_PAUSE),
      NB4_STR(MARK_LAP), NB4_STR(FINISH_RACE), NB4_STR(RESET_TIMER),
      NB4_STR(UNDO_LAP_ACTION), NB4_STR(STEERING_MINUS), NB4_STR(STEERING_PLUS),
      NB4_STR(THROTTLE_MINUS), NB4_STR(THROTTLE_PLUS)};
  return action < NB4_CONTROL_ACTION_COUNT ? names[action]() : "--";
}
std::string summary(unsigned i)
{
  uint8_t binding = nb4ControlBinding(i);
  uint8_t action = binding & ~NB4_CONTROL_LONG;
  if (!action) {
    if (i == 0) return STR_NB4_BACK;
    if (i == 1) return STR_NB4_SELECT_OPEN_SETTINGS;
    if (i < 4) return STR_NB4_MODEL_SWITCH;
    if (i < 6) return actionName(NB4_CONTROL_ST_DOWN + i - 4);
    if (i < 8) return actionName(NB4_CONTROL_TH_DOWN + i - 6);
    return STR_NB4_ORIGINAL_TRIM;
  }
  std::string result = actionName(action);
  if (binding & NB4_CONTROL_LONG) result += STR_NB4_HOLD;
  return result;
}
int groupOf(uint8_t action)
{
  if (nb4ControlIsNavigation(action)) return 1;
  if (nb4ControlCanHold(action)) return 2;
  return action >= NB4_CONTROL_ST_DOWN ? 3 : 0;
}
constexpr uint8_t firstAction[] = {NB4_CONTROL_DEFAULT, NB4_CONTROL_PREVIOUS, NB4_CONTROL_RUN_PAUSE, NB4_CONTROL_ST_DOWN};
constexpr uint8_t lastAction[] = {NB4_CONTROL_OFF, NB4_CONTROL_QUICK, NB4_CONTROL_UNDO, NB4_CONTROL_TH_UP};

StaticText* note(Window* parent, const char* text)
{
  return new StaticText(parent, {0, 0, LV_PCT(100), 0}, text,
                        COLOR_THEME_PRIMARY3_INDEX, FONT(XS));
}
void sectionLabel(Window* parent, const char* text)
{
  new StaticText(parent, {0, 0, LV_PCT(100), 0}, text, COLOR_THEME_PRIMARY1_INDEX, FONT(BOLD));
}

class LearnControlDialog : public BaseDialog
{
 public:
  LearnControlDialog(uint8_t binding, std::function<void(unsigned)> assigned) :
      BaseDialog(STR_NB4_PRESS_A_CONTROL, false,
                 displayWidth() - 8, displayHeight() - 12), binding(binding),
      assigned(std::move(assigned)), started(get_tmr10ms())
  {
    useSectionHeader();
    form->padAll(PAD_LARGE);
    sectionLabel(form, actionName(binding & ~NB4_CONTROL_LONG));
    note(form, STR_NB4_PRESS_A_BUTTON_OR_A_FOUR);
    hint = note(form, STR_NB4_RELEASE_ANY_CONTROLS_YOU_ARE_ALREADY);
    remaining = note(form, "");
    new TextButton(form, {0, 0, LV_PCT(100), 44}, STR_NB4_CANCEL,
                   [this]() { deleteLater(); return 0; });
    nb4ControlsBeginLearn();
  }
  void deleteLater(bool detach = true, bool trash = true) override
  {
    if (deleted()) return;
    nb4ControlsEndLearn();
    BaseDialog::deleteLater(detach, trash);
  }
  void checkEvents() override
  {
    BaseDialog::checkEvents();
    if (deleted()) return;
    if (nb4ModelBlocked() || !nb4ControlsLearning()) { deleteLater(); return; }
    const unsigned result = nb4ControlsLearned();
    if (result < NB4_CONTROL_COUNT) {
      nb4ControlSetBinding(result, binding);
      assigned(result);
      deleteLater();
      return;
    }
    if (result == 0xfe)
      hint->setText(STR_NB4_MULTIPLE_CONTROLS_PRESSED_RELEASE_THEM_A);
    const unsigned elapsed = (tmr10ms_t)(get_tmr10ms() - started);
    if (elapsed >= 1500) { assigned(NB4_CONTROL_COUNT); deleteLater(); return; }
    const unsigned seconds = 15 - elapsed / 100;
    if (seconds != lastSecond) {
      lastSecond = seconds;
      char text[64];
      snprintf(text, sizeof(text), STR_NB4_WAITING_FOR_A_CONTROL_U_S, seconds);
      remaining->setText(text);
    }
  }
 private:
  uint8_t binding;
  std::function<void(unsigned)> assigned;
  tmr10ms_t started;
  unsigned lastSecond = 0;
  StaticText *hint, *remaining;
};

class ControlEditor : public BaseDialog
{
 public:
  explicit ControlEditor(unsigned index, uint8_t initial = NB4_CONTROL_RUN_PAUSE) :
      BaseDialog(index < NB4_CONTROL_COUNT ? names[index] : STR_NB4_ASSIGN_FUNCTION,
                 false, displayWidth() - 8, displayHeight() - 12), index(index),
      draft(index < NB4_CONTROL_COUNT ? nb4ControlBinding(index) : initial)
  {
    useSectionHeader();
    form->padAll(PAD_MEDIUM);
    lv_obj_set_style_pad_row(form->getLvObj(), 8, 0);
    note(form, index < NB4_CONTROL_COUNT ? location(index) :
        STR_NB4_1_CHOOSE_A_FUNCTION_2_PRESS);
    Window *settings = form, *info = form;
    if (wide()) {
      auto columns = new Window(form, {0, 0, LV_PCT(100), LV_SIZE_CONTENT});
      columns->setFlexLayout(LV_FLEX_FLOW_ROW, PAD_LARGE, LV_PCT(100), LV_SIZE_CONTENT);
      lv_obj_update_layout(columns->getLvObj());
      const coord_t columnWidth = (lv_obj_get_content_width(columns->getLvObj()) - PAD_LARGE) / 2;
      settings = new Window(columns, rect_t{});
      info = new Window(columns, rect_t{});
      settings->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_MEDIUM, columnWidth, LV_SIZE_CONTENT);
      info->setFlexLayout(LV_FLEX_FLOW_COLUMN, PAD_MEDIUM, columnWidth, LV_SIZE_CONTENT);
    }
    sectionLabel(settings, STR_NB4_FUNCTION);
    auto group = new Choice(settings, {0, 0, LV_PCT(100), 40}, 0, 3,
        [this]() { return groupOf(draft & ~NB4_CONTROL_LONG); },
        [this](int v) { draft = firstAction[v]; refreshChoices(); });
    group->setTextHandler([](int v) {
      static const Nb4Str names[] = {NB4_STR(ORIGINAL_DISABLED), NB4_STR(NAVIGATION),
          NB4_STR(TIMER_AND_LAPS), NB4_STR(ADJUST_TRIMS)};
      return std::string(names[v]());
    });
    action = new Choice(settings, {0, 0, LV_PCT(100), 40}, 0, 1,
        [this]() { return int(draft & ~NB4_CONTROL_LONG); },
        [this](int v) { draft = v | (v == NB4_CONTROL_RESET ? NB4_CONTROL_LONG : draft & NB4_CONTROL_LONG); refreshChoices(); });
    action->setTextHandler([](int v) { return std::string(actionName(v)); });
    gesture = new Choice(settings, {0, 0, LV_PCT(100), 40}, 0, 1,
        [this]() { return !!(draft & NB4_CONTROL_LONG); },
        [this](int v) { draft = (draft & ~NB4_CONTROL_LONG) | (v ? NB4_CONTROL_LONG : 0); if (result) result->hide(); });
    gesture->setTextHandler([](int v) { return std::string(v ? STR_NB4_HOLD_0_6_S : STR_NB4_ON_PRESS); });
    detail = note(info, "");
    if (index == 2 || index == 3) {
      note(info, STR_NB4_THE_SW_SIGNAL_REMAINS_AVAILABLE_TO);
      std::string uses;
      auto usesSwitch = [index](int sw) {
        sw = abs(sw);
        return sw >= SWSRC_FIRST_SWITCH && sw < SWSRC_FIRST_SWITCH + 6 &&
               unsigned((sw - SWSRC_FIRST_SWITCH) / 3) == index - 2;
      };
      if (usesSwitch(g_model.nb4Racing.engineCutSw)) uses += STR_NB4_ENGINE_CUT_F024;
      if (usesSwitch(g_model.nb4Racing.idleUpSw)) uses += STR_NB4_IDLE_UP_23E4;
      if (usesSwitch(g_model.nb4Racing.lapSw)) uses += STR_NB4_LAPS_A8F4;
      for (const auto& timer : g_model.timers) if (usesSwitch(timer.swtch)) { uses += STR_NB4_TIMERS; break; }
      for (const auto& mix : g_model.mixData) if (mix.srcRaw && usesSwitch(mix.swtch)) { uses += STR_NB4_MIXES; break; }
      for (const auto& fn : g_model.customFn) if (usesSwitch(fn.swtch)) { uses += STR_NB4_SPECIAL_FUNCTIONS; break; }
      if (!uses.empty()) note(info, (std::string(STR_NB4_ALSO_ASSIGNED) + uses).c_str());
    }
    result = note(info, "");
    result->hide();
    auto save = new TextButton(info, {0, 0, LV_PCT(100), 44},
        index < NB4_CONTROL_COUNT ? STR_NB4_SAVE_ASSIGNMENT : STR_NB4_ASSIGN_BY_PRESSING, [this]() {
      if (this->index < NB4_CONTROL_COUNT) {
        nb4ControlSetBinding(this->index, draft);
        deleteLater();
      } else {
        new LearnControlDialog(draft, [this](unsigned detected) {
          result->show();
          if (detected >= NB4_CONTROL_COUNT) {
            result->setText(STR_NB4_NO_CONTROL_DETECTED_TRY_AGAIN);
            return;
          }
          std::string text = STR_NB4_ASSIGNED;
          text += names[detected]; text += " / "; text += summary(detected);
          if (detected == 2 || detected == 3)
            text += STR_NB4_ITS_CHANNEL_ASSIGNMENTS_ARE_KEPT;
          result->setText(text);
        });
      }
      return 0;
    });
    save->check();
    if (index >= NB4_CONTROL_COUNT)
      new TextButton(info, {0, 0, LV_PCT(100), 44}, STR_NB4_VIEW_BY_CONTROL,
                     []() { nb4OpenAssignmentsList(); return 0; });
    firstFocus = group->getLvObj();
    refreshChoices();
  }
  void checkEvents() override
  {
    BaseDialog::checkEvents();
    if (!settled && !deleted()) {
      settled = true;
      lv_group_focus_obj(firstFocus);
      lv_obj_scroll_to_y(form->getLvObj(), 0, LV_ANIM_OFF);
    }
  }
 private:
  unsigned index;
  uint8_t draft;
  Choice *action = nullptr, *gesture = nullptr;
  StaticText* detail = nullptr;
  StaticText* result = nullptr;
  bool settled = false;
  lv_obj_t* firstFocus = nullptr;
  void refreshChoices()
  {
    if (!action || !gesture || !detail) return;
    uint8_t selected = draft & ~NB4_CONTROL_LONG;
    if (result) { result->setText(""); result->hide(); }
    const int group = groupOf(selected);
    action->setMin(firstAction[group]); action->setMax(lastAction[group]); action->update();
    gesture->show(nb4ControlCanHold(selected));
    gesture->enable(selected != NB4_CONTROL_RESET);
    gesture->update();
    detail->setText(group == 1 ? STR_NB4_PREVIOUS_NEXT_MOVES_FOCUS_SELECT_OPENS :
        selected == NB4_CONTROL_RESET ? STR_NB4_HOLD_TO_CLEAR_TIMER_1_AND :
        group == 2 ? STR_NB4_CONTROLS_TIMER_1_AND_LAPS_PAUSE :
        group == 3 ? STR_NB4_USES_THE_NATIVE_TRIM_STEP_AND :
        STR_NB4_ORIGINAL_FUNCTION_RESTORES_THIS_KEY_S);
  }
};

class AssignmentsDialog : public BaseDialog
{
 public:
  explicit AssignmentsDialog(bool navigation) :
      BaseDialog(navigation ? STR_NB4_KEYS_AND_NAVIGATION : STR_NB4_ASSIGNMENTS, true, displayWidth() - 8, displayHeight() - 12)
  {
    useSectionHeader();
    form->padAll(PAD_MEDIUM);
    lv_obj_set_style_pad_row(form->getLvObj(), 6, 0);
    note(form, STR_NB4_PER_CAR_TAP_A_CONTROL_TO);
    if (navigation) note(form, STR_NB4_USE_WHEEL_GRIP_OR_TRIM_BUTTONS);
    sectionLabel(form, STR_NB4_BUTTONS);
    auto buttons = controlsGroup();
    for (unsigned i : {2u, 3u, 0u, 1u}) addControl(buttons, i);
    sectionLabel(form, "TRIMS");
    auto trims = controlsGroup();
    for (unsigned i = 4; i < NB4_CONTROL_COUNT; ++i) addControl(trims, i);
    new TextButton(form, {0, 0, LV_PCT(100), 44}, STR_NB4_SW2_SW3_PAIR, []() {
      auto menu = new Menu();
      menu->setTitle(STR_NB4_SW2_DOWN_SW3_UP);
      auto pair = [menu](const char* title, uint8_t first, uint8_t second) {
        menu->addLine(title, [first, second]() { nb4ControlSetBinding(2, first); nb4ControlSetBinding(3, second); });
      };
      pair(STR_NB4_NAVIGATION_PREVIOUS_NEXT, NB4_CONTROL_PREVIOUS, NB4_CONTROL_NEXT);
      pair(STR_NB4_STEERING_TRIM, NB4_CONTROL_ST_DOWN, NB4_CONTROL_ST_UP);
      pair(STR_NB4_THROTTLE_TRIM, NB4_CONTROL_TH_DOWN, NB4_CONTROL_TH_UP);
      pair(STR_NB4_RESTORE_BOTH, NB4_CONTROL_DEFAULT, NB4_CONTROL_DEFAULT);
      return 0;
    });
    new TextButton(form, {0, 0, LV_PCT(100), 44}, STR_NB4_OTHER_CONTROLS_AND_CHANNELS, []() {
      auto menu = new Menu();
      menu->setTitle(STR_NB4_OTHER_CONTROLS);
      menu->addLine(STR_NB4_WHEEL_AND_TRIGGER, []() { new HWInputDialog<HWSticks>(STR_NB4_WHEEL_AND_TRIGGER); });
      menu->addLine("VR1-L / VR1-R", []() { new HWInputDialog<HWPots>("VR1-L / VR1-R", HWPots::POTS_WINDOW_WIDTH); });
      menu->addLine(STR_NB4_CHANNELS, []() { nb4OpenRoute("settings/controls/channels"); });
      menu->addLine(STR_NB4_MIXES_60C8, []() { nb4OpenRoute("settings/advanced/mixes"); });
      menu->addLine(STR_NB4_SWITCHES, []() { new HWInputDialog<HWSwitches>(STR_NB4_SWITCHES, HWSwitches::SW_WINDOW_WIDTH); });
      return 0;
    });
    note(form, STR_NB4_ROTARY_CONTROLS_KEEP_THEIR_CURRENT_ROLE);
  }
  void checkEvents() override
  {
    BaseDialog::checkEvents();
    if (deleted()) return;
    for (unsigned i = 0; i < NB4_CONTROL_COUNT; ++i) {
      const uint8_t current = nb4ControlBinding(i);
      if (current != lastBindings[i]) { lastBindings[i] = current; labels[i]->setText(summary(i)); }
      const bool down = nb4ControlsPressed() & (1u << i);
      if (down != lastPressed[i]) {
        lastPressed[i] = down;
        etx_txt_color(namesText[i]->getLvObj(), down ? COLOR_THEME_EDIT_INDEX : COLOR_THEME_PRIMARY1_INDEX);
      }
    }
    if (!settled) { settled = true; lv_group_focus_obj(lv_obj_get_parent(namesText[2]->getLvObj())); lv_obj_scroll_to_y(form->getLvObj(), 0, LV_ANIM_OFF); }
  }
 private:
  StaticText *labels[NB4_CONTROL_COUNT]{}, *namesText[NB4_CONTROL_COUNT]{};
  uint8_t lastBindings[NB4_CONTROL_COUNT]{};
  bool lastPressed[NB4_CONTROL_COUNT]{}, settled = false;
  Window* controlsGroup()
  {
    auto group = new Window(form, rect_t{});
    group->setFlexLayout(LV_FLEX_FLOW_ROW_WRAP, PAD_MEDIUM, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_update_layout(group->getLvObj());
    return group;
  }
  void addControl(Window* parent, unsigned index)
  {
    const coord_t available = lv_obj_get_content_width(parent->getLvObj());
    const coord_t rowWidth = wide() ? (available - PAD_MEDIUM) / 2 : available;
    auto row = new ButtonBase(parent, {0, 0, rowWidth, 58}, [index]() { nb4OpenControlAssignment(index); return 0; });
    Nb4Ui::card(row->getLvObj());
    etx_border_color(row->getLvObj(), COLOR_THEME_FOCUS_INDEX, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(row->getLvObj(), 2, LV_STATE_FOCUSED);
    std::string title = names[index];
    if (index < 2) title += index ? STR_NB4_RIGHT_GRIP_8E97 : STR_NB4_LEFT_GRIP_23DC;
    if (index == 2 || index == 3) title += index == 2 ? STR_NB4_WHEEL_LEFT : STR_NB4_WHEEL_RIGHT;
    namesText[index] = new StaticText(row, {10, 6, rowWidth - 44, 22}, title.c_str(), COLOR_THEME_PRIMARY1_INDEX, FONT(BOLD));
    lastBindings[index] = nb4ControlBinding(index);
    labels[index] = new StaticText(row, {10, 31, rowWidth - 44, 18}, summary(index).c_str(), COLOR_THEME_PRIMARY3_INDEX, FONT(XS));
    new StaticText(row, {rowWidth - 30, 18, 20, 24}, LV_SYMBOL_RIGHT, COLOR_THEME_PRIMARY3_INDEX);
  }
};
}
void nb4OpenControlAssignment(unsigned index)
{
  if (index < NB4_CONTROL_COUNT && !nb4ModelBlocked()) new ControlEditor(index);
}
void nb4OpenAssignments() { if (!nb4ModelBlocked()) new ControlEditor(NB4_CONTROL_COUNT); }
void nb4OpenNavigationAssignments() { if (!nb4ModelBlocked()) new ControlEditor(NB4_CONTROL_COUNT, NB4_CONTROL_NEXT); }
void nb4OpenAssignmentsList() { if (!nb4ModelBlocked()) new AssignmentsDialog(false); }
#endif
