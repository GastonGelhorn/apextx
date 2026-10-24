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

APP_VERSION = '1.1'


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
    return dict(metadata, usb_backend=True, dfu_util=True, shared_ui=True, release_client=True)


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


def serve(port=0, open_browser=True):
    token = secrets.token_urlsafe(32)
    session = FlasherSession()
    lock, status = session.lock, session.status
    monitor = threading.Thread(target=session.monitor, daemon=True)
    monitor.start()

    def progress(message, percent):
        with lock:
            status.update(message=message, progress=percent)

    def update(image, device=None):
        try:
            with session.usb_lock:
                if device:
                    run_update(image, progress, serial=device['serial'], wait_seconds=0,
                               finder=session.pinned_finder(device))
                else:
                    run_update(image, progress)
        except Exception as error:
            with lock:
                status.update(message=str(error), error=True)
        finally:
            with lock:
                status['running'] = False

    def recover(image, radio_variant):
        with session.usb_lock:
            recover_locked(image, radio_variant)

    def recover_locked(image, radio_variant):
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
        except Exception as error:
            with lock:
                status.update(message=str(error), error=True)
        finally:
            with lock:
                status['running'] = False

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
                self.reply(200, session.snapshot())
            else:
                self.reply(404, {'error': 'Not found.'})

        def do_POST(self):
            if not self.authorized():
                self.reply(403, {'error': 'Invalid session. Reopen ApexTX Flasher.'})
                return
            request_path = urlparse(self.path).path
            if request_path in ('/api/refresh', '/api/channel', '/api/upgrade'):
                try:
                    size = int(self.headers.get('Content-Length', '0'))
                    if not 0 <= size <= 2048:
                        raise ValueError('Invalid request size.')
                    self.connection.settimeout(15)
                    data = json.loads(self.rfile.read(size) or b'{}')
                    if not isinstance(data, dict):
                        raise ValueError('Invalid request.')
                    with lock:
                        if request_path == '/api/upgrade':
                            device, offer = session.choice(data.get('serial'), data.get('target'), data.get('release_id'))
                            session.begin('upgrade')
                            threading.Thread(target=session.automatic, args=(device, offer), daemon=False).start()
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
                if status['running']:
                    self.reply(409, {'error': 'An operation is already in progress.'})
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
                    return not status['running']
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
        server.server_close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('firmware', type=Path, nargs='?', help='Update from the terminal. Omit the file to open the local interface.')
    parser.add_argument('--check', action='store_true', help='Validate the image or detect an update interface without writing firmware.')
    parser.add_argument('--serial')
    parser.add_argument('--port', type=int, default=0)
    parser.add_argument('--no-browser', action='store_true')
    parser.add_argument('--self-test', type=Path, metavar='REPORT.json',
                        help='Check the packaged app without accessing a radio and write a report.')
    args = parser.parse_args()
    _enable_bundled_libusb()
    try:
        if args.self_test:
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
            serve(args.port, not args.no_browser)
    except Exception as error:
        parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
    main()
