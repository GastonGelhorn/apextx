/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_widget_kit.h"
#include "nb4_racing.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

class Nb4LapWidget : public Widget
{
 public:
  Nb4LapWidget(const WidgetFactory* factory, Window* parent,
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

    caption(this, {PAD, 6, 80, 18}, STR_NB4_LAP);
    new DynamicText(
        this, {(coord_t)(w - PAD - 60), 6, 60, 18},
        []() { return std::to_string(nb4RacingLaps() + 1); },
        COLOR_THEME_PRIMARY3_INDEX, FONT(XS) | RIGHT);

    auto current = []() { return Nb4Ui::timeText(nb4RacingCurrentLap()); };

    if (compact) {
      const coord_t timeW = w * 11 / 20;
      const LcdFlags f = fontForChars(timeW - PAD, 7, h - 34);
      new DynamicText(this, {PAD, (coord_t)((h - fontHeight(f)) / 2 + 6), timeW, fontHeight(f)},
                      current, COLOR_THEME_PRIMARY1_INDEX, f | CENTERED);
      rectangle(lvobj, timeW + PAD * 2, 30, 1, h - 40, trackColor());
      rows(timeW + PAD * 3, 30, w - timeW - PAD * 4, (h - 36) / 3);
    } else {
      const LcdFlags f = fontForChars(w - PAD * 2, 7, 67);
      new DynamicText(this, {0, 34, w, fontHeight(f)}, current,
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
    const int32_t d = nb4RacingLaps() >= 2 ? nb4RacingLapDelta() : 0;
    const int8_t sign = d < 0 ? -1 : d > 0 ? 1 : 0;
    if (sign != lastSign && delta) {
      lastSign = sign;
      ink(delta, sign < 0 ? green() : sign > 0 ? red() : textColor());
    }
  }

 protected:
  StaticText* delta = nullptr;
  int8_t lastSign = 2;

  void rows(coord_t x, coord_t y, coord_t w, coord_t rowH)
  {
    row(x, y, w, rowH, STR_NB4_LAST,
        []() { return Nb4Ui::timeText(nb4RacingLastLap(), nb4RacingLaps() > 0); });
    auto best = row(x, y + rowH, w, rowH, STR_NB4_BEST_7CAE,
                    []() { return Nb4Ui::timeText(nb4RacingBestLap(), nb4RacingLaps() > 0); });
    ink(best, green());
    delta = row(x, y + rowH * 2, w, rowH, STR_NB4_DIFF, []() {
      return nb4RacingLaps() >= 2 ? deltaText(nb4RacingLapDelta()) : std::string("--");
    });
  }

  StaticText* row(coord_t x, coord_t y, coord_t w, coord_t rowH,
                  const char* label, std::function<std::string()> value)
  {
    caption(this, {x, y, (coord_t)(w / 2), rowH}, label);
    return new DynamicText(this, {(coord_t)(x + w / 2), y, (coord_t)(w - w / 2), rowH},
                           value, COLOR_THEME_PRIMARY1_INDEX, FONT(STD) | RIGHT);
  }
};

BaseWidgetFactory<Nb4LapWidget> nb4LapWidget("NB4Lap", nullptr, "Lap");

#endif
