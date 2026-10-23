#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""ApexTX Theme Editor: design NB4 themes locally with a live preview."""
import argparse
import base64
import functools
import json
import os
import re
import secrets
import string
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path, PurePosixPath
from urllib.parse import urlparse, parse_qs

import nb4_editor_files as radio_files

APP_VERSION = '1.0'
MAX_THEME_BYTES = 4 * 1024 * 1024
# Only the files a theme directory may hold (see sdcard/THEMES/README.md).
THEME_FILE = re.compile(r'^THEMES/[A-Za-z0-9_-]{1,26}/(theme\.yml|background\.png|logo\.png)$')
THEME_FOLDER = re.compile(r'^[A-Za-z0-9_-]{1,26}$')


def _resource(name):
    """Locate a bundled file both from the source tree and a frozen build."""
    base = getattr(sys, '_MEIPASS', None)
    return (Path(base) if base else Path(__file__).resolve().parent) / name


BACKUP_ROOT = Path.home() / 'ApexTX-Backups' / 'theme-editor'
# Extra radio roots given with --radio (unusual mount points, development).
EXTRA_ROOTS = []


@functools.lru_cache(maxsize=1)
def _catalog_json():
    """Layouts and widgets of the firmware (tools/nb4-editor-catalog.py)."""
    try:
        return _resource('apextx-theme-editor-catalog.json').read_text(encoding='utf-8').strip()
    except OSError:
        return 'null'


@functools.lru_cache(maxsize=1)
def _logo_data_uri():
    """Inline the ApexTX brand logo so the page stays a single response."""
    try:
        raw = _resource('apextx-updater-logo.png').read_bytes()
        return 'data:image/png;base64,' + base64.b64encode(raw).decode('ascii')
    except Exception:
        return ''


def theme_targets():
    """Radio storage (any drive with a THEMES folder) plus a local folder."""
    targets = []
    if os.name == 'nt':
        roots = [Path(f'{letter}:/') for letter in string.ascii_uppercase]
    else:
        roots = [p for base in ('/Volumes', '/media', f'/media/{os.environ.get("USER", "")}')
                 if Path(base).is_dir() for p in Path(base).iterdir()]
    roots += EXTRA_ROOTS
    for root in roots:
        try:
            # The radio's storage holds THEMES and, once set up, RADIO/radio.yml.
            if (root / 'THEMES').is_dir() or (root / 'RADIO' / 'radio.yml').is_file():
                targets.append({'path': str(root), 'radio': True,
                                'settings': (root / 'RADIO' / 'radio.yml').is_file()})
        except OSError:
            continue
    targets.append({'path': str(Path.home() / 'ApexTX-Themes'), 'radio': False, 'settings': False})
    return targets


def _checked_target(target):
    if target not in {t['path'] for t in theme_targets()}:
        raise ValueError('Unknown destination. Reconnect the radio and try again.')
    return Path(target)


def _radio_root(target):
    """Only a connected radio (with RADIO/radio.yml) has cars and settings."""
    root = _checked_target(target)
    if not (root / 'RADIO' / 'radio.yml').is_file():
        raise ValueError('No transmitter selected. Connect it over USB storage.')
    return root


def list_themes(target):
    """Theme folders on the radio (or local folder) with their display names."""
    themes = []
    root = _checked_target(target) / 'THEMES'
    if not root.is_dir():
        return themes
    for folder in sorted(root.iterdir()):
        yml = folder / 'theme.yml'
        if not (folder.is_dir() and yml.is_file() and THEME_FOLDER.match(folder.name)):
            continue
        name = folder.name
        try:
            match = re.search(r'^\s+name:\s*"?([^"\r\n]+)"?', yml.read_text(encoding='utf-8', errors='replace'), re.M)
            if match:
                name = match.group(1).strip()
        except OSError:
            pass
        themes.append({'folder': folder.name, 'name': name})
    return themes


def read_theme(target, folder):
    if not THEME_FOLDER.match(folder):
        raise ValueError('Invalid theme folder.')
    base = _checked_target(target) / 'THEMES' / folder
    yml = base / 'theme.yml'
    if yml.stat().st_size > 16384:
        raise ValueError('theme.yml is too large.')
    background = base / 'background.png'
    data = None
    if background.is_file() and background.stat().st_size <= MAX_THEME_BYTES:
        data = base64.b64encode(background.read_bytes()).decode('ascii')
    return {'yml': yml.read_text(encoding='utf-8', errors='replace'), 'background': data}


