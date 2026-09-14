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
#include <array>

namespace {
const char* storageText() {
  switch (nb4HistoryStatus()) {
    case Nb4HistoryStatus::Pending: return nb4Text("Resultado pendiente", "Result pending");
    case Nb4HistoryStatus::Saving: return nb4Text("Guardando manga...", "Saving run...");
    case Nb4HistoryStatus::Saved:
      return nb4RacePhase() == Nb4RacePhase::Finished
        ? nb4Text("Manga guardada", "Run saved")
        : nb4Text("Ver registro de mangas", "View race history");
    case Nb4HistoryStatus::Unavailable: return nb4Text("Almacenamiento no disponible • reintentar", "Storage unavailable • retry");
    case Nb4HistoryStatus::Failed: return nb4Text("No se guardó • reintentar", "Save failed • retry");
    case Nb4HistoryStatus::Full: return nb4Text("Guarda las mangas pendientes", "Save pending runs first");
    default: return nb4Text("Ver registro de mangas", "View race history");
  }
}
class DataPage : public NavWindow {
 public:

  explicit DataPage(const char* title) : NavWindow(MainWindow::instance(), {0, 0, lv_disp_get_hor_res(nullptr), lv_disp_get_ver_res(nullptr)}) {
    pushLayer();
    etx_solid_bg(lvobj, COLOR_THEME_SECONDARY3_INDEX);
    Nb4Ui::header(this, title, [this] { onCancel(); });
    body = new Window(this, {2, 56, width() - 4, height() - 58});
    lv_obj_set_scroll_dir(body->getLvObj(), LV_DIR_VER);
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
 protected:
  Window* body;
};
void lapTable(Window* parent, const uint32_t* times, unsigned count, uint32_t best) {
  const coord_t w = parent->width();
  nb4Label(parent, {8, 0, 48, 20}, nb4Text("VUELTA", "LAP"), FONT(XS), COLOR_THEME_PRIMARY3_INDEX);
  nb4Label(parent, {68, 0, w / 2 - 60, 20}, nb4Text("TIEMPO", "TIME"), FONT(XS), COLOR_THEME_PRIMARY3_INDEX);
  nb4Label(parent, {w / 2 + 25, 0, w / 2 - 32, 20}, "+/-", FONT(XS) | RIGHT, COLOR_THEME_PRIMARY3_INDEX);
  char text[24];
  for (unsigned row = 0; row < count && row < NB4_MAX_LAPS; ++row) {
    unsigned i = count - row - 1;
    coord_t y = 25 + row * 34;
    if (!(row % 2)) Nb4Ui::panel((new Window(parent, {0, y, w, 32}))->getLvObj());
    snprintf(text, sizeof(text), "%02u", i + 1);
    nb4Label(parent, {8, y + 5, 48, 22}, text);
    nb4RacingFormatTime(text, times[i]);
    nb4Label(parent, {68, y + 5, w / 2 - 45, 22}, text, FONT(STD), times[i] == best ? COLOR_THEME_EDIT_INDEX : COLOR_THEME_PRIMARY1_INDEX);
    snprintf(text, sizeof(text), "+%lu.%02lu", (unsigned long)((times[i] - best) / 100), (unsigned long)((times[i] - best) % 100));
    nb4Label(parent, {w / 2 + 30, y + 5, w / 2 - 38, 22}, times[i] == best ? "--" : text, FONT(STD) | RIGHT, times[i] == best ? COLOR_THEME_PRIMARY3_INDEX : COLOR_THEME_WARNING_INDEX);
  }
}
class RacePage : public DataPage {
 public:
  RacePage() : DataPage(nb4Text("Crono y vueltas", "Timers & laps")) {
    panel = new Nb4RacePanel(body, {0, 0, body->width(), 120});
    const auto half = (body->width() - 6) / 2;
    start = Nb4Ui::action(body, {0, 128, half, 44}, "", [this] {
      if (nb4RacePhase() == Nb4RacePhase::Running) nb4RaceFinish();
      else if (!nb4RaceStart()) new MessageDialog(nb4Text("Carrera", "Race"), nb4Text("Guarda los resultados pendientes y reintenta.", "Save pending results, then retry."));
    }, true);
    lap = Nb4Ui::action(body, {half + 6, 128, half, 44}, nb4Text("Marcar vuelta", "Mark lap"), [] { nb4RacingMarkLap(); });

    undo = Nb4Ui::action(body, {0, 178, half, 44}, nb4Text("Deshacer", "Undo lap"),
                         [] { nb4RacingUndoLap(); });
    Nb4Ui::action(body, {half + 6, 178, half, 44}, nb4Text("Ajustes", "Setup"), [] { QuickMenu::openPage(QM_MODEL_NB4_RACING); });
    auto retry = Nb4Ui::action(body, {0, 230, body->width(), 44}, "", [] {
      if (nb4HistoryStatus() == Nb4HistoryStatus::Idle || nb4HistoryStatus() == Nb4HistoryStatus::Saved) nb4OpenSection(Nb4Section::History);
      else nb4HistoryRetry();
    });
    statusLabel = nb4Label(retry, {8, 6, retry->width() - 16, 34}, "", FONT(XS));
    lv_label_set_long_mode(statusLabel->getLvObj(), LV_LABEL_LONG_WRAP);
    table = new Window(body, {0, 282, body->width(), 1});
    checkEvents();
  }
  void checkEvents() override {
    if (deleted()) return;
    DataPage::checkEvents();
    if (deleted()) return;
    panel->refresh(nb4ReadCarState());
    auto phase = nb4RacePhase();
    start->setText(phase == Nb4RacePhase::Running ? nb4Text("Finalizar", "Finish") : phase == Nb4RacePhase::Finished ? nb4Text("Nueva manga", "New run") : nb4Text("Iniciar", "Start"));
    lap->enable(phase == Nb4RacePhase::Running);
    undo->enable(phase == Nb4RacePhase::Running && nb4RacingLaps() > 0);
    statusLabel->setText(storageText());
    if (previousCount != nb4RacingLaps()) {
      previousCount = nb4RacingLaps();
      table->clear(); table->setHeight(25 + previousCount * 34);
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
  PitPage() : DataPage("Boxes") {
    auto card = nb4Card(body, {0, 0, body->width(), 142});
    nb4Label(card, {12, 8, card->width() - 24, 20}, nb4Text("DEPÓSITO / PACK", "FUEL / PACK"), FONT(XS), COLOR_THEME_PRIMARY3_INDEX);
    remaining = nb4Label(card, {12, 36, card->width() - 24, 58}, "--", FONT(LXL));
    info = nb4Label(card, {12, 104, card->width() - 24, 28}, "", FONT(STD));
    nb4Label(body, {8, 158, body->width() - 124, 42}, nb4Text("Duración (min)", "Duration (min)"));
    new NumberEdit(body, {body->width() - 108, 150, 108, 44}, 0, NB4_PIT_MAX_MINUTES,
      [] { return int(nb4PitMinutes()); }, [](int v) { nb4PitConfigure(v); });

    refuel = Nb4Ui::action(body, {0, 208, body->width(), 44}, nb4Text("Repostado / pack nuevo", "Refuelled / new pack"), [] { nb4PitRefuel(); }, true);
    auto note = nb4Label(body, {8, 264, body->width() - 16, 90}, nb4Text("Cuenta atrás del Crono 2. La alimentación RX es el BEC; no indica la carga del pack.", "Timer 2 countdown. RX supply is the BEC; it does not measure pack charge."), FONT(XS), COLOR_THEME_PRIMARY3_INDEX);
    lv_label_set_long_mode(note->getLvObj(), LV_LABEL_LONG_WRAP);
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
    if (!nb4PitEnabled()) info->setText(nb4Text("Configura la duración", "Set a duration"));
    else if (laps < 0) info->setText(nb4Text("Vueltas restantes: --", "Laps left: --"));
    else { snprintf(s, sizeof(s), "%s %d", nb4Text("Vueltas estimadas:", "Estimated laps:"), laps); info->setText(s); }
    refuel->enable(nb4PitEnabled());
  }
 private: StaticText *remaining, *info; TextButton* refuel = nullptr;
};
class HistoryPage : public DataPage {
 public:
  HistoryPage(uint32_t id = 0) : DataPage(nb4Text("Registro de mangas", "Race history")), detailId(id) {
    content = new Window(body, {0, 0, body->width(), body->height()});
    request();
  }
  void request() {
    content->clear();
    nb4Label(content, {8, 10, content->width() - 16, 28}, nb4Text("Cargando...", "Loading..."));
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
    content->clear();
    coord_t w = content->width(), y = 0;
    if (view.error) {
      auto note = nb4Label(content, {8, 0, w - 16, 80}, nb4Text("No se pudo leer el registro. Comprueba el almacenamiento o la conexión USB.", "History could not be read. Check storage or the USB connection."));
      lv_label_set_long_mode(note->getLvObj(), LV_LABEL_LONG_WRAP); y = 90;
    } else if (detailId && view.count) {
      const auto& r = view.records[0];
      auto card = nb4Card(content, {0, 0, w, 152});
      nb4Label(card, {10, 8, w - 20, 28}, r.model, FONT(L));
      nb4Label(card, {10, 44, w - 20, 52}, Nb4Ui::timeText(r.duration).c_str(), FONT(LXL));
      char s[64]; snprintf(s, sizeof(s), "#%lu • %u %s", (unsigned long)r.id, r.laps, nb4Text("vueltas", "laps"));
      nb4Label(card, {10, 102, w - 20, 24}, s);
      if (r.date) {
        struct gtm date; gtime_t timestamp = r.date; filltm(&timestamp, &date);
        snprintf(s, sizeof(s), "%04u-%02u-%02u %02u:%02u", date.tm_year + 1900, date.tm_mon + 1, date.tm_mday, date.tm_hour, date.tm_min);
      } else strAppend(s, nb4Text("Fecha no disponible", "Date unavailable"));
      nb4Label(card, {10, 128, w - 20, 20}, s, FONT(XS), COLOR_THEME_PRIMARY3_INDEX);
      auto rows = new Window(content, {0, 162, w, 25 + r.laps * 34});
      lapTable(rows, r.times, r.laps, r.best); y = 195 + r.laps * 34;
    } else {
      if (!view.count) { nb4Label(content, {8, y + 8, w - 16, 30}, nb4Text("Todavía no hay mangas", "No saved runs yet")); y += 50; }
      for (unsigned i = 0; i < view.count; ++i) {
        const auto& r = view.records[i];
        uint32_t id = r.id;
        auto row = Nb4Ui::action(content, {0, y, w, 66}, "", [id] { nb4OpenRaceRecord(id); });
        nb4Label(row, {10, 7, w - 44, 24}, r.model[0] ? r.model : nb4Text("Archivo inválido", "Invalid record"));
        char s[72]; snprintf(s, sizeof(s), "#%lu • %s • %u %s", (unsigned long)r.id, Nb4Ui::timeText(r.duration, r.model[0]).c_str(), r.laps, nb4Text("vueltas", "laps"));
        nb4Label(row, {10, 35, w - 44, 22}, s, FONT(XS), COLOR_THEME_PRIMARY3_INDEX);
        nb4Label(row, {w - 28, 22, 20, 20}, LV_SYMBOL_RIGHT); y += 72;
      }
      if (view.more) {
        Nb4Ui::action(content, {0, y, w, 44}, nb4Text("Más antiguas", "Older runs"), [this] { before = view.records[view.count - 1].id; request(); }); y += 50;
      }
      if (before) { Nb4Ui::action(content, {0, y, w, 44}, nb4Text("Más recientes", "Latest runs"), [this] { before = 0; request(); }); y += 50; }
    }
    if (!detailId || view.error) {
      Nb4Ui::action(content, {0, y, w, 44}, nb4Text("Actualizar / reintentar", "Refresh / retry"), [this] { nb4HistoryRetry(); request(); }); y += 52;
      auto note = nb4Label(content, {8, y, w - 16, 44}, storageText(), FONT(XS), COLOR_THEME_PRIMARY3_INDEX);
      lv_label_set_long_mode(note->getLvObj(), LV_LABEL_LONG_WRAP); y += 48;
    }
    content->setHeight(y);
  }
};

class BackupPage : public DataPage {
 public:
  BackupPage() : DataPage(nb4Text("Copias y restauración", "Backup & restore")) {

    content = new Window(body, {0, 0, body->width(), body->height()});
    const coord_t w = content->width();
    coord_t y = 0;

    const char* instructions = nb4Text(
      "Copia RADIO, MODELS, SCRIPTS, THEMES y LOGS desde Archivos o por USB antes de probar firmware.\n\n"
      "La configuración anterior a Racing se conserva en RADIO/radio-pre-racing-ui-v2.yml.\n\n"
      "Para volver, apaga la radio, restaura los archivos de tu copia y utiliza el firmware de retorno del paquete. No mezcles datos de distintas versiones.",
      "Copy RADIO, MODELS, SCRIPTS, THEMES and LOGS using Files or USB before testing firmware.\n\n"
      "The settings before Racing are retained in RADIO/radio-pre-racing-ui-v2.yml.\n\n"
      "To roll back, power off, restore your backup files and use the package's rollback firmware. Keep data and firmware versions together.");
    auto note = nb4Label(content, {8, y, w - 16, 300}, instructions);
    lv_label_set_long_mode(note->getLvObj(), LV_LABEL_LONG_WRAP);
    y += 310;

    Nb4Ui::action(content, {0, y, w, Nb4Ui::Touch}, nb4Text("Abrir archivos", "Open files"),
                  [] { QuickMenu::openPage(QM_TOOLS_STORAGE); }, true);
    y += Nb4Ui::Touch + 24;

    nb4Label(content, {8, y, w - 16, 24},
             nb4Text("REINICIO DE FÁBRICA", "FACTORY RESET"), FONT(XS),
             COLOR_THEME_PRIMARY3_INDEX);
    y += 28;

    auto keeps = nb4Label(content, {8, y, w - 16, 62},
                          nb4Text("Se conservan siempre la calibración de los ejes "
                                  "y el idioma: no tendrás que recalibrar ni buscar "
                                  "los menús en otro idioma.",
                                  "Axis calibration and the interface language are "
                                  "always kept: no recalibrating, and the menus stay "
                                  "in your language."),
                          FONT(XS), COLOR_THEME_PRIMARY3_INDEX);
    lv_label_set_long_mode(keeps->getLvObj(), LV_LABEL_LONG_WRAP);

    y += 70;

    y = resetAction(y, w, nb4Text("Ajustes de la emisora", "Radio settings"),
                    nb4Text("Devuelve tema, pantalla de inicio, sonido, unidades y "
                            "opciones avanzadas a como vienen de fábrica.\n\nNo toca "
                            "los coches ni la calibración.",
                            "Returns theme, home screen, sound, units and advanced "
                            "options to factory.\n\nDoes not touch cars or "
                            "calibration."),
                    Nb4Reset::Radio);

    y = resetAction(y, w, nb4Text("Este coche", "This car"),
                    nb4Text("Devuelve el coche abierto a los valores de fábrica: "
                            "recorridos, curvas, freno, ABS y motor.\n\nConserva el "
                            "nombre. No toca los demás coches ni la emisora.",
                            "Returns the open car to factory values: travel, curves, "
                            "brake, ABS and engine.\n\nKeeps the name. Does not touch "
                            "other cars or the radio."),
                    Nb4Reset::Model);

    y = resetAction(y, w, nb4Text("Emisora y este coche", "Radio and this car"),
                    nb4Text("Las dos cosas a la vez.\n\nLos demás coches guardados "
                            "siguen donde están.",
                            "Both at once.\n\nYour other saved cars stay where they "
                            "are."),
                    Nb4Reset::Both);

    content->setHeight(y);
  }

 private:
  Window* content = nullptr;

  coord_t resetAction(coord_t y, coord_t w, const char* label, const char* ask,
                      Nb4Reset what)
  {
    Nb4Ui::action(content, {0, y, w, Nb4Ui::Touch}, label, [label, ask, what] {
      new ConfirmDialog(label, ask, [what] { nb4FactoryReset(what); });
    });
    return y + Nb4Ui::Touch + Nb4Ui::Gap;
  }
};

class TelemetryPage : public DataPage {
 public:
  TelemetryPage() : DataPage(nb4Text("Telemetría", "Telemetry")) {
    const auto w = body->width();
    Nb4Ui::action(body, {0, 0, (w - 6) / 2, 44}, nb4Text("Sensores", "Sensors"), [] { QuickMenu::openPage(QM_MODEL_TELEMETRY); });
    Nb4Ui::action(body, {(w + 6) / 2, 0, (w - 6) / 2, 44}, nb4Text("Archivos", "Log files"), [] { QuickMenu::openPage(QM_TOOLS_STORAGE); });
    graphCard = nb4Card(body, {0, 54, w, 176});
    title = nb4Label(graphCard, {10, 6, w - 20, 22}, nb4Text("Selecciona un sensor", "Select a sensor"));
    value = nb4Label(graphCard, {10, 32, w - 20, 30}, "--", FONT(L));
    chart = lv_chart_create(graphCard->getLvObj());
    lv_obj_set_pos(chart, 10, 68); lv_obj_set_size(chart, w - 20, 82);
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
    range = nb4Label(graphCard, {10, 153, w - 20, 18}, "-30 s                              0 s", FONT(XS), COLOR_THEME_PRIMARY3_INDEX);
    cards = new Window(body, {0, 240, w, 10});
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
    if (s.validity == Nb4Validity::Stale) label += nb4Text(" • Antiguo", " • Stale");
    else if (s.validity == Nb4Validity::Absent) label += nb4Text(" • Sin datos", " • No data");
    title->setText(label);
    value->setText(s.validity == Nb4Validity::Absent ? "--" : getSensorCustomValue(selected, s.value, 0));
    if (s.unit > UNIT_MAX) {
      lv_chart_set_all_value(chart, series, LV_CHART_POINT_NONE);
      range->setText(nb4Text("Lectura sin gráfica numérica", "Reading has no numeric chart"));
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
    auto w = (cards->width() - 6) / 2;
    for (unsigned index = 0; index < MAX_TELEMETRY_SENSORS; ++index) {
      if (!g_model.telemetrySensors[index].isAvailable()) continue;
      if (seen++ < offset) continue;
      if (count == 6) { more = true; break; }
      auto card = Nb4Ui::action(cards, {coord_t((count % 2) * (w + 6)), coord_t((count / 2) * 68), w, 62}, "", [this, index] { select(index); });
      indices[count] = index;
      nb4Label(card, {8, 4, w - 16, 18}, nb4ReadSensor(index).name, FONT(XS), COLOR_THEME_PRIMARY3_INDEX);
      fields[count] = nb4Label(card, {8, 25, w - 16, 30}, "--", FONT(L));
      if (selected < 0) select(index);
      ++count;
    }
    coord_t y = ((count + 1) / 2) * 68;
    if (!count) { nb4Label(cards, {8, 4, cards->width() - 16, 30}, nb4Text("No hay sensores configurados", "No sensors configured")); y = 40; }
    if (more || offset) {
      Nb4Ui::action(cards, {0, y, cards->width(), 44}, more ? nb4Text("Más sensores", "More sensors") : nb4Text("Primeros sensores", "First sensors"), [this, more] { offset = more ? offset + 6 : 0; buildCards(); }); y += 50;
    }
    cards->setHeight(y);
  }
};
}
void nb4OpenRaceRecord(uint32_t id) { new HistoryPage(id); }
void nb4OpenDataPage(Nb4Section section) {
  switch (section) {
    case Nb4Section::Backup: new BackupPage(); break;
    case Nb4Section::Chrono: new RacePage(); break;
    case Nb4Section::Pit: new PitPage(); break;
    case Nb4Section::Telemetry: new TelemetryPage(); break;
    case Nb4Section::History: new HistoryPage(); break;
    default: break;
  }
}
#endif
