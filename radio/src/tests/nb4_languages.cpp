/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "gtests.h"
#include "edgetx.h"
#include "nb4_ui_languages.h"

#if defined(RADIO_NB4_FAMILY)
#include "widget.h"

TEST(Nb4Languages, OnlyTheShippedLanguagesAreSelectable)
{
  ASSERT_EQ(4, LANG_COUNT);
  EXPECT_STREQ("en", languagePacks[LANG_EN]->id);
  EXPECT_STREQ("es", languagePacks[LANG_ES]->id);
  EXPECT_STREQ("de", languagePacks[LANG_DE]->id);
  EXPECT_STREQ("cn", languagePacks[LANG_CN]->id);
  EXPECT_EQ(nullptr, languagePacks[LANG_COUNT]);
  EXPECT_TRUE(isTextLangAvail(LANG_EN));
  EXPECT_TRUE(isTextLangAvail(LANG_ES));
  EXPECT_TRUE(isTextLangAvail(LANG_DE));
  EXPECT_TRUE(isTextLangAvail(LANG_CN));
  EXPECT_FALSE(isTextLangAvail(-1));
  EXPECT_FALSE(isTextLangAvail(LANG_COUNT));
  EXPECT_EQ(&enLangStrings, langStrings[getLanguageId("en")]);
  EXPECT_EQ(&esLangStrings, langStrings[getLanguageId("es")]);
  EXPECT_EQ(&deLangStrings, langStrings[getLanguageId("de")]);
  EXPECT_EQ(&cnLangStrings, langStrings[getLanguageId("cn")]);
}

TEST(Nb4Languages, LayoutChecksCoverEveryShippedLanguage)
{
  ASSERT_EQ(DIM(NB4_UI_LANGUAGES), unsigned(LANG_COUNT))
      << "a language was added to the build but not to NB4_UI_LANGUAGES";
  for (unsigned i = 0; i < DIM(NB4_UI_LANGUAGES); ++i)
    EXPECT_STREQ(NB4_UI_LANGUAGES[i], languagePacks[i]->id);
}

TEST(Nb4Languages, AFreshInstallStartsInEnglish)
{
  memset(g_eeGeneral.uiLanguage, 0, sizeof(g_eeGeneral.uiLanguage));
  generalDefaultUILanguage();
  EXPECT_EQ(0, strncmp(g_eeGeneral.uiLanguage, "en", 2));
  EXPECT_EQ(LANG_EN, getLanguageId(g_eeGeneral.uiLanguage));
}

TEST(Nb4Languages, WidgetNamesFollowTheLanguageAndKeepTheirIds)
{
  char saved[sizeof(g_eeGeneral.uiLanguage)];
  memcpy(saved, g_eeGeneral.uiLanguage, sizeof(saved));
  for (const char* code : NB4_UI_LANGUAGES) {
    memcpy(g_eeGeneral.uiLanguage, code, 2);
    nb4TestApplyUiLanguage();
    auto racing = WidgetFactory::getWidgetFactory("NB4Racing");
    ASSERT_NE(racing, nullptr) << code;
    EXPECT_STREQ(racing->getName(), "NB4Racing") << code;
    EXPECT_STREQ(racing->getDisplayName(), STR_NB4_WIDGET_RACING) << code;
  }
  memcpy(g_eeGeneral.uiLanguage, saved, sizeof(saved));
  nb4TestApplyUiLanguage();
}

TEST(Nb4Languages, StoredCodesSurviveCompactedLanguageRegistry)
{
  // Settings persist two bytes without a terminating NUL.
  const char english[2] = {'e', 'n'};
  const char spanish[2] = {'e', 's'};
  const char german[2] = {'d', 'e'};
  const char chinese[2] = {'c', 'n'};
  EXPECT_EQ(LANG_EN, getLanguageId(english));
  EXPECT_EQ(LANG_ES, getLanguageId(spanish));
  EXPECT_EQ(LANG_DE, getLanguageId(german));
  EXPECT_EQ(LANG_CN, getLanguageId(chinese));
  for (const char* oldCode : {"cz", "da", "fi", "fr", "he",
                              "hu", "it", "jp", "ko", "nl", "pl", "pt",
                              "ru", "se", "sk", "tw", "ua", "", "zz"}) {
    const auto index = getLanguageId(oldCode);
    EXPECT_EQ(LANG_EN, index);
    EXPECT_STREQ("en", languagePacks[index]->id);
    EXPECT_EQ(&enLangStrings, langStrings[index]);
  }
}

TEST(Nb4Languages, ChineseGlyphsCoverEveryCompiledStringWithoutChangingVoice)
{
  char saved[sizeof(g_eeGeneral.uiLanguage)];
  memcpy(saved, g_eeGeneral.uiLanguage, sizeof(saved));
  const auto voice = currentLanguagePack;
  const auto voiceIndex = currentLanguagePackIdx;
  memcpy(g_eeGeneral.uiLanguage, "cn", 2);
  nb4TestApplyUiLanguage();
  unsigned checked = 0;
  auto check = [&](const char* text, const char* name) {
    for (uint32_t i = 0; text && text[i];) {
      const auto c = _lv_txt_encoded_next(text, &i);
      if (c < 0x2000 || c > 0xFFEF) continue;
      for (LcdFlags size : {FONT(XXS), FONT(XS), FONT(STD), FONT(BOLD),
                            FONT(L), FONT(XL)}) {
        lv_font_glyph_dsc_t glyph;
        EXPECT_TRUE(lv_font_get_glyph_dsc(getFont(size), &glyph, c, 0))
            << name << ": U+" << std::hex << c;
      }
      ++checked;
    }
  };
#define STR(name) check(STR_##name, #name);
#define STRARRAY(name)
#include "translations/string_list.h"
#undef STR
#undef STRARRAY
  check(STR_NB4_MODES[0], "NB4_MODES[0]");
  check(STR_NB4_MODES[1], "NB4_MODES[1]");
  EXPECT_GT(checked, 1000u);
  EXPECT_EQ(voice, currentLanguagePack);
  EXPECT_EQ(voiceIndex, currentLanguagePackIdx);
  memcpy(g_eeGeneral.uiLanguage, saved, sizeof(saved));
  nb4TestApplyUiLanguage();
}
#endif
