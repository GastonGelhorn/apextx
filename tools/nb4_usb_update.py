# SPDX-License-Identifier: GPL-2.0-only
"""ApexTX's RAM-staged NB4 updater transport. Never writes a ROM DFU device."""
import struct
import time
from pathlib import Path

from nb4_update_image import HEADER, update_package

STAGE = 0xc0200000
CONTROL = 0xc03f0000
TRANSFER = 4096
PRODUCT = 'ApexTX NB4 Update'
# Staging is 15 x 128 KiB. The first bootloaders advertised one sector more
# than they had; they are otherwise identical, so keep accepting them.
INTERFACES = ('@ApexTX NB4 Update /0xC0200000/015*128Kg',
              '@ApexTX NB4 Update /0xC0200000/016*128Kg')


class UpdateError(RuntimeError):
    pass


def connection_lost(phase, cause):
    """A dropped USB connection means something different in every phase."""
    if phase == 'installing':
        advice = ('The radio was writing flash memory. It finishes on its own, so do '
                  'not switch it off: watch its display, and hold the power button to '
                  'restart once it reports the firmware verified.')
    elif phase == 'verified':
        advice = ('The firmware was already written and verified. Hold the power '
                  'button on the radio to restart it.')
    else:
        advice = 'No flash memory had been written yet. Reconnect the radio and try again.'
    return UpdateError(f'The USB connection to the radio was lost. {advice} ({cause})')


def usb_modules():
    try:
        import usb.core
        import usb.util
        import usb.backend.libusb1
    except ImportError as error:
        raise UpdateError('Install the dependencies listed in tools/requirements-updater.txt.') from error
    backend = usb.backend.libusb1.get_backend()
    if backend is None:
        # Homebrew libraries may not be in the GUI process's loader path.
        for location in ['/opt/homebrew/lib/libusb-1.0.dylib', '/usr/local/lib/libusb-1.0.dylib']:
            if Path(location).is_file():
                backend = usb.backend.libusb1.get_backend(find_library=lambda _: location)
                if backend:
                    break
    if backend is None:
        raise UpdateError('The libusb 1.0 backend is unavailable. See docs/nb4/UPDATE.md for platform setup instructions.')
    return usb.core, usb.util, backend


def discover(serial=None):
    core, util, backend = usb_modules()
    found = []
    unreadable = 0
    for device in core.find(find_all=True, idVendor=0x0483, idProduct=0xdf11, backend=backend):
        try:
            # Do not claim, reset, detach, or configure an unidentified device.
            try:
                product = util.get_string(device, device.iProduct)
                identity = util.get_string(device, device.iSerialNumber)
            except Exception:
                # Reading a descriptor needs the device open, which fails when
                # no libusb-compatible driver is bound to it or another process
                # holds it. Skip it rather than abandoning the search: it may
                # not even be the radio.
                unreadable += 1
                continue
            if product != PRODUCT:
                continue
            if serial and serial != identity:
                continue
            for config in device:
                for interface in config:
                    if ((interface.bInterfaceClass, interface.bInterfaceSubClass,
                         interface.bInterfaceProtocol) == (0xfe, 1, 2) and
                            util.get_string(device, interface.iInterface) in INTERFACES):
                        found.append((device, config.bConfigurationValue,
                                      interface.bInterfaceNumber, interface.bAlternateSetting, identity))
        finally:
            if not any(entry[0] is device for entry in found):
                util.dispose_resources(device)
    if len(found) > 1:
        for entry in found:
            util.dispose_resources(entry[0])
        raise UpdateError('Multiple ApexTX radios were detected. Connect one radio or select it with --serial.')
    if not found and unreadable:
        raise UpdateError('A USB device was found but could not be read. Install a '
                          'compatible USB driver for the update interface, or close '
                          'other software using it. See docs/nb4/UPDATE.md.')
    return found[0] if found else None


