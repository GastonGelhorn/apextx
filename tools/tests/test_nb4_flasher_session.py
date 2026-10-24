# SPDX-License-Identifier: GPL-2.0-only
"""Read-only USB identity and the automatic update transaction's device pin."""
import pathlib
import sys
import types
import unittest
from unittest.mock import Mock, patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import nb4_usb_update as usb
import nb4_flasher_session as sessions
from tools.tests.test_nb4_updater import radio


class Probe(unittest.TestCase):
    def device(self, product='ApexTX NB4 Update', info='DFU Config', interface=None, config_index=2):
        interface = interface or '@ApexTX NB4 Update /0xC0200000/015*128Kg'
        d = Mock(idVendor=0x0483, idProduct=0xdf11, iProduct=2, iSerialNumber=3)
        i = types.SimpleNamespace(bInterfaceClass=0xfe,bInterfaceSubClass=1,bInterfaceProtocol=2,iInterface=6)
        config = Mock(iConfiguration=config_index)
        config.__iter__ = Mock(return_value=iter([i]))
        d.__iter__ = Mock(return_value=iter([config]))
        d.strings = {0:None,2:product,3:'A',4:info,6:interface}
        return d

    def probe(self, *devices):
        util = Mock(get_string=lambda d,i:d.strings[i])
        core = Mock(find=Mock(return_value=devices))
        with patch.object(usb, 'usb_modules', return_value=(core,util,None)):
            result = usb.probe()
        for d in devices:
            d.set_configuration.assert_not_called()
            d.ctrl_transfer.assert_not_called()
        util.claim_interface.assert_not_called()
        self.assertEqual(util.dispose_resources.call_count, len(devices))
        return result

    def test_original_plus_and_installed_application_version(self):
        for model, hw, target in [('NB4','nb4',1),('NB4+','nb4plus',2)]:
            info=f'APXINFO1;{hw};apextx-nb4-2.12.5-apextx-0.1.0-alpha.4 (123456789)'
            result=self.probe(self.device(f'ApexTX {model} Update',info,f'@ApexTX {model} Update /0xC0200000/015*128Kg'))
            self.assertEqual((result['target'],result['version'],result['state']),(target,'0.1.0-alpha.4','ready'))

    def test_legacy_version_is_unknown_and_old_normal_model_is_ambiguous(self):
        self.assertIsNone(self.probe(self.device())['version'])
        normal=self.probe(self.device('ApexTX NB4 Joystick'))
        self.assertEqual(normal['state'],'normal')
        self.assertIsNone(normal['target'])

    def test_info_slot_works_with_inherited_dfu_hid_and_cdc_configuration_indices(self):
        import re
        library = ROOT / 'radio/src/thirdparty/STM32_USB_Device_Library'
        definition = (library / 'Core/Inc/usbd_def.h').read_text()
        index = int(re.search(r'#define\s+USBD_IDX_CONFIG_STR\s+0x([0-9A-F]+)', definition)[1], 16)
        self.assertEqual(usb.USB_INFO_STRING, index)
        # Use the actual shipped class fields, including DFU's product slot
        # and HID/CDC's absent string. None of those points to the info slot.
        for mode, product in [('DFU','Update'),('HID','Joystick'),('CDC','Serial Port')]:
            source = (library / f'Class/{mode}/Src/usbd_{mode.lower()}.c').read_text()
            config_index = int(re.search(r'0x([0-9A-Fa-f]+),\s*/\* iConfiguration:', source)[1], 16)
            info = 'APXINFO1;nb4;apextx-nb4-2.12.5-apextx-0.1.0-alpha.4 (123456789)'
            result = self.probe(self.device(f'ApexTX NB4 {product}',info,config_index=config_index))
            self.assertEqual(result['version'], '0.1.0-alpha.4')

    def test_absent_recovery_multiple_and_mismatched_identity(self):
        self.assertEqual(self.probe()['state'],'absent')
        self.assertEqual(self.probe(self.device('STM32 BOOTLOADER'))['state'],'recovery')
        self.assertEqual(self.probe(self.device(),self.device())['state'],'multiple')
        with self.assertRaises(usb.UpdateError): self.probe(self.device(info='APXINFO1;nb4plus;'))


