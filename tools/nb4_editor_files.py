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
from pathlib import Path

MODEL_FILE = re.compile(r'^[A-Za-z0-9_-]{1,32}\.yml$')
RADIO_KEYS = ('selectedTheme', 'nb4Accent', 'nb4Orientation', 'nb4LedMode', 'nb4LedColor')
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


def dump_yaml(node, indent=0):
    out = []
    for key, value in node.items():
        if isinstance(value, dict):
            out.append(' ' * indent + f'{key}:')
            out += dump_yaml(value, indent + 2)
        else:
            out.append(' ' * indent + f'{key}: {value}')
    return out


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


def write_durable(path, data):
    """Write and flush to the device before reporting success: the radio is
    often unplugged right after saving, and buffered data would be lost."""
    with open(path, 'wb') as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())


def backup(path, backup_root):
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    dest = Path(backup_root) / stamp / Path(path).parent.name / Path(path).name
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, dest)
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
    path = Path(root) / 'MODELS' / file
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
        if len(zones) > MAX_ZONES or any(not z.isdigit() for z in zones):
            raise EditorFileError('Invalid zone list.')


def _check_scalars(node):
    for key, value in node.items():
        if not re.fullmatch(r'\w{1,32}', key):
            raise EditorFileError(f'Invalid key: {key}')
        if isinstance(value, dict):
            _check_scalars(value)
        elif not isinstance(value, str) or '\n' in value or '\r' in value or len(value) > 128:
            raise EditorFileError(f'Invalid value for {key}')


def write_model(root, file, expected_hash, screens, backup_root):
    path = _model_path(root, file)
    raw = path.read_bytes()
    if digest(raw) != expected_hash:
        raise EditorFileError('The model was changed on the radio meanwhile. Load it again.')
    lines, newline = _split_lines(raw)
    if _scalar(lines, 'nb4ScreenVersion') != '1':
        raise EditorFileError('Open this car once on the radio before editing its screens.')
    _check_screens(screens)
    _check_scalars(screens)
    new_block = dump_yaml({'screenData': screens})
    block = _block(lines, 'screenData')
    if block:
        lines[block[0]:block[1]] = new_block
    else:
        anchor = _block(lines, 'topbarData')
        at = anchor[0] if anchor else len(lines) - (1 if lines and lines[-1] == '' else 0)
        lines[at:at] = new_block
    saved = backup(path, backup_root)
    write_durable(path, newline.join(lines).encode('utf-8'))
    return {'backup': str(saved), 'hash': digest(path.read_bytes())}


def read_radio(root):
    path = Path(root) / 'RADIO' / 'radio.yml'
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


def write_radio(root, expected_hash, settings, backup_root):
    path = Path(root) / 'RADIO' / 'radio.yml'
    raw = path.read_bytes()
    if digest(raw) != expected_hash:
        raise EditorFileError('The radio settings changed meanwhile. Load them again.')
    # The firmware rejects a file whose checksum line does not match and
    # restores the previous copy, so the line is recomputed after editing.
    header = CHECKSUM.match(raw)
    if header:
        raw = raw[header.end():].lstrip(b'\r\n')
    lines, newline = _split_lines(raw)
    for key, value in settings.items():
        if key not in RADIO_KEYS:
            raise EditorFileError(f'Setting not editable: {key}')
        if key == 'selectedTheme':
            text = str(value)
            if not text or len(text) > 26 or not re.fullmatch(r'[\w .,()+\-/]+', text):
                raise EditorFileError('Invalid theme name.')
            value = quote(text)
        elif not (isinstance(value, int) and 0 <= value <= 15):
            raise EditorFileError(f'Invalid value for {key}')
        line = f'{key}: {value}'
        for i, old in enumerate(lines):
            if re.match(re.escape(key) + r':', old):
                lines[i] = line
                break
        else:
            at = len(lines) - (1 if lines and lines[-1] == '' else 0)
            lines.insert(at, line)
    saved = backup(path, backup_root)
    body = newline.join(lines).encode('utf-8')
    if header:
        body = b'checksum: %d\r\n' % radio_checksum(body) + body
    write_durable(path, body)
    return {'backup': str(saved), 'hash': digest(path.read_bytes())}
