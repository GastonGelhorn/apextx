/* Copyright (C) Gaston Gelhorn
 * Copyright (C) EdgeTX
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include "window.h"

#if defined(RADIO_NB4_FAMILY)

struct Nb4HelpEntry {
  const char* labelEs;
  const char* labelEn;
  const char* bodyEs;
  const char* bodyEn;
};

void nb4AddHelp(Window* form, const char* titleEs, const char* titleEn,
                const Nb4HelpEntry* entries, unsigned count);

#endif  // RADIO_NB4_FAMILY
