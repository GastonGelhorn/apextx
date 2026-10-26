# SPDX-License-Identifier: GPL-2.0-only
"""One device/release session shared by the Windows and macOS front ends."""
import copy
import threading
import time

from nb4_releases import ReleaseClient, bootloader_required, image_bootloader_generation, update_decision
from nb4_usb_update import UpdateError, discover, probe, release, run_update


class FlasherSession:
    def __init__(self, client=None):
        self.client = client or ReleaseClient()
        self.lock = threading.RLock()
        self.usb_lock = threading.RLock()
        self.stop = threading.Event()
        self.wake = threading.Event()
        self.device = dict(state='searching')
        self.offer = None
        self.bootloader_install = None
        self.preview = True  # ApexTX is currently an alpha; clearly labelled in the UI.
        self.release_error = None
        self.checking = False
        self.next_check = 0
        self.cache_key = None
        self.status = dict(running=False, progress=0, message='', error=False,
                           operation=None, serial=None, target=None)

    def snapshot(self):
        with self.lock:
            offer = None
            selected = self.bootloader_install['offer'] if self.bootloader_install else self.offer
            if selected:
                offer = {k: selected[k] for k in ('id', 'version', 'prerelease', 'url', 'target')}
                offer['bootloader'] = selected.get('bootloader', 0)
                offer['decision'] = update_decision(self.device, selected)
            plan = None
            if self.bootloader_install:
                original = self.bootloader_install['device']
                plan = {k: original.get(k) for k in ('serial', 'target', 'model', 'version')}
                plan['ready'] = (self.device.get('state') == 'recovery' and
                                 self.device.get('serial') == original['serial'])
            return copy.deepcopy(dict(self.status, device=self.device, release=offer,
                bootloader_install=plan,
                preview=self.preview, checking=self.checking, release_error=self.release_error))

    def refresh(self, preview=None):
        with self.lock:
            if self.status['running']:
                raise UpdateError('An operation is already in progress.')
            if preview is not None and self.bootloader_install:
                raise UpdateError('Finish or cancel the bootloader installation before changing release channels.')
            if preview is not None:
                self.preview = preview
                self.offer = None
                self.release_error = None
                self.checking = bool(self.device.get('target'))
            self.next_check = 0
            self.wake.set()

    def scan(self):
        with self.usb_lock:
            with self.lock:
                if self.status['running']:
                    return
            try:
                device = probe()
            except Exception as error:
                device = dict(state='error', message=str(error))
            with self.lock:
                # A completed operation belongs to its radio. Keep its restart
                # instruction across a detach, but never show it for another.
                if (not self.bootloader_install and device.get('serial') and self.status['serial'] and
                        (device['serial'] != self.status['serial'] or
                         device.get('target') not in (None, self.status['target']))):
                    self.status.update(operation=None, progress=0, message='',
                                       error=False, serial=None, target=None)
                self.device = device
                if self.bootloader_install:
                    # Keep the verified image and original serial across the
                    # deliberate disconnect/reconnect into ROM DFU. Never write
                    # merely because a DFU device has appeared.
                    self.checking = False
                    return
                key = (device.get('target'), self.preview)
                if device.get('state') not in ('ready', 'normal') or not device.get('target'):
                    self.offer = None
                    self.release_error = None
                    self.cache_key = None
                    self.checking = False
                    return
                if key != self.cache_key:
                    self.offer = None
                    self.release_error = None
                    self.next_check = 0
                if time.monotonic() < self.next_check:
                    return
                self.cache_key = key
                self.checking = True
        # Network I/O never holds the USB lock. A check for another channel or
        # target cannot replace the currently selected offer.
        try:
            offer = self.client.latest(*key)
            error = None
        except Exception as cause:
            offer, error = None, str(cause)
        with self.lock:
            if key != (self.device.get('target'), self.preview):
                self.checking = False
                return
            self.offer, self.release_error, self.checking = offer, error, False
            self.next_check = time.monotonic() + (30 if error else 300)

    def monitor(self):
        while not self.stop.is_set():
            self.scan()
            self.wake.wait(1.5)
            self.wake.clear()

    def progress(self, message, percent):
        with self.lock:
            self.status.update(message=message, progress=percent)

    def begin(self, operation):
        with self.lock:
            if self.status['running']:
                raise UpdateError('An operation is already in progress.')
            self.status.update(running=True, error=False, progress=0, message='',
                               operation=operation, serial=self.device.get('serial'),
                               target=self.device.get('target'))

    def finish(self, error=None):
        with self.lock:
            if error:
                self.status.update(error=True, message=str(error))
            self.status['running'] = False
        self.wake.set()

    def choice(self, serial, target, release_id, bootloader=False):
        with self.lock:
            device = self.device
            if self.bootloader_install:
                raise UpdateError('Finish or cancel the pending bootloader installation first.')
            if (device.get('state') != 'ready' or not serial or
                    (serial, target) != (device.get('serial'), device.get('target')) or
                    not self.offer or release_id != self.offer['id'] or target != self.offer['target']):
                raise UpdateError('The radio or release changed. Check the detected radio and try again.')
            decision = update_decision(device, self.offer)
            if bootloader:
                if decision != 'bootloader':
                    raise UpdateError('This radio does not need a bootloader update. Check again.')
            elif decision == 'bootloader':
                raise UpdateError('This release needs a bootloader update. Follow the STM32 ROM DFU installation guide in the flasher.')
            elif decision in ('current', 'ahead', 'development'):
                raise UpdateError('No automatic upgrade is available. Use a local image for a deliberate reinstall or development build replacement.')
            return copy.deepcopy(device), copy.deepcopy(self.offer)

    def prepare_bootloader(self, device, offer):
        try:
            image = self.client.download(offer, self.progress)
            from nb4_rom_flash import validate_full_image
            if validate_full_image(image) != device['model']:
                raise UpdateError('The complete image is for another radio. Nothing was written.')
            with self.lock:
                self.bootloader_install = dict(device=device, offer=offer, image=image)
                self.status.update(progress=0, message='Firmware downloaded and verified. Enter STM32 ROM DFU to continue.')
        except Exception as error:
            self.finish(error)
        else:
            self.finish()

    def cancel_bootloader(self):
        with self.lock:
            if self.status['running']:
                raise UpdateError('An operation is already in progress.')
            self.bootloader_install = None
            self.status.update(operation=None, progress=0, message='', error=False, serial=None, target=None)
            self.next_check = 0
            self.wake.set()

    def bootloader_choice(self, serial, target, release_id):
        with self.lock:
            plan = self.bootloader_install
            if (not plan or (serial, target, release_id) !=
                    (plan['device']['serial'], plan['device']['target'], plan['offer']['id']) or
                    self.device.get('state') != 'recovery' or self.device.get('serial') != serial):
                raise UpdateError('Reconnect the original radio in STM32 ROM DFU mode before installing.')
            return plan['image'], plan['device']['model'], serial

    @staticmethod
    def check_bootloader(device, image):
        if bootloader_required(device, image_bootloader_generation(image)):
            raise UpdateError('This firmware needs a new bootloader. Use the guided STM32 ROM DFU installation or Install / Recovery; a normal Update preserves the old bootloader.')

    @staticmethod
    def pinned_finder(device):
        def find(_serial):
            selection = discover()  # Also rejects multiple attached Update radios.
            if selection is None:
                return None
            if (selection.serial, selection.target) != (device['serial'], device['target']):
                release(selection)
                raise UpdateError('The connected radio changed. Nothing was written. Reconnect the original radio.')
            return selection
        return find

    def automatic(self, device, offer):
        try:
            with self.usb_lock:
                finder = self.pinned_finder(device)
                selection = finder(device['serial'])
                if selection is None:
                    raise UpdateError('The radio was disconnected. Reconnect it in ApexTX Update mode.')
                release(selection)
                image = self.client.download(offer, self.progress)
                self.check_bootloader(device, image)
                if offer.get('bootloader'):
                    # Re-read only standard descriptors before opening a write
                    # transaction; discovery alone does not prove compatibility.
                    current = probe()
                    if (current.get('state'), current.get('serial'), current.get('target')) != ('ready', device['serial'], device['target']):
                        raise UpdateError('The connected radio changed. Nothing was written.')
                    self.check_bootloader(current, image)
                run_update(image, lambda message, percent: self.progress(message, 8 + percent * 92 // 100),
                           serial=device['serial'], wait_seconds=0, finder=finder)
        except Exception as error:
            self.finish(error)
        else:
            self.finish()
