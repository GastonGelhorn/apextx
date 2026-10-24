/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "gtests.h"
#include "nb4_led_diagnostic.h"
#include "nb4_leds.h"
#include "boards/generic_stm32/rgb_leds.h"

#if defined(NB4_LED_DIAGNOSTIC)
TEST(Nb4LedDiagnostic, PatternOverridesChargingAndRestoresUserColourWithoutSaving)
{
  extern uint8_t nb4SimuChargeSource;
  nb4LedBootComplete();
  g_eeGeneral.nb4LedMode = NB4_LED_FIXED;
  g_eeGeneral.nb4LedColor = NB4_LED_BLUE;
  g_eeGeneral.nb4LedBrightness = 100;
  nb4SimuChargeSource = 1;
  const auto settings = g_eeGeneral;
  nb4LedDiagStart(100, 128);
  nb4LedRefresh();
  EXPECT_EQ(rgbGetLedColor(0), 0x800000u);
  EXPECT_EQ(rgbGetLedColor(1), 0x008000u);
  EXPECT_EQ(rgbGetLedColor(2), 0x000080u);
  EXPECT_EQ(rgbGetLedColor(3), 0x808080u);
  nb4LedDiagStop();
  nb4SimuChargeSource = 0;
  nb4LedRefresh();
  for (uint8_t i = 0; i < 4; ++i) EXPECT_EQ(rgbGetLedColor(i), 0x0000FFu);
  EXPECT_EQ(memcmp(&settings, &g_eeGeneral, sizeof(settings)), 0);
}

TEST(Nb4LedDiagnostic, FinishesAcrossClockWrapAndCancelAllowsRestart)
{
  const uint32_t start = UINT32_MAX - 1000;
  nb4LedDiagStart(start, 255);
  nb4LedDiagTick(start + NB4_LED_DIAG_PHASE_MS - 1);
  EXPECT_EQ(nb4LedDiagPhase(), 0u);
  for (unsigned p = 1; p <= NB4_LED_DIAG_PHASES; ++p) {
    nb4LedDiagTick(start + p * NB4_LED_DIAG_PHASE_MS);
    EXPECT_EQ(nb4LedDiagPhase(), p);
    EXPECT_EQ(nb4LedDiagLcdPaused(), p == 1);
  }
  EXPECT_FALSE(nb4LedDiagActive());
  nb4LedDiagStart(10, 128);
  nb4LedDiagTick(10 + NB4_LED_DIAG_PHASE_MS);
  EXPECT_EQ(nb4LedDiagPhase(), 1u);
  nb4LedDiagStop();
  EXPECT_FALSE(nb4LedDiagActive());
  EXPECT_FALSE(nb4LedDiagLcdPaused());
  nb4LedDiagStart(20, 255);
  EXPECT_EQ(nb4LedDiagPhase(), 0u);
  EXPECT_TRUE(nb4LedDiagWritePattern());
  EXPECT_EQ(rgbGetLedColor(3), 0xFFFFFFu);
  nb4LedDiagStop();
}
#endif
