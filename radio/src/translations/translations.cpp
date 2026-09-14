/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "edgetx.h"
#include "translations/translation_def.h"

#if defined(TRANSLATIONS_FR)
#include "translations/i18n/fr.h"
#elif defined(TRANSLATIONS_IT)
#include "translations/i18n/it.h"
#elif defined(TRANSLATIONS_DA)
#include "translations/i18n/da.h"
#elif defined(TRANSLATIONS_SE)
#include "translations/i18n/se.h"
#elif defined(TRANSLATIONS_FI)
#include "translations/i18n/fi.h"
#elif defined(TRANSLATIONS_DE)
#include "translations/i18n/de.h"
#elif defined(TRANSLATIONS_CZ)
#include "translations/i18n/cz.h"
#elif defined(TRANSLATIONS_ES)
#include "translations/i18n/es.h"
#elif defined(TRANSLATIONS_PL)
#include "translations/i18n/pl.h"
#elif defined(TRANSLATIONS_PT)
#include "translations/i18n/pt.h"
#elif defined(TRANSLATIONS_NL)
#include "translations/i18n/nl.h"
#elif defined(TRANSLATIONS_CN)
#include "translations/i18n/cn.h"
#elif defined(TRANSLATIONS_TW)
#include "translations/i18n/tw.h"
#elif defined(TRANSLATIONS_JP)
#include "translations/i18n/jp.h"
#elif defined(TRANSLATIONS_RU)
#include "translations/i18n/ru.h"
#elif defined(TRANSLATIONS_HE)
#include "translations/i18n/he.h"
#elif defined(TRANSLATIONS_KO)
#include "translations/i18n/ko.h"
#elif defined(TRANSLATIONS_UA)
#include "translations/i18n/ua.h"
#else
#include "translations/i18n/en.h"
#endif

uint8_t getLanguageId(const char* lang)
{
  for (uint8_t i = 0; languagePacks[i] != nullptr; i++) {
    if (!strncmp(lang, languagePacks[i]->id, 2)) {
      return i;
    }
  }
  return LANG_EN;
}

const char CHR_HOUR = TR_CHR_HOUR;
const char CHR_INPUT = TR_CHR_INPUT;

#if !defined(ALL_LANGS) || defined(BOOT)

// Static string
#define STR(x) const char STR_##x[] = TR_##x;
// Static string array
#define STRARRAY(x) const char* const STR_##x[] = { TR_##x };

#include "string_list.h"
#include "string_list_notrans.h"

#undef STR
#undef STRARRAY

#else

bool isTextLangAvail(int lang)
{
#if defined(RADIO_NB4_FAMILY)
  return lang == LANG_EN || lang == LANG_ES;
#elif defined(RADIO_LANG_SUBSET)

  switch (lang) {
#if defined(HAS_LANG_CN)
    case LANG_CN: return true;
#endif
#if defined(HAS_LANG_CZ)
    case LANG_CZ: return true;
#endif
#if defined(HAS_LANG_DA)
    case LANG_DA: return true;
#endif
#if defined(HAS_LANG_DE)
    case LANG_DE: return true;
#endif
#if defined(HAS_LANG_EN)
    case LANG_EN: return true;
#endif
#if defined(HAS_LANG_ES)
    case LANG_ES: return true;
#endif
#if defined(HAS_LANG_FI)
    case LANG_FI: return true;
#endif
#if defined(HAS_LANG_FR)
    case LANG_FR: return true;
#endif
#if defined(HAS_LANG_HE)
    case LANG_HE: return true;
#endif
#if defined(HAS_LANG_HU)
    case LANG_HU: return true;
#endif
#if defined(HAS_LANG_IT)
    case LANG_IT: return true;
#endif
#if defined(HAS_LANG_JP)
    case LANG_JP: return true;
#endif
#if defined(HAS_LANG_KO)
    case LANG_KO: return true;
#endif
#if defined(HAS_LANG_NL)
    case LANG_NL: return true;
#endif
#if defined(HAS_LANG_PL)
    case LANG_PL: return true;
#endif
#if defined(HAS_LANG_PT)
    case LANG_PT: return true;
#endif
#if defined(HAS_LANG_RU)
    case LANG_RU: return true;
#endif
#if defined(HAS_LANG_SE)
    case LANG_SE: return true;
#endif
#if defined(HAS_LANG_SK)
    case LANG_SK: return true;
#endif
#if defined(HAS_LANG_TW)
    case LANG_TW: return true;
#endif
#if defined(HAS_LANG_UA)
    case LANG_UA: return true;
#endif
    default: return false;
  }
#elif defined(COLORLCD)
  // Skip languages with no translation files
  return lang != LANG_HU && lang != LANG_SK;
#else
  // Skip languages with no translation files or no unicode fonts
  return lang != LANG_CN && lang != LANG_HE && lang != LANG_HU &&
         lang != LANG_JP && lang != LANG_KO && lang != LANG_SK &&
         lang != LANG_TW;
#endif
}

