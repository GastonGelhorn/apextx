/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "edgetx.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_home.h"
#include "nb4_ui.h"
#include "nb4_history.h"
#include "nb4_pit.h"
#include "mainwindow.h"
#include "quick_menu.h"
#include "model_telemetry.h"
#include "numberedit.h"
#include "dialog.h"
#include "nb4_help.h"
#include "nb4_routes.h"
#include "theme_manager.h"
#include <array>

namespace {
const char* storageText() {
  switch (nb4HistoryStatus()) {
    case Nb4HistoryStatus::Pending: return STR_NB4_RESULT_PENDING;
    case Nb4HistoryStatus::Saving: return STR_NB4_SAVING_RUN;
    case Nb4HistoryStatus::Saved:
      return nb4RacePhase() == Nb4RacePhase::Finished
        ? STR_NB4_RUN_SAVED
        : STR_NB4_VIEW_RACE_HISTORY;
    case Nb4HistoryStatus::Unavailable: return STR_NB4_STORAGE_UNAVAILABLE_RETRY;
    case Nb4HistoryStatus::Failed: return STR_NB4_SAVE_FAILED_RETRY;
    case Nb4HistoryStatus::Full: return STR_NB4_SAVE_PENDING_RUNS_FIRST;
    default: return STR_NB4_VIEW_RACE_HISTORY;
  }
}
class DataPage : public NavWindow {
 public:

  DataPage(const char* title, const char* routePath) : NavWindow(MainWindow::instance(), {0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)}) {
    helpHandler = nb4InheritedHelp();
    pushLayer();
    etx_solid_bg(lvobj, COLOR_THEME_SECONDARY3_INDEX);
    Nb4Ui::header(this, title, [this] { onCancel(); });
    helpButton = Nb4Ui::action(this, {coord_t(width() - 124), (Nb4Ui::HeaderH - Nb4Ui::Touch) / 2, Nb4Ui::Touch, Nb4Ui::Touch}, "?",
      [this] { if (helpHandler) helpHandler(); });
    Nb4Ui::help(helpButton->getLvObj());
    using namespace Nb4Ui;
    const coord_t top = HeaderH + SpaceXS;
    body = new Window(this, {Margin, top, width() - 2 * Margin, height() - top});
    body->setFlexLayout(LV_FLEX_FLOW_COLUMN, Gap, width() - 2 * Margin, height() - top);
    body->padBottom(Margin);
    lv_obj_set_scroll_dir(body->getLvObj(), LV_DIR_VER);
    if (const auto route = nb4RouteByPath(routePath)) {
      setRouteIcon(nb4RouteIcon(*route));
      setScopeText(nb4RouteScope(*route));
      setHelpHandler([route] { nb4OpenHelp(route->path, true); });
    }
  }
#if defined(HARDWARE_KEYS)
  void onPressSYS() override { nb4Navigate(Nb4Section::System); }
  void onLongPressSYS() override { nb4Navigate(Nb4Section::Appearance); }
  void onPressMDL() override { nb4Navigate(Nb4Section::Car); }
  void onLongPressMDL() override { nb4Navigate(Nb4Section::Chrono); }
  void onPressTELE() override { nb4Navigate(Nb4Section::Telemetry); }
  void onLongPressTELE() override { nb4Navigate(Nb4Section::History); }
  void onLongPressRTN() override { onCancel(); }
#endif
  void onCancel() override { deleteLater(); }
  bool setHelpHandler(std::function<void()> action) override {
    helpHandler = std::move(action);
    helpButton->show(bool(helpHandler));
    return true;
  }
  std::function<void()> getHelpHandler() const override { return helpHandler; }
  void setScopeText(const std::string& text) override {
    scopeText = text;
    nb4ScopeBar(this, body, scopeLabel, text);
  }
  const std::string& getScopeText() const override { return scopeText; }
  void setRouteIcon(uint8_t icon) override {
    if (!routeIcon) routeIcon = new HeaderIcon(this, (EdgeTxIcon)icon);
    else routeIcon->setIcon((EdgeTxIcon)icon);
  }
 protected:
  Window* body;
  TextButton* helpButton;
  StaticText* scopeLabel = nullptr;
  HeaderIcon* routeIcon = nullptr;
  std::string scopeText;
  std::function<void()> helpHandler;
};
// A full-width row of controls that share the width equally.
Window* actionRow(Window* parent, coord_t height = Nb4Ui::Touch)
{
  auto row = new Window(parent, {0, 0, LV_PCT(100), height});
  row->setFlexLayout(LV_FLEX_FLOW_ROW, Nb4Ui::Gap, LV_PCT(100), height);
  return row;
}

