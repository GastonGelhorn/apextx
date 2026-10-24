# SPDX-License-Identifier: GPL-2.0-only
"""Run the actual LED refresh and battery curve against live charger pins."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from tools.tests.test_stm32_usart_tx_mode import function

ROOT = pathlib.Path(__file__).resolve().parents[2]


class LedRefresh(unittest.TestCase):
    def test_battery_units_and_charger_changes_reach_every_led_without_restart(self):
        if not shutil.which('c++'):
            self.skipTest('A native C++ compiler is unavailable')
        leds = (ROOT/'radio/src/nb4_leds.cpp').read_text()
        battery = function((ROOT/'radio/src/nb4_car_state.cpp').read_text(), 'uint8_t nb4BatteryPercent(')
        header = r'''
#pragma once
#include <cstdint>
#include <cassert>
#define RADIO_NB4_FAMILY
#define LED_STRIP_GPIO 1
#define LED_STRIP_LENGTH 4
struct Settings { uint8_t nb4LedMode, nb4LedColor; };
extern Settings g_eeGeneral;
extern uint16_t g_vbat100mV;
uint32_t timersGetMsTick();
uint8_t nb4ChargeSource();
'''
        scenario = r'''
#include "edgetx.h"
#include "nb4_leds.h"
Settings g_eeGeneral{};
uint16_t g_vbat100mV=41;
uint8_t charge=0, nb4SimuChargeSource=0;
uint32_t now=0;
int liveReads=0, publications=0;
Nb4LedRgb pixels[LED_STRIP_LENGTH]{};
// Simulate starting with USB attached: the old generic state stays latched.
bool isChargerActive() { return true; }
uint8_t nb4ChargeSource() { ++liveReads; return charge; }
uint32_t timersGetMsTick() { return now; }
void rgbSetLedColor(uint8_t i,uint8_t r,uint8_t g,uint8_t b) { pixels[i]={r,g,b}; }
void rgbLedColorApply() { ++publications; }
void source(uint8_t value) { charge=nb4SimuChargeSource=value; }
void expect(Nb4LedRgb colour) {
  for (const auto pixel:pixels) assert(pixel==colour);
}
int main() {
  // 4.1 V must show a healthy battery rather than breathing red at 0%.
  source(0); g_eeGeneral={NB4_LED_BATTERY,NB4_LED_BLUE};
  nb4LedRefresh(); expect(nb4LedPalette(NB4_LED_GREEN));
  g_vbat100mV=36; nb4LedRefresh(); expect(nb4LedPalette(NB4_LED_ORANGE));
  g_vbat100mV=33; nb4LedRefresh();
  for(const auto pixel:pixels) assert(pixel.r>0 && pixel.g==0 && pixel.b==0);
  // Plug/unplug USB or the base repeatedly: the user colour returns at once.
  g_vbat100mV=41; g_eeGeneral={NB4_LED_FIXED,NB4_LED_BLUE};
  for(uint8_t connection:{2,0,1,0,2,0}) {
    source(connection); nb4LedRefresh();
    if(connection) {
      for(const auto pixel:pixels) assert(pixel.g>0 && pixel.r==0 && pixel.b==0);
    } else expect(nb4LedPalette(NB4_LED_BLUE));
  }
  // Charging a full cell is steady green, independent of breathing time.
  source(2); g_vbat100mV=42;
  now=0; nb4LedRefresh(); expect(nb4LedPalette(NB4_LED_GREEN));
  now=1200; nb4LedRefresh(); expect(nb4LedPalette(NB4_LED_GREEN));
  g_eeGeneral.nb4LedMode=NB4_LED_OFF;
  nb4LedRefresh(); expect({0,0,0});
  assert(publications>10);
#if !defined(SIMU)
  assert(liveReads>10);
#endif
}
'''
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            (folder/'edgetx.h').write_text(header)
            (folder/'leds.cpp').write_text(leds)
            (folder/'test.cpp').write_text('#include <initializer_list>\n'+scenario.replace('int main()', battery+'\nint main()'))
            for flags in (['-DRADIO_NB4'], ['-DRADIO_NB4','-DRADIO_NB4PLUS_HW'], ['-DRADIO_NB4','-DSIMU']):
                with self.subTest(flags=flags):
                    command = ['c++','-std=c++17',*flags,'-I',str(folder),'-I',str(ROOT/'radio/src'),
                               str(folder/'leds.cpp'),str(folder/'test.cpp'),'-o',str(folder/'test')]
                    result = subprocess.run(command,capture_output=True,text=True)
                    self.assertEqual(result.returncode,0,result.stderr)
                    subprocess.run([str(folder/'test')],check=True)


if __name__ == '__main__':
    unittest.main()
