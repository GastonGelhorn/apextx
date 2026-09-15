/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include "widgets_container.h"
#if defined(RADIO_NB4_FAMILY)
#include "nb4_car_state.h"
class StaticText;
class Nb4RacePanel;
class Nb4Dial;
class Nb4Column;
class Nb4Telltale;
class Nb4Chrono;
class Nb4Stats;

enum class Nb4Section { Car, Race, System, Steering, Throttle, Auxiliary, Advanced, Appearance, Cards, Telemetry, History, Chrono, Pit, Backup };
void nb4OpenDataPage(Nb4Section section);
void nb4OpenRaceRecord(uint32_t id);
void nb4OpenSection(Nb4Section section);
void nb4Navigate(Nb4Section section);
void nb4BuildAppearance(Window* parent);
void nb4BuildCards(Window* parent);
void nb4RequestOrientation(bool landscape, bool reopenAppearance = true);
void nb4ProcessOrientation();

// True while an orientation change is still waiting for the open pages to
// close. The request is dropped after a bounded number of attempts, so this
// always returns to false whether or not the change went through.
bool nb4OrientationChangePending();

// The only native Home. Persistent objects update from native readings.
class Nb4HomeScreen : public WidgetsContainer
{
 public:
  explicit Nb4HomeScreen(Window* parent, const rect_t& rect);
  unsigned getZonesCount() const override { return 0; }
  rect_t getZone(unsigned) const override { return {}; }
  Widget* createWidget(unsigned, const WidgetFactory*) override { return nullptr; }
  void checkEvents() override;

 private:
  void build();
  void refresh(const Nb4CarState& state);
  bool spanish = false;
  bool blockedModel = false;
  uint32_t builtTheme = 0;

  uint32_t builtIdentity = 0;
  uint8_t builtPanel = 0xff;
  coord_t builtWidth = 0, builtHeight = 0;
  StaticText* name = nullptr;
  Nb4Telltale* telltales[3] = {};
  Nb4Dial* dial = nullptr;
  Nb4Column* column = nullptr;
  Nb4Chrono* chrono = nullptr;
  Nb4Stats* stats = nullptr;
};
#endif
