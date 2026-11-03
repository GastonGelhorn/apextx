# SPDX-License-Identifier: GPL-2.0-only
"""Firmware preview for the ApexTX Theme Editor.

The editor's preview is drawn by the firmware itself: nb4-preview.dll is the
NB4 firmware built for the PC (radio/src/targets/simu, target nb4-preview).
It runs in a worker process on a private copy of the transmitter's storage,
so every change restarts it from a clean state and a crash cannot take the
editor down. Frames come back over an authenticated local connection.
"""
import base64
import ctypes
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
import secrets
import socket
from multiprocessing.connection import Listener, Client
from pathlib import Path, PurePosixPath

import nb4_editor_files as radio_files

DLL_NAME = 'nb4-preview.dll' if os.name == 'nt' else 'nb4-preview.dylib' if sys.platform == 'darwin' else 'nb4-preview.so'
FRAME_MAGIC = b'NB4F'
# Magic, width, height, pixel bytes, zone count, screen shown; then the
# RGB565 pixels and x, y, w, h (int16) per zone of that screen.
FRAME_HEADER = struct.Struct('<4sHHIHh')
MAX_ZONES = 16
MAX_PIXELS = 480 * 480
THEME_FILE = re.compile(r'^THEMES/[A-Za-z0-9_-]{1,26}/(theme\.yml|background\.png|background_480x320\.png|logo\.png)$')
# Raw ADC inputs of the NB4: steering, throttle, then the two flex inputs.
ANALOG_INPUTS = {'steer': (0, -1), 'thr': (1, 1), 'ch3': (2, 1), 'ch4': (3, 1)}


def _resource(name):
    base = getattr(sys, '_MEIPASS', None)
    return (Path(base) if base else Path(__file__).resolve().parent) / name


def dll_path(target="nb4"):
    name = DLL_NAME if target == "nb4" else DLL_NAME.replace("nb4-preview", "nb4plus-preview")
    override = os.environ.get('APEXTX_PREVIEW_DLL')
    path = Path(override) if override else _resource(name)
    if path.is_file(): return path
    if not getattr(sys,'frozen',False) and not override:
        build=Path(__file__).resolve().parent.parent/'build'/('nb4-customization' if target=='nb4' else 'nb4plus-preview')/'native'/DLL_NAME
        if build.is_file(): return build
    return None


# ---------------------------------------------------------------------------
# Worker process: loads the firmware and streams its frames.
# ---------------------------------------------------------------------------
def worker_main(storage, view=0):
    connection = Client(('127.0.0.1', int(os.environ['APEXTX_PREVIEW_PORT'])), authkey=bytes.fromhex(os.environ['APEXTX_PREVIEW_KEY']))
    path = dll_path()
    os.add_dll_directory(str(path.parent)) if hasattr(os, 'add_dll_directory') else None
    lib = ctypes.CDLL(str(path))
    lib.nb4p_catalog.restype = ctypes.c_char_p
    lib.nb4p_start.argtypes = [ctypes.c_char_p]
    lib.nb4p_frame.argtypes = [ctypes.POINTER(ctypes.c_uint16), ctypes.c_int,
                               ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]

    lock = threading.Lock()

    def commands():
        while True:
            try:
                cmd = json.loads(connection.recv_bytes(65536))
            except (EOFError, OSError):
                os._exit(0)
            with lock:
                if cmd.get('analog'):
                    for index, value in cmd['analog']:
                        lib.nb4p_set_analog(int(index), int(value))
                elif 'touch' in cmd:
                    down, x, y = cmd['touch']
                    lib.nb4p_touch(int(down), int(x), int(y))
        os._exit(0)  # editor gone

    threading.Thread(target=commands, daemon=True).start()
    if not lib.nb4p_start(storage.encode('utf-8')):
        os._exit(2)
    lib.nb4p_zones.argtypes = [ctypes.POINTER(ctypes.c_int16), ctypes.c_int]
    buf = (ctypes.c_uint16 * MAX_PIXELS)()
    zones = (ctypes.c_int16 * (MAX_ZONES * 4))()
    w, h = ctypes.c_int(), ctypes.c_int()
    sent_catalog=False
    started, asked, heartbeat = time.time(), 0.0, time.time()
    while True:
        with lock:
            fresh = lib.nb4p_frame(buf, MAX_PIXELS, ctypes.byref(w), ctypes.byref(h))
            count = lib.nb4p_zones(zones, MAX_ZONES * 4) if fresh else 0
            shown = lib.nb4p_current_view()
            # Startup settles on the first screen; ask for the edited one.
            if shown != view and time.time() - started < 5 and time.time() - asked > 0.2:
                lib.nb4p_show_view(view)
                asked = time.time()
        if fresh and not sent_catalog:
            metadata=lib.nb4p_catalog()
            if metadata:
                connection.send_bytes(b'NB4M'+metadata)
                sent_catalog=True
        if fresh:
            size = w.value * h.value * 2
            connection.send_bytes(FRAME_HEADER.pack(FRAME_MAGIC, w.value, h.value, size, count, shown) + ctypes.string_at(buf, size) + ctypes.string_at(zones, count * 8))
        if time.time()-heartbeat > 1:
            connection.send_bytes(b'')
            heartbeat=time.time()
        time.sleep(0.015)


