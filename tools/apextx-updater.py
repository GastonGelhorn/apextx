#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""ApexTX Flasher: local browser interface or command-line NB4 update."""
import argparse
import json
import secrets
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

from nb4_update_image import FLASH_SIZE, VARIANTS, package_target, update_package
from nb4_usb_update import run_update, discover
from nb4_flasher_session import FlasherSession
import nb4_rom_flash

import base64
import datetime
import functools
import os
import sys

from flasher_app_update import app_version, AppUpdateController, InstanceLease

APP_VERSION = app_version()


def package_self_test():
    """Check the packaged dependencies without discovering or writing a radio."""
    from nb4_usb_update import usb_modules
    folder = Path(__file__).resolve().parent
    page = (folder / 'apextx-updater.html').read_text(encoding='utf-8')
    if 'id="updatePanel"' not in page or 'id="recoverPanel"' not in page or not _logo_data_uri():
        raise RuntimeError('The shared flasher interface is incomplete.')
    _core, _util, backend = usb_modules()
    if backend is None:
        raise RuntimeError('The bundled USB backend could not be loaded.')
    result = nb4_rom_flash._run([nb4_rom_flash.dfu_util_path(), '--version'])
    if result.returncode != 0 or 'dfu-util 0.11' not in result.stdout:
        raise RuntimeError('The bundled dfu-util could not be started: ' + result.stdout)
    metadata = json.loads((folder / 'apextx-flasher-build.json').read_text(encoding='utf-8'))
    import certifi
    import ssl
    if ssl.create_default_context(cafile=certifi.where()).cert_store_stats()['x509_ca'] == 0:
        raise RuntimeError('The packaged release-download CA store is missing.')
    from nb4_releases import Version
    if not Version('0.1.0-alpha.9') < Version('0.1.0-alpha.10'):
        raise RuntimeError('The shared release client could not be loaded.')
    from flasher_app_update import trusted_keys
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    for value in trusted_keys().values():
        Ed25519PublicKey.from_public_bytes(base64.b64decode(value, validate=True))
    from nb4_rf_catalog import PROFILES, manifest
    import serial
    from nb4_maintenance import Decoder, encode
    from nb4_audio_install import JOURNAL_FORMAT
    from nb4_rf_transfer import TransferClient
    from nb4_backup_agent import RAM, RAM_CAPACITY, validate_agent
    from nb4_integrated_update import IntegratedUpdate
    from nb4_nor_restore import STATUS as NOR_STATUS
    if not callable(IntegratedUpdate.load) or NOR_STATUS.size!=32:
        raise RuntimeError('The integrated component/restoration runtime is incomplete.')
    if (18 not in TransferClient.allowed_commands or RAM != 0x20010000 or
            RAM + RAM_CAPACITY != 0x2002fe00 or not callable(validate_agent)):
        raise RuntimeError('The experimental RF/backup runtime is incomplete.')
    if Decoder().feed(encode(1, 123)) != [(1, 123, b'')] or not JOURNAL_FORMAT:
        raise RuntimeError('The shared maintenance/audio runtime is incomplete.')
    if not PROFILES or any(manifest(profile)['rf_write_supported'] for profile in PROFILES):
        raise RuntimeError('The read-only RF package preparer is missing or unsafe.')
    from nb4_rf_catalog import MAX_BUNDLE, verify_bundle
    for radio in ('nb4', 'nb4plus'):
        bundle = folder / 'rf-catalog' / f'{radio}.zip'
        if not bundle.is_file() or bundle.stat().st_size > MAX_BUNDLE or verify_bundle(bundle.read_bytes())[0]['radio'] != radio:
            raise RuntimeError('The embedded RF catalog is missing or targets the wrong radio.')
    return dict(metadata, usb_backend=True, dfu_util=True, shared_ui=True,
                release_client=True, rf_catalog=True, maintenance=True, audio_installer=True,
                rf_transfer=True, backup_agent=True, app_updates=True, integrated_update=True, nor_restore=True)