#if defined(RADIO_NB4_FAMILY)
const LangStrings* const langStrings[] = { &enLangStrings, &esLangStrings };
#elif defined(COLORLCD)
const LangStrings* const langStrings[] = {
#if defined(HAS_LANG_CN) || !defined(RADIO_LANG_SUBSET)
  &cnLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_CZ) || !defined(RADIO_LANG_SUBSET)
  &czLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_DA) || !defined(RADIO_LANG_SUBSET)
  &daLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_DE) || !defined(RADIO_LANG_SUBSET)
  &deLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_EN) || !defined(RADIO_LANG_SUBSET)
  &enLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_ES) || !defined(RADIO_LANG_SUBSET)
  &esLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_FI) || !defined(RADIO_LANG_SUBSET)
  &fiLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_FR) || !defined(RADIO_LANG_SUBSET)
  &frLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_HE) || !defined(RADIO_LANG_SUBSET)
  &heLangStrings,
#else
  &enLangStrings,
#endif
  &enLangStrings,
#if defined(HAS_LANG_IT) || !defined(RADIO_LANG_SUBSET)
  &itLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_JP) || !defined(RADIO_LANG_SUBSET)
  &jpLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_KO) || !defined(RADIO_LANG_SUBSET)
  &koLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_NL) || !defined(RADIO_LANG_SUBSET)
  &nlLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_PL) || !defined(RADIO_LANG_SUBSET)
  &plLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_PT) || !defined(RADIO_LANG_SUBSET)
  &ptLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_RU) || !defined(RADIO_LANG_SUBSET)
  &ruLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_SE) || !defined(RADIO_LANG_SUBSET)
  &seLangStrings,
#else
  &enLangStrings,
#endif
  &enLangStrings,
#if defined(HAS_LANG_TW) || !defined(RADIO_LANG_SUBSET)
  &twLangStrings,
#else
  &enLangStrings,
#endif
#if defined(HAS_LANG_UA) || !defined(RADIO_LANG_SUBSET)
  &uaLangStrings,
#else
  &enLangStrings,
#endif
};
#else
const LangStrings* const langStrings[] = {
  &enLangStrings,
  &czLangStrings,
  &daLangStrings,
  &deLangStrings,
  &enLangStrings,
  &esLangStrings,
  &fiLangStrings,
  &frLangStrings,
  &enLangStrings,
  &enLangStrings,
  &itLangStrings,
  &enLangStrings,
  &enLangStrings,
  &enLangStrings,
  &plLangStrings,
  &ptLangStrings,
  &ruLangStrings,
  &seLangStrings,
  &enLangStrings,
  &enLangStrings,
  &uaLangStrings,
};
#endif
const LangStrings* currentLangStrings = &enLangStrings;

// Static string
#define STR(x) const char STR_##x[] = TR_##x;
// Static string array
#define STRARRAY(x) const char* const STR_##x[] = { TR_##x };

#include "string_list_notrans.h"

#undef STR
#undef STRARRAY

// Static string
#define STR(x) const char* STR_##x##_FN() { return STR_##x; }
// Static string array
#define STRARRAY(x) const char* const* STR_##x##_FN() { return STR_##x; }

#include "string_list.h"
#include "string_list_notrans.h"

#undef STR
#undef STRARRAY

#endif