TextButton* grow(TextButton* button)
{
  lv_obj_set_flex_grow(button->getLvObj(), 1);
  return button;
}

constexpr coord_t LapRow = 32, LapPitch = LapRow + Nb4Ui::SpaceXS, LapHead = 24;

void lapTable(Window* parent, const uint32_t* times, unsigned count, uint32_t best) {
  const coord_t w = parent->width();
  using namespace Nb4Ui;
  const coord_t timeX = 64, deltaW = w / 2 - 2 * SpaceS;
  caption(parent, {SpaceS, 0, 48, LapHead}, STR_NB4_LAP);
  caption(parent, {timeX, 0, w / 2 - timeX, LapHead}, STR_NB4_TIME);
  caption(parent, {w - SpaceS - deltaW, 0, deltaW, LapHead}, "+/-", RIGHT);
  char text[24];
  for (unsigned row = 0; row < count && row < NB4_MAX_LAPS; ++row) {
    unsigned i = count - row - 1;
    coord_t y = LapHead + row * LapPitch;
    if (!(row % 2)) well((new Window(parent, {0, y, w, LapRow}))->getLvObj());
    snprintf(text, sizeof(text), "%02u", i + 1);
    nb4Label(parent, {SpaceS, y, 48, LapRow}, text);
    nb4RacingFormatTime(text, times[i]);
    nb4Label(parent, {timeX, y, w / 2 - timeX, LapRow}, text, TypeBody, times[i] == best ? COLOR_THEME_EDIT_INDEX : COLOR_THEME_PRIMARY1_INDEX);
    snprintf(text, sizeof(text), "+%lu.%02lu", (unsigned long)((times[i] - best) / 100), (unsigned long)((times[i] - best) % 100));
    nb4Label(parent, {w - SpaceS - deltaW, y, deltaW, LapRow}, times[i] == best ? "--" : text, TypeBody | RIGHT, times[i] == best ? COLOR_THEME_PRIMARY3_INDEX : COLOR_THEME_WARNING_INDEX);
  }
}

