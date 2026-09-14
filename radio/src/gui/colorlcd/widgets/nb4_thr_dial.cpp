/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_dial_widget.h"

#if defined(RADIO_NB4_FAMILY)

class Nb4ThrDialWidget : public Nb4DialWidget
{
 public:
  using Nb4DialWidget::Nb4DialWidget;

 protected:
  uint8_t channel() const override { return g_model.nb4Racing.throttleChannel; }
  uint8_t trimIndex() const override { return ADC_MAIN_TH; }
  const char* titleText() const override { return STR_NB4_THR_BRAKE; }
  lv_color_t accent(int16_t pct) const override
  {
    return pct < 0 ? Nb4Ui::brakeColor() : Nb4Ui::throttleColor();
  }
};

BaseWidgetFactory<Nb4ThrDialWidget> nb4ThrDialWidget("NB4ThrDial", nullptr,
                                                     "Thr dial");

#endif
