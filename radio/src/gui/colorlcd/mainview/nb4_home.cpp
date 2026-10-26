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
#include "menu.h"
#include "quick_menu.h"
#include "nb4_routes.h"
#include "view_main.h"
#include "view_channels.h"
#include "hal/usb_driver.h"

namespace {
int pendingOrientation = -1;
bool reopenAfterOrientation = true;
unsigned orientationAttempts = 0;

// Switching orientation has to cancel any gesture in flight and unwind the
// open pages before the views are rebuilt. Both are safe once; repeated on
// every UI iteration they cancel the user's every touch, which is
// indistinguishable from a frozen screen. So the request is given a bounded
// number of attempts and then dropped: not changing orientation is a far
// better outcome than a radio that ignores its own screen.
constexpr unsigned OrientationAttemptLimit = 8;

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

void appearanceControl(lv_obj_t* o)
{
  using namespace Nb4Ui;
  button(o);
  for (lv_state_t state : {lv_state_t(0), lv_state_t(LV_STATE_FOCUSED),
      lv_state_t(LV_STATE_FOCUS_KEY), lv_state_t(LV_STATE_CHECKED),
      lv_state_t(LV_STATE_PRESSED)}) {
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, state);
    lv_obj_set_style_border_width(o, 1, state);
    lv_obj_set_style_border_side(o, LV_BORDER_SIDE_FULL, state);
    lv_obj_set_style_outline_width(o, 0, state);
    lv_obj_set_style_shadow_width(o, 0, state);
  }
  // A solid inset control keeps the same rounded edge when focus moves.
  // Feedback changes its border/fill, without blending over its neighbours.
  etx_bg_color(o, COLOR_THEME_SECONDARY3_INDEX, LV_STATE_FOCUSED);
  etx_bg_color(o, COLOR_THEME_SECONDARY2_INDEX, LV_STATE_PRESSED);
  etx_border_color(o, COLOR_THEME_FOCUS_INDEX, LV_STATE_FOCUSED);
  etx_txt_color(o, COLOR_THEME_PRIMARY1_INDEX);
  lv_obj_set_style_color_filter_opa(o, LV_OPA_TRANSP, LV_STATE_PRESSED);
}

class AppearanceChoice : public Choice
{
 public:
  AppearanceChoice(Window* parent, int last, std::function<int()> get,
                   std::function<void(int)> set, const char* title) :
      Choice(parent, {0, 0, LV_PCT(100), Nb4Ui::Touch}, 0, last,
             std::move(get), std::move(set), title)
  {
    appearanceControl(lvobj);
    setFlexLayout(LV_FLEX_FLOW_ROW, Nb4Ui::SpaceS, LV_PCT(100), Nb4Ui::Touch);
    padLeft(PAD_LARGE);
    padRight(PAD_LARGE);
    padTop(PAD_ZERO);
    padBottom(PAD_ZERO);
    lv_obj_set_flex_align(lvobj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_to_index(label, 0);
    lv_obj_set_width(label, 0);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    setPopupWidth(lv_disp_get_hor_res(nullptr) - 2 * Nb4Ui::Margin);
    setFillMenuHandler([](Menu* menu, int, int&) {
      menu->setMinimumRowHeight(Nb4Ui::Touch);
      menu->setTitleColors(COLOR_THEME_HEADER_BG_INDEX, COLOR_THEME_HEADER_FG_INDEX);
    });
  }
};

TextButton* selectedAction(Window* p, rect_t r, const char* text, std::function<void()> fn,
                    std::function<bool()> selected)
{
  auto b = action(p, r, text, fn);

  auto o = b->getLvObj();
  appearanceControl(o);
  for (lv_state_t state : {lv_state_t(LV_STATE_CHECKED),
      lv_state_t(LV_STATE_CHECKED | LV_STATE_FOCUSED),
      lv_state_t(LV_STATE_CHECKED | LV_STATE_FOCUS_KEY),
      lv_state_t(LV_STATE_CHECKED | LV_STATE_PRESSED)}) {
    etx_bg_color(o, COLOR_THEME_FOCUS_INDEX, state);
    etx_border_color(o, COLOR_THEME_FOCUS_INDEX, state);
  }
  // Inherit the state colour instead of keeping action()'s fixed label ink.
  etx_remove_txt_color(lv_obj_get_child(o, 0), 0);
  b->setCheckHandler([b, selected, lastInk = uint32_t(-1)]() mutable {
    bool active = selected();
    if (active != b->checked()) b->check(active);
    const uint32_t ink = lv_color_brightness(Nb4Ui::color(COLOR_THEME_FOCUS_INDEX)) > 140
        ? 0x071015 : 0xFFFFFF;
    if (ink != lastInk) {
      lv_obj_set_style_text_color(b->getLvObj(), lv_color_hex(ink), LV_STATE_CHECKED);
      lastInk = ink;
    }
  });
  return b;
}

}  // namespace

