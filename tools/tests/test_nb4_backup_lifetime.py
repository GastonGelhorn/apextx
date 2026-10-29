# SPDX-License-Identifier: GPL-2.0-only
from pathlib import Path
import unittest
from test_nb4_usb_transitions import run_cpp
from tools.tests.test_stm32_usart_tx_mode import function


class BackupLifetimeTests(unittest.TestCase):
    def test_actual_usb_write_callback_accepts_only_exit_and_never_memory_contents(self):
        path=Path(__file__).resolve().parents[2]/'radio/src/bootloader/nb4_backup_agent.cpp'
        source=path.read_text()
        callbacks=function(source,'uint16_t write(')+function(source,'uint16_t erase(')
        model=r'''
#include <cstring>
constexpr uint16_t USBD_OK=0, USBD_FAIL=2;
volatile uint32_t milliseconds=77,exitAt=0;
volatile bool exitRequested=false;
uint32_t HAL_GetTick() { return milliseconds; }
'''
        scenario=r'''
int main() {
  uint8_t exit[]="APXEXIT1",other[]="APXSTART";
  auto control=reinterpret_cast<uint8_t*>(0x91000100);
  assert(erase(0x08000000)==USBD_FAIL);
  assert(erase(0x90000000)==USBD_FAIL);
  assert(write(exit,reinterpret_cast<uint8_t*>(0x08000000),8)==USBD_FAIL);
  assert(write(exit,reinterpret_cast<uint8_t*>(0x90000000),8)==USBD_FAIL);
  assert(write(exit,control,7)==USBD_FAIL);
  assert(write(other,control,8)==USBD_FAIL);
  assert(!exitRequested);
  assert(write(exit,control,8)==USBD_OK && exitRequested && exitAt==77);
  milliseconds=100;
  assert(write(exit,control,8)==USBD_OK && exitAt==77);
}
'''
        run_cpp(model+callbacks+scenario)

    def test_timer_interrupt_cannot_keep_a_stalled_agent_alive(self):
        path=Path(__file__).resolve().parents[2]/'radio/src/bootloader/nb4_backup_agent.cpp'
        tick=function(path.read_text(),'void SysTick_Handler(')
        # No watchdog is exposed to the timer handler: an accidental reload
        # here would fail this compilation instead of masking a stalled loop.
        run_cpp('volatile uint32_t milliseconds=0;\nvoid HAL_IncTick(){++milliseconds;}\n'+tick+
                '\nint main(){SysTick_Handler();assert(milliseconds==1);}')

    def test_startup_hold_manual_exit_idle_timeout_and_restart_are_distinct(self):
        header=Path(__file__).resolve().parents[2]/'radio/src/bootloader/nb4_backup_lifetime.h'
        run_cpp('#include "'+str(header)+'"\n'+r'''
using namespace nb4;
int main() {
  BackupLifetime policy;
  // Holding Power to enter ROM must never immediately exit the agent.
  for(unsigned t=0;t<10000;t+=10) assert(policy.poll(t,true)==BackupAction::None);
  policy.poll(10000,false);
  policy.poll(10010,true);
  assert(policy.poll(11999,true)==BackupAction::None);
  assert(policy.poll(12010,true)==BackupAction::PowerOff);
  assert(policy.poll(20000,true)==BackupAction::None);
  policy.poll(20010,false);
  policy.poll(20020,true);
  assert(policy.poll(22020,true)==BackupAction::Restart);
  BackupLifetime idle;
  assert(idle.poll(119999,false)==BackupAction::None);
  idle.activity(119999);
  assert(idle.poll(239998,false)==BackupAction::None);
  assert(idle.poll(239999,false)==BackupAction::PowerOff);
  BackupLifetime hostExit;
  hostExit.powerOff();
  assert(hostExit.poll(5000,true)==BackupAction::None);
  hostExit.poll(5001,false);
  hostExit.poll(5002,true);
  assert(hostExit.poll(7002,true)==BackupAction::Restart);
  BackupLifetime wrap;
  wrap.activity(0xffffff00u);
  assert(wrap.poll(100,false)==BackupAction::None);
}
''')


if __name__=='__main__': unittest.main()
