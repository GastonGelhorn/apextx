/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include "pagegroup.h"

#if defined(RADIO_NB4_FAMILY)

void nb4OpenThrottleTraceDialog();

void nb4SetSteeringTab(uint8_t tab);
void nb4SetThrottleTab(uint8_t tab);
uint8_t nb4TakeSteeringTab();
uint8_t nb4TakeThrottleTab();

class ModelNb4SteeringPage : public PageGroupItem
{
 public:

  ModelNb4SteeringPage(PageDef& pageDef) : PageGroupItem(pageDef)
  {
    tab = nb4TakeSteeringTab();
  }
  void build(Window* window) override;

 protected:

  void checkEvents() override;

 private:
  uint8_t tab = 0;
  int8_t pendingTab = -1;
  Window* body = nullptr;
  void rebuild(Window* window);
};

class ModelNb4ThrottlePage : public PageGroupItem
{
 public:

  ModelNb4ThrottlePage(PageDef& pageDef) : PageGroupItem(pageDef)
  {
    tab = nb4TakeThrottleTab();
  }
  void build(Window* window) override;

 protected:

  void checkEvents() override;

 private:
  uint8_t tab = 0;
  int8_t pendingTab = -1;
  Window* body = nullptr;

  void rebuild(Window* window);
};

#endif