// The home screen's header for every other screen of the car: its name and
// the two buttons into the menus, so no screen layout can lock them away.
StaticText* nb4ScreenHeader(Window* parent, rect_t rect)
{
  auto header = card(parent, rect);
  const std::string tags = modelLabels();
  const coord_t nameW = rect.w - 108;
  auto name = label(header, {8, tags.empty() ? (coord_t)14 : (coord_t)6, nameW, 20},
                    nb4ReadCarState().model, FONT(BOLD));
  if (!tags.empty()) {
    auto tagLabel = label(header, {8, 27, nameW, 15}, tags.c_str(), FONT(XXS),
                          COLOR_THEME_PRIMARY3_INDEX);
    lv_obj_set_style_text_letter_space(tagLabel->getLvObj(), 1, LV_PART_MAIN);
  }
  const coord_t gearX = rect.w - 48;
  const coord_t wrenchX = gearX - 44;
  nb4Hairline(header, {(coord_t)(wrenchX - 6), 10, 1, 28});
  auto wrench = new Window(header, {wrenchX, 2, 44, 44});
  lv_obj_set_style_bg_opa(wrench->getLvObj(), LV_OPA_TRANSP, 0);
  new StaticIcon(wrench, 7, 7, ICON_RADIO_TOOLS, COLOR_THEME_PRIMARY3_INDEX);
  lv_obj_add_event_cb(wrench->getLvObj(), [](lv_event_t*) {
    nb4OpenQuickAccessModal();
  }, LV_EVENT_CLICKED, nullptr);
  auto gear = new Window(header, {gearX, 2, 44, 44});
  lv_obj_set_style_bg_opa(gear->getLvObj(), LV_OPA_TRANSP, 0);
  new StaticIcon(gear, 7, 7, ICON_RADIO_SETUP, COLOR_THEME_PRIMARY3_INDEX);
  lv_obj_add_event_cb(gear->getLvObj(), [](lv_event_t*) {
    nb4OpenSettingsModal();
  }, LV_EVENT_CLICKED, nullptr);
  return name;
}

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
      section == Nb4Section::Chrono || section == Nb4Section::Pit || section == Nb4Section::Backup || section == Nb4Section::Reset) {
    nb4OpenDataPage(section);
    return;
  }
  switch (section) {
    case Nb4Section::Car: nb4OpenRoute("settings/car/general"); break;
    case Nb4Section::Race: nb4OpenRoute("settings/race/setup"); break;
    case Nb4Section::Steering:
      nb4OpenRoute("settings/steering/travel");
      break;
    case Nb4Section::Throttle:
      nb4OpenRoute("settings/throttle_brake/travel");
      break;
    case Nb4Section::Auxiliary: QuickMenu::openPage(QM_MODEL_MIXES); break;
    case Nb4Section::Advanced: nb4OpenSettingsSection("advanced"); break;
    case Nb4Section::System: nb4OpenSettingsSection("system"); break;

    case Nb4Section::Appearance:
    case Nb4Section::Cards: nb4OpenAppearance(); break;
    default: break;
  }
}

void nb4RequestOrientation(bool landscape, bool reopenAppearance)
{
  pendingOrientation = landscape ? 1 : 0;
  reopenAfterOrientation = reopenAppearance;
  orientationAttempts = 0;
}

bool nb4OrientationChangePending() { return pendingOrientation >= 0; }

