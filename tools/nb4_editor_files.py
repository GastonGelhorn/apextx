# SPDX-License-Identifier: GPL-2.0-only
"""Read and write the radio files the ApexTX Theme Editor changes.

Only the sections the editor owns are rewritten: ``screenData``/``topbarData``
in a model file and single top-level keys in ``RADIO/radio.yml``. Everything
else stays byte-for-byte as the radio wrote it. Every write is preceded by a
backup on the PC and refused if the file changed since it was read.
"""
import binascii
import datetime
import hashlib
import os
import re
import shutil
import tempfile
import uuid
import threading
import functools
from nb4_storage_backup import sync_directory

WRITE_LOCK = threading.RLock()
_UNCHECKED = object()
from pathlib import Path

MODEL_FILE = re.compile(r'^[A-Za-z0-9_-]{1,32}\.yml$')
RADIO_KEYS = ('selectedTheme', 'nb4Accent', 'nb4Orientation', 'nb4LedMode', 'nb4LedColor')
# Only ever written to the preview's private copy, never to the radio.
PREVIEW_KEYS = ('currModelFilename',)
MAX_SCREENS = 10
MAX_ZONES = 10


class EditorFileError(ValueError):
    pass


def digest(data):
    return hashlib.sha256(data).hexdigest()


def parse_yaml(lines):
    """Nested dicts of raw scalar text for the indentation-only subset the
    radio writes (no flow style, no lists)."""
    root = {}
    stack = [(-1, root)]
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith('#') or stripped == '---':
            continue
        indent = len(line) - len(line.lstrip(' '))
        key, sep, value = stripped.partition(':')
        if not sep:
            raise EditorFileError(f'Unexpected line: {stripped}')
        while stack[-1][0] >= indent:
            stack.pop()
        parent = stack[-1][1]
        value = value.strip()
        if value:
            parent[key] = value
        else:
            node = {}
            parent[key] = node
            stack.append((indent, node))
    return root


def dump_yaml(node, indent=0, step=3, open_suffix=': '):
    """The radio's own style: three spaces per level and "key: " before a
    nested block; index keys ("0:") carry no trailing space."""
    out = []
    for key, value in node.items():
        if isinstance(value, dict):
            out.append(' ' * indent + key + (':' if key.isdigit() else open_suffix))
            out += dump_yaml(value, indent + step, step, open_suffix)
        else:
            out.append(' ' * indent + f'{key}: {value}')
    return out


def yaml_style(lines):
    """Indent step and block opener of an existing block, as written."""
    step = 3
    for line in lines[1:]:
        if line.strip():
            step = (len(line) - len(line.lstrip(' '))) or 3
            break
    return step, (': ' if lines and lines[0].endswith(': ') else ':')


def unquote(value):
    if isinstance(value, str) and len(value) >= 2 and value[0] == value[-1] == '"':
        return value[1:-1].replace('\\"', '"').replace('\\\\', '\\')
    return value


def quote(text):
    return '"' + str(text).replace('\\', '\\\\').replace('"', '\\"') + '"'


def _split_lines(raw):
    text = raw.decode('utf-8')
    newline = '\r\n' if '\r\n' in text else '\n'
    return text.split(newline), newline


def _block(lines, key):
    """(start, end) line range of a top-level block, or None."""
    for i, line in enumerate(lines):
        if line.rstrip() == key + ':':
            end = i + 1
            while end < len(lines) and (not lines[end].strip() or lines[end][0] == ' '):
                end += 1
            return i, end
    return None


def _scalar(lines, key):
    for line in lines:
        m = re.match(re.escape(key) + r':\s*(.*)$', line)
        if m:
            return m.group(1).strip()
    return None


def safe_path(path):
    path = Path(path)
    if any(p.is_symlink() and str(p) not in ('/var', '/tmp', '/etc') for p in (path, *path.parents)):
        raise EditorFileError('Symbolic links are not editable.')
    return path