def save_theme(target, files):
    """Write the theme files below target; returns the theme directory."""
    _checked_target(target)
    if not files or len(files) > 3:
        raise ValueError('A theme consists of theme.yml, logo.png and an optional background.png.')
    total, written = 0, None
    for entry in files:
        name = entry.get('name', '')
        if not THEME_FILE.match(name):
            raise ValueError(f'Refusing unexpected file name: {name}')
        data = base64.b64decode(entry.get('data', ''), validate=True)
        total += len(data)
        if total > MAX_THEME_BYTES:
            raise ValueError('The theme is too large.')
        path = Path(target).joinpath(*PurePosixPath(name).parts)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        written = path.parent
    return str(written)


def serve(port=0, open_browser=True):
    token = secrets.token_urlsafe(32)

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
            self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' data: blob:; connect-src 'self'; frame-ancestors 'none'")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if not self.authorized():
                self.reply(403, {'error': 'Invalid session. Reopen the theme editor.'})
            elif urlparse(self.path).path == '/':
                page = _resource('apextx-theme-editor.html').read_text(encoding='utf-8')
                page = (page.replace('__SESSION_TOKEN__', token)
                            .replace('__LOGO_DATA__', _logo_data_uri())
                            .replace('__APP_VERSION__', APP_VERSION)
                            .replace('/*__CATALOG__*/null', _catalog_json()))
                self.reply(200, page.encode(), 'text/html; charset=utf-8')
            elif self.path == '/api/targets':
                self.reply(200, theme_targets())
            elif urlparse(self.path).path in ('/api/models', '/api/model', '/api/radio'):
                query = parse_qs(urlparse(self.path).query)
                route = urlparse(self.path).path
                try:
                    root = _radio_root(query.get('target', [''])[0])
                    if route == '/api/models':
                        self.reply(200, radio_files.list_models(root))
                    elif route == '/api/model':
                        self.reply(200, radio_files.read_model(root, query.get('file', [''])[0]))
                    else:
                        self.reply(200, radio_files.read_radio(root))
                except (ValueError, OSError) as error:
                    self.reply(400, {'error': str(error)})
            elif urlparse(self.path).path in ('/api/themes', '/api/theme'):
                query = parse_qs(urlparse(self.path).query)
                target = query.get('target', [''])[0]
                try:
                    if urlparse(self.path).path == '/api/themes':
                        self.reply(200, list_themes(target))
                    else:
                        self.reply(200, read_theme(target, query.get('folder', [''])[0]))
                except (ValueError, OSError) as error:
                    self.reply(400, {'error': str(error)})
            else:
                self.reply(404, {'error': 'Not found.'})

        def do_POST(self):
            if not self.authorized():
                self.reply(403, {'error': 'Invalid session. Reopen the theme editor.'})
                return
            route = urlparse(self.path).path
            if route not in ('/api/save', '/api/model', '/api/radio'):
                self.reply(404, {'error': 'Not found.'})
                return
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if size > 2 * MAX_THEME_BYTES:
                    raise ValueError('The theme is too large.')
                self.connection.settimeout(15)
                request = json.loads(self.rfile.read(size))
                if route == '/api/save':
                    result = {'ok': True, 'folder': save_theme(request.get('target', ''), request.get('files', []))}
                elif route == '/api/model':
                    result = radio_files.write_model(_radio_root(request.get('target', '')), request.get('file', ''),
                                                     request.get('hash', ''), request.get('screenData'), BACKUP_ROOT)
                else:
                    result = radio_files.write_radio(_radio_root(request.get('target', '')), request.get('hash', ''),
                                                     request.get('settings', {}), BACKUP_ROOT)
            except (ValueError, OSError, json.JSONDecodeError) as error:
                self.reply(400, {'error': str(error)})
                return
            self.reply(200, result)

    server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    address = f'http://127.0.0.1:{server.server_port}/?token={token}'
    print(f'ApexTX Theme Editor: {address}', flush=True)
    print('Local theme editor running. Press Ctrl+C to close.', flush=True)
    if os.environ.get('APEXTX_THEME_EDITOR_WINDOW') == '1':
        # Show the UI in a native application window; serve in the background.
        # Fall back to the default browser where no system WebView is present.
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            import webview
            webview.create_window('ApexTX Theme Editor', address,
                                  width=1280, height=960, min_size=(720, 680))
            webview.start()
        except Exception:
            webbrowser.open(address)
            try:
                server.serve_forever()
            except KeyboardInterrupt:
                pass
        finally:
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
        server.server_close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=0)
    parser.add_argument('--no-browser', action='store_true')
    parser.add_argument('--radio', type=Path, action='append', default=[],
                        help='Folder of a connected radio that is not found automatically.')
    args = parser.parse_args()
    EXTRA_ROOTS.extend(p.resolve() for p in args.radio)
    if getattr(sys, 'frozen', False):
        os.environ.setdefault('APEXTX_THEME_EDITOR_WINDOW', '1')
    serve(args.port, not args.no_browser)


if __name__ == '__main__':
    main()
