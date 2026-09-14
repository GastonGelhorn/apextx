# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the actual flash engine, image format and host update ordering."""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from tools.tests.test_stm32_usart_tx_mode import function

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import nb4_update_image as image_format
import nb4_usb_update as usb_update


def fixture():
    app = bytearray(b'\x55' * 8192)
    struct.pack_into('<II', app, 0, 0x1000fff0, 0x08020101)
    app[32:46] = b'apextx-nb4-1.0'
    return image_format.finalize(b'\x00' * 0x20000 + app)


class Images(unittest.TestCase):
    def test_roundtrip_and_idempotent_finalization(self):
        image = fixture()
        self.assertEqual(len(image), 0x200000)
        self.assertEqual(image_format.finalize(image), image)
        self.assertEqual(image_format.update_package(image)[32:], image[0x20000:0x22000])

    def test_corrupt_header_payload_and_old_images_rejected(self):
        image = fixture()
        for location in (-1, -32, 0x20500):
            changed = bytearray(image)
            changed[location] ^= 1
            with self.assertRaises(ValueError):
                image_format.update_package(changed)
        with self.assertRaises(ValueError):
            image_format.update_package(image[:0x22000])

    def test_wrong_model_and_vectors_rejected(self):
        raw = bytearray(fixture()[:0x22000])
        raw[0x20020:0x2002e] = b'apextx-nb4p-1.'
        with self.assertRaises(ValueError):
            image_format.finalize(raw)
        raw = bytearray(fixture()[:0x22000])
        struct.pack_into('<I', raw, 0x20004, 0x08000101)
        with self.assertRaises(ValueError):
            image_format.finalize(raw)


class Host(unittest.TestCase):
    def run_fake(self, corrupt=False, error=False, initial_failures=0):
        package = image_format.update_package(fixture())
        size, crc = struct.unpack_from('<II', package, 16)
        operations = []
        class Fake:
            def __init__(self, selection):
                self.staging = bytearray()
                self.attempt = 0
            def address(self, address):
                self.address_value = address
                if address == usb_update.STAGE:
                    self.staging.clear()
                    self.attempt += 1
            def download(self, block, data):
                assert self.address_value == usb_update.STAGE
                assert block == 2 + len(self.staging) // 4096
                self.staging.extend(data)
            def read(self, address, length):
                if address == usb_update.STAGE:
                    operations.append('readback')
                    data = bytes(self.staging)
                    return data[:-1] + bytes([data[-1] ^ 1]) if corrupt else data
                assert address == usb_update.CONTROL
                operations.append('flash-status')
                if self.attempt <= initial_failures:
                    return struct.pack('<8s6I', b'APXSTAT1', 4, 0, size, crc, 2, len(package))
                return struct.pack('<8s6I', b'APXSTAT1', 4 if error else 3, 100,
                                   size, crc, 3 if error else 0, len(package))
            def command(self, command):
                operations.append(command)
            def close(self):
                operations.append('close')
        selections = iter([object(), None])
        try:
            usb_update.run_update(fixture(), lambda *_: None,
                                  transport_factory=Fake, finder=lambda _: next(selections))
        except usb_update.UpdateError:
            if not corrupt and not error and initial_failures < 2:
                raise
        return operations

    def test_readback_precedes_flash_and_verified_status_precedes_restart(self):
        self.assertEqual(self.run_fake(), ['readback', b'APXSTART', 'flash-status', b'APXRESET', 'close'])

    def test_corrupt_transfer_never_commits(self):
        self.assertEqual(self.run_fake(corrupt=True), ['readback', 'close'])

    def test_failed_flash_never_restarts(self):
        self.assertEqual(self.run_fake(error=True), ['readback', b'APXSTART', 'flash-status', 'close'])

    def test_initial_erase_failure_retries_once_with_both_verifications(self):
        self.assertEqual(self.run_fake(initial_failures=1),
                         ['readback', b'APXSTART', 'flash-status',
                          'readback', b'APXSTART', 'flash-status', b'APXRESET', 'close'])

    def test_persistent_initial_erase_failure_stops_without_restart(self):
        self.assertEqual(self.run_fake(initial_failures=2),
                         ['readback', b'APXSTART', 'flash-status',
                          'readback', b'APXSTART', 'flash-status', 'close'])

    def test_invalid_image_does_not_access_usb(self):
        with self.assertRaises(ValueError):
            usb_update.run_update(b'bad', lambda *_: None,
                                  finder=lambda _: self.fail('USB accessed'))

    def test_discovery_does_not_claim_rom_dfu(self):
        class Device:
            iProduct = 1
            iSerialNumber = 2
            def __iter__(self):
                raise AssertionError('ROM interfaces should not be opened')
        class Core:
            @staticmethod
            def find(**_): return [Device()]
        class Util:
            @staticmethod
            def get_string(device, index): return 'STM32 BOOTLOADER'
            @staticmethod
            def dispose_resources(device): pass
        with patch.object(usb_update, 'usb_modules', return_value=(Core, Util, None)):
            self.assertIsNone(usb_update.discover())


