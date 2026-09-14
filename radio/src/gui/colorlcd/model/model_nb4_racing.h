/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
/*
 * Copyright (C) EdgeTX
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#pragma once

#include "libui/form.h"
#include "pagegroup.h"

#if defined(RADIO_NB4_FAMILY)

struct Nb4ChannelSlots {
  Window* steeringReverse = nullptr;
  Window* throttleReverse = nullptr;
};

void nb4BuildChannelAssignment(Window* form, FlexGridLayout& grid,
                               Nb4ChannelSlots* slots);

void nb4OpenChannelsDialog();

#endif  // RADIO_NB4_FAMILY

class ModelNb4RacingPage : public PageGroupItem
{
 public:
  ModelNb4RacingPage(PageDef& pageDef) : PageGroupItem(pageDef) {}

  void build(Window* window) override;

 protected:

  void rebuild(Window* window);

 private:
#if defined(RADIO_NB4_FAMILY)

  Nb4ChannelSlots channels;
#endif
};
