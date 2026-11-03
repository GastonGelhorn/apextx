/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include "nb4_widget_kit.h"

namespace nb4w {

// The options of every steering and throttle display, from minimal (no title,
// no trim) to everything: show its trim, show its title.
inline const WidgetOption trimOptions[] = {
    {STR_TRIMS, WidgetOption::Bool, WidgetOptionValue{.boolValue = false}},
    {STR_NB4_WIDGET_TITLE, WidgetOption::Bool, WidgetOptionValue{.boolValue = true}},
    {nullptr, WidgetOption::Bool}};

// The input's trim along the bottom of a widget, as the home screen shows it:
// the value in steps and its position on a centre bar.
struct TrimRow {
  static constexpr coord_t H = 24;

  bool shown = false;
  uint8_t input = 0;
  StaticText* value = nullptr;
  CentreBar bar;
  int last = INT32_MIN;

  static bool wanted(Widget* w)
  {
    return w->getPersistentData()->options[0].value.boolValue;
  }

  // Builds the row when the widget asks for it; returns the height it takes
  // from the bottom of the widget.
  coord_t build(Widget* w, uint8_t trimInput, lv_color_t color)
  {
    shown = wanted(w);
    value = nullptr;
    bar = CentreBar();
    last = INT32_MIN;
    if (!shown || w->width() < 150 || w->height() < 110) return 0;
    input = trimInput;
    const coord_t y = w->height() - H, width = w->width();
    caption(w, {PAD, (coord_t)(y + 3), 40, 16}, STR_NB4_TRIM_CAPTION);
    value = new StaticText(w, {(coord_t)(width - PAD - 40), (coord_t)(y + 2), 40, 18}, "",
                           COLOR_THEME_PRIMARY1_INDEX, FONT(XS) | RIGHT);
    bar.build(w->getLvObj(), PAD + 42, y + 9, width - PAD * 2 - 42 - 44, 6, color);
    update();
    return H;
  }

  void update()
  {
    if (!value) return;
    const bool ok = trimOk(input);
    const int v = ok ? trimValue(input) : INT32_MIN + 1;
    if (v == last) return;
    last = v;
    value->setText(ok ? signedText(v) : std::string("--"));
    bar.set(ok ? trimPct(input) : 0);
  }
};

// A steering or throttle display with the trim and title options. Switching
// either rebuilds the widget, since the rest of it lays out in what is left.
class Nb4TrimWidget : public Widget
{
 public:
  using Widget::Widget;

  void update() override
  {
    if (loaded && (trim.shown != TrimRow::wanted(this) || titleShown != titled() || builtW != width() || builtH != height() || builtTheme != nb4ThemeKey() || language != currentLangStrings)) {
      clear();
      // clear() also removed the focus outline; enableFocus() recreates it.
      focusBorder = nullptr;
      delayedInit();
    }
  }

 protected:
  coord_t builtW = 0, builtH = 0;
  uint32_t builtTheme = 0;
  const void* language = nullptr;
  bool minimal = false;
  bool minimalDisplay(uint8_t channel)
  {
    builtW = width(); builtH = height(); builtTheme = nb4ThemeKey(); language = currentLangStrings;
    trim.shown = TrimRow::wanted(this); titleShown = titled();
    minimal = height() < 120 + (trim.shown ? TrimRow::H : 0) || width() < 100;
    if (!minimal) return false;
    if (!strcmp(getFactory()->getName(), "NB4SteerThr")) {
      const coord_t h = height() / 2;
      const auto f = fontForChars(max<coord_t>(0,width()-8),8,h);
      new DynamicNumber<int16_t>(this,{0,0,width(),h},[](){return channelPct(g_model.nb4Racing.steeringChannel);},COLOR_THEME_PRIMARY1_INDEX,f|CENTERED,"ST ","%");
      new DynamicNumber<int16_t>(this,{0,h,width(),h},[](){return channelPct(g_model.nb4Racing.throttleChannel);},COLOR_THEME_PRIMARY1_INDEX,f|CENTERED,"TH ","%");
      return true;
    }
    const auto f = fontForChars(max<coord_t>(0, width() - 8), 5, height());
    new DynamicNumber<int16_t>(this, {0, max<coord_t>(0, (height() - fontHeight(f))/2), width(), min<coord_t>(height(),fontHeight(f))},
        [channel]() { return channelPct(channel); }, COLOR_THEME_PRIMARY1_INDEX, f | CENTERED, nullptr, "%");
    return true;
  }
  TrimRow trim;
  bool titleShown = true;

  bool titled() { return getPersistentData()->options[1].value.boolValue; }

  // The title row when the option asks for it. Returns where the content
  // starts: below the title, or near the top edge without one.
  coord_t titleRow(const char* text, lv_color_t color)
  {
    titleShown = titled();
    if (!titleShown) return 4;
    auto t = nb4Label(this, {PAD, 6, (coord_t)(width() - PAD * 2), 20}, text, FONT(BOLD));
    ink(t, color);
    return 28;
  }
};

}  // namespace nb4w