class Transport:
    def __init__(self, selection):
        self.device, config, self.interface, alternate, self.serial = selection
        _, self.util, _ = usb_modules()
        try:
            current = self.device.get_active_configuration().bConfigurationValue
        except Exception:
            current = None
        try:
            if current != config:
                self.device.set_configuration(config)
            self.util.claim_interface(self.device, self.interface)
            self.device.set_interface_altsetting(interface=self.interface, alternate_setting=alternate)
            self.idle()
        except Exception:
            self.close()
            raise

    def close(self):
        self.util.dispose_resources(self.device)

    def out(self, request, value=0, data=b''):
        return self.device.ctrl_transfer(0x21, request, value, self.interface, data, timeout=5000)

    def incoming(self, request, size, value=0):
        return bytes(self.device.ctrl_transfer(0xa1, request, value, self.interface, size, timeout=5000))

    def status(self):
        result = self.incoming(3, 6)
        if len(result) != 6:
            raise UpdateError('The radio returned an incomplete USB response.')
        return result[0], int.from_bytes(result[1:4], 'little') / 1000, result[4]

    def idle(self):
        error, _, state = self.status()
        if error or state == 10:
            self.out(4)  # CLRSTATUS
        self.out(6)  # ABORT returns upload/download idle to DFU idle, preserves staging.
        error, _, state = self.status()
        if error or state != 2:
            raise UpdateError(f'The radio is not ready: USB state {state}, error {error}.')

    def download(self, block, data):
        self.out(1, block, data)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            error, delay, state = self.status()
            if error:
                raise UpdateError(f'The radio rejected the transfer: USB error {error}.')
            if state == 5:
                return
            if state not in (3, 4):
                raise UpdateError(f'Unexpected USB state: {state}.')
            time.sleep(min(max(delay, .001), 2))
        raise UpdateError('The radio did not acknowledge the data block within the allowed time.')

    def address(self, address):
        self.idle()
        self.download(0, b'\x21' + struct.pack('<I', address))
        self.idle()

    def read(self, address, size):
        self.address(address)
        result = bytearray()
        for offset in range(0, size, TRANSFER):
            count = min(TRANSFER, size - offset)
            block = self.incoming(2, count, 2 + offset // TRANSFER)
            if len(block) != count:
                raise UpdateError('The verification read returned fewer bytes than expected.')
            result.extend(block)
        return bytes(result)

    def command(self, command):
        self.address(CONTROL)
        self.download(2, command)


def run_update(image, progress, serial=None, wait_seconds=90, transport_factory=Transport,
               finder=discover):
    package = update_package(image)  # Validate before any USB operation.
    _, _, _, size, crc, _, _ = HEADER.unpack(package[:32])
    progress('Waiting for ApexTX Update. Connect the radio and select Settings > System > Update.', 0)
    deadline = time.monotonic() + wait_seconds
    selection = finder(serial)
    while selection is None and time.monotonic() < deadline:
        time.sleep(.5)
        selection = finder(serial)
    if selection is None:
        raise UpdateError('ApexTX Update was not detected. Confirm that a compatible bootloader is installed and the radio displays ApexTX Update.')
    transport = transport_factory(selection)
    # How far the radio has got, so a lost connection can be explained.
    reached = ['transfer']

    def install():
        for attempt in range(2):
            sending = 'Transferring firmware...' if not attempt else 'Retrying installation setup (1/1)...'
            progress(sending, 2)
            transport.address(STAGE)
            for offset in range(0, len(package), TRANSFER):
                transport.download(2 + offset // TRANSFER, package[offset:offset + TRANSFER])
                progress(sending, 2 + (offset + min(TRANSFER, len(package) - offset)) * 43 // len(package))
            progress('Verifying the transfer...', 46)
            if transport.read(STAGE, len(package)) != package:
                raise UpdateError('Transfer verification failed. Flash memory has not been written in this attempt.')
            transport.command(b'APXSTART')
            reached[0] = 'installing'
            deadline = time.monotonic() + 180
            while True:
                status = transport.read(CONTROL, 32)
                magic, state, percent, actual_size, actual_crc, error, received = struct.unpack('<8s6I', status)
                if (magic != b'APXSTAT1' or actual_size != size or actual_crc != crc or
                        received != len(package) or percent > 100):
                    raise UpdateError('The radio response does not match the current firmware image.')
                if state == 4 and error == 2 and percent == 0 and attempt == 0:
                    # A bench run rejected the first erase; a complete retry
                    # succeeded. HAL clears pending flash error flags on failure.
                    # Retry only this initial failure, once, with both checks.
                    reached[0] = 'transfer'
                    break
                if state == 4 or error:
                    raise UpdateError(f'Installation failed (code {error}). Keep the radio in update mode and try again.')
                if state == 3:
                    reached[0] = 'verified'
                    break
                if state not in (1, 2) or time.monotonic() > deadline:
                    raise UpdateError('Installation could not be confirmed. Check the radio display and reconnect the updater.')
                progress('Installing and verifying firmware...', 50 + percent * 47 // 100)
                time.sleep(.5)
            if state == 3:
                break
        progress('Firmware verified. Restarting the radio...', 98)
        transport.command(b'APXRESET')

    try:
        try:
            install()
        except UpdateError:
            raise
        except Exception as error:
            # Whatever the USB stack raises here, a pulled cable above all, has
            # to say what state the radio is in rather than surface an errno.
            raise connection_lost(reached[0], error) from error
    finally:
        transport.close()
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        try:
            selection = finder(serial)
        except UpdateError:
            # The radio is detaching, and a device being torn down stops
            # answering descriptor reads. A radio still sitting in update mode
            # answers them, as it did throughout the transfer, so a read that
            # fails here means it left rather than that anything is wrong.
            selection = None
        if selection is None:
            progress('Firmware verified. Confirm that the radio has returned to the home screen.', 100)
            return
        _, util, _ = usb_modules()
        util.dispose_resources(selection[0])
        time.sleep(.5)
    # The firmware is written and verified; only the restart did not happen.
    # That is a finished update with an instruction, not a failure.
    progress('Firmware verified, but the radio is still in update mode. Hold the '
             'power button on the radio to restart it.', 100)