coord_t lapTableHeight(unsigned count) { return LapHead + count * LapPitch; }
class RacePage : public DataPage {
 public:
  RacePage() : DataPage(STR_NB4_TIMERS_LAPS, "settings/race/timer_laps") {
    panel = new Nb4RacePanel(body, {0, 0, body->width(), 120});
    auto first = actionRow(body);
    start = grow(Nb4Ui::action(first, {}, "", [this] {
      if (nb4RacePhase() == Nb4RacePhase::Running) nb4RaceFinish();
      else if (!nb4RaceStart()) new MessageDialog(STR_NB4_RACE_5527, STR_NB4_SAVE_PENDING_RESULTS_THEN_RETRY);
    }, true));
    lap = grow(Nb4Ui::action(first, {}, STR_NB4_MARK_LAP, [] { nb4RacingMarkLap(); }));
    auto second = actionRow(body);
    undo = grow(Nb4Ui::action(second, {}, STR_NB4_UNDO_LAP, [] { nb4RacingUndoLap(); }));
    grow(Nb4Ui::action(second, {}, STR_NB4_SETUP, [] { nb4OpenRoute("settings/race/setup"); }));
    auto retry = Nb4Ui::action(body, {0, 0, body->width(), Nb4Ui::Touch}, "", [] {
      const auto saved = nb4HistorySavedId(nb4RaceResultToken());
      if (nb4RacePhase() == Nb4RacePhase::Finished && saved) nb4OpenRaceRecord(saved);
      else if (nb4RacePhase() != Nb4RacePhase::Finished &&
               (nb4HistoryStatus() == Nb4HistoryStatus::Idle || nb4HistoryStatus() == Nb4HistoryStatus::Saved)) nb4OpenSection(Nb4Section::History);
      else nb4HistoryRetry();
    });
    statusLabel = Nb4Ui::paragraph(retry, {Nb4Ui::SpaceS, Nb4Ui::SpaceXS, retry->width() - 2 * Nb4Ui::SpaceS, 36}, "", FONT(XS));
    table = new Window(body, {0, 0, body->width(), 1});
    checkEvents();
  }
  void checkEvents() override {
    if (deleted()) return;
    DataPage::checkEvents();
    if (deleted()) return;
    panel->refresh(nb4UiCarState());
    auto phase = nb4RacePhase();
    start->setText(phase == Nb4RacePhase::Running ? STR_NB4_FINISH : phase == Nb4RacePhase::Finished ? STR_NB4_NEW_RUN : STR_NB4_START);
    lap->enable(phase == Nb4RacePhase::Running);
    undo->enable(phase == Nb4RacePhase::Running && nb4RacingLaps() > 0);
    statusLabel->setText(phase == Nb4RacePhase::Finished &&
        nb4HistorySavedId(nb4RaceResultToken()) ? STR_NB4_UX_SAVED_RESULT : storageText());
    if (previousCount != nb4RacingLaps()) {
      previousCount = nb4RacingLaps();
      table->clear(); table->setHeight(lapTableHeight(previousCount));
      uint32_t times[NB4_MAX_LAPS];
      for (unsigned i = 0; i < previousCount; ++i) times[i] = nb4RacingLapTime(i);
      lapTable(table, times, previousCount, nb4RacingBestLap());
    }
  }
 private:
  Nb4RacePanel* panel;
  TextButton *start, *lap, *undo;
  StaticText* statusLabel;
  Window* table;
  unsigned previousCount = 100;
};
class PitPage : public DataPage {
 public:
  PitPage() : DataPage(STR_NB4_PIT, "settings/race/pit") {
    using namespace Nb4Ui;
    auto card = Nb4Ui::card(body, {0, 0, body->width(), 140});
    const coord_t inner = card->width() - 2 * SpaceM;
    caption(card, {SpaceM, SpaceS, inner, CaptionH}, STR_NB4_FUEL_PACK);
    remaining = nb4Label(card, {SpaceM, 32, inner, 60}, "--", FONT(LXL));
    info = nb4Label(card, {SpaceM, 100, inner, 28}, "", TypeBody);
    auto duration = actionRow(body);
    auto name = nb4Label(duration, {0, 0, 0, Touch}, STR_NB4_DURATION_MIN);
    lv_obj_set_flex_grow(name->getLvObj(), 1);
    new NumberEdit(duration, {0, 0, 108, Touch}, 0, NB4_PIT_MAX_MINUTES,
      [] { return int(nb4PitMinutes()); }, [](int v) { nb4PitConfigure(v); });

    refuel = Nb4Ui::action(body, {0, 0, body->width(), Touch}, STR_NB4_REFUELLED_NEW_PACK, [] { nb4PitRefuel(); }, true);
    paragraph(body, {SpaceS, 0, body->width() - 2 * SpaceS, LV_SIZE_CONTENT}, STR_NB4_TIMER_2_COUNTDOWN_RX_SUPPLY_IS, TypeSmall, COLOR_THEME_PRIMARY3_INDEX);
    checkEvents();
  }
  void checkEvents() override {
    if (deleted()) return;
    DataPage::checkEvents();
    if (deleted()) return;
    const int32_t secs = nb4PitRemaining();
    auto text = Nb4Ui::timeText(uint32_t(abs(secs)) * 100, nb4PitEnabled());
    remaining->setText(secs < 0 && nb4PitEnabled() ? "-" + text : text);
    etx_txt_color(remaining->getLvObj(), nb4PitEnabled() && secs <= 0 ? COLOR_THEME_WARNING_INDEX : COLOR_THEME_PRIMARY1_INDEX);
    char s[64]; int laps = nb4PitLapsLeft();
    if (!nb4PitEnabled()) info->setText(STR_NB4_SET_A_DURATION);
    else if (laps < 0) info->setText(STR_NB4_LAPS_LEFT);
    else { snprintf(s, sizeof(s), "%s %d", STR_NB4_ESTIMATED_LAPS, laps); info->setText(s); }
    refuel->enable(nb4PitEnabled());
  }
 private: StaticText *remaining, *info; TextButton* refuel = nullptr;
};
class HistoryPage : public DataPage {
 public:
  HistoryPage(uint32_t id = 0) : DataPage(STR_NB4_HISTORY, "settings/race/history"), detailId(id) {
    content = new Window(body, {0, 0, body->width(), LV_SIZE_CONTENT});
    content->setFlexLayout(LV_FLEX_FLOW_COLUMN, Nb4Ui::Gap, body->width(), LV_SIZE_CONTENT);
    request();
  }
  void request() {
    content->clear();
    nb4Label(content, {Nb4Ui::SpaceS, 0, content->width() - 2 * Nb4Ui::SpaceS, Nb4Ui::Touch}, STR_NB4_LOADING);
    token = 0; waiting = true;
  }
  void checkEvents() override {
    if (deleted()) return;
    DataPage::checkEvents();
    if (deleted()) return;
    if (!waiting) return;
    if (!token) token = nb4HistoryRequest(detailId ? detailId : before, detailId != 0);
    if (token && nb4HistoryPoll(token, view)) { waiting = false; build(); }
  }
 private:
  Window* content;
  Nb4HistoryView view{};
  uint32_t token = 0, before = 0, detailId;
  bool waiting = false;
  void build() {
    using namespace Nb4Ui;
    content->clear();
    const coord_t w = content->width(), inner = w - 2 * SpaceM;
    if (view.error) {
      paragraph(content, {SpaceS, 0, w - 2 * SpaceS, LV_SIZE_CONTENT}, STR_NB4_HISTORY_COULD_NOT_BE_READ_CHECK);
    } else if (detailId && view.count) {
      const auto& r = view.records[0];
      auto card = Nb4Ui::card(content, {0, 0, w, 152});
      nb4Label(card, {SpaceM, SpaceS, inner, 32}, r.model, TypeValue);
      nb4Label(card, {SpaceM, 44, inner, 56}, Nb4Ui::timeText(r.duration).c_str(), FONT(LXL));
      char s[64]; snprintf(s, sizeof(s), "#%lu • %u %s", (unsigned long)r.id, r.laps, STR_NB4_LAPS_8959);
      nb4Label(card, {SpaceM, 100, inner, 24}, s);
      if (r.date) {
        struct gtm date; gtime_t timestamp = r.date; filltm(&timestamp, &date);
        snprintf(s, sizeof(s), "%04u-%02u-%02u %02u:%02u", date.tm_year + 1900, date.tm_mon + 1, date.tm_mday, date.tm_hour, date.tm_min);
      } else strAppend(s, STR_NB4_DATE_UNAVAILABLE);
      nb4Label(card, {SpaceM, 124, inner, 20}, s, TypeSmall, COLOR_THEME_PRIMARY3_INDEX);
      auto rows = new Window(content, {0, 0, w, lapTableHeight(r.laps)});
      lapTable(rows, r.times, r.laps, r.best);
    } else {
      if (!view.count) nb4Label(content, {SpaceS, 0, w - 2 * SpaceS, Touch}, STR_NB4_NO_SAVED_RUNS_YET);
      for (unsigned i = 0; i < view.count; ++i) {
        const auto& r = view.records[i];
        uint32_t id = r.id;
        auto row = Nb4Ui::action(content, {0, 0, w, 64}, "", [id] { nb4OpenRaceRecord(id); });
        nb4Label(row, {SpaceS, SpaceS, w - 44, 24}, r.model[0] ? r.model : STR_NB4_INVALID_RECORD);
        char s[72]; snprintf(s, sizeof(s), "#%lu • %s • %u %s", (unsigned long)r.id, Nb4Ui::timeText(r.duration, r.model[0]).c_str(), r.laps, STR_NB4_LAPS_8959);
        nb4Label(row, {SpaceS, 32, w - 44, 24}, s, TypeSmall, COLOR_THEME_PRIMARY3_INDEX);
        nb4Label(row, {w - 28, 20, 20, 24}, LV_SYMBOL_RIGHT);
      }
      if (view.more)
        Nb4Ui::action(content, {0, 0, w, Touch}, STR_NB4_OLDER_RUNS, [this] { before = view.records[view.count - 1].id; request(); });
      if (before) Nb4Ui::action(content, {0, 0, w, Touch}, STR_NB4_LATEST_RUNS, [this] { before = 0; request(); });
    }
    if (!detailId || view.error) {
      Nb4Ui::action(content, {0, 0, w, Touch}, STR_NB4_REFRESH_RETRY, [this] { nb4HistoryRetry(); request(); });
      paragraph(content, {SpaceS, 0, w - 2 * SpaceS, LV_SIZE_CONTENT}, storageText(), TypeSmall, COLOR_THEME_PRIMARY3_INDEX);
    }
  }
};

