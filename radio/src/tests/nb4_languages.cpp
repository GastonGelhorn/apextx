/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "gtests.h"
#include "edgetx.h"
#include "nb4_ui_languages.h"

#if defined(RADIO_NB4_FAMILY)
TEST(Nb4Languages, OnlyTheShippedLanguagesAreSelectable)
{
  ASSERT_EQ(3, LANG_COUNT);
  EXPECT_STREQ("en", languagePacks[LANG_EN]->id);
  EXPECT_STREQ("es", languagePacks[LANG_ES]->id);
  EXPECT_STREQ("de", languagePacks[LANG_DE]->id);
  EXPECT_EQ(nullptr, languagePacks[LANG_COUNT]);
  EXPECT_TRUE(isTextLangAvail(LANG_EN));
  EXPECT_TRUE(isTextLangAvail(LANG_ES));
  EXPECT_TRUE(isTextLangAvail(LANG_DE));
  EXPECT_FALSE(isTextLangAvail(-1));
  EXPECT_FALSE(isTextLangAvail(LANG_COUNT));
  EXPECT_EQ(&enLangStrings, langStrings[getLanguageId("en")]);
  EXPECT_EQ(&esLangStrings, langStrings[getLanguageId("es")]);
  EXPECT_EQ(&deLangStrings, langStrings[getLanguageId("de")]);
}

TEST(Nb4Languages, LayoutChecksCoverEveryShippedLanguage)
{
  ASSERT_EQ(DIM(NB4_UI_LANGUAGES), unsigned(LANG_COUNT))
      << "a language was added to the build but not to NB4_UI_LANGUAGES";
  for (unsigned i = 0; i < DIM(NB4_UI_LANGUAGES); ++i)
    EXPECT_STREQ(NB4_UI_LANGUAGES[i], languagePacks[i]->id);
}

TEST(Nb4Languages, StoredCodesSurviveCompactedLanguageRegistry)
{
  // Settings persist two bytes without a terminating NUL.
  const char english[2] = {'e', 'n'};
  const char spanish[2] = {'e', 's'};
  const char german[2] = {'d', 'e'};
  EXPECT_EQ(LANG_EN, getLanguageId(english));
  EXPECT_EQ(LANG_ES, getLanguageId(spanish));
  EXPECT_EQ(LANG_DE, getLanguageId(german));
  for (const char* oldCode : {"cn", "cz", "da", "fi", "fr", "he",
                              "hu", "it", "jp", "ko", "nl", "pl", "pt",
                              "ru", "se", "sk", "tw", "ua", "", "zz"}) {
    const auto index = getLanguageId(oldCode);
    EXPECT_EQ(LANG_EN, index);
    EXPECT_STREQ("en", languagePacks[index]->id);
    EXPECT_EQ(&enLangStrings, langStrings[index]);
  }
}
#endif