def write_durable(path, data, expected_hash=_UNCHECKED):
    """Verify a sibling temporary before replacing the previous file."""
    path = safe_path(path)
    fd, temporary = tempfile.mkstemp(prefix='.' + path.name + '-', dir=path.parent)
    temporary = Path(temporary)
    try:
        with os.fdopen(fd, 'wb') as stream:
            if stream.write(data) != len(data):
                raise EditorFileError('Incomplete file write.')
            stream.flush()
            os.fsync(stream.fileno())
        if temporary.read_bytes() != data:
            raise EditorFileError('Temporary file verification failed.')
        if expected_hash is not _UNCHECKED:
            current = digest(path.read_bytes()) if path.exists() else None
            if current != expected_hash: raise EditorFileError('The file changed during saving.')
        os.replace(temporary, path)
        sync_directory(path.parent)
        if path.read_bytes() != data:
            raise EditorFileError('Installed file verification failed.')
    finally:
        temporary.unlink(missing_ok=True)


def backup(path, backup_root):
    path = safe_path(path)
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '-' + uuid.uuid4().hex
    dest = Path(backup_root) / stamp / path.parent.name / path.name
    dest.parent.mkdir(parents=True, exist_ok=False)
    write_durable(dest, path.read_bytes())
    return dest


def list_models(root):
    models = []
    folder = Path(root) / 'MODELS'
    current = read_radio(root)['settings'].get('currModelFilename') if (Path(root) / 'RADIO' / 'radio.yml').is_file() else None
    for path in sorted(folder.glob('*.yml')) if folder.is_dir() else []:
        if not MODEL_FILE.match(path.name):
            continue
        lines, _ = _split_lines(path.read_bytes())
        header = _block(lines, 'header')
        name = None
        if header:
            name = unquote(parse_yaml(lines[header[0]:header[1]]).get('header', {}).get('name'))
        version = _scalar(lines, 'nb4ScreenVersion')
        models.append({'file': path.name, 'name': name or path.stem,
                       'editable': version == '1', 'current': path.name == current})
    return models


def _model_path(root, file):
    if not MODEL_FILE.match(file):
        raise EditorFileError('Invalid model file name.')
    path = safe_path(Path(root) / 'MODELS' / file)
    if not path.is_file():
        raise EditorFileError('The model file no longer exists.')
    return path


def read_model(root, file):
    path = _model_path(root, file)
    raw = path.read_bytes()
    lines, _ = _split_lines(raw)
    result = {'file': file, 'hash': digest(raw), 'editable': _scalar(lines, 'nb4ScreenVersion') == '1'}
    for key in ('screenData', 'topbarData'):
        block = _block(lines, key)
        result[key] = parse_yaml(lines[block[0]:block[1]]).get(key, {}) if block else {}
    return result


def _check_screens(screens):
    if not isinstance(screens, dict) or len(screens) > MAX_SCREENS:
        raise EditorFileError('Invalid screen list.')
    for index, screen in screens.items():
        if not (index.isdigit() and int(index) < MAX_SCREENS) or not isinstance(screen, dict):
            raise EditorFileError('Invalid screen index.')
        if not re.fullmatch(r'\w{1,24}', unquote(screen.get('LayoutId', ''))):
            raise EditorFileError('Every screen needs a layout.')
        zones = screen.get('layoutData', {}).get('zones', {})
        if len(zones) > MAX_ZONES or any(not z.isdigit() or int(z) >= MAX_ZONES for z in zones):
            raise EditorFileError('Invalid zone list.')


def _check_scalars(node):
    for key, value in node.items():
        if not re.fullmatch(r'\w{1,32}', key):
            raise EditorFileError(f'Invalid key: {key}')
        if isinstance(value, dict):
            _check_scalars(value)
        elif not isinstance(value, str) or '\n' in value or '\r' in value or len(value) > 128:
            raise EditorFileError(f'Invalid value for {key}')


