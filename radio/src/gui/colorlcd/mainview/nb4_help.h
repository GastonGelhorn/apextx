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

std::function<void()> nb4InheritedHelp();
std::string nb4InheritedScope();
class StaticText;
void nb4ScopeBar(Window* owner, Window* body, StaticText*& label,
                 const std::string& text);
void nb4DismissHelp();

// Fits a page or dialog title into a header bar: one bold line when it fits,
// two bold lines when the bar has room, two small lines otherwise. Sets the
// font and wrap mode and returns the text's height, for the caller to centre.
coord_t nb4FitHeaderTitle(lv_obj_t* label, coord_t width, coord_t barHeight);

void nb4AddHelp(Window* form, const char* title, const Nb4HelpEntry* entries,
                unsigned count);

#endif  // RADIO_NB4_FAMILY
