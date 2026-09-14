/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "nb4_assignments.h"
#if defined(RADIO_NB4_FAMILY)
#include "edgetx.h"
#include "nb4_controls.h"
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
  if (i < 2) return i ? nb4Text("Grip derecho", "Right grip") : nb4Text("Grip izquierdo", "Left grip");
  if (i < 4) return i == 2 ? nb4Text("Lado izquierdo del volante", "Left of steering wheel") : nb4Text("Lado derecho del volante", "Right of steering wheel");
  return i < 8 ? nb4Text("Trim adelante / atrás", "Forward / back trim") : nb4Text("Trim izquierda / derecha", "Left / right trim");
}
const char* actionName(unsigned action)
{
  static const char* const es[] = {"Función original", "Sin acción", "Anterior", "Siguiente", "Aceptar", "Volver", "Abrir ajustes", "Acceso rápido", "Iniciar / pausar", "Marcar vuelta", "Finalizar manga", "Reiniciar crono", "Deshacer vuelta", "Dirección -", "Dirección +", "Gas -", "Gas +"};
  static const char* const en[] = {"Original function", "No action", "Previous", "Next", "Select", "Back", "Open settings", "Quick access", "Start / pause", "Mark lap", "Finish race", "Reset timer", "Undo lap", "Steering -", "Steering +", "Throttle -", "Throttle +"};
  return action < NB4_CONTROL_ACTION_COUNT ? nb4Text(es[action], en[action]) : "--";
}
std::string summary(unsigned i)
{
  uint8_t binding = nb4ControlBinding(i);
  uint8_t action = binding & ~NB4_CONTROL_LONG;
  if (!action) {
    if (i == 0) return nb4Text("Volver", "Back");
    if (i == 1) return nb4Text("Aceptar / abrir ajustes", "Select / open settings");
    if (i < 4) return nb4Text("Interruptor del modelo", "Model switch");
    if (i < 6) return actionName(NB4_CONTROL_ST_DOWN + i - 4);
    if (i < 8) return actionName(NB4_CONTROL_TH_DOWN + i - 6);
    return nb4Text("Trim original", "Original trim");
  }
  std::string result = actionName(action);
  if (binding & NB4_CONTROL_LONG) result += nb4Text(" (mantener)", " (hold)");
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
      BaseDialog(nb4Text("Pulsa un mando", "Press a control"), false,
                 displayWidth() - 8, displayHeight() - 12), binding(binding),
      assigned(std::move(assigned)), started(get_tmr10ms())
  {
    useSectionHeader();
    form->padAll(PAD_LARGE);
    sectionLabel(form, actionName(binding & ~NB4_CONTROL_LONG));
    note(form, nb4Text("Pulsa un botón o un trim de cuatro direcciones. Se asignará automáticamente.",
                      "Press a button or a four-way trim. It will be assigned automatically."));
    hint = note(form, nb4Text("Suelta primero los mandos que ya tengas pulsados.",
                            "Release any controls you are already holding."));
    remaining = note(form, "");
    new TextButton(form, {0, 0, LV_PCT(100), 44}, nb4Text("Cancelar", "Cancel"),
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
      hint->setText(nb4Text("Se han pulsado varios mandos. Suéltalos y pulsa solo uno.",
                           "Multiple controls pressed. Release them and press just one."));
    const unsigned elapsed = (tmr10ms_t)(get_tmr10ms() - started);
    if (elapsed >= 1500) { assigned(NB4_CONTROL_COUNT); deleteLater(); return; }
    const unsigned seconds = 15 - elapsed / 100;
    if (seconds != lastSecond) {
      lastSecond = seconds;
      char text[64];
      snprintf(text, sizeof(text), nb4Text("Esperando un mando... %u s", "Waiting for a control... %u s"), seconds);
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
      BaseDialog(index < NB4_CONTROL_COUNT ? names[index] : nb4Text("Asignar función", "Assign function"),
                 false, displayWidth() - 8, displayHeight() - 12), index(index),
      draft(index < NB4_CONTROL_COUNT ? nb4ControlBinding(index) : initial)
  {
    useSectionHeader();
    form->padAll(PAD_MEDIUM);
    lv_obj_set_style_pad_row(form->getLvObj(), 8, 0);
    note(form, index < NB4_CONTROL_COUNT ? location(index) :
        nb4Text("1. Elige la función. 2. Pulsa el mando.", "1. Choose a function. 2. Press a control."));
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
    sectionLabel(settings, nb4Text("Función", "Function"));
    auto group = new Choice(settings, {0, 0, LV_PCT(100), 40}, 0, 3,
        [this]() { return groupOf(draft & ~NB4_CONTROL_LONG); },
        [this](int v) { draft = firstAction[v]; refreshChoices(); });
    group->setTextHandler([](int v) {
      static const char* const es[] = {"Original / desactivado", "Navegación", "Crono y vueltas", "Ajustar trims"};
      static const char* const en[] = {"Original / disabled", "Navigation", "Timer and laps", "Adjust trims"};
      return std::string(nb4Text(es[v], en[v]));
    });
    action = new Choice(settings, {0, 0, LV_PCT(100), 40}, 0, 1,
        [this]() { return int(draft & ~NB4_CONTROL_LONG); },
        [this](int v) { draft = v | (v == NB4_CONTROL_RESET ? NB4_CONTROL_LONG : draft & NB4_CONTROL_LONG); refreshChoices(); });
    action->setTextHandler([](int v) { return std::string(actionName(v)); });
    gesture = new Choice(settings, {0, 0, LV_PCT(100), 40}, 0, 1,
        [this]() { return !!(draft & NB4_CONTROL_LONG); },
        [this](int v) { draft = (draft & ~NB4_CONTROL_LONG) | (v ? NB4_CONTROL_LONG : 0); if (result) result->hide(); });
    gesture->setTextHandler([](int v) { return std::string(v ? nb4Text("Mantener 0,6 s", "Hold 0.6 s") : nb4Text("Al pulsar", "On press")); });
    detail = note(info, "");
    if (index == 2 || index == 3) {
      note(info, nb4Text("La señal SW sigue disponible en canales, mezclas y funciones del modelo.", "The SW signal remains available to model channels, mixes and functions."));
      std::string uses;
      auto usesSwitch = [index](int sw) {
        sw = abs(sw);
        return sw >= SWSRC_FIRST_SWITCH && sw < SWSRC_FIRST_SWITCH + 6 &&
               unsigned((sw - SWSRC_FIRST_SWITCH) / 3) == index - 2;
      };
      if (usesSwitch(g_model.nb4Racing.engineCutSw)) uses += nb4Text("Corte de motor. ", "Engine cut. ");
      if (usesSwitch(g_model.nb4Racing.idleUpSw)) uses += nb4Text("Ralentí alto. ", "Idle up. ");
      if (usesSwitch(g_model.nb4Racing.lapSw)) uses += nb4Text("Vueltas. ", "Laps. ");
      for (const auto& timer : g_model.timers) if (usesSwitch(timer.swtch)) { uses += nb4Text("Cronómetros. ", "Timers. "); break; }
      for (const auto& mix : g_model.mixData) if (mix.srcRaw && usesSwitch(mix.swtch)) { uses += nb4Text("Mezclas. ", "Mixes. "); break; }
      for (const auto& fn : g_model.customFn) if (usesSwitch(fn.swtch)) { uses += nb4Text("Funciones especiales. ", "Special functions. "); break; }
      if (!uses.empty()) note(info, (std::string(nb4Text("También asignado: ", "Also assigned: ")) + uses).c_str());
    }
    result = note(info, "");
    result->hide();
    auto save = new TextButton(info, {0, 0, LV_PCT(100), 44},
        index < NB4_CONTROL_COUNT ? nb4Text("Guardar asignación", "Save assignment") : nb4Text("Asignar pulsando", "Assign by pressing"), [this]() {
      if (this->index < NB4_CONTROL_COUNT) {
        nb4ControlSetBinding(this->index, draft);
        deleteLater();
      } else {
        new LearnControlDialog(draft, [this](unsigned detected) {
          result->show();
          if (detected >= NB4_CONTROL_COUNT) {
            result->setText(nb4Text("No se detectó un mando. Vuelve a intentarlo.", "No control detected. Try again."));
            return;
          }
          std::string text = nb4Text("Asignado: ", "Assigned: ");
          text += names[detected]; text += " / "; text += summary(detected);
          if (detected == 2 || detected == 3)
            text += nb4Text(". Conserva sus asignaciones de canal.", ". Its channel assignments are kept.");
          result->setText(text);
        });
      }
      return 0;
    });
    save->check();
    if (index >= NB4_CONTROL_COUNT)
      new TextButton(info, {0, 0, LV_PCT(100), 44}, nb4Text("Ver por mando", "View by control"),
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
    detail->setText(group == 1 ? nb4Text("Anterior / siguiente mueve el foco. Aceptar abre el control seleccionado; Volver cierra la vista.", "Previous / next moves focus. Select opens the focused control; Back closes the view.") :
        selected == NB4_CONTROL_RESET ? nb4Text("Mantén pulsado para poner a cero el Crono 1 y las vueltas de la manga.", "Hold to clear Timer 1 and the current race laps.") :
        group == 2 ? nb4Text("Controla el Crono 1 y las vueltas. Pausar conserva el tiempo; volver a pulsar continúa la manga.", "Controls Timer 1 and laps. Pause keeps elapsed time; press again to continue the race.") :
        group == 3 ? nb4Text("Usa el paso y la repetición de los trims. Sustituye la función original de esta tecla.", "Uses the native trim step and repeat. Replaces this key's original function.") :
        nb4Text("Función original restaura el comportamiento de esta tecla.", "Original function restores this key's usual behaviour."));
  }
};

class AssignmentsDialog : public BaseDialog
{
 public:
  explicit AssignmentsDialog(bool navigation) :
      BaseDialog(navigation ? nb4Text("Teclas y navegación", "Keys and navigation") : nb4Text("Asignaciones", "Assignments"), true, displayWidth() - 8, displayHeight() - 12)
  {
    useSectionHeader();
    form->padAll(PAD_MEDIUM);
    lv_obj_set_style_pad_row(form->getLvObj(), 6, 0);
    note(form, nb4Text("Por coche. Toca un mando para cambiar su función.", "Per car. Tap a control to change its function."));
    if (navigation) note(form, nb4Text("Puedes usar los botones del volante, grip o trims para navegar. La pantalla táctil sigue disponible.", "Use wheel, grip or trim buttons to navigate. Touch remains available."));
    sectionLabel(form, nb4Text("PULSADORES", "BUTTONS"));
    auto buttons = controlsGroup();
    for (unsigned i : {2u, 3u, 0u, 1u}) addControl(buttons, i);
    sectionLabel(form, "TRIMS");
    auto trims = controlsGroup();
    for (unsigned i = 4; i < NB4_CONTROL_COUNT; ++i) addControl(trims, i);
    new TextButton(form, {0, 0, LV_PCT(100), 44}, nb4Text("Pareja SW2 / SW3", "SW2 / SW3 pair"), []() {
      auto menu = new Menu();
      menu->setTitle(nb4Text("SW2 baja / SW3 sube", "SW2 down / SW3 up"));
      auto pair = [menu](const char* title, uint8_t first, uint8_t second) {
        menu->addLine(title, [first, second]() { nb4ControlSetBinding(2, first); nb4ControlSetBinding(3, second); });
      };
      pair(nb4Text("Navegación: anterior / siguiente", "Navigation: previous / next"), NB4_CONTROL_PREVIOUS, NB4_CONTROL_NEXT);
      pair(nb4Text("Trim de dirección - / +", "Steering trim - / +"), NB4_CONTROL_ST_DOWN, NB4_CONTROL_ST_UP);
      pair(nb4Text("Trim de gas - / +", "Throttle trim - / +"), NB4_CONTROL_TH_DOWN, NB4_CONTROL_TH_UP);
      pair(nb4Text("Restaurar ambos", "Restore both"), NB4_CONTROL_DEFAULT, NB4_CONTROL_DEFAULT);
      return 0;
    });
    new TextButton(form, {0, 0, LV_PCT(100), 44}, nb4Text("Otros controles y canales", "Other controls and channels"), []() {
      auto menu = new Menu();
      menu->setTitle(nb4Text("Otros controles", "Other controls"));
      menu->addLine(nb4Text("Volante y gatillo", "Wheel and trigger"), []() { new HWInputDialog<HWSticks>(nb4Text("Volante y gatillo", "Wheel and trigger")); });
      menu->addLine("VR1-L / VR1-R", []() { new HWInputDialog<HWPots>("VR1-L / VR1-R", HWPots::POTS_WINDOW_WIDTH); });
      menu->addLine(nb4Text("Canales", "Channels"), []() { nb4OpenRoute("settings/controls/channels"); });
      menu->addLine(nb4Text("Mezclas", "Mixes"), []() { nb4OpenRoute("settings/advanced/mixes"); });
      menu->addLine(nb4Text("Interruptores", "Switches"), []() { new HWInputDialog<HWSwitches>(nb4Text("Interruptores", "Switches"), HWSwitches::SW_WINDOW_WIDTH); });
      return 0;
    });
    note(form, nb4Text("Los mandos giratorios conservan su uso actual. TR4 aún no tiene lectura en este firmware.", "Rotary controls keep their current role. TR4 is not yet read by this firmware."));
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
    if (index < 2) title += index ? nb4Text("  Grip der.", "  Right grip") : nb4Text("  Grip izq.", "  Left grip");
    if (index == 2 || index == 3) title += index == 2 ? nb4Text("  Volante izq.", "  Wheel left") : nb4Text("  Volante der.", "  Wheel right");
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
