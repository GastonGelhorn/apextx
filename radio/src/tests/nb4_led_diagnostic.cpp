/* Copyright (C) Gaston Gelhorn
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "gtests.h"
#include "nb4_led_diagnostic.h"
#include "nb4_leds.h"
#include "nb4_history.h"
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

TEST(Nb4LedDiagnostic, QuietRunDropsTheModuleProtocolOnlyWhileItRuns)
{
  extern uint8_t getRequiredProtocol(uint8_t module);
  nb4LedBootComplete();
  g_model.moduleData[INTERNAL_MODULE].type = MODULE_TYPE_FLYSKY_AFHDS3;
  const uint8_t before = getRequiredProtocol(INTERNAL_MODULE);
  EXPECT_NE(before, PROTOCOL_CHANNELS_NONE);

  nb4LedDiagStart(100, 128);
  EXPECT_FALSE(nb4LedDiagQuiet());
  EXPECT_EQ(getRequiredProtocol(INTERNAL_MODULE), before);
  nb4LedDiagStop();

  nb4LedDiagStart(200, 128, true);
  EXPECT_TRUE(nb4LedDiagQuiet());
  EXPECT_EQ(getRequiredProtocol(INTERNAL_MODULE), PROTOCOL_CHANNELS_NONE);
  EXPECT_TRUE(nb4LedDiagWritePattern());
  // The test ends by itself and the module comes back without a restart.
  for (unsigned p = 1; p <= NB4_LED_DIAG_PHASES; ++p)
    nb4LedDiagTick(200 + p * NB4_LED_DIAG_PHASE_MS);
  EXPECT_FALSE(nb4LedDiagActive());
  EXPECT_FALSE(nb4LedDiagQuiet());
  EXPECT_EQ(getRequiredProtocol(INTERNAL_MODULE), before);

  // Cancelling a quiet run restores it as well.
  nb4LedDiagStart(300, 128, true);
  EXPECT_TRUE(nb4LedDiagQuiet());
  nb4LedDiagStop();
  EXPECT_FALSE(nb4LedDiagQuiet());
  EXPECT_EQ(getRequiredProtocol(INTERNAL_MODULE), before);
}

TEST(Nb4LedDiagnostic, QuietRunPausesTheStorageTaskAndResumesItAfterwards)
{
  nb4StorageResume();
  EXPECT_FALSE(nb4StoragePaused());
  nb4LedDiagStart(100, 128);
  EXPECT_FALSE(nb4StoragePaused());
  EXPECT_FALSE(nb4LedDiagStoragePaused());
  nb4LedDiagStop();

  nb4LedDiagStart(200, 128, true);
  EXPECT_TRUE(nb4StoragePaused());
  EXPECT_TRUE(nb4LedDiagStoragePaused());
  for (unsigned p = 1; p <= NB4_LED_DIAG_PHASES; ++p)
    nb4LedDiagTick(200 + p * NB4_LED_DIAG_PHASE_MS);
  EXPECT_FALSE(nb4LedDiagActive());
  EXPECT_FALSE(nb4StoragePaused());
  EXPECT_FALSE(nb4LedDiagStoragePaused());

  nb4LedDiagStart(300, 128, true);
  EXPECT_TRUE(nb4StoragePaused());
  nb4LedDiagStop();
  EXPECT_FALSE(nb4StoragePaused());
}
#endif
