/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_menu_pages.h"
#include "nb4_routes.h"
#include "nb4_home.h"
#include "nb4_model_compat.h"
#include "dialog.h"
#include "menu.h"
#include "static.h"
#include "layout.h"
#include "view_main.h"
#include "quick_menu.h"
#include "hw_bluetooth.h"
#include "nb4_help.h"
#include "timer_setup.h"
#include "model_nb4_axis.h"

namespace {
class MenuPage : public BaseDialog {
 public:
  explicit MenuPage(const char* title) :
    BaseDialog(title, true, lv_disp_get_hor_res(nullptr) - 8,
               lv_disp_get_ver_res(nullptr) - 12) {
    useSectionHeader();
    setHelpHandler(nb4InheritedHelp());
    form->padAll(PAD_MEDIUM);
  }
  Window* content() { return form; }
  void note(const char* text) {
    auto label = new StaticText(form, {0, 0, LV_PCT(100), 0}, text,
                                COLOR_THEME_PRIMARY3_INDEX);
    lv_label_set_long_mode(label->getLvObj(), LV_LABEL_LONG_WRAP);
  }
  void button(const char* text, std::function<void()> action) {
    auto b = new TextButton(form, {0, 0, LV_PCT(100), 44}, text,
      [action] { action(); return 0; });
    b->setWrap();
  }
};

class HelpPage : public MenuPage {
 public:
  explicit HelpPage(const char* title) : MenuPage(title) { setHelpHandler({}); }
  bool isHelpPage() const override { return true; }
};

class QuickAccessSetup : public MenuPage {
 public:
  QuickAccessSetup() : MenuPage(STR_NB4_CONFIGURE_QUICK_ACCESS) {
    setScopeText(STR_NB4_UX_SCOPE_RADIO);
    nb4QuickAccessNormalize(); build();
  }
  void checkEvents() override {
    MenuPage::checkEvents();
    if (refresh && !deleted()) { refresh = false; build(); }
  }
 private:
  bool refresh = false;
  void choose(unsigned slot) {
    auto menu = new Menu();
    menu->setTitle(STR_NB4_UX_REPLACE);
    unsigned count;
    const auto routes = nb4Routes(&count);
    for (unsigned i = 0; i < count; ++i) {
      const auto route = &routes[i];
      if (!route->shortcut || !nb4RouteIsOpenable(*route)) continue;
      const auto id = nb4RouteId(route->path);
      bool used = false;
      for (unsigned j = 0; j < NB4_QUICK_ACCESS_COUNT; ++j)
        if (j != slot && g_eeGeneral.nb4QuickAccess[j] == id) used = true;
      if (used) continue;
      std::string title;
      unsigned sectionCount;
      const auto sections = nb4Sections(&sectionCount);
      for (unsigned j = 0; j < sectionCount; ++j) {
        if (nb4RouteInSection(*route, sections[j].id)) {
          if (strcmp(sections[j].label(), route->label())) {
            title = sections[j].label(); title += " / ";
          }
          break;
        }
      }
      title += route->label();
      menu->addLineBuffered(title, [this, slot, id] {
        nb4QuickAccessSet(slot, id); refresh = true;
      });
    }
  }
  void edit(unsigned slot) {
    auto menu = new Menu();
    const auto route = nb4RouteById(g_eeGeneral.nb4QuickAccess[slot]);
    menu->setTitle(route ? nb4QuickAccessLabel(*route).c_str() : STR_NB4_UX_EMPTY_SLOT);
    menu->addLine(STR_NB4_UX_REPLACE, [this, slot] { choose(slot); });
    if (!route) return;
    if (slot) menu->addLine(STR_NB4_UX_MOVE_UP, [this, slot] {
      nb4QuickAccessMove(slot, -1); refresh = true;
    });
    if (slot + 1 < NB4_QUICK_ACCESS_COUNT)
      menu->addLine(STR_NB4_UX_MOVE_DOWN, [this, slot] {
        nb4QuickAccessMove(slot, 1); refresh = true;
      });
    menu->addLine(STR_NB4_UX_REMOVE, [this, slot] {
      nb4QuickAccessSet(slot, 0); refresh = true;
    });
  }
  void build() {
    form->clear();
    note(STR_NB4_UX_HELP_QUICK);
    for (unsigned i = 0; i < NB4_QUICK_ACCESS_COUNT; ++i) {
      const auto route = nb4RouteById(g_eeGeneral.nb4QuickAccess[i]);
      std::string label = std::to_string(i + 1) + ". " +
        (route ? nb4QuickAccessLabel(*route) : STR_NB4_UX_EMPTY_SLOT);
      button(label.c_str(), [this, i] { edit(i); });
    }
    button(STR_NB4_UX_RESTORE_QUICK, [this] {
      new ConfirmDialog(STR_NB4_UX_RESTORE_QUICK, STR_NB4_UX_HELP_QUICK,
        [this] { nb4QuickAccessReset(); refresh = true; });
    });
  }
};

class ScreensPage : public MenuPage {
 public:
  ScreensPage() : MenuPage(STR_NB4_SCREENS) {
    setScopeText(nb4RouteScope(*nb4RouteByPath("settings/display/screens")));
    build();
  }
  void checkEvents() override {
    MenuPage::checkEvents();
    if (deleted()) return;
    unsigned count = 0;
    while (count < MAX_CUSTOM_SCREENS && customScreens[count]) ++count;
    if (count != displayedScreens) { form->clear(); build(); }
  }
 private:
  unsigned displayedScreens = 0;
  void build() {
    note(STR_NB4_UX_HELP_SCREENS);
    unsigned count = 0;
    for (; count < MAX_CUSTOM_SCREENS && customScreens[count]; ++count) {
      const unsigned index = count;
      const std::string label = index ? std::string(STR_SCREEN) + " " + std::to_string(index + 1)
                                      : STR_NB4_HOME;
      button(label.c_str(), [index] { QuickMenu::openPage((QMPage)(QM_UI_SCREEN1 + index)); });
    }
    displayedScreens = count;
    if (count < MAX_CUSTOM_SCREENS) {
      button(STR_QM_ADD_SCREEN, [this, count] {
        if (nb4ModelBlocked() || !defaultLayout) return;
        if (defaultLayout->createCustomScreen(count)) {
          storageDirty(EE_MODEL);
          form->clear(); build();
          QuickMenu::openPage((QMPage)(QM_UI_SCREEN1 + count));
        }
      });
    } else note(STR_NB4_UX_SCREENS_FULL);
  }
};
}

