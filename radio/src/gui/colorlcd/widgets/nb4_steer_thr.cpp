/* SPDX-License-Identifier: GPL-2.0-only */

#include "nb4_trim_row.h"

#if defined(RADIO_NB4_FAMILY)

using namespace nb4w;

// Steering and throttle in one tile: each with its value and a bar, and with
// the trim option each also shows its trim.
class Nb4SteerThrWidget : public Nb4TrimWidget
{
 public:
  Nb4SteerThrWidget(const WidgetFactory* factory, Window* parent,
                    const rect_t& rect, int screenNum, int zoneNum) :
      Nb4TrimWidget(factory, parent, rect, screenNum, zoneNum)
  {
    delayLoad();
  }

  void delayedInit() override
  {
    Nb4Ui::card(lvobj);
    const coord_t w = width(), h = height();
    trim.shown = TrimRow::wanted(this);
    titleShown = titled();
    gasInk = Nb4Ui::throttleColor();
    brakeInk = Nb4Ui::brakeColor();
    const lv_color_t steerInk = Nb4Ui::steeringColor();

    const coord_t blockH = (h - PAD) / 2;
    const LcdFlags f = fontForChars(w / 2 - PAD, 5, blockH - 22);
    const coord_t fh = fontHeight(f);
    const char* titles[2] = {STR_NB4_STEERING, STR_NB4_THR};
    const lv_color_t inks[2] = {steerInk, gasInk};
    for (unsigned i = 0; i < 2; ++i) {
      const coord_t y = PAD / 2 + (coord_t)i * blockH;
      if (titleShown) {
        auto t = nb4Label(this, {PAD, (coord_t)(y + 2), (coord_t)(w / 2), 20}, titles[i], FONT(BOLD));
        ink(t, inks[i]);
      }
      trimText[i] = trim.shown ? caption(this, {PAD, (coord_t)(y + (titleShown ? 22 : 4)), (coord_t)(w / 2), 16}, "") : nullptr;
      trimLast[i] = INT32_MIN;
      const coord_t valueY = y + (blockH - 14 - fh) / 2;
      values[i] = new StaticText(this, {(coord_t)(w / 2), valueY, (coord_t)(w / 2 - PAD), fh}, "",
                                 COLOR_THEME_PRIMARY1_INDEX, f | RIGHT);
      const coord_t barY = y + blockH - 12;
      if (i == 0) {
        steerBar.build(lvobj, PAD, barY, w - PAD * 2, 6, steerInk);
      } else {
        thrX = PAD;
        thrW = w - PAD * 2;
        thrZero = thrW * 3 / 10;
        track(lvobj, thrX, barY, thrW, 6, 3);
        thrFill = rectangle(lvobj, thrX + thrZero, barY, 0, 6, gasInk, 2);
        rectangle(lvobj, thrX + thrZero - 1, barY - 3, 2, 12, textColor());
        thrY = barY;
      }
      last[i] = INT16_MIN;
    }
    update();
    checkEvents();
  }

  void checkEvents() override
  {
    if (!loaded) return;
    Widget::checkEvents();
    const uint8_t inputs[2] = {ADC_MAIN_ST, ADC_MAIN_TH};
    for (unsigned i = 0; i < 2; ++i) {
      if (trimText[i]) {
        const bool ok = trimOk(inputs[i]);
        const int t = ok ? trimValue(inputs[i]) : INT32_MIN + 1;
        if (t != trimLast[i]) {
          trimLast[i] = t;
          trimText[i]->setText(std::string(STR_NB4_TRIM_CAPTION) + " " + (ok ? signedText(t) : std::string("--")));
        }
      }
      const int16_t v = channelPct(i ? g_model.nb4Racing.throttleChannel : g_model.nb4Racing.steeringChannel);
      if (v == last[i]) continue;
      last[i] = v;
      values[i]->setText(signedText(v) + "%");
      if (i == 0) {
        steerBar.set(v);
      } else {
        // Throttle fills right of the zero mark, brake left of it.
        const coord_t len = v >= 0 ? (coord_t)((thrW - thrZero) * v / 100) : (coord_t)(thrZero * -v / 100);
        place(thrFill, v >= 0 ? thrX + thrZero : thrX + thrZero - len, thrY, len, 6);
        recolor(thrFill, v < 0 ? brakeInk : gasInk);
        ink(values[i], v < 0 ? brakeInk : gasInk);
      }
    }
  }

 protected:
  StaticText* values[2] = {};
  StaticText* trimText[2] = {};
  int trimLast[2] = {INT32_MIN, INT32_MIN};
  int16_t last[2] = {INT16_MIN, INT16_MIN};
  CentreBar steerBar;
  lv_obj_t* thrFill = nullptr;
  coord_t thrX = 0, thrW = 0, thrZero = 0, thrY = 0;
  lv_color_t gasInk{}, brakeInk{};
};

Nb4WidgetFactory<Nb4SteerThrWidget> nb4SteerThrWidget(
    "NB4SteerThr", trimOptions, NB4_STR(WIDGET_STEER_THR));

#endif
