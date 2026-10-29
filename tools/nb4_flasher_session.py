# SPDX-License-Identifier: GPL-2.0-only
"""One device/release session shared by the Windows and macOS front ends."""
import copy
from pathlib import Path
import threading
import time

from nb4_releases import ReleaseClient, bootloader_required, image_bootloader_generation, update_decision
from nb4_usb_update import UpdateError, discover, probe, release, run_update
from nb4_maintenance import TARGETS, connection
from nb4_rf_transfer import AssetClient, publish_assets, reboot_into_update

REPLUG = 'Unplug USB, plug it again and choose ApexTX Flasher on the radio.'


def maintenance_mode(device):
    return device.get('state') == 'normal' and device.get('maintenance') is True


class FlasherSession:
    def __init__(self, client=None):
        self.client = client or ReleaseClient()
        self.lock = threading.RLock()
        self.usb_lock = threading.RLock()
        self.stop = threading.Event()
        self.wake = threading.Event()
        self.device = dict(state='searching')
        self.offer = None
        self.inventory = None
        self.inventory_time = 0
        self.integrated = None
        self.integrated_view = None
        self.bootloader_install = None
        self.app_updating = False
        self.radio_unconfirmed = False
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
                offer['audio_languages'] = list(selected.get('audio_assets',{}))
                offer['decision'] = update_decision(self.device, selected)
            plan = None
            if self.bootloader_install:
                original = self.bootloader_install['device']
                plan = {k: original.get(k) for k in ('serial', 'target', 'model', 'version')}
                plan['ready'] = (self.device.get('state') == 'recovery' and
                                 self.device.get('serial') == original['serial'])
            return copy.deepcopy(dict(self.status, device=self.device, release=offer,
                bootloader_install=plan,
                preview=self.preview, checking=self.checking, release_error=self.release_error,
                app_updating=self.app_updating, components=self.inventory,
                integrated=self.integrated_view,
                components_fresh=bool(self.inventory and time.monotonic() - self.inventory_time < 10)))

    def refresh(self, preview=None):
        with self.lock:
            if self.status['running'] or self.app_updating:
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
                if self.status['running'] or self.app_updating:
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
                if (self.inventory and (device.get('state') != 'normal' or
                        device.get('maintenance') is not True or
                        device.get('serial') != self.inventory.get('serial') or
                        TARGETS.get(device.get('target')) != self.inventory.get('target'))):
                    self.inventory = None
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

    def app_restart_blocked(self):
        # Keep uncertainty independent of the status display, which discovery
        # may clear when a different radio appears.
        unfinished=bool(self.integrated_view and any(step['state'] in ('running','staged','verifying','recovery_required','failed')
            for step in self.integrated_view['steps'].values()))
        if self.integrated_view and self.integrated_view.get('outcome')=='recovery_required': unfinished=True
        return bool(self.status['running'] or self.bootloader_install or self.radio_unconfirmed or unfinished)

    def reserve_app_update(self):
        with self.lock:
            if self.app_updating or self.app_restart_blocked():
                raise UpdateError('Finish the radio operation or recovery before updating ApexTX Flasher.')
            self.app_updating = True

    def acknowledge_radio(self):
        with self.lock:
            if self.status['running'] or self.bootloader_install or self.app_updating:
                raise UpdateError('Finish or cancel the radio operation first.')
            self.radio_unconfirmed = False

    def begin(self, operation):
        with self.lock:
            if self.status['running'] or self.app_updating:
                raise UpdateError('An operation is already in progress.')
            self.status.update(running=True, error=False, progress=0, message='',
                               operation=operation, serial=self.device.get('serial'),
                               target=self.device.get('target'))

    def finish(self, error=None):
        with self.lock:
            if error:
                self.status.update(error=True, message=str(error))
                if self.status['operation'] in ('upgrade', 'manual', 'recover', 'bootloader'):
                    self.radio_unconfirmed = True
            self.status['running'] = False
        self.wake.set()

    def inventory_choice(self, serial, target):
        with self.lock:
            if (self.bootloader_install or self.device.get('state') != 'normal' or
                    self.device.get('maintenance') is not True or
                    not serial or (serial, target) !=
                    (self.device.get('serial'), self.device.get('target'))):
                raise UpdateError('Select the original radio in its ApexTX Flasher connection mode.')
            return copy.deepcopy(self.device)

    def read_inventory(self, device):
        from nb4_maintenance import TARGETS, read_inventory
        try:
            with self.usb_lock:
                current = probe()
                identity = (device['serial'], device['target'])
                if current.get('state') != 'normal' or current.get('maintenance') is not True or identity != (current.get('serial'), current.get('target')):
                    raise UpdateError('The selected radio changed. Nothing was written.')
                inventory = read_inventory(device['serial'], TARGETS[device['target']], verified=True)
                current = probe()
                if current.get('state') != 'normal' or current.get('maintenance') is not True or identity != (current.get('serial'), current.get('target')):
                    raise UpdateError('The radio disconnected during inventory. Read it again.')
                with self.lock:
                    self.inventory, self.inventory_time = inventory, time.monotonic()
                self.progress('Component versions read. Nothing was written.', 100)
        except Exception as error:
            self.inventory = None
            self.finish(error)
        else:
            self.finish()

    def prepare_components(self,data,expected_serial=None):
        from pathlib import Path
        import tempfile,uuid
        from nb4_integrated_update import IntegratedUpdate,unpack_package,Package
        try:
            with self.usb_lock:
                device=probe()
                if not device.get('serial') or device.get('state') not in ('normal','ready','recovery'):
                    raise UpdateError('Connect only the transmitter you want to prepare.')
                if device['serial']!=expected_serial: raise UpdateError('The selected transmitter changed before package preparation.')
                if self.integrated and any(step['state'] in ('staged','running','recovery_required') for step in self.integrated.record['steps'].values()):
                    raise UpdateError('Finish or recover the existing component plan before replacing it.')
                with tempfile.TemporaryDirectory(prefix='apextx-components-') as folder:
                    package=unpack_package(data,Path(folder)/'package')
                    target=Package(package).target
                    if device.get('target') not in (None,target): raise UpdateError('This complete package is for another transmitter.')
                    plan=IntegratedUpdate.create(package,device['serial'],target,
                        Path.home()/'ApexTX-updates'/device['serial']/uuid.uuid4().hex)
                with self.lock: self.integrated=plan; self.integrated_view=plan.snapshot()
                self.remember_components()
                self.progress('Complete package verified and prepared offline. Review components and create the recovery backup.',100)
        except Exception as error: self.finish(error)
        else: self.finish()

    def download_components(self,device,offer):
        try:
            asset=offer.get('package_asset')
            if not asset: raise UpdateError('This release has no complete component package. Choose a local package.')
            data=self.client.download_asset(offer,asset,self.progress)
            with self.usb_lock:
                current=probe()
                if (current.get('serial'),current.get('target'))!=(device['serial'],device['target']):
                    raise UpdateError('The selected transmitter changed while downloading.')
                self.prepare_components(data,device['serial'])
        except Exception as error: self.finish(error)

    def download_component_audio(self,device,offer,identifier,language):
        from pathlib import Path
        import tempfile
        from nb4_audio_install import safe_extract,read_and_verify_pack
        try:
            with self.lock:
                plan=self.integrated
                if (not plan or plan.record['id']!=identifier or plan.record['serial']!=device['serial'] or
                        plan.record['target']!=device['target'] or plan.package.manifest['release']!=offer['version']):
                    raise UpdateError('The selected radio, package or release changed. Review the component plan.')
            asset=offer.get('audio_assets',{}).get(language)
            if not asset: raise UpdateError('This release has no selected voice pack. Choose a local audio ZIP.')
            data=self.client.download_asset(offer,asset,self.progress)
            with tempfile.TemporaryDirectory(prefix='apextx-voice-download-') as folder:
                archive=Path(folder)/'audio.zip'; archive.write_bytes(data)
                safe_extract(archive,Path(folder)/'pack')
                manifest,_=read_and_verify_pack(Path(folder)/'pack')
                if (manifest['language'],manifest['release'])!=(language,offer['version']):
                    raise UpdateError('The downloaded voice pack does not match the selected language/release.')
            self.component_audio(data,identifier)
        except Exception as error: self.finish(error)

    def component_action(self,identifier,action,component=None,path=None,selected=None,reinstall=False):
        plan=None
        def progress(message,percent):
            self.progress(message,percent)
            if plan:
                with self.lock: self.integrated_view=plan.snapshot()
        try:
            with self.usb_lock:
                plan=self.integrated
                if not plan or plan.record['id']!=identifier: raise UpdateError('The selected component plan changed.')
                progress('Preparing component action',0)
                if action=='read': plan.inventory()
                elif action=='select': plan.select(selected)
                elif action=='approve': plan.approve()
                elif action=='backup': plan.backup(progress)
                elif action=='attach-backup': plan.attach_backup(path)
                elif action=='storage': plan.prepare_storage()
                elif action=='restore-nor': plan.restore_nor(progress)
                elif action=='restore-application': plan.restore_application(progress)
                elif action=='restore-audio': plan.restore_audio(path,progress)
                elif action=='audio-pack': plan.add_audio(path)
                elif component in ('module','receiver') and action in ('stage','install','recover','cancel'):
                    plan.rf(component,action,progress,reinstall=reinstall)
                elif action=='install' and component=='application': plan.application(progress)
                elif action=='install' and component=='resources': plan.resources(path,progress)
                elif action=='install' and component=='audio': plan.audio(path,progress)
                else: raise UpdateError('Unsupported component-plan action.')
                with self.lock: self.integrated_view=plan.snapshot()
                self.progress('Component plan saved. Review the result and the required connection for the next phase.',100)
        except Exception as error:
            if plan:
                with self.lock: self.integrated_view=plan.snapshot()
            self.finish(error)
        else: self.finish()

    def open_components(self,path):
        from nb4_integrated_update import IntegratedUpdate
        try:
            with self.usb_lock:
                if self.integrated and any(step['state'] in ('staged','running','recovery_required') for step in self.integrated.record['steps'].values()):
                    raise UpdateError('Finish or recover the current component plan before opening another.')
                plan=IntegratedUpdate.load(path)
                with self.lock: self.integrated=plan; self.integrated_view=plan.snapshot()
                self.remember_components()
        except Exception as error: self.finish(error)
        else: self.finish()

    def remember_components(self):
        from pathlib import Path
        from nb4_rf_transfer import save_json
        if not self.integrated: return
        root=Path.home()/'ApexTX-updates'; root.mkdir(mode=0o700,parents=True,exist_ok=True)
        save_json(root/'active.json',dict(format='apextx-active-components-1',directory=str(self.integrated.root.resolve())))

    def resume_components(self):
        from pathlib import Path
        import json
        from nb4_integrated_update import IntegratedUpdate
        marker=Path.home()/'ApexTX-updates/active.json'
        if not marker.exists(): return
        try:
            if marker.is_symlink() or marker.stat().st_size>4096: raise UpdateError('Invalid active component-session record.')
            value=json.loads(marker.read_text(encoding='utf-8'))
            if value.get('format')!='apextx-active-components-1': raise UpdateError('Unsupported active component-session record.')
            plan=IntegratedUpdate.load(value['directory'])
            self.integrated=plan; self.integrated_view=plan.snapshot()
        except Exception as error:
            self.radio_unconfirmed=True; self.status.update(error=True,operation='components',message=str(error))

    def component_audio(self,data,identifier):
        import tempfile
        from pathlib import Path
        try:
            with tempfile.TemporaryDirectory(prefix='apextx-audio-input-') as folder:
                path=Path(folder)/'audio.zip'; path.write_bytes(data)
                self.component_action(identifier,'audio-pack',path=path)
        except Exception as error: self.finish(error)

    def choice(self, serial, target, release_id, bootloader=False):
        with self.lock:
            device = self.device
            if self.bootloader_install:
                raise UpdateError('Finish or cancel the pending bootloader installation first.')
            # Update mode, or the radio's own ApexTX Flasher connection, which
            # the one-click path restarts into Update mode. Any other USB mode
            # (joystick, storage, plain serial) cannot be updated from here.
            if ((device.get('state') != 'ready' and not maintenance_mode(device)) or not serial or
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
            if self.status['running'] or self.app_updating:
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

    @staticmethod
    def require_maintenance(device):
        current = probe()
        if not maintenance_mode(current) or (current.get('serial'), current.get('target')) != (device['serial'], device['target']):
            raise UpdateError('The radio left its ApexTX Flasher connection. Nothing was written. ' + REPLUG)
        return current

    def catalog_images(self, device, offer):
        """Verified RF images for this radio: the downloaded release package's
        rf/catalog.zip when a plan for this radio and release holds one, else
        the catalog embedded next to the tools. Both are re-verified here."""
        from nb4_rf_catalog import MAX_BUNDLE, verify_bundle
        hardware = TARGETS[device['target']]
        with self.lock:
            plan = self.integrated
        if (plan and plan.package.catalog and plan.record['serial'] == device['serial'] and
                plan.record['target'] == device['target'] and plan.package.manifest.get('release') == offer['version']):
            return plan.package.images
        path = Path(__file__).with_name('rf-catalog') / f'{hardware}.zip'
        if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_BUNDLE:
            raise UpdateError('The RF catalog for this radio is missing from ApexTX Flasher. Download a complete copy.')
        catalog, images = verify_bundle(path.read_bytes())
        if catalog['radio'] != hardware:
            raise UpdateError('The RF catalog targets a different transmitter. Nothing was written.')
        return images

    @staticmethod
    def wait_for_update_mode(device, *, timeout=20, clock=time.monotonic, sleep=time.sleep):
        """Only standard descriptor reads while the radio re-enumerates. A
        read that fails mid-transition is retried; a different radio is not."""
        deadline = clock() + timeout
        while True:
            try:
                current = probe()
            except UpdateError:
                current = dict(state='error')
            if (current.get('state'), current.get('serial'), current.get('target')) == ('ready', device['serial'], device['target']):
                return current
            if current.get('state') == 'multiple' or current.get('serial') not in (None, device['serial']):
                raise UpdateError('A different radio appeared. Nothing was written. Reconnect the original radio.')
            if clock() >= deadline:
                raise UpdateError('The radio did not return in ApexTX Update mode. Nothing was written. '
                                  'On the radio open System > Update > Update radio firmware, or reconnect it.')
            sleep(.5)

    def automatic(self, device, offer, *, clock=time.monotonic, sleep=time.sleep):
        try:
            with self.usb_lock:
                finder = self.pinned_finder(device)
                maintenance = maintenance_mode(device)
                if maintenance:
                    self.require_maintenance(device)
                else:
                    selection = finder(device['serial'])
                    if selection is None:
                        raise UpdateError('The radio was disconnected. Reconnect it in ApexTX Update mode.')
                    release(selection)
                image = self.client.download(offer, self.progress)
                if maintenance:
                    # The user confirmed this update once. Store the catalog
                    # images first (files only; the radio installs RF itself),
                    # then let the radio restart into Update mode. Its
                    # bootloader generation is only known once it is there.
                    images = self.catalog_images(device, offer)
                    self.require_maintenance(device)
                    hardware = TARGETS[device['target']]
                    with connection(device['serial'], hardware, verified=True) as port:
                        client = AssetClient(port)
                        publish_assets(client, hardware, images,
                                       progress=lambda message, percent: self.progress(message, 8 + percent * 4 // 100))
                        self.progress('Restarting the radio into ApexTX Update...', 12)
                        reboot_into_update(client)
                    current = self.wait_for_update_mode(device, clock=clock, sleep=sleep)
                    self.check_bootloader(current, image)
                    wait_seconds = 10
                else:
                    self.check_bootloader(device, image)
                    if offer.get('bootloader'):
                        # Re-read only standard descriptors before opening a write
                        # transaction; discovery alone does not prove compatibility.
                        current = probe()
                        if (current.get('state'), current.get('serial'), current.get('target')) != ('ready', device['serial'], device['target']):
                            raise UpdateError('The connected radio changed. Nothing was written.')
                        self.check_bootloader(current, image)
                    wait_seconds = 0
                run_update(image, lambda message, percent: self.progress(message, 12 + percent * 88 // 100),
                           serial=device['serial'], wait_seconds=wait_seconds, finder=finder)
        except Exception as error:
            self.finish(error)
        else:
            self.finish()