void nb4OpenQuickAccessSetup() { new QuickAccessSetup(); }
void nb4OpenTimers() {
  auto page = new MenuPage(STR_NB4_TIMERS_85E8);
  for (uint8_t i = 0; i < MAX_TIMERS && i < 3; ++i) {
    const auto label = std::string(STR_NB4_TIMER_BF94) + " " + std::to_string(i + 1);
    page->button(label.c_str(), [i] { new TimerWindow(i); });
  }
  page->button(STR_NB4_THROTTLE_TRACKING, nb4OpenThrottleTraceDialog);
}
void nb4OpenSessionResets() {
  auto page = new MenuPage(STR_NB4_SESSION_RESETS);
  page->note(STR_NB4_UX_HELP_RESETS);
  const char* names[] = {STR_RESET_TIMER1, STR_RESET_TIMER2, STR_RESET_TIMER3};
  for (unsigned i = 0; i < 3; ++i)
    page->button(names[i], [i, title = std::string(names[i])] {
      new ConfirmDialog(title.c_str(), STR_NB4_UX_HELP_RESETS, [i] { timerReset(i); });
    });
  page->button(STR_RESET_TELEMETRY, [] {
    new ConfirmDialog(STR_RESET_TELEMETRY, STR_NB4_UX_HELP_RESETS, [] { telemetryReset(); });
  });
}
void nb4OpenScreens() {
  if (!customScreens[0]) LayoutFactory::loadCustomScreens();
  new ScreensPage();
}
void nb4OpenAppearance() {
  auto page = new MenuPage(STR_NB4_UX_APPEARANCE);
  page->setScopeText(STR_NB4_UX_SCOPE_RADIO);
  nb4BuildAppearance(page->content());
}
void nb4OpenHelp(const char* path, bool contextual) {
  if (path) {
    if (const auto route = nb4RouteByPath(path)) {
      auto page = new HelpPage(route->label());
      page->note(nb4RouteHelp(*route));
      if (route->reason) page->note(route->reason());
      if (!contextual && nb4RouteIsOpenable(*route) && strcmp(route->path, "settings/system/help"))
        page->button(STR_NB4_UX_OPEN_SETTING, [route] {
          nb4DismissHelp(); nb4OpenRoute(route->path);
        });
      if (contextual) page->button(STR_NB4_GOT_IT, [page] { page->deleteLater(); });
      return;
    }
    const char* title = STR_NB4_HELP;
    unsigned sectionCount;
    const auto sections = nb4Sections(&sectionCount);
    for (unsigned i = 0; i < sectionCount; ++i)
      if (!strcmp(path, sections[i].id)) title = sections[i].label();
    auto page = new HelpPage(title);
    const Nb4Route* routes[32];
    const unsigned count = nb4RoutesOfSection(path, routes, 32);
    for (unsigned i = 0; i < count && i < 32; ++i) {
      const auto route = routes[i];
      page->button(route->label(), [route] { nb4OpenHelp(route->path); });
    }
    return;
  }
  auto page = new HelpPage(STR_NB4_HELP);
  page->note(STR_NB4_UX_HELP_INDEX);
  unsigned count;
  const auto sections = nb4Sections(&count);
  for (unsigned i = 0; i < count; ++i) {
    const auto section = &sections[i];
    page->button(section->label(), [section] { nb4OpenHelp(section->id); });
  }
}

void nb4OpenBluetooth() {
#if defined(BLUETOOTH)
  auto page = new MenuPage(STR_NB4_BLUETOOTH);
  static const lv_coord_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static const lv_coord_t rows[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
  FlexGridLayout grid(cols, rows, PAD_SMALL);
  new BluetoothConfigWindow(page->content(), grid);
#endif
}
#endif
