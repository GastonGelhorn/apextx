/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

// Every interface language the NB4 build compiles, in registry order. The
// layout, glyph and fit checks iterate this list, so a new language is held to
// the same rules as the others. Nb4Languages checks it against the compiled
// registry, so adding a language without adding it here fails a test.
constexpr const char* NB4_UI_LANGUAGES[] = {"en", "es", "de"};