void nb4ProcessOrientation()
{
  if (pendingOrientation < 0) return;
  auto display = lv_disp_get_default();
  // Mid-flush is an ordinary transient and costs nothing to retry, so it does
  // not spend an attempt and must stay clear of the side effects below.
  if (!display || lv_disp_get_draw_buf(display)->flushing) return;
  if (++orientationAttempts > OrientationAttemptLimit) {
    pendingOrientation = -1;
    orientationAttempts = 0;
    return;
  }
  // Resolved before anything is cancelled: instance() creates the main view on
  // first use and pushes a layer, and creating it mid-unwind would put it on
  // top of the very pages being closed and end the walk early.
  auto main = ViewMain::instance();
  // Finish native live edits and cancel gestures before rebuilding any view.
  Keyboard::hide(false);
  lv_indev_reset(nullptr, nullptr);
  // This runs between UI iterations, never inside a control's callback.
  for (unsigned i = 0; i < 32 && Layer::back() && Layer::back() != main; ++i) {
    auto page = Layer::back();
    page->onCancel();
    // A page that refuses to close, or that reopens another in its place, is
    // retried on the next iteration rather than spun on here.
    if (Layer::back() == page) return;
  }
  if (Layer::back() != main) return;
  if (!lcdSetOrientation(pendingOrientation != 0)) return;
  if (!nb4HealthRecovery()) {
    g_eeGeneral.nb4Orientation = pendingOrientation;
    storageDirty(EE_GENERAL);
  }
  pendingOrientation = -1;
  orientationAttempts = 0;
  main->resizeToDisplay();
  nb4ReloadThemeBackground();
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
  using namespace Nb4Ui;
  parent->padAll(PAD_MEDIUM);
  parent->setFlexLayout(LV_FLEX_FLOW_COLUMN, SpaceM, LV_PCT(100), LV_SIZE_CONTENT);
  const bool wide = lv_disp_get_hor_res(nullptr) > lv_disp_get_ver_res(nullptr);
  auto fields = new Window(parent, {0, 0, LV_PCT(100), LV_SIZE_CONTENT});
  fields->setFlexLayout(wide ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN,
                       SpaceM, LV_PCT(100), LV_SIZE_CONTENT);
  auto field = [fields, wide](const char* title) {
    auto group = new Window(fields, {0, 0, wide ? 0 : LV_PCT(100), LV_SIZE_CONTENT});
    group->setFlexLayout(LV_FLEX_FLOW_COLUMN, SpaceXS,
                          wide ? 0 : LV_PCT(100), LV_SIZE_CONTENT);
    if (wide) lv_obj_set_flex_grow(group->getLvObj(), 1);
    caption(group, {0, 0, LV_PCT(100), CaptionH}, title);
    return group;
  };
  auto palettes = new AppearanceChoice(field(STR_NB4_PALETTE), nb4PaletteCount() - 1,
    [] { return nb4PaletteIndexByName(g_eeGeneral.selectedTheme); },
    [](int value) { applyPaletteByName(nb4Palette(value).name); }, STR_NB4_PALETTE);
  palettes->setTextHandler([](int value) {
    // External themes have no built-in index; show their actual saved name
    // until the user explicitly chooses a built-in palette.
    return value >= 0 && unsigned(value) < nb4PaletteCount()
        ? std::string(nb4Palette(value).label()) : std::string(g_eeGeneral.selectedTheme);
  });
  auto choice = new AppearanceChoice(field(STR_NB4_ACCENT), nb4AccentCount() - 1,
    [] { return int(g_eeGeneral.nb4Accent); }, [](int value) {
      g_eeGeneral.nb4Accent = value; storageDirty(EE_GENERAL);
      applyPaletteByName(g_eeGeneral.selectedTheme);
    }, STR_NB4_ACCENT);
  choice->setTextHandler([](int value) { return std::string(nb4AccentName(value)); });
  auto orientation = new Window(parent, {0, 0, LV_PCT(100), LV_SIZE_CONTENT});
  orientation->setFlexLayout(LV_FLEX_FLOW_COLUMN, SpaceXS, LV_PCT(100), LV_SIZE_CONTENT);
  caption(orientation, {0, 0, LV_PCT(100), CaptionH}, STR_NB4_ORIENTATION);
  auto row = new Window(orientation, {0, 0, LV_PCT(100), Touch});
  row->setFlexLayout(LV_FLEX_FLOW_ROW, Gap, LV_PCT(100), Touch);
  auto portrait = selectedAction(row, {0, 0, 0, Touch}, STR_NB4_PORTRAIT,
    [] { nb4RequestOrientation(false); }, [] { return !g_eeGeneral.nb4Orientation; });
  auto landscape = selectedAction(row, {0, 0, 0, Touch}, STR_NB4_LANDSCAPE,
    [] { nb4RequestOrientation(true); }, [] { return bool(g_eeGeneral.nb4Orientation); });
  lv_obj_set_flex_grow(portrait->getLvObj(), 1);
  lv_obj_set_flex_grow(landscape->getLvObj(), 1);
  auto external = Nb4Ui::action(parent, {0, 0, LV_PCT(100), Touch},
    STR_NB4_EXTERNAL_THEMES, [] { QuickMenu::openPage(QM_UI_THEMES); });
  appearanceControl(external->getLvObj());
}

