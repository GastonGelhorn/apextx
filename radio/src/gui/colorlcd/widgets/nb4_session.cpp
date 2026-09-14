/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_widget_kit.h"
#include "nb4_racing.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

class Nb4SessionWidget : public Widget
{
 public:
  Nb4SessionWidget(const WidgetFactory* factory, Window* parent,
                   const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  void delayedInit() override
  {
    Nb4Ui::card(lvobj);
    const coord_t w = width(), h = height();
    const bool compact = compactZone(h);

    caption(this, {PAD, 6, 80, 18}, STR_NB4_SESSION);
    state = pill(this, {(coord_t)(w - PAD - 100), 4, 100, 20}, "", orange());

    auto elapsed = []() { return Nb4Ui::timeText(nb4RaceElapsed()); };

    if (compact) {
      const coord_t timeW = w * 11 / 20;
      const LcdFlags f = fontForChars(timeW - PAD, 7, h - 34);
      new DynamicText(this, {PAD, (coord_t)((h - fontHeight(f)) / 2 + 6), timeW, fontHeight(f)},
                      elapsed, COLOR_THEME_PRIMARY1_INDEX, f | CENTERED);
      rectangle(lvobj, timeW + PAD * 2, 30, 1, h - 40, trackColor());
      rows(timeW + PAD * 3, 30, w - timeW - PAD * 4, (h - 36) / 3);
    } else {
      const LcdFlags f = fontForChars(w - PAD * 2, 7, 67);
      new DynamicText(this, {0, 34, w, fontHeight(f)}, elapsed,
                      COLOR_THEME_PRIMARY1_INDEX, f | CENTERED);
      const coord_t rowH = 20;
      rows(PAD, h - rowH * 3 - PAD, w - PAD * 2, rowH);
    }
    update();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();
    const uint8_t s = nb4RaceIsPaused() ? 1 : nb4RaceElapsed() > 0 ? 2 : 0;
    if (s == lastState || !state) return;
    lastState = s;
    state->setText(s == 1 ? STR_NB4_PAUSED_52B4
                 : s == 2 ? STR_NB4_RUNNING_A5C9
                          : STR_NB4_READY_66E1);
    recolorPill(state, s == 1 ? muted() : s == 2 ? orange() : muted());
  }

 protected:
  StaticText* state = nullptr;
  uint8_t lastState = 0xff;

  void rows(coord_t x, coord_t y, coord_t w, coord_t rowH)
  {
    row(x, y, w, rowH, STR_NB4_LAPS_7AA1,
        []() { return std::to_string(nb4RacingLaps()); });
    row(x, y + rowH, w, rowH, STR_NB4_AVERAGE_C834,
        []() { return Nb4Ui::timeText(nb4RacingRaceAverage(), nb4RacingLaps() > 0); });
    auto best = row(x, y + rowH * 2, w, rowH, STR_NB4_BEST_7CAE,
                    []() { return Nb4Ui::timeText(nb4RacingBestLap(), nb4RacingLaps() > 0); });
    ink(best, green());
  }

  StaticText* row(coord_t x, coord_t y, coord_t w, coord_t rowH,
                  const char* label, std::function<std::string()> value)
  {
    caption(this, {x, y, (coord_t)(w / 2), rowH}, label);
    return new DynamicText(this, {(coord_t)(x + w / 2), y, (coord_t)(w - w / 2), rowH},
                           value, COLOR_THEME_PRIMARY1_INDEX, FONT(STD) | RIGHT);
  }
};

BaseWidgetFactory<Nb4SessionWidget> nb4SessionWidget("NB4Session", nullptr, "Session");

#endif