class BackupPage : public DataPage {
 public:
  BackupPage() : DataPage(STR_NB4_BACKUP_RESTORE, "settings/system/backup_restore") {

    const coord_t w = body->width();
    Nb4Ui::paragraph(body, {Nb4Ui::SpaceS, 0, w - 2 * Nb4Ui::SpaceS, LV_SIZE_CONTENT},
                     STR_NB4_COPY_RADIO_MODELS_SCRIPTS_THEMES_AND);
    Nb4Ui::action(body, {0, 0, w, Nb4Ui::Touch}, STR_NB4_OPEN_FILES,
                  [] { nb4OpenRoute("settings/system/storage"); }, true);
  }
};

class ResetPage : public DataPage {
 public:
  ResetPage() : DataPage(STR_NB4_UX_RESET_SETTINGS, "settings/system/reset") {
    using namespace Nb4Ui;
    const coord_t w = body->width();
    caption(body, {SpaceS, 0, w - 2 * SpaceS, CaptionH}, STR_NB4_FACTORY_RESET);
    paragraph(body, {SpaceS, 0, w - 2 * SpaceS, LV_SIZE_CONTENT},
              STR_NB4_AXIS_CALIBRATION_AND_THE_INTERFACE_LANGU,
              TypeSmall, COLOR_THEME_PRIMARY3_INDEX);
    resetAction(w, STR_NB4_RADIO_SETTINGS,
                STR_NB4_RETURNS_THEME_HOME_SCREEN_SOUND_UNITS, Nb4Reset::Radio);
    resetAction(w, STR_NB4_THIS_CAR,
                STR_NB4_RETURNS_THE_OPEN_CAR_TO_FACTORY, Nb4Reset::Model);
    resetAction(w, STR_NB4_RADIO_AND_THIS_CAR,
                STR_NB4_BOTH_AT_ONCE_YOUR_OTHER_SAVED, Nb4Reset::Both);
  }

