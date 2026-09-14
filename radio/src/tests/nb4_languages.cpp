/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "gtests.h"
#include "edgetx.h"

#if defined(RADIO_NB4_FAMILY)
TEST(Nb4Languages, OnlyEnglishAndSpanishAreSelectable)
{
  ASSERT_EQ(2, LANG_COUNT);
  EXPECT_STREQ("en", languagePacks[LANG_EN]->id);
  EXPECT_STREQ("es", languagePacks[LANG_ES]->id);
  EXPECT_EQ(nullptr, languagePacks[LANG_COUNT]);
  EXPECT_TRUE(isTextLangAvail(LANG_EN));
  EXPECT_TRUE(isTextLangAvail(LANG_ES));
  EXPECT_FALSE(isTextLangAvail(-1));
  EXPECT_FALSE(isTextLangAvail(LANG_COUNT));
  EXPECT_EQ(&enLangStrings, langStrings[getLanguageId("en")]);
  EXPECT_EQ(&esLangStrings, langStrings[getLanguageId("es")]);
}

TEST(Nb4Languages, StoredCodesSurviveCompactedLanguageRegistry)
{
  // Settings persist two bytes without a terminating NUL.
  const char english[2] = {'e', 'n'};
  const char spanish[2] = {'e', 's'};
  EXPECT_EQ(LANG_EN, getLanguageId(english));
  EXPECT_EQ(LANG_ES, getLanguageId(spanish));
  for (const char* oldCode : {"cn", "cz", "da", "de", "fi", "fr", "he",
                              "hu", "it", "jp", "ko", "nl", "pl", "pt",
                              "ru", "se", "sk", "tw", "ua", "", "zz"}) {
    const auto index = getLanguageId(oldCode);
    EXPECT_EQ(LANG_EN, index);
    EXPECT_STREQ("en", languagePacks[index]->id);
    EXPECT_EQ(&enLangStrings, langStrings[index]);
  }
}
#endif