class ImageGates(unittest.TestCase):
    """The checks that stop a wrong or damaged image reaching the radio."""

    def application(self):
        return image_format.update_package(fixture())[32:]

    def test_sibling_radio_image_is_refused_even_with_an_incidental_marker(self):
        app = bytearray(self.application())
        app[2048:2068] = b'apextx-nb4p-2.12.4-x'
        with self.assertRaises(ValueError) as caught:
            image_format.validate_vectors(bytes(app))
        self.assertIn('nb4p', str(caught.exception))

    def test_an_image_without_the_nb4_marker_is_refused(self):
        app = bytearray(self.application())
        marker = app.index(b'apextx-nb4-')
        app[marker:marker + 11] = b'x' * 11
        with self.assertRaises(ValueError):
            image_format.validate_vectors(bytes(app))

    def test_every_vector_bound_is_enforced(self):
        for label, stack, reset in [
                ('misaligned stack', 0x1000fff4, 0x08020101),
                ('stack below CCM', 0x0ffffff0, 0x08020101),
                ('stack above CCM', 0x10010008, 0x08020101),
                ('stack above SRAM', 0x20030008, 0x08020101),
                ('even reset vector', 0x1000fff0, 0x08020100),
                ('reset below the application', 0x1000fff0, 0x08000101),
                ('reset past the application', 0x1000fff0, 0x09000101)]:
            app = bytearray(self.application())
            struct.pack_into('<II', app, 0, stack, reset)
            with self.subTest(label), self.assertRaises(ValueError):
                image_format.validate_vectors(bytes(app))

    def test_a_truncated_application_is_refused(self):
        with self.assertRaises(ValueError):
            image_format.validate_vectors(b'\x00' * 4)

    def test_every_manifest_field_is_checked(self):
        good = fixture()
        for label, offset, value in [('magic', 0, b'APXNB4U2'),
                                     ('target', 8, struct.pack('<I', 2)),
                                     ('address', 12, struct.pack('<I', 0x08000000)),
                                     ('size', 16, struct.pack('<I', 4)),
                                     ('unaligned size', 16, struct.pack('<I', 8193)),
                                     ('oversized', 16, struct.pack('<I', image_format.MAX_APP + 4)),
                                     ('version', 24, struct.pack('<I', 2))]:
            header = bytearray(good[-32:])
            header[offset:offset + len(value)] = value
            # Re-sign the header: the field check must stand on its own rather
            # than rely on the header CRC catching the edit.
            struct.pack_into('<I', header, 28, image_format.zlib.crc32(bytes(header[:28])))
            with self.subTest(label), self.assertRaises(ValueError):
                image_format.update_package(good[:-32] + bytes(header))

    def test_an_unaligned_application_is_padded_before_the_manifest(self):
        app = bytearray(b'\x55' * 8190)
        struct.pack_into('<II', app, 0, 0x1000fff0, 0x08020101)
        app[32:46] = b'apextx-nb4-1.0'
        package = image_format.update_package(
            image_format.finalize(b'\x00' * 0x20000 + bytes(app)))
        self.assertEqual(struct.unpack_from('<I', package, 16)[0], 8192)
        self.assertEqual(package[32 + 8190:32 + 8192], b'\xff\xff')

    def test_an_image_without_an_application_is_refused(self):
        with self.assertRaises(ValueError):
            image_format.finalize(b'\x00' * 0x20000)

    def test_finalize_replaces_the_file_atomically(self):
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / 'firmware.bin'
            target.write_bytes(fixture())
            result = subprocess.run([sys.executable, str(ROOT / 'tools/nb4_update_image.py'),
                                     str(target)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(target.read_bytes(), fixture())
            self.assertEqual(list(pathlib.Path(directory).iterdir()), [target])


class Discovery(unittest.TestCase):
    """discover() must never touch a device it has not identified."""

    def modules(self, devices, readable=True):
        class Core:
            @staticmethod
            def find(**_): return devices
        disposed = []
        class Util:
            @staticmethod
            def get_string(device, index):
                if not readable or not device.strings:
                    raise RuntimeError('Operation not supported or unimplemented on this platform')
                return device.strings[index]
            @staticmethod
            def dispose_resources(device): disposed.append(device)
        return Core, Util, disposed

    def device(self, product='ApexTX NB4 Update', serial='A', interface=None):
        interface = interface if interface is not None else usb_update.INTERFACES[0]
        class Interface:
            bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol = 0xfe, 1, 2
            bInterfaceNumber, bAlternateSetting = 0, 0
            iInterface = 3
        class Config:
            bConfigurationValue = 1
            def __iter__(self): return iter([Interface()])
        class Device:
            iProduct, iSerialNumber = 1, 2
            strings = {1: product, 2: serial, 3: interface}
            def __iter__(self): return iter([Config()])
        return Device()

    def test_a_device_that_cannot_be_read_asks_for_a_driver(self):
        core, util, _ = self.modules([self.device()], readable=False)
        with patch.object(usb_update, 'usb_modules', return_value=(core, util, None)):
            with self.assertRaises(usb_update.UpdateError) as caught:
                usb_update.discover()
        self.assertIn('USB driver', str(caught.exception))

    def test_an_unreadable_device_does_not_hide_the_radio(self):
        class Opaque:
            iProduct, iSerialNumber = 1, 2
            strings = {}
            def __iter__(self): raise AssertionError('must not be opened')
        core, util, _ = self.modules([Opaque(), self.device()])
        with patch.object(usb_update, 'usb_modules', return_value=(core, util, None)):
            selection = usb_update.discover()
        self.assertIsNotNone(selection)

    def test_the_serial_filter_selects_one_radio(self):
        core, util, disposed = self.modules([self.device(serial='A'), self.device(serial='B')])
        with patch.object(usb_update, 'usb_modules', return_value=(core, util, None)):
            selection = usb_update.discover('B')
        self.assertEqual(selection[4], 'B')
        self.assertEqual(len(disposed), 1)

    def test_two_radios_are_refused_rather_than_guessed(self):
        core, util, disposed = self.modules([self.device(serial='A'), self.device(serial='B')])
        with patch.object(usb_update, 'usb_modules', return_value=(core, util, None)):
            with self.assertRaises(usb_update.UpdateError) as caught:
                usb_update.discover()
        self.assertIn('--serial', str(caught.exception))
        self.assertEqual(len(disposed), 2)

    def test_a_foreign_dfu_interface_is_not_accepted(self):
        core, util, _ = self.modules([self.device(interface='@Internal Flash /0x08000000/04*016Kg')])
        with patch.object(usb_update, 'usb_modules', return_value=(core, util, None)):
            self.assertIsNone(usb_update.discover())


class Reporting(unittest.TestCase):
    """What the user is told when the update does not go to plan."""

    def drive(self, states, fail_at=None, mismatch=None, selections=None, finder=None):
        package = image_format.update_package(fixture())
        size, crc = struct.unpack_from('<II', package, 16)
        reports = []
        queue = list(states)
        class Fake:
            def __init__(self, selection): self.staging = bytearray()
            def address(self, address):
                if address == usb_update.STAGE: self.staging.clear()
                self.address_value = address
            def download(self, block, data):
                if fail_at == 'transfer': raise RuntimeError('[Errno 19] No such device')
                self.staging.extend(data)
            def read(self, address, length):
                if address == usb_update.STAGE: return bytes(self.staging)
                if fail_at == 'status': raise RuntimeError('[Errno 19] No such device')
                state, percent = queue.pop(0)
                values = dict(size=size, crc=crc, received=len(package))
                if mismatch: values[mismatch] = 1
                return struct.pack('<8s6I', b'APXSTAT1', state, percent,
                                   values['size'], values['crc'], 0, values['received'])
            def command(self, command):
                if fail_at == 'restart' and command == b'APXRESET':
                    raise RuntimeError('[Errno 19] No such device')
            def close(self): pass
        queued = iter(selections if selections is not None else [object(), None])
        look = finder if finder else (lambda _: next(queued))
        clock = [0.0]
        def monotonic():
            clock[0] += 2.0  # Expire the real waits in a handful of iterations.
            return clock[0]
        with patch.object(usb_update.time, 'sleep', lambda _: None), \
                patch.object(usb_update.time, 'monotonic', monotonic):
            usb_update.run_update(fixture(), lambda message, percent: reports.append((percent, message)),
                                  transport_factory=Fake, finder=look)
        return reports

    def test_installation_progress_is_reported_while_the_radio_writes(self):
        reports = self.drive([(1, 0), (2, 40), (2, 80), (3, 100)])
        percents = [percent for percent, _ in reports]
        self.assertTrue(any(50 <= percent < 98 for percent in percents), percents)
        self.assertEqual(percents[-1], 100)

    def test_a_lost_cable_during_the_transfer_says_nothing_was_written(self):
        with self.assertRaises(usb_update.UpdateError) as caught:
            self.drive([(3, 100)], fail_at='transfer')
        self.assertIn('No flash memory had been written', str(caught.exception))
        self.assertIn('Errno 19', str(caught.exception))

    def test_a_lost_cable_during_the_install_says_not_to_switch_off(self):
        with self.assertRaises(usb_update.UpdateError) as caught:
            self.drive([(1, 10)], fail_at='status')
        self.assertIn('do not switch it off', str(caught.exception))

    def test_a_lost_cable_after_verification_says_the_firmware_is_written(self):
        with self.assertRaises(usb_update.UpdateError) as caught:
            self.drive([(3, 100)], fail_at='restart')
        self.assertIn('already written and verified', str(caught.exception))

    def test_a_reply_about_another_image_stops_the_update(self):
        for field in ('size', 'crc', 'received'):
            with self.subTest(field), self.assertRaises(usb_update.UpdateError) as caught:
                self.drive([(1, 0)], mismatch=field)
            self.assertIn('does not match', str(caught.exception))

    def test_a_radio_detaching_after_a_verified_install_is_not_an_error(self):
        # The radio stops answering descriptor reads while it detaches, which
        # is what discovery reports as an unreadable device.
        def finder(_):
            if finder.calls:
                raise usb_update.UpdateError('A USB device was found but could not be read.')
            finder.calls = 1
            return (object(),)
        finder.calls = 0
        reports = self.drive([(3, 100)], selections=None, finder=finder)
        self.assertEqual(reports[-1][0], 100)
        self.assertNotIn('could not be read', reports[-1][1])

    def test_a_radio_that_stays_in_update_mode_is_not_reported_as_a_failure(self):
        class Util:
            @staticmethod
            def dispose_resources(device): pass
        with patch.object(usb_update, 'usb_modules', return_value=(None, Util, None)):
            reports = self.drive([(3, 100)], selections=[(object(),)] * 40)
        self.assertEqual(reports[-1][0], 100)
        self.assertIn('Hold the power button', reports[-1][1])

    def test_the_updater_waits_for_the_radio_to_appear(self):
        calls = []
        def finder(_):
            calls.append(1)
            return (object(),) if len(calls) > 3 else None
        opened = []
        with patch.object(usb_update.time, 'sleep', lambda _: None):
            with self.assertRaises(RuntimeError):
                usb_update.run_update(fixture(), lambda *_: None, wait_seconds=30,
                                      transport_factory=lambda selection: opened.append(selection)
                                      or (_ for _ in ()).throw(RuntimeError('stop here')),
                                      finder=finder)
        self.assertEqual(len(calls), 4)
        self.assertEqual(len(opened), 1)

    def test_a_radio_that_never_appears_is_reported_clearly(self):
        with self.assertRaises(usb_update.UpdateError) as caught:
            usb_update.run_update(fixture(), lambda *_: None, wait_seconds=0,
                                  transport_factory=lambda _: self.fail('opened without a radio'),
                                  finder=lambda _: None)
        self.assertIn('ApexTX Update was not detected', str(caught.exception))


# Windows is a target platform for the updater but has no host compiler by
# default, so the two tests that build the real engine skip rather than error.
@unittest.skipUnless(shutil.which('c++'), 'no host C++ compiler available')
class FlashEngine(unittest.TestCase):
    def test_boot_handoff_preserves_resume_without_arming_watchdog_on_cold_boot(self):
        boot = function((ROOT / 'radio/src/bootloader/boot.cpp').read_text(), 'void bootloaderInitApp()')
        adapter = (ROOT / 'radio/src/bootloader/boot_nb4_update.cpp').read_text()
        reboot = (ROOT / 'radio/src/targets/common/arm/stm32/abnormal_reboot.cpp').read_text()
        actual = '\n'.join([function(reboot, 'void abnormalRebootRequestResume()'),
                            function(reboot, 'bool abnormalRebootTakeResumeRequest()'),
                            function(adapter, 'static void restartApplication()'), boot])
        model = r'''
#include <cassert>
#include <cstdint>
#include "hal/abnormal_reboot.h"
#define RADIO_NB4
#define TRACE(...)
constexpr uint32_t APP_START_ADDRESS=0x08020000;
constexpr int USB_DFU_MODE=4;
uint32_t _reboot_cmd=0;bool watchdog=false,usb=true,jumped=false;int usbMode=0;
void boardBLEarlyInit(){} void pwrInit(){} void pwrOff(){} void pwrOn(){}
uint32_t abnormalRebootGetCmd(){return _reboot_cmd;}
void abnormalRebootResetCmd(){_reboot_cmd=0;}
void abnormalRebootEnterRomDfu(){assert(false);}
void watchdogInit(unsigned){watchdog=true;}
void keysInit(){} void delaysInit(){} void delay_ms(int){}
bool boardBLStartCondition(){return false;}
bool nb4BootApplicationValid(){return true;}
void boardBLPreJump(){}
void jumpTo(uint32_t){jumped=true;throw 1;}
void setSelectedUsbMode(int mode){usbMode=mode;}
void __enable_irq(){} void timersInit(){} void usbInit(){} void boardBLInit(){}
void usbStop(){usb=false;}
void NVIC_SystemReset(){assert(!usb);throw 2;}
uint32_t abnormalRebootGetCause(){return ARC_Software;}
'''
        scenario = r'''
int main(){
 try{bootloaderInitApp();}catch(int){}
 assert(jumped&&!watchdog&&!abnormalRebootTakeResumeRequest());
 try{restartApplication();}catch(int){}
 assert(_reboot_cmd==REBOOT_CMD_RESUME);
 jumped=false;try{bootloaderInitApp();}catch(int){}
 assert(jumped&&!watchdog);
 // The app can resume immediately; no power-button wait or watchdog recovery.
 assert(abnormalRebootTakeResumeRequest());
 assert(!abnormalRebootTakeResumeRequest());
 _reboot_cmd=REBOOT_CMD_DFU;jumped=false;bootloaderInitApp();
 assert(!jumped&&usbMode==USB_DFU_MODE&&_reboot_cmd==0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            source = directory / 'handoff.cpp'
            source.write_text(model + actual + scenario)
            result = subprocess.run(['c++', '-std=c++17', '-I', str(ROOT / 'radio/src'),
                                     str(source), '-o', str(directory / 'test')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(directory / 'test')], check=True)

    def test_actual_engine_handles_interrupted_flash_and_rejects_unsafe_writes(self):
        source = r'''
#include "bootloader/nb4_update.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
using namespace nb4update;
static std::vector<uint8_t> memory(0x200000), stage(StageCapacity);
static int writes=0, fault=-1;
static bool crossed=false;
static uint32_t sector(uint32_t address) {
  uint32_t relative=address&0xfffff;
  return (address&0x100000?12:0)+(relative<0x10000?relative/0x4000:relative/0x20000+4);
}
static uint32_t sectorSize(uint32_t s) {s%=12;return s<4?0x4000:s==4?0x10000:0x20000;}
static int erase(uint32_t addr) {
  assert(addr>=AppAddress && addr<=ManifestAddress);
  if(addr==0x08100000)crossed=true;
  unsigned size=sectorSize(sector(addr));
  bool fail=writes++==fault;
  memset(memory.data()+addr-0x08000000, 0xff, fail?size/2:size);
  return fail?-1:0;
}
static int program(uint32_t addr, void* src, uint32_t size) {
  assert(addr>=AppAddress && size<=0x08200000-addr && !(size&3) && !(addr&3));
  bool fail=writes++==fault;
  auto bytes=(uint8_t*)src;
  for(unsigned i=0;i<(fail?size/2:size);++i) memory[addr-0x08000000+i]&=bytes[i];
  return fail?-1:0;
}
static void service(){}
static etx_flash_driver_t driver={nullptr,sector,sectorSize,erase,program,nullptr};
static bool bootable() {
  Manifest m;memcpy(&m,memory.data()+0x1fffe0,32);
  return validApplication(m,memory.data()+0x20000);
}
static void send(Updater& updater,const std::vector<uint8_t>& package) {
  for(unsigned i=0;i<package.size();i+=4096) {
    unsigned n=package.size()-i;if(n>4096)n=4096;
    assert(updater.write(StageAddress+i,package.data()+i,n));
  }
}
int main(int argc,char**argv) {
  std::ifstream f(argv[1],std::ios::binary);
  std::vector<uint8_t> image((std::istreambuf_iterator<char>(f)),{});
  Manifest m;memcpy(&m,image.data()+0x1fffe0,32);
  assert(validManifest(m));
  assert(crc32("123456789",9)==0xcbf43926);
  std::vector<uint8_t> package(32+m.size);
  memcpy(package.data(),&m,32);memcpy(package.data()+32,image.data()+0x20000,m.size);
  const uint8_t commit[]="APXSTART",reset[]="APXRESET";
  Updater empty(stage.data());
  assert(!empty.write(ControlAddress,commit,8));
  assert(!empty.write(AppAddress,package.data(),4096));
  assert(!empty.write(0xfffffff0,package.data(),4096));
  assert(!empty.erase(0x08000000));assert(!empty.erase(0xffffffff));
  assert(!empty.write(StageAddress+4096,package.data(),4096));
  assert(empty.write(StageAddress,package.data(),4096));
  assert(!empty.write(ControlAddress,commit,8));
  uint8_t status[32];assert(!empty.read(0x08000000,status,32));
  assert(!empty.read(StageAddress+4090,status,32));
  assert(!empty.read(0xfffffff0,status,32));
  assert(empty.read(ControlAddress,status,32));
  auto bad=package;bad[32+200]^=1;
  memory=image;writes=0;
  Updater corrupt(stage.data());send(corrupt,bad);
  assert(corrupt.write(ControlAddress,commit,8));
  corrupt.install(driver,memory.data()+0x20000,service);
  assert(corrupt.state()==State::Error && writes==0 && bootable());
  // Every erase and program operation can fail after changing part of flash.
  // The next boot must accept only a complete image, never a partial one.
  int operationCount=0;
  for(int run=-1;run<operationCount;run++) {
    memory.assign(0x200000,0xa5);writes=0;fault=run;crossed=false;
    Updater updater(stage.data());send(updater,package);
    assert(updater.write(ControlAddress,commit,8));
    assert(!updater.write(StageAddress,package.data(),4096));
    assert(!updater.erase(StageAddress));
    updater.install(driver,memory.data()+0x20000,service);
    for(unsigned i=0;i<0x20000;i++)assert(memory[i]==0xa5);
    if(run==-1) {
      operationCount=writes;assert(crossed && bootable());
      assert(updater.state()==State::Done);
      assert(!memcmp(memory.data()+0x20000,image.data()+0x20000,m.size));
      assert(updater.write(ControlAddress,reset,8));
      assert(updater.state()==State::Restart);
    } else {
      assert(updater.state()==State::Error);
      assert(!bootable());
      assert(!updater.write(ControlAddress,reset,8));
      // Retry from the same bootloader after the partial write.
      fault=-1;send(updater,package);assert(updater.write(ControlAddress,commit,8));
      updater.install(driver,memory.data()+0x20000,service);
      assert(bootable() && updater.state()==State::Done);
    }
  }
  // A torn first-sector erase that preserves the vectors still fails full CRC.
  memory=image;memory[0x20100]^=1;assert(!bootable());
}
'''
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            (directory / 'test.cpp').write_text(source)
            (directory / 'image.bin').write_bytes(fixture())
            subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra',
                            '-I', str(ROOT / 'radio/src'), str(directory / 'test.cpp'),
                            str(ROOT / 'radio/src/bootloader/nb4_update.cpp'),
                            '-o', str(directory / 'test')], check=True, capture_output=True)
            subprocess.run([str(directory / 'test'), str(directory / 'image.bin')], check=True)


if __name__ == '__main__':
    unittest.main()