def model_with_screens(raw, screens, view=None):
    """The model file with its screenData block replaced (and, for the
    preview, the main view to start on)."""
    lines, newline = _split_lines(raw)
    if _scalar(lines, 'nb4ScreenVersion') != '1':
        raise EditorFileError('Open this car once on the radio before editing its screens.')
    _check_screens(screens)
    _check_scalars(screens)
    block = _block(lines, 'screenData')
    step, opener = yaml_style(lines[block[0]:block[1]]) if block else (3, ': ')
    new_block = dump_yaml({'screenData': screens}, 0, step, opener)
    if block:
        lines[block[0]:block[1]] = new_block
    else:
        anchor = _block(lines, 'topbarData')
        at = anchor[0] if anchor else len(lines) - (1 if lines and lines[-1] == '' else 0)
        lines[at:at] = new_block
    if view is not None:
        for i, line in enumerate(lines):
            if re.match(r'view:', line):
                lines[i] = f'view: {int(view)}'
                break
    return newline.join(lines).encode('utf-8')


def synchronized(function):
    @functools.wraps(function)
    def call(*args, **kwargs):
        with WRITE_LOCK: return function(*args, **kwargs)
    return call


@synchronized
def write_model(root, file, expected_hash, screens, backup_root):
    path = _model_path(root, file)
    raw = path.read_bytes()
    if digest(raw) != expected_hash:
        raise EditorFileError('The model was changed on the radio meanwhile. Load it again.')
    data = model_with_screens(raw, screens)
    saved = backup(path, backup_root)
    write_durable(path, data, expected_hash=expected_hash)
    return {'backup': str(saved), 'hash': digest(path.read_bytes())}


def read_radio(root):
    path = safe_path(Path(root) / 'RADIO' / 'radio.yml')
    raw = path.read_bytes()
    lines, _ = _split_lines(raw)
    settings = {}
    for key in RADIO_KEYS + ('currModelFilename',):
        value = _scalar(lines, key)
        if value is not None:
            settings[key] = unquote(value)
    return {'hash': digest(raw), 'settings': settings}


CHECKSUM = re.compile(rb'checksum: (\d+)\r?\n')


def radio_checksum(body):
    """CRC-16/CCITT (0x1021, start 0xFFFF) as the firmware writes it in the
    first line of radio.yml (nb4QueueSettings(), readYamlFile())."""
    return binascii.crc_hqx(body, 0xFFFF)


def radio_with_settings(raw, settings):
    """radio.yml with single keys replaced and its checksum line recomputed."""
    # The firmware rejects a file whose checksum line does not match and
    # restores the previous copy, so the line is recomputed after editing.
    header = CHECKSUM.match(raw)
    if header:
        raw = raw[header.end():].lstrip(b'\r\n')
    lines, newline = _split_lines(raw)
    for key, value in settings.items():
        if key not in RADIO_KEYS + PREVIEW_KEYS:
            raise EditorFileError(f'Setting not editable: {key}')
        if key in ('selectedTheme', 'currModelFilename'):
            text = str(value)
            if not text or len(text.encode('utf-8')) > 26 or not re.fullmatch(r'[\w .,()+\-/]+', text):
                raise EditorFileError('Invalid theme name.')
            value = quote(text)
        elif not (type(value) is int and 0 <= value <= {'nb4Accent':8,'nb4Orientation':1,'nb4LedMode':3,'nb4LedColor':7}[key]):
            raise EditorFileError(f'Invalid value for {key}')
        line = f'{key}: {value}'
        for i, old in enumerate(lines):
            if re.match(re.escape(key) + r':', old):
                lines[i] = line
                break
        else:
            at = len(lines) - (1 if lines and lines[-1] == '' else 0)
            lines.insert(at, line)
    body = newline.join(lines).encode('utf-8')
    if header:
        body = b'checksum: %d\r\n' % radio_checksum(body) + body
    return body


@synchronized
def write_radio(root, expected_hash, settings, backup_root):
    path = safe_path(Path(root) / 'RADIO' / 'radio.yml')
    raw = path.read_bytes()
    if digest(raw) != expected_hash:
        raise EditorFileError('The radio settings changed meanwhile. Load them again.')
    if any(key not in RADIO_KEYS for key in settings):
        raise EditorFileError('Setting not editable.')
    data = radio_with_settings(raw, settings)
    saved = backup(path, backup_root)
    write_durable(path, data, expected_hash=expected_hash)
    return {'backup': str(saved), 'hash': digest(path.read_bytes())}