class Session(unittest.TestCase):
    def setup_session(self):
        session=sessions.FlasherSession(client=Mock())
        session.device=dict(state='ready',serial='A',target=1,model='NB4',version=None)
        session.offer=dict(id=42,target=1,version='0.1.0-alpha.4',prerelease=True,url='https://github.com/')
        return session

    def test_offer_is_bound_to_the_detected_serial_model_and_release(self):
        s=self.setup_session()
        for serial,target,release_id in [('B',1,42),('A',2,42),('A',1,43)]:
            with self.assertRaises(usb.UpdateError): s.choice(serial,target,release_id)
        self.assertEqual(s.choice('A',1,42)[0]['serial'],'A')
        for version,development in [('0.1.0-alpha.4',False),('0.1.0-alpha.5',False),('0.1.0-alpha.4',True)]:
            s.device.update(version=version,development=development)
            with self.assertRaises(usb.UpdateError): s.choice('A',1,42)

    def test_download_failure_and_device_swap_never_start_writing(self):
        for swap in (False,True):
            s=self.setup_session()
            s.client.download.side_effect=RuntimeError('bad download')
            s.begin('upgrade')
            with patch.object(sessions,'discover',return_value=radio(serial='B' if swap else 'A')), \
                    patch.object(sessions,'release'),patch.object(sessions,'run_update') as write:
                s.automatic(s.device,s.offer)
            write.assert_not_called()
            self.assertFalse(s.snapshot()['running'])
            self.assertTrue(s.snapshot()['error'])
            if swap: s.client.download.assert_not_called()

    def test_development_build_can_explicitly_upgrade_to_a_newer_release(self):
        s = self.setup_session()
        s.device.update(version='0.1.0-alpha.4', development=True)
        s.offer.update(version='0.1.0-alpha.5')
        device, offer = s.choice('A', 1, 42)
        self.assertEqual(device['version'], '0.1.0-alpha.4')
        self.assertEqual(offer['version'], '0.1.0-alpha.5')
        s.client.download.assert_not_called()
        self.assertFalse(s.snapshot()['running'])

    def test_full_download_passes_a_serial_pinned_finder_and_maps_progress(self):
        s=self.setup_session()
        s.client.download.return_value=b'validated image'
        s.begin('upgrade')
        def update(image,progress,**kwargs):
            self.assertEqual(image,b'validated image')
            self.assertEqual(kwargs['serial'],'A')
            self.assertEqual(kwargs['wait_seconds'],0)
            progress('finished',100)
        with patch.object(sessions,'discover',return_value=radio()),patch.object(sessions,'release'), \
                patch.object(sessions,'run_update',side_effect=update): s.automatic(s.device,s.offer)
        self.assertEqual(s.snapshot()['progress'],100)
        self.assertFalse(s.snapshot()['error'])

    def test_radio_replaced_after_download_is_rejected_before_transport_opens(self):
        from tools.tests.test_nb4_releases import image
        s = self.setup_session()
        s.client.download.return_value = image()
        s.begin('upgrade')
        with patch.object(sessions, 'discover', side_effect=[radio(), radio(serial='B')]), \
                patch.object(sessions, 'release'), \
                patch.object(usb, 'usb_modules', side_effect=AssertionError('Transport must not open')):
            s.automatic(s.device, s.offer)
        s.client.download.assert_called_once()
        self.assertTrue(s.snapshot()['error'])
        self.assertIn('radio changed', s.snapshot()['message'])

    def test_probe_pauses_while_busy_and_network_check_is_cached(self):
        s=self.setup_session()
        with patch.object(sessions,'probe',return_value=s.device) as probe:
            s.begin('manual')
            s.scan()
            probe.assert_not_called()
            with self.assertRaises(usb.UpdateError): s.begin('upgrade')
            s.finish()
            s.scan(); s.scan()
            s.client.latest.assert_called_once_with(1,True)
            s.refresh(False); s.scan()
            self.assertEqual(s.client.latest.call_args.args,(1,False))

    def test_completion_survives_detach_but_is_not_shown_for_another_radio(self):
        s = self.setup_session()
        s.client.latest.return_value = None
        s.begin('upgrade')
        s.progress('Verified', 100)
        s.finish()
        with patch.object(sessions, 'probe', return_value={'state': 'absent'}):
            s.scan()
        self.assertEqual(s.snapshot()['progress'], 100)
        other = dict(state='ready', serial='B', target=2, model='NB4+', version=None)
        with patch.object(sessions, 'probe', return_value=other):
            s.scan()
        self.assertIsNone(s.snapshot()['operation'])
        self.assertEqual(s.snapshot()['progress'], 0)


class BootUsbIdentity(unittest.TestCase):
    def test_bootloader_reports_installed_application_not_its_own_version(self):
        import shutil
        import subprocess
        import tempfile
        from tools.tests.test_stm32_usart_tx_mode import function
        if not shutil.which('c++'):
            self.skipTest('Native C++ compiler is unavailable')
        source=(ROOT/'radio/src/bootloader/boot_nb4_update.cpp').read_text()
        actual=function(source,'static void cacheUsbInfo(')
        setup=r'''
#include <cstdio>
#include <cstring>
#include <cassert>
#include "bootloader/nb4_update_id.h"
static char usbInfo[126] = NB4_USB_INFO_PREFIX;
static bool mayExit = true;
static const char* application = "apextx-nb4-2.12.5-apextx-0.1.0-alpha.4 (123456789)";
const char* getFirmwareVersion(const unsigned char*) { return application; }
'''
        scenario=r'''
int main() {
 cacheUsbInfo();
 assert(!strcmp(usbInfo, NB4_USB_INFO_PREFIX "apextx-nb4-2.12.5-apextx-0.1.0-alpha.4 (123456789)"));
 mayExit=false; cacheUsbInfo();
 assert(!strcmp(usbInfo, NB4_USB_INFO_PREFIX));
 mayExit=true; application=nullptr; cacheUsbInfo();
 assert(!strcmp(usbInfo, NB4_USB_INFO_PREFIX));
 char unbounded[100]; memset(unbounded,'x',sizeof(unbounded));
 application=unbounded; cacheUsbInfo();
 assert(!strcmp(usbInfo, NB4_USB_INFO_PREFIX));
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path=pathlib.Path(folder)/'identity.cpp'
            path.write_text(setup+actual+scenario)
            for flags in ([],['-DRADIO_NB4PLUS_HW']):
                exe=pathlib.Path(folder)/'identity'
                subprocess.run(['c++','-std=c++17','-I',str(ROOT/'radio/src'),*flags,str(path),'-o',str(exe)],check=True,capture_output=True)
                subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    unittest.main()
