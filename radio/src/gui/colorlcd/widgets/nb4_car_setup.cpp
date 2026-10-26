/* SPDX-License-Identifier: GPL-2.0-only */

#include "nb4_widget_kit.h"
#include "nb4_params.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

// The car's brake and steering setup at a glance, formatted exactly as the
// settings pages show it (nb4ParamNumeric()).
class Nb4CarSetupWidget : public Widget
{
 public:
  Nb4CarSetupWidget(const WidgetFactory* factory, Window* parent,
                    const rect_t& rect, int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  void delayedInit() override
  {
    Nb4Ui::card(lvobj);
    const coord_t w = width(), h = height();
    nb4Label(this, {PAD, 6, (coord_t)(w - PAD * 2), 20}, STR_NB4_WIDGET_CAR_SETUP, FONT(BOLD));
    // As many rows as fit, most important first.
    constexpr coord_t ROW = 24;
    count = 0;
    for (unsigned i = 0; i < DIM(rows) && 30 + (coord_t)(i + 1) * ROW <= h - 4; ++i, ++count) {
      const coord_t y = 30 + (coord_t)i * ROW;
      if (i) rectangle(lvobj, PAD, y - 1, w - PAD * 2, 1, trackColor());
      caption(this, {PAD, (coord_t)(y + 4), (coord_t)(w * 3 / 5), 16}, label(rows[i]));
      values[i] = new StaticText(this, {(coord_t)(w * 3 / 5), (coord_t)(y + 2), (coord_t)(w * 2 / 5 - PAD), 20},
                                 "", COLOR_THEME_PRIMARY1_INDEX, FONT(STD) | RIGHT);
      shown[i].clear();
    }
    update();
    checkEvents();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();
    for (unsigned i = 0; i < count; ++i) {
      const std::string text = valueText(rows[i]);
      if (text == shown[i]) continue;
      shown[i] = text;
      values[i]->setText(text);
    }
  }

 protected:
  static constexpr Nb4Param rows[] = {Nb4Param::BrakeMax, Nb4Param::DragBrake, Nb4Param::AbsEnable,
                                      Nb4Param::SteerSpeedTurn, Nb4Param::SteerSpeedReturn};
  StaticText* values[DIM(rows)] = {};
  std::string shown[DIM(rows)];
  unsigned count = 0;

  static const char* label(Nb4Param p) { return nb4ParamLabel(p); }

  static std::string valueText(Nb4Param p)
  {
    // ABS: off, or the brake point where it engages.
    if (p == Nb4Param::AbsEnable) {
      if (!g_model.nb4Racing.absEnable) return STR_OFF;
      p = Nb4Param::AbsPoint;
    }
    const auto n = nb4ParamNumeric(p);
    if (!n.valid() || !n.get) return "--";
    const int32_t v = n.get();
    if (v == 0 && n.zeroText) return n.zeroText;
    char b[24];
    if (n.prec1) snprintf(b, sizeof(b), "%ld.%ld", (long)(v / 10), (long)abs(v % 10));
    else snprintf(b, sizeof(b), "%ld", (long)v);
    return std::string(b) + (n.suffix ? n.suffix : "");
  }
};

Nb4WidgetFactory<Nb4CarSetupWidget> nb4CarSetupWidget(
    "NB4CarSetup", nullptr, NB4_STR(WIDGET_CAR_SETUP));

#endif
