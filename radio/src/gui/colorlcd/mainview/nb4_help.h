/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include "nb4_i18n.h"
#include "window.h"

#if defined(RADIO_NB4_FAMILY)

struct Nb4HelpEntry {
  Nb4Str label;
  Nb4Str body;
};

void nb4AddHelp(Window* form, const char* title, const Nb4HelpEntry* entries,
                unsigned count);

#endif  // RADIO_NB4_FAMILY
