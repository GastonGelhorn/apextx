# SPDX-License-Identifier: GPL-2.0-only
"""Firmware preview for the ApexTX Theme Editor.

The editor's preview is drawn by the firmware itself: nb4-preview.dll is the
NB4 firmware built for the PC (radio/src/targets/simu, target nb4-preview).
It runs in a worker process on a private copy of the transmitter's storage,
so every change restarts it from a clean state and a crash cannot take the
editor down. Frames come back over the worker's stdout.
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
from pathlib import Path, PurePosixPath

import nb4_editor_files as radio_files

DLL_NAME = 'nb4-preview.dll' if os.name == 'nt' else 'nb4-preview.so'
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


def dll_path():
    override = os.environ.get('APEXTX_PREVIEW_DLL')
    path = Path(override) if override else _resource(DLL_NAME)
    return path if path.is_file() else None


# ---------------------------------------------------------------------------
# Worker process: loads the firmware and streams its frames.
# ---------------------------------------------------------------------------
def worker_main(storage, view=0):
    path = dll_path()
    os.add_dll_directory(str(path.parent)) if hasattr(os, 'add_dll_directory') else None
    lib = ctypes.CDLL(str(path))
    lib.nb4p_start.argtypes = [ctypes.c_char_p]
    lib.nb4p_frame.argtypes = [ctypes.POINTER(ctypes.c_uint16), ctypes.c_int,
                               ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
    out = sys.stdout.buffer
    lock = threading.Lock()

    def commands():
        for line in sys.stdin:
            try:
                cmd = json.loads(line)
            except ValueError:
                continue
            with lock:
                if cmd.get('analog'):
                    for index, value in cmd['analog']:
                        lib.nb4p_set_analog(int(index), int(value))
                elif 'touch' in cmd:
                    down, x, y = cmd['touch']
                    lib.nb4p_touch(int(down), int(x), int(y))
        os._exit(0)  # editor gone

    # The firmware prints its trace on stdout; frames need that channel.
    stdout_fd = os.dup(1)
    os.dup2(os.open(os.devnull, os.O_WRONLY), 1)
    out = os.fdopen(stdout_fd, 'wb')
    threading.Thread(target=commands, daemon=True).start()
    if not lib.nb4p_start(storage.encode('utf-8')):
        os._exit(2)
    lib.nb4p_zones.argtypes = [ctypes.POINTER(ctypes.c_int16), ctypes.c_int]
    buf = (ctypes.c_uint16 * MAX_PIXELS)()
    zones = (ctypes.c_int16 * (MAX_ZONES * 4))()
    w, h = ctypes.c_int(), ctypes.c_int()
    started, asked = time.time(), 0.0
    while True:
        with lock:
            fresh = lib.nb4p_frame(buf, MAX_PIXELS, ctypes.byref(w), ctypes.byref(h))
            count = lib.nb4p_zones(zones, MAX_ZONES * 4) if fresh else 0
            shown = lib.nb4p_current_view()
            # Startup settles on the first screen; ask for the edited one.
            if shown != view and time.time() - started < 5 and time.time() - asked > 0.2:
                lib.nb4p_show_view(view)
                asked = time.time()
        if fresh:
            size = w.value * h.value * 2
            out.write(FRAME_HEADER.pack(FRAME_MAGIC, w.value, h.value, size, count, shown))
            out.write(ctypes.string_at(buf, size))
            out.write(ctypes.string_at(zones, count * 8))
            out.flush()
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

    @staticmethod
    def available():
        return dll_path() is not None

    def close(self):
        self._stop()
        shutil.rmtree(self.root, ignore_errors=True)

    def _stop(self):
        process, self.process = self.process, None
        if process and process.poll() is None:
            process.kill()
            try:
                process.wait(3)
            except subprocess.TimeoutExpired:
                pass

    def _command(self):
        if getattr(sys, 'frozen', False):
            return [sys.executable, '--preview-worker']
        return [sys.executable, str(Path(__file__).resolve().parent / 'apextx-theme-editor.py'), '--preview-worker']

    # Storage the firmware starts from: the transmitter's own files when one
    # is connected, otherwise the firmware's factory files.
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
            process = subprocess.Popen(self._command() + [str(storage), str(view)], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                       creationflags=flags)
            self.process = process
            threading.Thread(target=self._read, args=(process,), daemon=True).start()
            if self.inputs:
                self._send({'analog': self._analog(self.inputs)})
        return {'ok': True}

    def _read(self, process):
        stream = process.stdout
        while True:
            header = stream.read(FRAME_HEADER.size)
            if len(header) < FRAME_HEADER.size:
                return
            magic, width, height, size, count, shown = FRAME_HEADER.unpack(header)
            if magic != FRAME_MAGIC or size != width * height * 2 or count > MAX_ZONES:
                return
            data = stream.read(size)
            raw = stream.read(count * 8)
            if len(data) < size or len(raw) < count * 8:
                return
            values = struct.unpack(f'<{count * 4}h', raw)
            zones = [values[i:i + 4] for i in range(0, len(values), 4)]
            with self.lock:
                if process is not self.process:
                    return
                self.seq += 1
                self.frame = (self.seq, width, height, data, zones, shown)

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
                process.stdin.write((json.dumps(message) + '\n').encode())
                process.stdin.flush()
            except OSError:
                pass

    def set_inputs(self, values):
        self.inputs.update({k: v for k, v in values.items() if k in ANALOG_INPUTS})
        self._send({'analog': self._analog(values)})

    def touch(self, down, x, y):
        self._send({'touch': [1 if down else 0, int(x), int(y)]})