def _enable_bundled_libusb():
    """Make the bundled libusb runtime loadable in a frozen Windows build.

    This supplies the user-mode DLL; Windows must still associate the USB
    device with a WinUSB-compatible kernel driver.
    """
    candidates = []
    meipass = getattr(sys, '_MEIPASS', None)
    if meipass:
        candidates.append(Path(meipass))
    candidates.append(Path(__file__).resolve().parent)
    for folder in candidates:
        if (folder / 'libusb-1.0.dll').is_file():
            os.environ['PATH'] = str(folder) + os.pathsep + os.environ.get('PATH', '')
            try:
                os.add_dll_directory(str(folder))
            except (OSError, AttributeError):
                pass
            return


@functools.lru_cache(maxsize=1)
def _logo_data_uri():
    """Inline the ApexTX brand logo so the page stays a single response."""
    try:
        raw = Path(__file__).with_name('apextx-updater-logo.png').read_bytes()
        return 'data:image/png;base64,' + base64.b64encode(raw).decode('ascii')
    except Exception:
        return ''


def serve(port=0, open_browser=True, startup_health=None):
    token = secrets.token_urlsafe(32)
    session = FlasherSession()
    session.resume_components()
    lock, status = session.lock, session.status
    window = None
    restarting = threading.Event()
    health_ready = threading.Event()
    startup_failed = threading.Event()
    if not startup_health:
        health_ready.set()
    else:
        session.app_updating = True

    def restart_app():
        restarting.set()
        if window is not None:
            window.destroy()
        server.shutdown()

    app_updates = AppUpdateController(session, restart_app)

    def monitor_radio():
        health_ready.wait()
        session.monitor()

    monitor = threading.Thread(target=monitor_radio, daemon=True)
    monitor.start()
    threading.Thread(target=app_updates.monitor, daemon=True).start()

    def progress(message, percent):
        with lock:
            status.update(message=message, progress=percent)

    def update(image, device=None):
        try:
            with session.usb_lock:
                # Local images must obey the same bootloader requirement as
                # release downloads; manual loading cannot silently skip it.
                from nb4_usb_update import probe
                session.check_bootloader(probe(), image)
                if device:
                    run_update(image, progress, serial=device['serial'], wait_seconds=0,
                               finder=session.pinned_finder(device))
                else:
                    run_update(image, progress)
        except Exception as error:
            with lock:
                status.update(message=str(error), error=True)
                session.radio_unconfirmed = True
        finally:
            with lock:
                status['running'] = False

    def recover(image, radio_variant, expected_serial=None):
        with session.usb_lock:
            recover_locked(image, radio_variant, expected_serial)

    def recover_locked(image, radio_variant, expected_serial=None):
        try:
            wanted = nb4_rom_flash.validate_full_image(image)
            if wanted != radio_variant:
                raise nb4_rom_flash.RomFlashError(
                    f'This image is for the Noble {wanted}, but you selected Noble '
                    f'{radio_variant}. Nothing was written.')
            serial = nb4_rom_flash.discover_rom_dfu()
            if serial is None:
                raise nb4_rom_flash.RomFlashError(
                    'No radio in STM32 ROM DFU mode was found. Put the radio into ROM DFU '
                    '(hold the recessed grip button while switching it on) and connect USB.')
            if expected_serial and serial != expected_serial:
                raise nb4_rom_flash.RomFlashError('The connected DFU device changed. Reconnect the original radio. Nothing was written.')
            with lock:
                status.update(serial=serial, target=next(
                    number for number, name in VARIANTS.items() if name == radio_variant))
            # 1) Mandatory backup of the radio's current firmware.
            folder = Path.home() / 'ApexTX-Backups'
            folder.mkdir(parents=True, exist_ok=True)
            stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
            safe_serial = nb4_rom_flash.safe_serial(serial)
            backup = folder / f'{radio_variant.replace("+", "plus")}-firmware-backup-{safe_serial}-{stamp}.bin'
            nb4_rom_flash.backup_internal(backup, progress, serial)
            metadata = nb4_rom_flash.write_backup_metadata(backup, radio_variant, serial)
            # 2) Detect the current firmware and cross-check the image's model.
            current = nb4_rom_flash.inspect_flash(backup.read_bytes())
            if current['firmware']!='ApexTX':
                raise nb4_rom_flash.RomFlashError('First installation requires a complete verified internal/NOR backup. Use the Components wizard; first-installation rollback remains experimental.')
            if (current['firmware'] == 'ApexTX' and
                    current['variant'] != radio_variant):
                raise nb4_rom_flash.RomFlashError(
                    f"You selected Noble {radio_variant}, but the radio currently runs ApexTX "
                    f"for the Noble {current['variant']}. Your backup was saved to {backup}. "
                    "Select the image for your radio; nothing else was written.")
            progress(f'Backup saved: {backup} (verification: {metadata}). '
                     'Installing the complete image...', 23)
            # 3) Write the full image, verify by read-back, then start the radio.
            nb4_rom_flash.run_install(image, progress, serial)
            if expected_serial:
                with lock:
                    session.bootloader_install = None
        except Exception as error:
            with lock:
                status.update(message=str(error), error=True)
                session.radio_unconfirmed = True
        finally:
            with lock:
                status['running'] = False
            session.wake.set()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def authorized(self):
            parsed = urlparse(self.path)
            supplied = self.headers.get('X-ApexTX-Token', '')
            if parsed.path == '/':
                supplied = parse_qs(parsed.query).get('token', [''])[0]
            return (self.headers.get('Host') == f'127.0.0.1:{self.server.server_port}' and
                    secrets.compare_digest(supplied, token))

        def reply(self, code, data, content_type='application/json'):
            if not isinstance(data, bytes):
                data = json.dumps(data).encode()
            self.send_response(code)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Referrer-Policy', 'no-referrer')
            self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if not self.authorized():
                self.reply(403, {'error': 'Invalid session. Reopen ApexTX Flasher.'})
            elif urlparse(self.path).path == '/':
                page = Path(__file__).with_name('apextx-updater.html').read_text(encoding='utf-8')
                page = (page.replace('__SESSION_TOKEN__', token)
                            .replace('__LOGO_DATA__', _logo_data_uri())
                            .replace('__APP_VERSION__', APP_VERSION)
                            .replace('__HOST_PLATFORM__', sys.platform))
                self.reply(200, page.encode(), 'text/html; charset=utf-8')
            elif self.path == '/api/status':
                if startup_health and not health_ready.is_set() and not startup_failed.is_set():
                    try:
                        startup_health()
                    except Exception as error:
                        startup_failed.set()
                        app_updates.state.update(state='error', error=str(error))
                    else:
                        with lock:
                            session.app_updating = False
                        health_ready.set()
                result = session.snapshot()
                result['app_update'] = app_updates.snapshot()
                result['radio_unconfirmed'] = session.radio_unconfirmed
                self.reply(200, result)
            else:
                self.reply(404, {'error': 'Not found.'})

        def do_POST(self):
            if not self.authorized():
                self.reply(403, {'error': 'Invalid session. Reopen ApexTX Flasher.'})
                return
            request_path = urlparse(self.path).path
            if request_path.startswith('/api/app-update/'):
                try:
                    size = int(self.headers.get('Content-Length', '0'))
                    if not 0 <= size <= 2048:
                        raise ValueError('Invalid request size.')
                    self.connection.settimeout(15)
                    data = json.loads(self.rfile.read(size) or b'{}')
                    if not isinstance(data, dict):
                        raise ValueError('Invalid request.')
                    if request_path.endswith('/check'):
                        app_updates.check(data.get('preview'), manual=True)
                    elif request_path.endswith('/install'):
                        if type(data.get('relocate', False)) is not bool:
                            raise ValueError('Invalid installation location.')
                        app_updates.install(data.get('version'), data.get('relocate', False))
                    elif request_path.endswith('/later'):
                        app_updates.defer()
                    elif request_path.endswith('/skip'):
                        app_updates.defer(skip=True)
                    elif request_path.endswith('/acknowledge-radio'):
                        session.acknowledge_radio()
                    else:
                        raise ValueError('Unknown app update action.')
                    self.reply(202, {'ok': True})
                except (OSError, ValueError, RuntimeError) as error:
                    self.reply(409, {'error': str(error)})
                return
            if request_path in ('/api/components/package','/api/components/audio'):
                try:
                    size=int(self.headers.get('Content-Length','0'))
                    if not 1<=size<=(12 if request_path.endswith('/audio') else 128)*1024*1024:
                        raise ValueError('The selected package exceeds its size limit.')
                    self.connection.settimeout(30)
                    data=self.rfile.read(size)
                    if len(data)!=size: raise ValueError('The complete-package upload was interrupted.')
                    serial=self.headers.get('X-ApexTX-Serial')
                    with lock:
                        if not serial or serial!=session.device.get('serial'): raise ValueError('The selected transmitter changed.')
                        if session.bootloader_install: raise ValueError('Finish or cancel the pending bootloader installation first.')
                        session.begin('components')
                        audio=request_path.endswith('/audio')
                        threading.Thread(target=session.component_audio if audio else session.prepare_components,
                            args=(data,self.headers.get('X-ApexTX-Plan') if audio else serial),daemon=False).start()
                    self.reply(202,{'ok':True})
                except (ValueError,OSError,RuntimeError) as error: self.reply(409,{'error':str(error)})
                return
            if request_path in ('/api/refresh', '/api/channel', '/api/upgrade',
                                '/api/bootloader/prepare', '/api/bootloader/install', '/api/bootloader/cancel',
                                '/api/components/read','/api/components/action','/api/components/open',
                                '/api/components/pick','/api/components/download','/api/components/download-audio'):
                try:
                    size = int(self.headers.get('Content-Length', '0'))
                    if not 0 <= size <= 2048:
                        raise ValueError('Invalid request size.')
                    self.connection.settimeout(15)
                    data = json.loads(self.rfile.read(size) or b'{}')
                    if not isinstance(data, dict):
                        raise ValueError('Invalid request.')
                    if request_path=='/api/components/pick':
                        if window is None: raise ValueError('Paste the folder path, or use the desktop app to browse.')
                        import webview
                        with lock: session.begin('components')
                        try:
                            paths=window.create_file_dialog(webview.FileDialog.OPEN if data.get('kind')=='audio' else webview.FileDialog.FOLDER)
                        finally: session.finish()
                        self.reply(200,{'path':paths[0] if paths else None}); return
                    with lock:
                        if request_path in ('/api/components/action','/api/components/open','/api/components/download','/api/components/download-audio'):
                            if session.bootloader_install: raise ValueError('Finish or cancel the pending bootloader installation first.')
                            if request_path=='/api/components/action':
                                action=data.get('action')
                                if action in ('approve','install','recover','restore-nor','restore-application','restore-audio') and data.get('confirmed') is not True:
                                    raise ValueError('Review and confirm the selected component action.')
                                if type(data.get('reinstall',False)) is not bool: raise ValueError('Invalid reinstallation choice.')
                                worker=session.component_action
                                args=(data.get('id'),action,data.get('component'),data.get('path'),data.get('selected'),data.get('reinstall',False))
                            elif request_path=='/api/components/open':
                                worker=session.open_components; args=(data.get('path'),)
                            else:
                                device=dict(session.device); offer=session.offer
                                if (not offer or data.get('release_id')!=offer['id'] or
                                        data.get('serial')!=device.get('serial') or data.get('target')!=device.get('target')):
                                    raise ValueError('The selected radio or release changed.')
                                if request_path.endswith('/download-audio'):
                                    language=data.get('language'); plan=session.integrated
                                    if (language not in offer.get('audio_assets',{}) or not plan or plan.record['id']!=data.get('id') or
                                            plan.package.manifest['release']!=offer['version']):
                                        raise ValueError('Select a voice pack from the same release as this component plan.')
                                    worker=session.download_component_audio; args=(device,dict(offer),data['id'],language)
                                else: worker=session.download_components; args=(device,dict(offer))
                            session.begin('components')
                            threading.Thread(target=worker,args=args,daemon=False).start()
                        elif request_path == '/api/components/read':
                            device = session.inventory_choice(data.get('serial'), data.get('target'))
                            session.begin('inventory')
                            threading.Thread(target=session.read_inventory,
                                             args=(device,), daemon=False).start()
                        elif request_path in ('/api/upgrade', '/api/bootloader/prepare'):
                            preparing = request_path.endswith('/prepare')
                            device, offer = session.choice(data.get('serial'), data.get('target'), data.get('release_id'), bootloader=preparing)
                            session.begin('prepare_bootloader' if preparing else 'upgrade')
                            threading.Thread(target=session.prepare_bootloader if preparing else session.automatic,
                                             args=(device, offer), daemon=False).start()
                        elif request_path == '/api/bootloader/install':
                            args = session.bootloader_choice(data.get('serial'), data.get('target'), data.get('release_id'))
                            session.begin('bootloader')
                            threading.Thread(target=recover, args=args, daemon=False).start()
                        elif request_path == '/api/bootloader/cancel':
                            session.cancel_bootloader()
                        elif request_path == '/api/channel':
                            if not isinstance(data.get('preview'), bool):
                                raise ValueError('Choose a release channel.')
                            session.refresh(data['preview'])
                        else:
                            session.refresh()
                    self.reply(202, {'ok': True})
                except (ValueError, OSError, RuntimeError) as error:
                    self.reply(409, {'error': str(error)})
                return
            if request_path not in ('/api/update', '/api/recover'):
                self.reply(404, {'error': 'Not found.'})
                return
            recovery = request_path == '/api/recover'
            radio_variant = None
            try:
                size = int(self.headers.get('Content-Length', '0'))
                self.connection.settimeout(15)
                if recovery:
                    if size != FLASH_SIZE:
                        raise ValueError('Select an exact 2 MiB ApexTX firmware.bin.')
                    radio_variant = self.headers.get('X-ApexTX-Radio-Variant', '')
                    if radio_variant not in VARIANTS.values():
                        raise ValueError('Select whether your radio is a Noble NB4 or NB4+.')
                    image = self.rfile.read(size)
                    image_variant = nb4_rom_flash.validate_full_image(image)
                    if image_variant != radio_variant:
                        raise ValueError(f'This image is for the Noble {image_variant}, but '
                                         f'you selected Noble {radio_variant}.')
                else:
                    if size != FLASH_SIZE:
                        raise ValueError('Select a 2 MiB firmware.bin image built for ApexTX Update.')
                    image = self.rfile.read(size)
                    update_package(image)
            except (ValueError, OSError, nb4_rom_flash.RomFlashError) as error:
                self.reply(400, {'error': str(error)})
                return
            with lock:
                if status['running'] or session.app_updating:
                    self.reply(409, {'error': 'An operation is already in progress.'})
                    return
                if session.bootloader_install:
                    self.reply(409, {'error': 'Finish or cancel the guided bootloader installation first.'})
                    return
                device = None
                serial = self.headers.get('X-ApexTX-Serial')
                if not recovery and serial:
                    current = session.device
                    if current.get('state') != 'ready' or serial != current.get('serial'):
                        self.reply(409, {'error': 'The radio changed. Check the detected radio and try again.'})
                        return
                    device = dict(current)
                session.begin('recover' if recovery else 'manual')
            # Non-daemon: closing the UI/server must not kill an active write.
            target = recover if recovery else update
            args = (image, radio_variant) if recovery else (image, device)
            threading.Thread(target=target, args=args, daemon=False).start()
            self.reply(202, {'ok': True})

    server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    address = f'http://127.0.0.1:{server.server_port}/?token={token}'
    print(f'ApexTX Flasher: {address}', flush=True)
    print('Local updater running. Press Ctrl+C to close; an active installation will finish before exit.', flush=True)
    import os
    if os.environ.get('APEXTX_UPDATER_WINDOW') == '1':
        # Show the UI in a native application window; serve in the background.
        # Fall back to the default browser where no system WebView is present,
        # so the shared UI remains available if the native renderer is absent.
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        try:
            import webview
            window = webview.create_window('ApexTX Flasher', address,
                                           width=860, height=940, min_size=(560, 680))
            def can_close():
                with lock:
                    return (restarting.is_set() or
                            startup_failed.is_set() and not status['running'] or
                            not (session.app_updating or session.app_restart_blocked()))
            window.events.closing += can_close
            webview.start()
        except Exception:
            webbrowser.open(address)
            try:
                server_thread.join()
            except KeyboardInterrupt:
                pass
        finally:
            session.stop.set()
            app_updates.stopped.set()
            server.shutdown()
            server.server_close()
        return
    if open_browser:
        webbrowser.open(address)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        session.stop.set()
        app_updates.stopped.set()
        server.server_close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('firmware', type=Path, nargs='?', help='Update from the terminal. Omit the file to open the local interface.')
    parser.add_argument('--check', action='store_true', help='Validate the image or detect an update interface without writing firmware.')
    parser.add_argument('--serial')
    parser.add_argument('--inventory', action='store_true',
                        help='Read component versions over the explicitly selected maintenance connection.')
    parser.add_argument('--port', type=int, default=0)
    parser.add_argument('--no-browser', action='store_true')
    parser.add_argument('--self-test', type=Path, metavar='REPORT.json',
                        help='Check the packaged app without accessing a radio and write a report.')
    rf = parser.add_mutually_exclusive_group()
    rf.add_argument('--rf-source', type=Path, help='Inspect a reviewed installation package; never accesses USB.')
    rf.add_argument('--rf-bundle', type=Path, help='Verify a prepared RF catalog ZIP; never accesses USB.')
    rf.add_argument('--backup-storage', type=Path, metavar='VOLUME',
                    help='Back up an explicitly selected mounted ApexTX filesystem; never changes the radio.')
    rf.add_argument('--verify-backup', type=Path, metavar='DIRECTORY',
                    help='Verify a private filesystem backup without accessing a radio.')
    parser.add_argument('--backup-output', type=Path, metavar='NEW_DIRECTORY')
    parser.add_argument('--rf-output', type=Path, help='With --rf-source, create a new local RF ZIP without overwriting files.')
    parser.add_argument('--rf-inventory', type=Path, help='With --rf-bundle, assess a read-only receiver inventory JSON.')
    parser.add_argument('--apply-app-update', type=Path, nargs=2, metavar=('TRANSACTION', 'NONCE'), help=argparse.SUPPRESS)
    parser.add_argument('--app-update-start', type=Path, nargs=2, metavar=('TRANSACTION', 'NONCE'), help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.apply_app_update:
        from flasher_app_installer import run_helper
        run_helper(args.apply_app_update[0], str(args.apply_app_update[1]))
        return
    if args.backup_storage or args.verify_backup:
        if (args.firmware or args.check or args.self_test or args.inventory or
                args.rf_output or args.rf_inventory or
                bool(args.backup_storage) != bool(args.backup_output)):
            parser.error('Filesystem backup cannot be combined with update/recovery; use --backup-output with --backup-storage.')
        from nb4_storage_backup import backup_storage, verify_backup
        try:
            result = (backup_storage(args.backup_storage, args.backup_output)
                      if args.backup_storage else verify_backup(args.verify_backup))
            print(json.dumps(dict(format=result['format'], raw_nor=False,
                                  verified_files=len(result['files']), device_writes=False), indent=2))
        except (OSError, ValueError, RuntimeError) as error:
            parser.exit(1, f'Error: {error}\n')
        return
    if args.backup_output:
        parser.error('--backup-output requires --backup-storage.')
    if ((args.rf_source or args.rf_bundle) and (args.firmware or args.check or args.self_test or args.inventory) or
            args.inventory and (args.firmware or args.check or args.self_test) or
            args.rf_output and not args.rf_source or args.rf_inventory and not args.rf_bundle):
        parser.error('RF preparation cannot be combined with radio update/recovery or self-test.')
    if args.rf_source or args.rf_bundle:
        # Return before USB runtime setup/discovery: package preparation must
        # remain safe even with a radio already connected in Update/ROM DFU.
        from nb4_rf_catalog import (MAX_BUNDLE, assess_receiver, inspect_source_file,
                                    prepare, verify_bundle)
        try:
            if args.rf_source:
                description = (prepare(args.rf_source, args.rf_output) if args.rf_output
                               else inspect_source_file(args.rf_source)[0])
            else:
                with args.rf_bundle.open('rb') as stream:
                    description, _ = verify_bundle(stream.read(MAX_BUNDLE + 1))
            result = dict(catalog=description, status='verified-catalog-only', device_writes=False)
            if args.rf_inventory:
                with args.rf_inventory.open('rb') as stream:
                    raw = stream.read(16385)
                if len(raw) > 16384:
                    raise ValueError('Receiver inventory exceeds the permitted size.')
                result['receiver_assessment'] = assess_receiver(description, json.loads(raw))
            print(json.dumps(result, indent=2))
        except (OSError, ValueError) as error:
            parser.exit(1, f'Error: {error}\n')
        return
    lease = None
    startup_health = None
    if not args.self_test and not args.check:
        lease = InstanceLease(update_nonce=str(args.app_update_start[1]) if args.app_update_start else None)
        if args.app_update_start:
            from flasher_app_installer import startup_handshake
            startup_health = startup_handshake(args.app_update_start[0], str(args.app_update_start[1]), APP_VERSION)
        elif getattr(sys, 'frozen', False):
            from flasher_app_installer import recover_interrupted
            if recover_interrupted():
                lease.close()
                import subprocess
                from flasher_app_installer import clean_environment
                subprocess.Popen([sys.executable], env=clean_environment())
                return
    _enable_bundled_libusb()
    try:
        if args.inventory:
            from nb4_usb_update import probe
            from nb4_maintenance import TARGETS, read_inventory
            device = probe()
            if (device.get('state') != 'normal' or device.get('maintenance') is not True or device.get('target') not in TARGETS or
                    args.serial and args.serial != device.get('serial')):
                raise RuntimeError('Connect one identified radio from System > Update > Connect to ApexTX Flasher.')
            inventory = read_inventory(device['serial'], TARGETS[device['target']], verified=True)
            print(json.dumps(inventory, indent=2))
        elif args.self_test:
            args.self_test.write_text(json.dumps(package_self_test(), indent=2) + '\n', encoding='utf-8')
        elif args.firmware:
            image = args.firmware.read_bytes()
            if args.check:
                package = update_package(image)
                variant = VARIANTS[package_target(package)]
                print(f'Valid {variant} firmware image: {len(package) - 32} application bytes.')
            else:
                last = [None]
                def progress(message, percent):
                    value = (message, percent)
                    if value != last[0]:
                        print(f'{percent:3d}% {message}', flush=True)
                        last[0] = value
                run_update(image, progress, args.serial)
        elif args.check:
            selection = discover(args.serial)
            print(f'ApexTX {VARIANTS[selection.target]} Update detected.' if selection
                  else 'No NB4 in ApexTX Update mode was detected.')
        else:
            # A packaged build opens the UI in a native window by default.
            if getattr(sys, 'frozen', False):
                os.environ.setdefault('APEXTX_UPDATER_WINDOW', '1')
            serve(args.port, not args.no_browser, startup_health)
    except Exception as error:
        parser.exit(1, f'Error: {error}\n')
    finally:
        if lease:
            lease.close()


if __name__ == '__main__':
    main()