// The PC theme editor works on /THEMES, /RADIO and /MODELS over USB storage.
// Only this connection (or the next one) opens as storage; the USB mode in
// System > USB stays as it is, and the radio restarts after the session.
void nb4ConnectThemeEditor()
{
#if !defined(SIMU)
  if (usbStarted() && getSelectedUsbMode() == USB_MASS_STORAGE_MODE) {
    new MessageDialog(STR_NB4_THEME_EDITOR, STR_NB4_THEME_EDITOR_CONNECTED,
                      nullptr, LEFT);
    return;
  }
  nb4UsbSelectMassStorage();
  // With the cable in, the main loop switches to storage right away.
  if (usbPlugged()) return;
#endif
  new MessageDialog(STR_NB4_THEME_EDITOR, STR_NB4_THEME_EDITOR_PLUG_USB,
                    nullptr, LEFT);
}

void nb4BuildCards(Window* parent) { nb4BuildAppearance(parent); }

Nb4HomeScreen::Nb4HomeScreen(Window* parent, const rect_t& rect, bool instruments) :
    WidgetsContainer(parent, rect, 0), instruments(instruments)
{
  setWindowFlag(OPAQUE);
  build();
}

void Nb4HomeScreen::build()
{
  clear();
  memcpy(builtLanguage, g_eeGeneral.uiLanguage, sizeof(builtLanguage));
  builtWidth = width(); builtHeight = height(); builtTheme = nb4ThemeKey();
  blockedModel = nb4ModelBlocked();
  builtIdentity = modelIdentity();

  const Nb4CarState state = nb4UiCarState();
  builtPanel = state.homeShowsRace ? 1 : state.homeTimerVisible ? 2 + state.homeTimerIndex : 0;
  const bool landscape = width() > height();
  const coord_t w = width();
  constexpr coord_t edge = Nb4Ui::SpaceXS, gap = Nb4Ui::SpaceXS;
  // The header card holds 44 px buttons inside its one-pixel border.
  constexpr coord_t headerH = Nb4Ui::Touch + 2;
  const coord_t fullWidth = w - 2 * edge;
  lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_SCROLLABLE);

  etx_solid_bg(lvobj, COLOR_THEME_SECONDARY3_INDEX);
  // Let the theme's background image show around the cards.
  if (Nb4Ui::backgroundShown()) lv_obj_set_style_bg_opa(lvobj, LV_OPA_TRANSP, 0);
  telltales[0] = telltales[1] = telltales[2] = nullptr;
  dial = nullptr; column = nullptr; chrono = nullptr; stats = nullptr;

  const std::string tags = modelLabels();
  auto headerCard = card(this, {edge, edge, fullWidth, headerH});
  const coord_t nameX = Nb4Ui::SpaceS;
  const coord_t nameW = landscape ? 132 : fullWidth - 108;
  name = label(headerCard, {nameX, tags.empty() ? (coord_t)0 : (coord_t)4, nameW,
                            tags.empty() ? Nb4Ui::Touch : (coord_t)20}, "", FONT(BOLD));
  if (!tags.empty())
    Nb4Ui::caption(headerCard, {nameX, 24, nameW, Nb4Ui::CaptionH}, tags.c_str());

  const coord_t gearX = (coord_t)(headerCard->width() - 2 - Nb4Ui::Touch);
  const coord_t wrenchX = (coord_t)(gearX - Nb4Ui::Touch);
  nb4Hairline(headerCard, {(coord_t)(wrenchX - Nb4Ui::SpaceXS), 8, 1, 28});
  auto wrench = new Window(headerCard, {wrenchX, 0, Nb4Ui::Touch, Nb4Ui::Touch});
  lv_obj_set_style_bg_opa(wrench->getLvObj(), LV_OPA_TRANSP, 0);
  Nb4Ui::centreIcon(new StaticIcon(wrench, 0, 0, ICON_RADIO_TOOLS, COLOR_THEME_PRIMARY3_INDEX),
                    wrench->getLvObj());

  lv_obj_add_event_cb(wrench->getLvObj(), [](lv_event_t*) {
    nb4OpenQuickAccessModal();
  }, LV_EVENT_CLICKED, nullptr);
  auto gear = new Window(headerCard, {gearX, 0, Nb4Ui::Touch, Nb4Ui::Touch});
  lv_obj_set_style_bg_opa(gear->getLvObj(), LV_OPA_TRANSP, 0);
  Nb4Ui::centreIcon(new StaticIcon(gear, 0, 0, ICON_RADIO_SETUP, COLOR_THEME_PRIMARY3_INDEX),
                    gear->getLvObj());

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
    Nb4Ui::paragraph(this, {4, 124, (coord_t)(w - 8), 70}, STR_NB4_BUILT_IN_MINIMAL_INTERFACE_CAR_CONFIGURA);
    action(this, {4, 208, (coord_t)(w - 8), 44}, STR_NB4_SYSTEM_DIAGNOSTICS, [] { ViewMain::instance()->openMenu(); });
    action(this, {4, 260, (coord_t)(w - 8), 44}, STR_NB4_CAR, [] { QuickMenu::openPage(QM_MODEL_SETUP); });
    name->setText(state.model);
    return;
  }

  // --- Indicators: RF, TX, and RX -------------------------------------------
  if (landscape) {
    nb4Hairline(headerCard, {148, 6, 1, 32});
    for (unsigned i = 0; i < 3; ++i) {
      const coord_t x = (coord_t)(156 + 74 * i);
      if (i) nb4Hairline(headerCard, {(coord_t)(x - 6), 6, 1, 32});
      telltales[i] = new Nb4Telltale(headerCard, {x, 0, 68, Nb4Ui::Touch}, i, false);
    }
  } else {
    const coord_t usable = fullWidth - 2 * gap;
    for (unsigned i = 0; i < 3; ++i) {
      const coord_t x = edge + usable * i / 3 + gap * i;
      const coord_t cellWidth = usable * (i + 1) / 3 - usable * i / 3;
      telltales[i] = new Nb4Telltale(this, {x, (coord_t)(edge + headerH + gap), cellWidth, 56}, i, true);
    }
  }

  if (!instruments) { refresh(state); return; }

  // --- Instruments ----------------------------------------------------------
  const coord_t instrumentY = edge + headerH + gap + (landscape ? 0 : 56 + gap);
  constexpr coord_t chronoH = 68, statsH = 48, barH = 56;
  const coord_t footerY = height() - edge - (landscape ? barH : chronoH + gap + statsH);
  const coord_t instrumentH = footerY - gap - instrumentY;
  const coord_t dialWidth = (fullWidth - gap) * 3 / 5;
  dial = new Nb4Dial(this, {edge, instrumentY, dialWidth, instrumentH}, landscape);
  column = new Nb4Column(this, {(coord_t)(edge + dialWidth + gap), instrumentY,
                              (coord_t)(fullWidth - gap - dialWidth), instrumentH}, landscape);

  // --- Lap timer and statistics ---------------------------------------------
  if (landscape) {
    auto bar = card(this, {edge, footerY, fullWidth, barH});
    chrono = new Nb4Chrono(bar, {0, 0, 202, barH - 2}, state, true);
    auto split = nb4Hairline(bar, {202, 9, 1, 36});
    lv_obj_clear_flag(split->getLvObj(), LV_OBJ_FLAG_CLICKABLE);
    stats = new Nb4Stats(bar, {204, 0, (coord_t)(fullWidth - 206), barH - 2}, true);
  } else {
    chrono = new Nb4Chrono(this, {edge, footerY, fullWidth, chronoH}, state, false);
    stats = new Nb4Stats(this, {edge, (coord_t)(footerY + chronoH + gap), fullWidth, statsH}, false);
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
    Nb4Ui::paragraph(warning, {12, 48, warning->width() - 24, 100}, STR_NB4_UNSUPPORTED_MODEL_THE_ORIGINAL_FILE_IS);
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
  const auto& state = nb4UiCarState();
  const uint8_t panel = state.homeShowsRace ? 1 : state.homeTimerVisible ? 2 + state.homeTimerIndex : 0;
  if (builtWidth != width() || builtHeight != height() || blockedModel != nb4ModelBlocked() ||
      memcmp(builtLanguage, g_eeGeneral.uiLanguage, sizeof(builtLanguage)) ||
      builtTheme != nb4ThemeKey() ||
      builtPanel != panel || builtIdentity != modelIdentity()) build();
  else refresh(state);
}
#endif
