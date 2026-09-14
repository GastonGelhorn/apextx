/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once

#include "translations/translations.h"

// Static data tables (route catalogue, help entries, home templates,
// palettes) cannot hold STR_NB4_* directly: with several languages compiled
// in, STR_* reads the active translation table at run time. The tables store
// an accessor instead and call it when the text is shown, so they follow the
// language the user selects without a rebuild.
typedef const char* (*Nb4Str)();

#if defined(ALL_LANGS) && !defined(BOOT)
// EdgeTX generates one STR_*_FN() accessor per translated string.
#define NB4_STR(x) (STR_NB4_##x##_FN)
#else
// Single-language build: STR_NB4_* is a constant, wrap it in a function.
#define NB4_STR(x) ([]() -> const char* { return STR_NB4_##x; })
#endif

inline const char* nb4StrOrNull(Nb4Str text) { return text ? text() : nullptr; }