 private:
  void resetAction(coord_t w, const char* label, const char* ask, Nb4Reset what)
  {
    Nb4Ui::action(body, {0, 0, w, Nb4Ui::Touch}, label, [label, ask, what] {
      new ConfirmDialog(label, ask, [what] { nb4FactoryReset(what); });
    });
  }
};

class TelemetryPage : public DataPage {
 public:
  TelemetryPage() : DataPage(STR_NB4_TRACK_VIEW, "settings/telemetry/track_view") {
    using namespace Nb4Ui;
    const auto w = body->width(), inner = w - 2 * SpaceM;
    auto links = actionRow(body);
    grow(Nb4Ui::action(links, {}, STR_NB4_SENSORS, [] { QuickMenu::openPage(QM_MODEL_TELEMETRY); }));
    grow(Nb4Ui::action(links, {}, STR_NB4_LOG_FILES, [] { QuickMenu::openPage(QM_TOOLS_STORAGE); }));
    graphCard = Nb4Ui::card(body, {0, 0, w, 176});
    title = nb4Label(graphCard, {SpaceM, SpaceXS, inner, 24}, STR_NB4_SELECT_A_SENSOR);
    value = nb4Label(graphCard, {SpaceM, 28, inner, 32}, "--", TypeValue);
    chart = lv_chart_create(graphCard->getLvObj());
    lv_obj_set_pos(chart, SpaceM, 64); lv_obj_set_size(chart, inner, 84);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0); lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, Nb4Ui::color(COLOR_THEME_SECONDARY2_INDEX), LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_opa(chart, LV_OPA_COVER, LV_PART_ITEMS);
    etx_border_color(chart, COLOR_THEME_SECONDARY2_INDEX);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE); lv_chart_set_point_count(chart, 60);
    lv_chart_set_div_line_count(chart, 4, 7); lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    series = lv_chart_add_series(chart, Nb4Ui::color(COLOR_THEME_FOCUS_INDEX), LV_CHART_AXIS_PRIMARY_Y);
    range = nb4Label(graphCard, {SpaceM, 152, inner, 20}, "-30 s                              0 s", TypeSmall, COLOR_THEME_PRIMARY3_INDEX);
    cards = new Window(body, {0, 0, w, LV_SIZE_CONTENT});
    cards->setFlexLayout(LV_FLEX_FLOW_ROW_WRAP, Gap, w, LV_SIZE_CONTENT);
    buildCards();
  }
  void checkEvents() override {
    if (deleted()) return;
    DataPage::checkEvents();
    if (deleted()) return;
    for (unsigned i = 0; i < count; ++i) {
      auto s = nb4ReadSensor(indices[i]);
      fields[i]->setText(s.validity == Nb4Validity::Absent ? "--" : getSensorCustomValue(indices[i], s.value, 0));
      etx_txt_color(fields[i]->getLvObj(), s.validity == Nb4Validity::Stale ? COLOR_THEME_PRIMARY3_INDEX : COLOR_THEME_PRIMARY1_INDEX);
    }
    if (selected < 0) return;
    auto s = nb4ReadSensor(selected);
    std::string label = s.name;
    if (s.validity == Nb4Validity::Stale) label += STR_NB4_STALE;
    else if (s.validity == Nb4Validity::Absent) label += STR_NB4_NO_DATA;
    title->setText(label);
    value->setText(s.validity == Nb4Validity::Absent ? "--" : getSensorCustomValue(selected, s.value, 0));
    if (s.unit > UNIT_MAX) {
      lv_chart_set_all_value(chart, series, LV_CHART_POINT_NONE);
      range->setText(STR_NB4_READING_HAS_NO_NUMERIC_CHART);
      return;
    }
    etx_txt_color(value->getLvObj(), s.validity == Nb4Validity::Stale ? COLOR_THEME_PRIMARY3_INDEX : COLOR_THEME_PRIMARY1_INDEX);
    if (uint32_t(lv_tick_get() - sampled) < 500) return;
    sampled = lv_tick_get();
    for (unsigned i = 1; i < 60; ++i) { samples[i - 1] = samples[i]; valid[i - 1] = valid[i]; }
    samples[59] = s.value; valid[59] = s.validity == Nb4Validity::Valid || s.validity == Nb4Validity::Alarm;
    int32_t low = INT32_MAX, high = INT32_MIN;
    for (unsigned i = 0; i < 60; ++i) if (valid[i]) { low = min(low, samples[i]); high = max(high, samples[i]); }
    for (unsigned i = 0; i < 60; ++i) lv_chart_set_value_by_id(chart, series, i,
      !valid[i] ? LV_CHART_POINT_NONE : high == low ? 500 : int64_t(samples[i] - int64_t(low)) * 1000 / (int64_t(high) - low));
    if (high != INT32_MIN) range->setText(getSensorCustomValue(selected, low, 0) + " ... " + getSensorCustomValue(selected, high, 0) + "  •  30 s");
  }
 private:
  Window *graphCard, *cards;
  StaticText *title, *value, *range;
  StaticText* fields[6]{};
  uint8_t indices[6]{};
  unsigned offset = 0, count = 0;
  int selected = -1;
  lv_obj_t* chart;
  lv_chart_series_t* series;
  uint32_t sampled = 0;
  std::array<int32_t, 60> samples{};
  std::array<bool, 60> valid{};
  void select(unsigned index) {
    selected = index;
    valid.fill(false); lv_chart_set_all_value(chart, series, LV_CHART_POINT_NONE);
    title->setText(nb4ReadSensor(index).name);
  }
  void buildCards() {
    cards->clear(); count = 0;
    unsigned seen = 0; bool more = false;
    auto w = (cards->width() - Nb4Ui::Gap) / 2;
    for (unsigned index = 0; index < MAX_TELEMETRY_SENSORS; ++index) {
      if (!g_model.telemetrySensors[index].isAvailable()) continue;
      if (seen++ < offset) continue;
      if (count == 6) { more = true; break; }
      auto card = Nb4Ui::action(cards, {0, 0, w, 64}, "", [this, index] { select(index); });
      indices[count] = index;
      nb4Label(card, {Nb4Ui::SpaceS, Nb4Ui::SpaceXS, w - 2 * Nb4Ui::SpaceS, 20}, nb4ReadSensor(index).name, Nb4Ui::TypeSmall, COLOR_THEME_PRIMARY3_INDEX);
      fields[count] = nb4Label(card, {Nb4Ui::SpaceS, 24, w - 2 * Nb4Ui::SpaceS, 32}, "--", Nb4Ui::TypeValue);
      if (selected < 0) select(index);
      ++count;
    }
    if (!count) nb4Label(cards, {Nb4Ui::SpaceS, 0, cards->width() - 2 * Nb4Ui::SpaceS, Nb4Ui::Touch}, STR_NB4_NO_SENSORS_CONFIGURED);
    if (more || offset)
      Nb4Ui::action(cards, {0, 0, cards->width(), Nb4Ui::Touch}, more ? STR_NB4_MORE_SENSORS : STR_NB4_FIRST_SENSORS, [this, more] { offset = more ? offset + 6 : 0; buildCards(); });
  }
};
}
void nb4OpenRaceRecord(uint32_t id) { new HistoryPage(id); }
void nb4OpenDataPage(Nb4Section section) {
  switch (section) {
    case Nb4Section::Backup: new BackupPage(); break;
    case Nb4Section::Reset: new ResetPage(); break;
    case Nb4Section::Chrono: new RacePage(); break;
    case Nb4Section::Pit: new PitPage(); break;
    case Nb4Section::Telemetry: new TelemetryPage(); break;
    case Nb4Section::History: new HistoryPage(); break;
    default: break;
  }
}
#endif
