# SPDX-License-Identifier: GPL-2.0-only
"""Exercise firmware USB transitions with host-side class/IRQ and task models."""
import pathlib
import subprocess
import tempfile
import unittest
from tools.tests.test_stm32_usart_tx_mode import function

ROOT = pathlib.Path(__file__).resolve().parents[2]


def actual(path, *signatures):
    source = (ROOT / 'radio/src' / path).read_text()
    return '\n'.join(function(source, signature) for signature in signatures)


def run_cpp(source):
    with tempfile.TemporaryDirectory() as directory:
        path = pathlib.Path(directory)
        (path / 'test.cpp').write_text('#include <cassert>\n#include <cstdint>\n#include <cstddef>\n' + source)
        result = subprocess.run(['c++', '-std=c++17', str(path / 'test.cpp'), '-o', str(path / 'test')], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        subprocess.run([str(path / 'test')], check=True)


class Nb4UsbTransitions(unittest.TestCase):
    def test_base_pack_uses_host_data_lines_and_reprobes_after_disconnect(self):
        source = (ROOT / 'radio/src/targets/pl18/board.cpp').read_text()
        probe = source[source.index('static volatile uint8_t _nb4UsbLines ='):
                       source.index('static uint32_t _nb4TickLast =')]
        session = source[source.index('#define NB4_USB_ANSWER_10MS'):
                         source.index('static uint8_t nb4UsbConnected()')]
        firmware = probe + session + actual('targets/pl18/board.cpp', 'static uint8_t nb4UsbConnected(')
        model = r'''
constexpr int USB_GPIO_DP=0, USB_GPIO_DM=1, NB4_CHARGE_BASE=1, NB4_CHARGE_USB=2;
uint32_t now=1000;
uint8_t chargeRaw=NB4_CHARGE_BASE, hostLines=0;
int probes=0;
bool started=false, enumerated=false, frames=false;
#define NB4_CHARGE_RAW() chargeRaw
uint32_t nb4Tick10ms(){return now;}
bool usbStarted(){return started;}
bool usbHostEnumerated(){return enumerated;}
bool usbHostSessionAlive(){return frames;}
uint8_t nb4UsbLinePulledDown(int pin){assert(!started);++probes;return (hostLines>>pin)&1;}
'''
        scenario = r'''
int main(){
 // A base pack alone and a single pulled-down line are not USB hosts.
 assert(!nb4UsbConnected() && probes==2);
 hostLines=1;now+=25;assert(!nb4UsbConnected() && probes==4);
 hostLines=3;now+=25;assert(nb4UsbConnected() && probes==6);
 started=true;assert(nb4UsbConnected());
 now+=199;assert(nb4UsbConnected());
 enumerated=frames=true;assert(nb4UsbConnected());
 // Frames cancel a pending disconnect; 500 ms of silence ends the session.
 frames=false;assert(nb4UsbConnected());
 now+=49;assert(nb4UsbConnected());
 frames=true;assert(nb4UsbConnected());
 frames=false;assert(nb4UsbConnected());
 now+=50;assert(!nb4UsbConnected());
 // Stopping the stack must discard cached host lines and probe immediately.
 int before=probes;started=false;hostLines=0;
 assert(!nb4UsbConnected() && probes==before+2);
 now+=25;hostLines=3;assert(nb4UsbConnected());
 // Repeated sessions work both on and off the base.
 for(int i=0;i<4;++i){
   chargeRaw=i%2?NB4_CHARGE_BASE:0;
   started=enumerated=frames=true;assert(nb4UsbConnected());
   frames=false;assert(nb4UsbConnected());now+=50;assert(!nb4UsbConnected());
   started=false;hostLines=0;assert(!nb4UsbConnected());
   now+=25;hostLines=3;assert(nb4UsbConnected());
 }
 // An unresponsive charger is suppressed after the enumeration timeout.
 chargeRaw=NB4_CHARGE_BASE;started=true;enumerated=false;assert(nb4UsbConnected());
 now+=200;assert(!nb4UsbConnected());
 started=false;assert(!nb4UsbConnected());
 now+=29999;assert(!nb4UsbConnected());
 ++now;assert(nb4UsbConnected());
 // A charge-source change also permits a fresh connection attempt.
 started=true;assert(nb4UsbConnected());now+=200;assert(!nb4UsbConnected());
 started=false;chargeRaw=0;assert(nb4UsbConnected());
}
'''
        run_cpp(model + firmware + scenario)

    def test_nb4_host_session_tracks_usb_frames(self):
        firmware = actual(
            'targets/common/arm/stm32/usb_driver.cpp',
            'extern "C" void usbHostSofReceived(',
            'bool usbHostSessionAlive(')
        model = r'''
#define RADIO_NB4
bool usbDriverStarted=false;
volatile uint32_t usbSofCounter=0;
uint32_t usbSofObserved=0;
'''
        scenario = r'''
int main(){
 assert(!usbHostSessionAlive());
 usbDriverStarted=true;
 assert(!usbHostSessionAlive());
 usbHostSofReceived();assert(usbHostSessionAlive());
 assert(!usbHostSessionAlive());
 for(int i=0;i<5;++i)usbHostSofReceived();
 assert(usbHostSessionAlive());assert(!usbHostSessionAlive());
}
'''
        run_cpp(model + firmware + scenario)

        usb_conf = (ROOT / 'radio/src/targets/common/arm/stm32/usbd_conf.c').read_text()
        sof_callback = function(usb_conf, 'void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)')
        self.assertIn('usbHostSofReceived();', sof_callback)

    def test_unmounted_storage_can_be_handed_to_usb_for_recovery(self):
        firmware = actual('nb4_history.cpp', 'bool nb4StorageQuiesce(')
        model = r'''
#include <atomic>
bool started=true, mounted=false, pending=true;
std::atomic<bool> paused{false}, busy{false};
bool sdMounted(){return mounted;}
bool nb4SettingsPending(){return pending;}
'''
        scenario = r'''
int main(){
 assert(nb4StorageQuiesce());assert(paused.load());
 paused=false;mounted=true;assert(!nb4StorageQuiesce());assert(!paused.load());
 pending=false;assert(nb4StorageQuiesce());assert(paused.load());
 paused=false;busy=true;assert(!nb4StorageQuiesce());assert(paused.load());
}
'''
        run_cpp(model + firmware + scenario)

    def test_full_nor_format_erases_old_ftl_before_mkfs(self):
        firmware = actual('sdcard.cpp', 'bool nb4RequestFilesystemCreation(')
        erase = firmware.index('spiFlashDiskEraseAll()')
        create = firmware.index('f_mkfs(')
        self.assertLess(firmware.index('sdDone()'), erase)
        self.assertLess(erase, firmware.index('storageInit()'))
        self.assertLess(firmware.index('storageInit()'), create)
        self.assertIn('_g_FATFS_init = _nb4LastMountResult == FR_OK;', firmware)

    def test_handoff_waits_for_audio_and_storage_and_recovers_on_unplug(self):
        firmware = actual('main.cpp', 'void handleUsbConnection(')
        model = r'''
#define STM32
#define RADIO_NB4
#define RADIO_NB4_FAMILY
#define COLORLCD
#define USB_SERIAL
#define TRACE(...) (void)0
constexpr int USB_UNSELECTED_MODE=0, USB_MASS_STORAGE_MODE=1, USB_SERIAL_MODE=2, USB_JOYSTICK_MODE=3, SP_VCP=0;
int selected=0, starts=0, closes=0, resumes=0, serialStarts=0, serialStops=0;
int storageSyncs=0, powerOns=0, resets=0;
bool plugged=false, started=false, audioReady=false, storageReady=false, _usbDisabled=false;
struct {int USBMode=0;} g_eeGeneral;
struct Audio {int resumed=0; bool pauseFiles(){return audioReady;} void resumeFiles(){++resumed;}} audioQueue;
struct UsbSDConnected {static int count; UsbSDConnected(){++count;} void deleteLater(){--count;delete this;}};
int UsbSDConnected::count=0;
UsbSDConnected* usbConnectedWindow=nullptr;
constexpr int CTRL_SYNC=0;
struct DiskDriver {int (*ioctl)(int,int,void*);};
int storageIoctl(int,int command,void*){assert(command==CTRL_SYNC);++storageSyncs;return 0;}
DiskDriver storageDriver{storageIoctl};
const DiskDriver* storageGetDefaultDriver(){return &storageDriver;}
void pwrOn(){++powerOns;}
int resumeRequests = 0;
void abnormalRebootRequestResume(){++resumeRequests;}
void NVIC_SystemReset(){assert(resumeRequests);++resets;usbConnectedWindow=nullptr;UsbSDConnected::count=0;}
bool usbPlugged(){return plugged;} bool usbStarted(){return started;}
int getSelectedUsbMode(){return selected;} void setSelectedUsbMode(int value){selected=value;}
void openUsbMenu(){} void closeUsbMenu(){} bool nb4StorageQuiesce(){return storageReady;}
void nb4StorageResume(){++resumes;}
void edgeTxClose(bool){assert(audioReady && storageReady);++closes;}
void edgeTxResume(){++resumes;audioQueue.resumeFiles();}
void serialInit(int,int){++serialStarts;} int serialGetMode(int){return 1;}
void serialStop(int){++serialStops;}
void usbStart(){assert(selected!=USB_MASS_STORAGE_MODE || (audioReady&&storageReady));started=true;++starts;}
void usbStop(){started=false;}
'''
        scenario = r'''
int main(){
 plugged=true;selected=USB_MASS_STORAGE_MODE;
 handleUsbConnection();assert(!started && !usbConnectedWindow && closes==0);
 audioReady=true;handleUsbConnection();assert(!started && closes==0);
 // A cable removed during the deferred transition must resume local tasks.
 plugged=false;handleUsbConnection();assert(selected==0 && resumes==1 && audioQueue.resumed==1);
 plugged=true;selected=USB_MASS_STORAGE_MODE;storageReady=true;
 handleUsbConnection();assert(started && closes==1 && usbConnectedWindow && UsbSDConnected::count==1);
 handleUsbConnection();assert(starts==1 && closes==1);
 plugged=false;handleUsbConnection();assert(!started && !usbConnectedWindow && UsbSDConnected::count==0 && selected==0);
 assert(storageSyncs==1 && powerOns==1 && resets==1 && resumes==1);
 plugged=true;selected=USB_SERIAL_MODE;handleUsbConnection();assert(started && serialStarts==1 && closes==1);
 plugged=false;handleUsbConnection();assert(!started && serialStops==1 && selected==0);
 assert(powerOns==1 && resets==1 && resumeRequests==1 && storageSyncs==1);
 plugged=true;selected=USB_SERIAL_MODE;handleUsbConnection();assert(started && serialStarts==2);
 plugged=false;handleUsbConnection();assert(serialStops==2);
 assert(powerOns==1 && resets==1 && resumeRequests==1 && storageSyncs==1);
 for(int i=0;i<3;++i){
   plugged=true;selected=USB_JOYSTICK_MODE;handleUsbConnection();assert(started && closes==1);
   plugged=false;handleUsbConnection();assert(!started && selected==USB_UNSELECTED_MODE);
   assert(powerOns==1 && resets==1 && resumeRequests==1 && storageSyncs==1);
 }
 // Cancelling the chooser or charging without selecting a mode never resets.
 plugged=true;handleUsbConnection();assert(!started);
 plugged=false;handleUsbConnection();assert(powerOns==1 && resets==1);
 // A subsequent storage session must still sync and restart exactly once.
 plugged=true;selected=USB_MASS_STORAGE_MODE;handleUsbConnection();assert(started && closes==2);
 plugged=false;handleUsbConnection();
 assert(!started && selected==USB_UNSELECTED_MODE && !usbConnectedWindow);
 assert(storageSyncs==2 && powerOns==2 && resets==2 && resumeRequests==2);
 handleUsbConnection();assert(powerOns==2 && resets==2);
}
'''
        run_cpp(model + firmware + scenario)

    def test_nb4_storage_io_uses_bounded_watchdog_grace(self):
        read = actual('targets/common/arm/stm32/usbd_storage_msd.cpp',
                      'int8_t STORAGE_Read (uint8_t lun,')
        write = actual('targets/common/arm/stm32/usbd_storage_msd.cpp',
                       'int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, '
                       'uint32_t blk_addr, uint16_t blk_len)\n{')
        for operation in (read, write):
            self.assertNotIn('UINT32_MAX', operation)
            self.assertIn('WATCHDOG_SUSPEND(1000', operation)

    def test_irq_is_enabled_after_registration_and_disabled_before_class_teardown(self):
        firmware = actual('targets/common/arm/stm32/usb_driver.cpp', 'void usbStart(', 'void usbStop(')
        model = r'''
#define RADIO_NB4
#define USB_SERIAL
constexpr int USB_MASS_STORAGE_MODE=1, USB_JOYSTICK_MODE=2, USB_SERIAL_MODE=3;
constexpr int USBD_OK=0, DEVICE_ID=0, OTG_FS_IRQn=0;
constexpr int LL_AHB2_GRP1_PERIPH_OTGFS=1;
int hUsbDevice, FS_Desc, USBD_MSC, USBD_HID, USBD_CDC, USBD_Storage_Interface_fops, USBD_Interface_fops;
bool usbDriverStarted=false, irq=false, registered=false, callbacks=false;int selected=USB_SERIAL_MODE, stops=0;
bool phyPowered=false, resetHeld=false;
int phyResets=0;
int getSelectedUsbMode(){return selected;}
void usbInitLUNs(){}
int USBD_Init(int*,int*,int){assert(!irq && !phyPowered && !resetHeld);registered=callbacks=false;phyPowered=true;return 0;}
void USBD_RegisterClass(int*,int*){assert(!irq);registered=true;}
void USBD_MSC_RegisterStorage(int*,int*){assert(!irq);callbacks=true;}
void USBD_CDC_RegisterInterface(int*,int*){assert(!irq);callbacks=true;}
int USBD_Start(int*){assert(!irq && registered && (callbacks || selected==USB_JOYSTICK_MODE));return 0;}
void NVIC_ClearPendingIRQ(int){assert(!irq);}
void NVIC_EnableIRQ(int){assert(registered && (callbacks || selected==USB_JOYSTICK_MODE) && usbDriverStarted);irq=true;}
void NVIC_DisableIRQ(int){irq=false;}
void USBD_DeInit(int*){assert(!irq && !usbDriverStarted);registered=callbacks=false;++stops;}
void LL_AHB2_GRP1_ForceReset(int peripheral){
 assert(peripheral==LL_AHB2_GRP1_PERIPH_OTGFS && !irq && !usbDriverStarted);
 assert(!registered && !callbacks && stops==phyResets+1 && phyPowered);
 resetHeld=true;phyPowered=false;
}
void LL_AHB2_GRP1_ReleaseReset(int peripheral){
 assert(peripheral==LL_AHB2_GRP1_PERIPH_OTGFS && resetHeld && !phyPowered);
 resetHeld=false;++phyResets;
}
'''
        scenario = r'''
int main(){
 const int modes[]={USB_MASS_STORAGE_MODE,USB_SERIAL_MODE,USB_JOYSTICK_MODE};
 for(int i=0;i<9;++i){
   selected=modes[i%3];usbStart();assert(irq && usbDriverStarted && phyPowered);
   usbStop();assert(!irq && !usbDriverStarted && !phyPowered && !resetHeld);
   assert(stops==i+1 && phyResets==stops);
 }
}
'''
        run_cpp(model + firmware + scenario)

    def test_bus_reset_preserves_cli_callbacks_and_sof_tolerates_freed_class(self):
        firmware = actual('targets/common/arm/stm32/usbd_cdc.cpp', 'static int8_t VCP_DeInit_FS(void)\n{', 'static int8_t VCP_StartOfFrame_FS()\n{')
        model = r'''
#define RADIO_NB4
constexpr int USBD_OK=0, CDC_IN_FRAME_INTERVAL=1, APP_TX_DATA_SIZE=64;
struct USBD_CDC_HandleTypeDef {int TxState=0;};
struct {void* pClassData=nullptr;} hUsbDevice;
bool cdcConnected=true;void cb(){};auto receiveDataCb=cb;auto baudRateCb=cb;
unsigned APP_Tx_ptr_in=0, APP_Tx_ptr_out=0;uint8_t UserTxBufferFS[64];int transfers=0;
void USBD_CDC_SetTxBuffer(decltype(hUsbDevice)*,uint8_t*,size_t){}
int USBD_CDC_TransmitPacket(decltype(hUsbDevice)*){++transfers;return USBD_OK;}
'''
        scenario = r'''
int main(){
 VCP_DeInit_FS();assert(!cdcConnected && receiveDataCb==cb && baudRateCb==cb);
 APP_Tx_ptr_in=8;for(int i=0;i<10;++i)VCP_StartOfFrame_FS();assert(transfers==0);
 USBD_CDC_HandleTypeDef live;hUsbDevice.pClassData=&live;
 for(int i=0;i<10;++i)VCP_StartOfFrame_FS();assert(transfers==1 && APP_Tx_ptr_out==8);
 hUsbDevice.pClassData=nullptr;VCP_DeInit_FS();for(int i=0;i<10;++i)VCP_StartOfFrame_FS();assert(transfers==1);
}
'''
        run_cpp(model + firmware + scenario)


if __name__ == '__main__':
    unittest.main()
