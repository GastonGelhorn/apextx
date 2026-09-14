#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""ApexTX Updater: local browser interface or command-line NB4 update."""
import argparse
import json
import secrets
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

from nb4_update_image import FLASH_SIZE, update_package
from nb4_usb_update import run_update, discover


def serve(port=0, open_browser=True):
    token = secrets.token_urlsafe(32)
    lock = threading.Lock()
    status = {'running': False, 'progress': 0, 'message': 'Ready to update', 'error': False}

    def progress(message, percent):
        with lock:
            status.update(message=message, progress=percent)

    def update(image):
        try:
            run_update(image, progress)
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
            self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if not self.authorized():
                self.reply(403, {'error': 'Invalid session. Reopen the updater from the terminal.'})
            elif urlparse(self.path).path == '/':
                page = Path(__file__).with_name('apextx-updater.html').read_text(encoding='utf-8')
                self.reply(200, page.replace('__SESSION_TOKEN__', token).encode(), 'text/html; charset=utf-8')
            elif self.path == '/api/status':
                with lock:
                    snapshot = dict(status)
                self.reply(200, snapshot)
            else:
                self.reply(404, {'error': 'Not found.'})

        def do_POST(self):
            if not self.authorized():
                self.reply(403, {'error': 'Invalid session. Reopen the updater from the terminal.'})
                return
            if self.path != '/api/update':
                self.reply(404, {'error': 'Not found.'})
                return
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if size != FLASH_SIZE:
                    raise ValueError('Select a 2 MiB firmware.bin image built for ApexTX Update.')
                self.connection.settimeout(15)
                image = self.rfile.read(size)
                update_package(image)
            except (ValueError, OSError) as error:
                self.reply(400, {'error': str(error)})
                return
            with lock:
                if status['running']:
                    self.reply(409, {'error': 'An update is already in progress.'})
                    return
                status.update(running=True, error=False, progress=0, message='Waiting for the radio...')
            # Non-daemon: closing the UI/server must not kill an active write.
            threading.Thread(target=update, args=(image,), daemon=False).start()
            self.reply(202, {'ok': True})

    server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    address = f'http://127.0.0.1:{server.server_port}/?token={token}'
    print(f'ApexTX Updater: {address}', flush=True)
    print('Local updater running. Press Ctrl+C to close; an active installation will finish before exit.', flush=True)
    if open_browser:
        webbrowser.open(address)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('firmware', type=Path, nargs='?', help='Update from the terminal. Omit the file to open the local interface.')
    parser.add_argument('--check', action='store_true', help='Validate the image or detect an update interface without writing firmware.')
    parser.add_argument('--serial')
    parser.add_argument('--port', type=int, default=0)
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args()
    try:
        if args.firmware:
            image = args.firmware.read_bytes()
            if args.check:
                package = update_package(image)
                print(f'Valid NB4 firmware image: {len(package) - 32} application bytes.')
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
            print('ApexTX NB4 Update detected.' if selection else 'No NB4 in ApexTX Update mode was detected.')
        else:
            serve(args.port, not args.no_browser)
    except Exception as error:
        parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
    main()