# ---------------------------------------------------------------------------
# Editor side
# ---------------------------------------------------------------------------
class FirmwarePreview:
    def __init__(self):
        self.root = Path(tempfile.mkdtemp(prefix='apextx-preview-'))
        self.lock = threading.Lock()
        self.process = None
        self.frame = None  # (seq, width, height, rgb565 bytes)
        self.seq = 0
        self.generation = 0
        self.inputs = {}
        self.connection = None
        self.error = None
        self.catalog = None

    @staticmethod
    def available():
        return dll_path() is not None

    def close(self):
        self._stop()
        shutil.rmtree(self.root, ignore_errors=True)

    def _stop(self):
        process, self.process = self.process, None
        if self.connection:
            self.connection.close()
            self.connection = None
        self.frame = None
        self.catalog = None
        if process and process.poll() is None:
            process.kill()
            try:
                process.wait(3)
            except subprocess.TimeoutExpired:
                pass

    def _command(self):
        if getattr(sys, 'frozen', False):
            return [sys.executable, '--preview-worker']
        return [sys.executable, str(Path(__file__).resolve().parent / 'apextx-updater.py'), '--preview-worker']

    # Storage the firmware starts from: the transmitter's own files when one
    # is connected, otherwise the firmware's factory files.
    def _assets(self, base, storage, model):
        """Copy referenced media; the simulator never opens the mounted volume."""
        # Model YAML also contains sequences outside screenData. Inspect only
        # scalar resource fields; the screen editor's indentation parser is
        # intentionally not a general model-file parser.
        scalars=re.findall(r'^\s*([A-Za-z_][A-Za-z0-9_]*):[ \t]*(.+)$',model.decode('utf-8'),re.M)
        values=[radio_files.unquote(value.strip()) for _,value in scalars]
        names=set()
        for value in values:
            path=PurePosixPath(value)
            if path.suffix.lower() not in ('.png','.bmp','.jpg','.jpeg','.lua','.txt'):continue
            if '..' in path.parts or '\\' in value:raise ValueError('Invalid preview resource path.')
            relative=value.lstrip('/')
            if '/' not in relative:relative='IMAGES/'+relative
            if relative.split('/')[0] in ('IMAGES','SCRIPTS','THEMES'):names.add(relative)
        bitmap=next((radio_files.unquote(value.strip()) for key,value in scalars if key=='bitmap'),'')
        if bitmap and '/' not in bitmap and bitmap not in ('.','..'):
            for suffix in ('.png','.bmp'):names.add('IMAGES/'+bitmap+suffix)
        # Existing Lua widgets may require adjacent modules. Copy only when the
        # model actually uses a widget outside the built-in compiled catalogue.
        catalogue=json.loads(_resource('apextx-theme-editor-catalog.json').read_bytes())
        known={entry['id'] for entry in catalogue['widgets']}
        has_custom=any(key=='widgetName' and bool(radio_files.unquote(value.strip())) and radio_files.unquote(value.strip()) not in known for key,value in scalars)
        if has_custom and (base/'SCRIPTS').is_dir():
            for source in (base/'SCRIPTS').rglob('*'):
                if source.is_symlink():raise ValueError('Preview resources cannot contain symbolic links.')
                if source.is_file():names.add(source.relative_to(base).as_posix())
        total=0
        for name in sorted(names):
            source=radio_files.safe_path(base/name)
            if not source.is_file():continue
            total+=source.stat().st_size
            if total>4*1024*1024 or len(names)>512:raise ValueError('Preview resources exceed the size limit.')
            target=storage/name;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(source,target)

    def _stage(self, request, radio_root):
        self.generation += 1
        storage = self.root / str(self.generation)
        for name in ('RADIO', 'MODELS', 'THEMES'):
            (storage / name).mkdir(parents=True)

        base = radio_root if radio_root and (radio_root / 'RADIO' / 'radio.yml').is_file() else _resource('nb4-preview-defaults')
        model_file = request.get('model') or ''
        model_src = base / 'MODELS' / model_file if radio_files.MODEL_FILE.match(model_file) else None
        if not model_src or not model_src.is_file():
            model_src = _resource('nb4-preview-defaults') / 'MODELS' / 'model1.yml'
        model = model_src.read_bytes()
        screens = request.get('screenData')
        if screens:
            model = radio_files.model_with_screens(model, screens, request.get('view'))
        # Under its own name: the firmware opens the selected model by the
        # file name the radio uses and starts a new one for anything else.
        (storage / 'MODELS' / model_src.name).write_bytes(model)
        self._assets(base,storage,model)

        settings = {'currModelFilename': model_src.name}
        for key in ('selectedTheme', 'nb4Orientation', 'nb4Accent'):
            if request.get(key) is not None:
                settings[key] = request[key]
        radio = radio_files.radio_with_settings((base / 'RADIO' / 'radio.yml').read_bytes(), settings)
        (storage / 'RADIO' / 'radio.yml').write_bytes(radio)

        total = 0
        for entry in request.get('files', [])[:4]:
            name = entry.get('name', '')
            if not THEME_FILE.match(name):
                raise ValueError(f'Refusing unexpected file name: {name}')
            data = base64.b64decode(entry.get('data', ''), validate=True)
            total += len(data)
            if total > 4 * 1024 * 1024:
                raise ValueError('The theme is too large.')
            path = storage.joinpath(*PurePosixPath(name).parts)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        return storage

    def restart(self, request, radio_root=None):
        """Show the given theme, screens and settings: a fresh firmware start."""
        with self.lock:
            self._stop()
            previous = self.root / str(self.generation)
            storage = self._stage(request, radio_root)
            shutil.rmtree(previous, ignore_errors=True)
            flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
            view = max(0, int(request.get('view') or 0))
            key = secrets.token_bytes(32)
            listener = Listener(('127.0.0.1', 0), authkey=key)
            listener._listener._socket.settimeout(5)
            target = 'nb4'
            if radio_root and (radio_root/'APEXTX-EDITOR.json').is_file():
                target=json.loads((radio_root/'APEXTX-EDITOR.json').read_bytes()).get('target','nb4')
            library=dll_path(target)
            if not library: raise ValueError('No firmware preview library for '+target)
            env = dict(os.environ, APEXTX_PREVIEW_DLL=str(library.resolve()), APEXTX_PREVIEW_PORT=str(listener.address[1]), APEXTX_PREVIEW_KEY=key.hex())
            process = subprocess.Popen(self._command() + [str(storage), str(view)], stdin=subprocess.DEVNULL,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env,
                                       creationflags=flags)
            self.process = process
            try:
                self.connection = listener.accept()
            except (OSError, EOFError) as error:
                self._stop()
                raise ValueError('Firmware preview worker did not start.') from error
            finally:
                listener.close()
            self.error = None
            threading.Thread(target=self._read, args=(process, self.connection), daemon=True).start()
            if self.inputs:
                self._send({'analog': self._analog(self.inputs)})
        return {'ok': True}

    def _read(self, process, connection):
        try:
            while True:
                if not connection.poll(5):
                    raise OSError('Firmware preview timed out.')
                packet = connection.recv_bytes(MAX_PIXELS * 2 + MAX_ZONES * 8 + FRAME_HEADER.size)
                if not packet: continue
                if packet.startswith(b'NB4M'):
                    value=json.loads(packet[4:])
                    with self.lock:
                        if process is not self.process:return
                        self.catalog=value
                    continue
                magic, width, height, size, count, shown = FRAME_HEADER.unpack(packet[:FRAME_HEADER.size])
                if (magic != FRAME_MAGIC or not 0 < width * height <= MAX_PIXELS or
                        size != width * height * 2 or count > MAX_ZONES or
                        len(packet) != FRAME_HEADER.size + size + count * 8):
                    raise ValueError('Invalid preview frame.')
                data = packet[FRAME_HEADER.size:FRAME_HEADER.size+size]
                values = struct.unpack(f'<{count * 4}h', packet[FRAME_HEADER.size+size:])
                zones = [values[i:i+4] for i in range(0,len(values),4)]
                with self.lock:
                    if process is not self.process: return
                    self.seq += 1
                    self.frame = (self.seq, width, height, data, zones, shown)
        except (OSError, EOFError, ValueError) as error:
            with self.lock:
                if process is self.process:
                    self.error = str(error)
                    self.frame = None

    def latest(self, after):
        with self.lock:
            if self.frame and self.frame[0] > after:
                return self.frame
        return None

    def running(self):
        return bool(self.process and self.process.poll() is None)

    @staticmethod
    def _analog(values):
        result = []
        for key, (index, sign) in ANALOG_INPUTS.items():
            if key in values:
                pct = max(-100.0, min(100.0, float(values[key])))
                result.append([index, round(2048 + sign * pct * 20.47)])
        return result

    def _send(self, message):
        process = self.process
        if process and process.poll() is None:
            try:
                self.connection.send_bytes(json.dumps(message).encode())
            except OSError:
                pass

    def set_inputs(self, values):
        self.inputs.update({k: v for k, v in values.items() if k in ANALOG_INPUTS})
        self._send({'analog': self._analog(values)})

    def touch(self, down, x, y):
        self._send({'touch': [1 if down else 0, int(x), int(y)]})
