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
import subprocess
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path, PurePosixPath
from urllib.parse import urlparse, parse_qs

import nb4_editor_files as radio_files
import nb4_preview
from nb4_editor_transaction import commit, recover
from nb4_usb_update import probe

APP_VERSION = '1.0'
MAX_THEME_BYTES = 4 * 1024 * 1024
# Only the files a theme directory may hold (see sdcard/THEMES/README.md).
THEME_FILE = re.compile(r'^THEMES/[A-Za-z0-9_-]{1,26}/(theme\.yml|background\.png|background_480x320\.png|logo\.png)$')
THEME_FOLDER = re.compile(r'^[A-Za-z0-9_-]{1,26}$')


def _resource(name):
    """Locate a bundled file both from the source tree and a frozen build."""
    base = getattr(sys, '_MEIPASS', None)
    return (Path(base) if base else Path(__file__).resolve().parent) / name


@functools.lru_cache(maxsize=1)
def build_revision():
    metadata = _resource('apextx-flasher-build.json')
    if metadata.is_file(): return json.loads(metadata.read_bytes())['revision'][:8]
    try: return subprocess.check_output(['git','rev-parse','--short=8','HEAD'],cwd=Path(__file__).resolve().parent.parent,text=True).strip()
    except (OSError,subprocess.CalledProcessError): return 'unknown'

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
        roots = [p for base in ('/Volumes', '/run/media', f'/run/media/{os.environ.get("USER", "")}', '/media', f'/media/{os.environ.get("USER", "")}')
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


def writable_identity(target):
    root = _checked_target(target)
    descriptor = radio_files.safe_path(root / 'APEXTX-EDITOR.json')
    if not descriptor.is_file() or descriptor.stat().st_size > 262144:
        raise ValueError('Update the radio firmware before saving. Offline design and export remain available.')
    value = json.loads(descriptor.read_bytes())
    device = probe()
    if (value.get('format') != 'apextx-editor-1' or value.get('protocol') != 1 or
            not value.get('serial') or device.get('mode') != 'storage' or
            value['serial'] != device.get('serial') or value.get('firmware') != device.get('revision') or
            {'nb4':1,'nb4plus':2}.get(value.get('target')) != device.get('target')):
        raise ValueError('The storage volume does not match the connected compatible radio.')
    return {'serial':value['serial'], 'target':value['target']}


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
    return {'yml': yml.read_text(encoding='utf-8', errors='replace'), 'background': data, 'hashes': {f'THEMES/{folder}/{n}': radio_files.digest(radio_files.safe_path(base/n).read_bytes()) if (base/n).exists() else None for n in ('theme.yml','background.png','background_480x320.png','logo.png')}}


def validate_theme_file(name, data):
    if name.endswith('theme.yml'):
        if len(data)>16384:raise ValueError('theme.yml is too large.')
        values=radio_files.parse_yaml(data.decode('utf-8').splitlines())
        title=radio_files.unquote(values.get('summary',{}).get('name',''))
        if not isinstance(title,str) or not title or len(title.encode('utf-8'))>26:
            raise ValueError('Theme names must fit 26 UTF-8 bytes.')
        opacity=values.get('nb4',{}).get('CARD_OPACITY','100')
        if not 30<=int(opacity)<=100:raise ValueError('Card opacity must be 30–100.')
    else:
        import io,struct
        from PIL import Image
        if len(data)<24 or data[:8]!=b'\x89PNG\r\n\x1a\n':raise ValueError('Theme images must be PNG files.')
        width,height=struct.unpack('>II',data[16:24])
        dimensions=(480,320) if name.endswith('background_480x320.png') else (320,480)
        if name.endswith('logo.png'):
            if not 0<width<=480 or not 0<height<=480:raise ValueError('Invalid theme thumbnail size.')
        elif (width,height)!=dimensions:raise ValueError('Background image orientation or dimensions do not match.')
        with Image.open(io.BytesIO(data)) as image:image.verify()


def save_theme(target, files, hashes=None):
    root = _checked_target(target)
    identity = writable_identity(target) if (root / 'RADIO').exists() else {'local':str(root.resolve())}
    if not files or len(files)>4: raise ValueError('Invalid theme file set.')
    changes = {}; folders=set(); total=0
    for entry in files:
        name=entry.get('name','')
        if not THEME_FILE.fullmatch(name) or name in changes: raise ValueError('Invalid theme file name.')
        data=base64.b64decode(entry.get('data',''),validate=True);total+=len(data)
        if total>MAX_THEME_BYTES: raise ValueError('The theme is too large.')
        validate_theme_file(name,data)
        changes[name]=data;folders.add(str(PurePosixPath(name).parent))
    if len(folders)!=1: raise ValueError('Save one theme at a time.')
    folder=folders.pop()
    if folder+'/theme.yml' not in changes: raise ValueError('A theme needs theme.yml.')
    for name in ('background.png','background_480x320.png','logo.png'):
        changes.setdefault(folder+'/'+name,None)
    expected={name:(hashes or {}).get(name) for name in changes}
    root.mkdir(parents=True,exist_ok=True)
    result=commit(root,changes,expected,BACKUP_ROOT,identity,check=(lambda:writable_identity(target)) if identity.get('serial') else None)
    return dict(ok=True,folder=str(root/folder),hashes=result['hashes'],backup=result['backup'])


def validate_model_capabilities(root, model):
    caps = json.loads((root/'APEXTX-EDITOR.json').read_bytes())
    previous = radio_files.read_model(root,model['file'])['screenData']
    for index, screen in model['screenData'].items():
        old = previous.get(index,{})
        if screen == old: continue
        if radio_files.unquote(screen.get('LayoutId','')) not in caps.get('layouts',[]):
            raise ValueError('This layout is not supported by the connected firmware.')
        old_zones=old.get('layoutData',{}).get('zones',{})
        for zone, data in screen.get('layoutData',{}).get('zones',{}).items():
            if data == old_zones.get(zone):continue
            widget=radio_files.unquote(data.get('widgetName',''))
            if widget not in caps.get('widgets',[]):raise ValueError('This widget is not supported by the connected firmware.')
            definitions=next((w.get('options',[]) for w in caps.get('catalog',[]) if w['id']==widget),[])
            old_options=old_zones.get(zone,{}).get('widgetData',{}).get('options',{})
            for option_index,opt in data.get('widgetData',{}).get('options',{}).items():
                if widget==radio_files.unquote(old_zones.get(zone,{}).get('widgetName','')) and opt == old_options.get(option_index): continue
                if not option_index.isdigit() or int(option_index)>=len(definitions):raise ValueError('Unsupported widget option.')
                definition=definitions[int(option_index)]
                kind=definition['type'];value=opt.get('value',{})
                expected_type={'Integer':'Signed','Bool':'Bool','String':'String','File':'String','Source':'Source','Color':'Color'}.get(kind,'Unsigned')
                if opt.get('type')!=expected_type:raise ValueError('Invalid widget option type.')
                if kind in ('Integer','Bool','TextSize','Timer','Align','Slider','Choice','Switch'):
                    number=int(next(iter(value.values())))
                    low,high=definition.get('min',0),definition.get('max',0)
                    if kind=='Bool':low,high=0,1
                    if kind=='TextSize':low,high=0,7
                    if kind in ('Timer','Align'):low,high=0,2
                    if kind=='Switch' and number>=32768:number=(number&65535)-65536
                    if not low<=number<=high:raise ValueError('Widget option is out of range.')
                if kind=='Color' and not re.fullmatch(r'(0x[0-9a-fA-F]{6}|COLIDX[0-9]{1,3})',value.get('color','')):raise ValueError('Invalid widget colour.')
                if opt.get('type')=='Source' and value.get('source') not in caps.get('sources',[]):raise ValueError('Unsupported source.')
                if opt.get('type')=='String' and '..' in Path(radio_files.unquote(value.get('stringValue',''))).parts:raise ValueError('Parent paths are not allowed in file options.')


def save_changes(request):
    target = request.get('target','')
    root = _radio_root(target)
    identity = writable_identity(target)
    changes, expected = {}, {}
    files = request.get('files')
    if files:
        if len(files)>4: raise ValueError('Invalid theme file set.')
        total=0; folders=set()
        for entry in files:
            name=entry.get('name','')
            if not THEME_FILE.fullmatch(name) or name in changes: raise ValueError('Invalid theme file.')
            data=base64.b64decode(entry.get('data',''),validate=True);total+=len(data)
            if total>MAX_THEME_BYTES: raise ValueError('The theme is too large.')
            validate_theme_file(name,data)
            changes[name]=data;folders.add(str(PurePosixPath(name).parent))
        if len(folders)!=1: raise ValueError('One theme per save.')
        folder=folders.pop()
        if folder+'/theme.yml' not in changes: raise ValueError('Missing theme.yml.')
        for name in ('background.png','background_480x320.png','logo.png'):changes.setdefault(folder+'/'+name,None)
        expected.update({n:request.get('hashes',{}).get(n) for n in changes})
    model=request.get('model')
    if model:
        validate_model_capabilities(root,model)
        path=radio_files._model_path(root,model['file']);name='MODELS/'+model['file']
        changes[name]=radio_files.model_with_screens(path.read_bytes(),model['screenData'])
        expected[name]=model['hash']
    settings=request.get('settings')
    if settings:
        if any(k not in radio_files.RADIO_KEYS for k in settings):raise ValueError('Setting not editable.')
        changes['RADIO/radio.yml']=radio_files.radio_with_settings(radio_files.safe_path(root/'RADIO/radio.yml').read_bytes(),settings)
        expected['RADIO/radio.yml']=request.get('radioHash')
    if not changes:raise ValueError('No changes to save.')
    result=commit(root,changes,expected,BACKUP_ROOT,identity,check=(lambda:writable_identity(target)) if identity.get('serial') else None)
    return dict(ok=True,**result)


def create_handler(token, preview, onclose=None, draft_changed=None):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def authorized(self):
            parsed = urlparse(self.path)
            supplied = self.headers.get('X-ApexTX-Token', '')
            if parsed.path in ('/', '/editor'):
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
            self.path = self.path.replace('/api/editor/', '/api/', 1)
            if not self.authorized():
                self.reply(403, {'error': 'Invalid session. Reopen the theme editor.'})
            elif urlparse(self.path).path in ('/', '/editor'):
                page = _resource('apextx-theme-editor.html').read_text(encoding='utf-8')
                page = (page.replace('__SESSION_TOKEN__', token)
                            .replace('__LOGO_DATA__', _logo_data_uri())
                            .replace('__APP_VERSION__', APP_VERSION).replace('__EDITOR_REVISION__',build_revision())
                            .replace('/*__CATALOG__*/null', _catalog_json()).replace('/api/', '/api/editor/'))
                self.reply(200, page.encode(), 'text/html; charset=utf-8')
            elif self.path == '/api/targets':
                self.reply(200, theme_targets())
            elif self.path == '/api/preview/status':
                self.reply(200, {'available': preview is not None})
            elif urlparse(self.path).path == '/api/preview/frame':
                after = int(parse_qs(urlparse(self.path).query).get('after', ['0'])[0] or 0)
                if preview and preview.error:
                    self.reply(503,{'error':preview.error})
                    return
                frame = preview.latest(after) if preview else None
                if not frame:
                    self.send_response(204)
                    self.send_header('Cache-Control', 'no-store')
                    self.end_headers()
                    return
                seq, width, height, data, zones, shown = frame
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Length', str(len(data)))
                self.send_header('Cache-Control', 'no-store')
                self.send_header('X-Frame', f'{seq} {width} {height} {shown}')
                self.send_header('X-Zones', ';'.join(','.join(map(str, z)) for z in zones))
                self.end_headers()
                self.wfile.write(data)
            elif urlparse(self.path).path == '/api/perf':
                try:
                    query=parse_qs(urlparse(self.path).query)
                    path=radio_files.safe_path(_radio_root(query.get('target',[''])[0])/'APEXTX-UI-PERF.json')
                    if path.stat().st_size>16384:raise ValueError('Invalid performance capture.')
                    self.reply(200,json.loads(path.read_bytes()))
                except (OSError,ValueError) as error:self.reply(400,{'error':str(error)})
            elif urlparse(self.path).path in ('/api/models', '/api/model', '/api/radio'):
                query = parse_qs(urlparse(self.path).query)
                route = urlparse(self.path).path
                try:
                    root = _radio_root(query.get('target', [''])[0])
                    if route == '/api/models':
                        self.reply(200, radio_files.list_models(root))
                    elif route == '/api/model':
                        result = radio_files.read_model(root, query.get('file', [''])[0])
                        marker = root / 'APEXTX-EDITOR.json'
                        if marker.is_file() and marker.stat().st_size <= 262144:
                            result['capabilities'] = json.loads(marker.read_bytes())
                        self.reply(200, result)
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
            self.path = self.path.replace('/api/editor/', '/api/', 1)
            if not self.authorized():
                self.reply(403, {'error': 'Invalid session. Reopen the theme editor.'})
                return
            route = urlparse(self.path).path
            if route not in ('/api/save', '/api/model', '/api/radio', '/api/preview', '/api/preview/input', '/api/recover', '/api/commit', '/api/close', '/api/target', '/api/draft'):
                self.reply(404, {'error': 'Not found.'})
                return
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if not 0 < size <= 2 * MAX_THEME_BYTES:
                    raise ValueError('The theme is too large.')
                self.connection.settimeout(15)
                request = json.loads(self.rfile.read(size))
                if not isinstance(request,dict): raise ValueError('Invalid editor request.')
                if route in ('/api/model','/api/radio'):
                    writable_identity(request.get('target',''))
                if route == '/api/draft':
                    if type(request.get('dirty')) is not bool: raise ValueError('Invalid draft state.')
                    if draft_changed: draft_changed(request['dirty'])
                    result = {'ok':True}
                elif route == '/api/target':
                    root = radio_files.safe_path(Path(request.get('path',''))).resolve()
                    if not root.is_dir() or not (root/'RADIO/radio.yml').is_file(): raise ValueError('Select the radio storage folder containing RADIO/radio.yml.')
                    if root not in EXTRA_ROOTS: EXTRA_ROOTS.append(root)
                    result = {'path':str(root)}
                elif route == '/api/close':
                    self.reply(200,{'ok':True})
                    if onclose: threading.Thread(target=onclose,daemon=True).start()
                    return
                elif route == '/api/commit':
                    result = save_changes(request)
                elif route == '/api/recover':
                    root = _radio_root(request.get('target',''))
                    identity = writable_identity(str(root))
                    folder = Path(request.get('backup','')).resolve()
                    if BACKUP_ROOT.resolve() not in folder.parents: raise ValueError('Unknown recovery backup.')
                    recover(root, folder, identity, request.get('restore') is True)
                    result = {'ok':True}
                elif route == '/api/preview/input':
                    if preview:
                        preview.set_inputs(request)
                    result = {'ok': True}
                elif route == '/api/preview':
                    if not preview:
                        raise ValueError('The firmware preview is not part of this build.')
                    target = request.get('target') or ''
                    root = _radio_root(target) if target else None
                    result = preview.restart(request, root)
                elif route == '/api/save':
                    result = save_theme(request.get('target', ''), request.get('files', []), request.get('hashes'))
                elif route == '/api/model':
                    validate_model_capabilities(_radio_root(request.get('target','')),request)
                    result = radio_files.write_model(_radio_root(request.get('target', '')), request.get('file', ''),
                                                     request.get('hash', ''), request.get('screenData'), BACKUP_ROOT)
                else:
                    result = radio_files.write_radio(_radio_root(request.get('target', '')), request.get('hash', ''),
                                                     request.get('settings', {}), BACKUP_ROOT)
            except (ValueError, OSError, json.JSONDecodeError, radio_files.EditorFileError) as error:
                self.reply(400, {'error': str(error)})
                return
            self.reply(200, result)

    return Handler


def serve(port=0, open_browser=True):
    token = secrets.token_urlsafe(32)
    preview = nb4_preview.FirmwarePreview() if nb4_preview.FirmwarePreview.available() else None

    Handler = create_handler(token, preview)

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
            if preview:
                preview.close()
        return
    if open_browser:
        webbrowser.open(address)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if preview:
            preview.close()


def main():
    if len(sys.argv) == 4 and sys.argv[1] == '--preview-worker':
        nb4_preview.worker_main(sys.argv[2], int(sys.argv[3]))
        return
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
