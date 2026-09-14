/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_home.h"
#include "nb4_ui.h"
#include "nb4_home_templates.h"
#include "nb4_health.h"
#include "nb4_palettes.h"
#include "nb4_pit.h"
#include "nb4_model_compat.h"
#include "nb4_racing.h"
#include "model_nb4_racing.h"
#include "layout.h"
#include "mainwindow.h"
#include "theme_manager.h"
#include "output_edit.h"
#include "module_setup.h"
#include "timer_setup.h"
#include "switchchoice.h"
#include "toggleswitch.h"
#include "dialog.h"
#include "keyboard_base.h"
#include "choice.h"
#include "quick_menu.h"
#include "nb4_routes.h"
#include "view_main.h"
#include "view_channels.h"

namespace {
int pendingOrientation = -1;
bool reopenAfterOrientation = true;

StaticText* label(Window* p, rect_t r, const char* text, LcdFlags font = FONT(STD),
                  LcdColorIndex color = COLOR_THEME_PRIMARY1_INDEX)
{
  return nb4Label(p, r, text, font, color);
}

Window* card(Window* parent, rect_t rect)
{
  auto w = new Window(parent, rect);
  Nb4Ui::card(w->getLvObj());
  return w;
}

uint32_t modelIdentity()
{
  uint32_t hash = 2166136261u;
  auto mix = [&hash](const char* text, size_t limit) {
    for (size_t i = 0; i < limit && text[i]; ++i) {
      hash ^= (uint8_t)text[i];
      hash *= 16777619u;
    }
    hash ^= 0x9e3779b9u;
  };
  mix(g_model.header.name, LEN_MODEL_NAME);
#if defined(STORAGE_MODELSLIST)
  mix(g_model.header.labels, LABELS_LENGTH);
#endif
  return hash;
}

std::string modelLabels()
{
  std::string text;
#if defined(STORAGE_MODELSLIST)
  for (const char* p = g_model.header.labels; *p; ++p) {
    if (*p == ',') text += " " LV_SYMBOL_BULLET " ";
    else if (*p >= 'a' && *p <= 'z') text += char(*p - 'a' + 'A');
    else text += *p;
  }
#endif
  return text;
}

TextButton* action(Window* p, rect_t r, const char* text, std::function<void()> fn)
{
  return Nb4Ui::action(p, r, text, fn);
}

void selectedAction(Window* p, rect_t r, const char* text, std::function<void()> fn,
                    std::function<bool()> selected)
{
  auto b = action(p, r, text, fn);

  auto o = b->getLvObj();
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
  etx_bg_color(o, COLOR_THEME_SECONDARY3_INDEX, LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_set_style_border_width(o, 3, LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_set_style_border_side(o, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_BOTTOM,
                               LV_PART_MAIN | LV_STATE_CHECKED);
  etx_border_color(o, COLOR_THEME_FOCUS_INDEX, LV_PART_MAIN | LV_STATE_CHECKED);
  b->setCheckHandler([b, selected] {
    bool active = selected();
    if (active != b->checked()) b->check(active);
  });
}

}  // namespace

void nb4Navigate(Nb4Section section)
{
  auto main = ViewMain::instance();
  for (unsigned i = 0; i < 32 && Layer::back() && Layer::back() != main; ++i) {
    auto page = Layer::back(); page->onCancel();
    if (Layer::back() == page) return;
  }
  nb4OpenSection(section);
}

void nb4OpenSection(Nb4Section section)
{
  if (section == Nb4Section::Telemetry || section == Nb4Section::History ||
      section == Nb4Section::Chrono || section == Nb4Section::Pit || section == Nb4Section::Backup) {
    nb4OpenDataPage(section);
    return;
  }
  switch (section) {
    case Nb4Section::Car: QuickMenu::openPage(QM_MODEL_SETUP); break;
    case Nb4Section::Race: QuickMenu::openPage(QM_MODEL_NB4_RACING); break;
    case Nb4Section::Steering:
      if (g_model.nb4Racing.steeringChannel < MAX_OUTPUT_CHANNELS)
        new OutputEditWindow(g_model.nb4Racing.steeringChannel);
      break;
    case Nb4Section::Throttle:
      if (g_model.nb4Racing.throttleChannel < MAX_OUTPUT_CHANNELS)
        new OutputEditWindow(g_model.nb4Racing.throttleChannel);
      break;
    case Nb4Section::Auxiliary: QuickMenu::openPage(QM_MODEL_MIXES); break;
    case Nb4Section::Advanced: ViewMain::instance()->openMenu(); break;
    case Nb4Section::System: ViewMain::instance()->openMenu(); break;

    case Nb4Section::Appearance:
    case Nb4Section::Cards: QuickMenu::openPage(QM_UI_SCREEN1); break;
    default: break;
  }
}

void nb4RequestOrientation(bool landscape, bool reopenAppearance)
{
  pendingOrientation = landscape ? 1 : 0;
  reopenAfterOrientation = reopenAppearance;
}

void nb4ProcessOrientation()
{
  if (pendingOrientation < 0) return;
  auto display = lv_disp_get_default();
  if (!display || lv_disp_get_draw_buf(display)->flushing) return;
  // Finish native live edits and cancel gestures before rebuilding any view.
  Keyboard::hide(false);
  lv_indev_reset(nullptr, nullptr);
  // This runs between UI iterations, never inside a control's callback.
  auto main = ViewMain::instance();
  while (Layer::back() && Layer::back() != main) {
    auto page = Layer::back();
    page->onCancel();
    if (Layer::back() == page) return;
  }
  if (!lcdSetOrientation(pendingOrientation != 0)) return;
  if (!nb4HealthRecovery()) {
    g_eeGeneral.nb4Orientation = pendingOrientation;
    storageDirty(EE_GENERAL);
  }
  pendingOrientation = -1;
  main->resizeToDisplay();
  LayoutFactory::loadCustomScreens();
  if (reopenAfterOrientation) nb4OpenSection(Nb4Section::Appearance);
}

namespace {

void applyPaletteByName(const char* name)
{
  auto tp = ThemePersistance::instance();
  auto names = tp->getNames();
  for (unsigned i = 0; i < names.size(); ++i) {
    if (names[i] == name) {
      tp->setDefaultTheme(i);
      tp->applyTheme(i);
      return;
    }
  }
}

}  // namespace

void nb4BuildAppearance(Window* parent)
{
  parent->padAll(PAD_TINY);
  lv_obj_update_layout(parent->getLvObj());
  coord_t y = 0, w = lv_obj_get_content_width(parent->getLvObj());
  label(parent, {8, y, w - 16, 28}, "ApexTX", FONT(L)); y += 36;
  label(parent, {8, y, w - 16, 22}, STR_NB4_PALETTE, FONT(XS), COLOR_THEME_PRIMARY3_INDEX); y += 24;
  for (unsigned i = 0; i < nb4PaletteCount(); ++i) {
    const auto& palette = nb4Palette(i);
    const std::string name = palette.name;
    selectedAction(parent, {0, y, w, 44}, palette.name,
      [name] { applyPaletteByName(name.c_str()); },
      [name] { return name == g_eeGeneral.selectedTheme; });
    y += 50;
  }
  label(parent, {8, y, w - 16, 22}, STR_NB4_ACCENT, FONT(XS), COLOR_THEME_PRIMARY3_INDEX); y += 24;
  auto choice = new Choice(parent, {0, y, w, 44}, 0, nb4AccentCount() - 1,
    [] { return int(g_eeGeneral.nb4Accent); }, [](int value) {
      g_eeGeneral.nb4Accent = value; storageDirty(EE_GENERAL);
      applyPaletteByName(g_eeGeneral.selectedTheme);
    });
  choice->setTextHandler([](int value) { return std::string(nb4AccentName(value)); });
  y += 54;
  action(parent, {0, y, w, 44}, STR_NB4_EXTERNAL_THEMES, [] { QuickMenu::openPage(QM_UI_THEMES); }); y += 54;
  label(parent, {8, y, w - 16, 22}, STR_NB4_ORIENTATION, FONT(XS), COLOR_THEME_PRIMARY3_INDEX); y += 24;
  selectedAction(parent, {0, y, w, 44}, STR_NB4_PORTRAIT, [] { nb4RequestOrientation(false); }, [] { return !g_eeGeneral.nb4Orientation; }); y += 50;
  selectedAction(parent, {0, y, w, 44}, STR_NB4_LANDSCAPE, [] { nb4RequestOrientation(true); }, [] { return g_eeGeneral.nb4Orientation; });
}

void nb4BuildCards(Window* parent) { nb4BuildAppearance(parent); }

Nb4HomeScreen::Nb4HomeScreen(Window* parent, const rect_t& rect) : WidgetsContainer(parent, rect, 0)
{
  setWindowFlag(OPAQUE);
  build();
}

void Nb4HomeScreen::build()
{
  clear();
  spanish = strncmp(g_eeGeneral.uiLanguage, "es", 2) == 0;
  builtWidth = width(); builtHeight = height(); builtTheme = nb4ThemeKey();
  blockedModel = nb4ModelBlocked();
  builtIdentity = modelIdentity();

  const Nb4CarState state = nb4ReadCarState();
  builtPanel = state.homeShowsRace ? 1 : state.homeTimerVisible ? 2 + state.homeTimerIndex : 0;
  const bool landscape = width() > height();
  const coord_t w = width();
  constexpr coord_t edge = 2, gap = 2;
  const coord_t fullWidth = w - 2 * edge;
  lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_SCROLLABLE);

  etx_solid_bg(lvobj, COLOR_THEME_SECONDARY3_INDEX);
  telltales[0] = telltales[1] = telltales[2] = nullptr;
  dial = nullptr; column = nullptr; chrono = nullptr; stats = nullptr;

  const std::string tags = modelLabels();
  auto headerCard = card(this, {edge, edge, fullWidth, 48});
  const coord_t nameX = 8;
  const coord_t nameW = landscape ? 132 : fullWidth - 108;
  name = label(headerCard, {nameX, tags.empty() ? (coord_t)14 : (coord_t)6, nameW, 20}, "", FONT(BOLD));
  if (!tags.empty()) {
    auto tagLabel = label(headerCard, {nameX, 27, nameW, 15}, tags.c_str(), FONT(XXS),
                          COLOR_THEME_PRIMARY3_INDEX);
    lv_obj_set_style_text_letter_space(tagLabel->getLvObj(), 1, LV_PART_MAIN);
  }

  const coord_t gearX = (coord_t)(headerCard->width() - 48);
  const coord_t wrenchX = (coord_t)(gearX - 44);
  nb4Hairline(headerCard, {(coord_t)(wrenchX - 6), 10, 1, 28});
  auto wrench = new Window(headerCard, {wrenchX, 2, 44, 44});
  lv_obj_set_style_bg_opa(wrench->getLvObj(), LV_OPA_TRANSP, 0);
  new StaticIcon(wrench, 7, 7, ICON_RADIO_TOOLS, COLOR_THEME_PRIMARY3_INDEX);

  lv_obj_add_event_cb(wrench->getLvObj(), [](lv_event_t*) {
    nb4OpenQuickAccessModal();
  }, LV_EVENT_CLICKED, nullptr);
  auto gear = new Window(headerCard, {gearX, 2, 44, 44});
  lv_obj_set_style_bg_opa(gear->getLvObj(), LV_OPA_TRANSP, 0);
  new StaticIcon(gear, 7, 7, ICON_RADIO_SETUP, COLOR_THEME_PRIMARY3_INDEX);

  lv_obj_add_event_cb(gear->getLvObj(), [](lv_event_t*) {
    nb4OpenSettingsModal();
  }, LV_EVENT_CLICKED, nullptr);

  if (nb4HealthRecovery()) {

    char banner[64];
    snprintf(banner, sizeof(banner), "%s " LV_SYMBOL_BULLET " %s",
             STR_NB4_RECOVERY, STR_NB4_LUA_DISABLED);
    label(this, {4, 58, (coord_t)(w - 8), 20}, banner, FONT(XS),
          COLOR_THEME_PRIMARY3_INDEX);
    label(this, {4, 84, (coord_t)(w - 8), 32}, "ApexTX", FONT(L));
    auto note = label(this, {4, 124, (coord_t)(w - 8), 70}, STR_NB4_BUILT_IN_MINIMAL_INTERFACE_CAR_CONFIGURA);
    lv_label_set_long_mode(note->getLvObj(), LV_LABEL_LONG_WRAP);
    action(this, {4, 208, (coord_t)(w - 8), 44}, STR_NB4_SYSTEM_DIAGNOSTICS, [] { ViewMain::instance()->openMenu(); });
    action(this, {4, 260, (coord_t)(w - 8), 44}, STR_NB4_CAR, [] { QuickMenu::openPage(QM_MODEL_SETUP); });
    name->setText(state.model);
    return;
  }

  // --- Indicators: RF, TX, and RX -------------------------------------------
  if (landscape) {
    nb4Hairline(headerCard, {148, 8, 1, 32});
    for (unsigned i = 0; i < 3; ++i) {
      const coord_t x = (coord_t)(154 + 74 * i);
      if (i) nb4Hairline(headerCard, {(coord_t)(x - 6), 8, 1, 32});
      telltales[i] = new Nb4Telltale(headerCard, {x, 0, 68, 48}, i, false);
    }
  } else {
    const coord_t usable = fullWidth - 2 * gap;
    for (unsigned i = 0; i < 3; ++i) {
      const coord_t x = edge + usable * i / 3 + gap * i;
      const coord_t cellWidth = usable * (i + 1) / 3 - usable * i / 3;
      telltales[i] = new Nb4Telltale(this, {x, 52, cellWidth, 56}, i, true);
    }
  }

  // --- Instruments ----------------------------------------------------------
  const coord_t instrumentY = landscape ? 52 : 110;
  const coord_t footerY = height() - edge - (landscape ? 58 : 122);
  const coord_t instrumentH = footerY - gap - instrumentY;
  const coord_t dialWidth = (fullWidth - gap) * 3 / 5;
  dial = new Nb4Dial(this, {edge, instrumentY, dialWidth, instrumentH}, landscape);
  column = new Nb4Column(this, {(coord_t)(edge + dialWidth + gap), instrumentY,
                              (coord_t)(fullWidth - gap - dialWidth), instrumentH}, landscape);

  // --- Lap timer and statistics ---------------------------------------------
  if (landscape) {
    auto bar = card(this, {edge, footerY, fullWidth, 58});
    chrono = new Nb4Chrono(bar, {0, 0, 202, 56}, state, true);
    auto split = nb4Hairline(bar, {202, 10, 1, 36});
    lv_obj_clear_flag(split->getLvObj(), LV_OBJ_FLAG_CLICKABLE);
    stats = new Nb4Stats(bar, {204, 0, (coord_t)(fullWidth - 206), 56}, true);
  } else {
    chrono = new Nb4Chrono(this, {edge, footerY, fullWidth, 70}, state, false);
    stats = new Nb4Stats(this, {edge, (coord_t)(footerY + 72), fullWidth, 50}, false);
  }
  Nb4Ui::passThrough(chrono->getLvObj());
  Nb4Ui::passThrough(stats->getLvObj());

  // --- Touch actions --------------------------------------------------------
  lv_obj_add_event_cb(dial->getLvObj(), [](lv_event_t*) {
    const auto channel = g_model.nb4Racing.steeringChannel;
    if (channel < MAX_OUTPUT_CHANNELS) new OutputEditWindow(channel);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(column->getLvObj(), [](lv_event_t*) {
    const auto channel = g_model.nb4Racing.throttleChannel;
    if (channel < MAX_OUTPUT_CHANNELS) new OutputEditWindow(channel);
  }, LV_EVENT_CLICKED, nullptr);

  void* chronoTarget = nullptr;
  if (!state.homeShowsRace && state.homeTimerIndex < MAX_TIMERS)
    chronoTarget = reinterpret_cast<void*>(uintptr_t(state.homeTimerIndex + 1));
  lv_obj_add_event_cb(chrono->getLvObj(), [](lv_event_t* event) {
    auto value = uintptr_t(lv_event_get_user_data(event));
    if (!value) nb4OpenSection(Nb4Section::Chrono);
    else new TimerWindow(value - 1);
  }, LV_EVENT_CLICKED, chronoTarget);

  if (blockedModel) {
    auto warning = card(this, {4, 108, (coord_t)(w - 8), (coord_t)(height() - 166)});
    label(warning, {12, 12, warning->width() - 24, 28}, STR_NB4_RF_DISABLED, FONT(BOLD), COLOR_THEME_WARNING_INDEX);
    auto detail = label(warning, {12, 48, warning->width() - 24, 100}, STR_NB4_UNSUPPORTED_MODEL_THE_ORIGINAL_FILE_IS);
    lv_label_set_long_mode(detail->getLvObj(), LV_LABEL_LONG_WRAP);
    action(warning, {12, warning->height() - 56, warning->width() - 24, 44}, STR_NB4_MODELS, [] { QuickMenu::openPage(QM_MANAGE_MODELS); });
  }
  refresh(state);
}

void Nb4HomeScreen::refresh(const Nb4CarState& state)
{
  if (name) name->setText(state.model);
  for (auto telltale : telltales) if (telltale) telltale->refresh(state);
  if (chrono) chrono->refresh(state);
  if (stats) stats->refresh(state);

}

void Nb4HomeScreen::checkEvents()
{
  WidgetsContainer::checkEvents();
  if (deleted()) return;
  const auto& state = nb4ReadCarState();
  const uint8_t panel = state.homeShowsRace ? 1 : state.homeTimerVisible ? 2 + state.homeTimerIndex : 0;
  if (builtWidth != width() || builtHeight != height() || blockedModel != nb4ModelBlocked() ||
      spanish != (strncmp(g_eeGeneral.uiLanguage, "es", 2) == 0) || builtTheme != nb4ThemeKey() ||
      builtPanel != panel || builtIdentity != modelIdentity()) build();
  else refresh(state);
}
#endif
