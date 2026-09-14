/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "nb4_dial_widget.h"

#if defined(RADIO_NB4_FAMILY)

class Nb4SteerDialWidget : public Nb4DialWidget
{
 public:
  using Nb4DialWidget::Nb4DialWidget;

 protected:
  uint8_t channel() const override { return g_model.nb4Racing.steeringChannel; }
  uint8_t trimIndex() const override { return ADC_MAIN_ST; }
  const char* titleText() const override { return STR_NB4_STEERING; }
  lv_color_t accent(int16_t) const override { return Nb4Ui::steeringColor(); }
};

BaseWidgetFactory<Nb4SteerDialWidget> nb4SteerDialWidget("NB4SteerDial", nullptr,
                                                         "Steer dial");

#endif
