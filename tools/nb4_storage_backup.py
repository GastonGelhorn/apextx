# SPDX-License-Identifier: GPL-2.0-only
"""Private filesystem backups. Never accesses USB or raw MCU/NOR memory."""
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import shutil

FORMAT = 'apextx-filesystem-backup-1'
MAX_BYTES = 9 * 1024 * 1024
MAX_FILES = 4096


def sync_directory(path):
    if os.name == 'nt':
        return
    fd = os.open(path, os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def file_hash(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(65536), b''):
            value.update(chunk)
    return value.hexdigest()


def snapshot_tree(root):
    root = Path(root)
    if not root.is_dir() or root.is_symlink():
        raise RuntimeError('Select an ordinary radio storage directory.')
    files, total = {}, 0
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise RuntimeError('Symbolic links cannot be included in radio backups.')
        if path.is_dir():
            continue
        if not path.is_file():
            raise RuntimeError('A radio backup contains a special file.')
        name = path.relative_to(root).as_posix()
        if (len(name) > 240 or '\\' in name or ':' in name or
                any(part.endswith((' ', '.')) for part in PurePosixPath(name).parts)):
            raise RuntimeError('A radio backup path is invalid.')
        total += path.stat().st_size
        if total > MAX_BYTES or len(files) >= MAX_FILES:
            raise RuntimeError('The directory exceeds the radio backup limits.')
        files[name] = dict(bytes=path.stat().st_size, sha256=file_hash(path))
    return files


def verify_backup(directory):
    directory = Path(directory)
    manifest = directory / 'storage.json'
    if directory.is_symlink() or manifest.is_symlink() or manifest.stat().st_size > 1024 * 1024:
        raise RuntimeError('Invalid filesystem backup manifest.')
    data = json.loads(manifest.read_text(encoding='utf-8'))
    if data.get('format') != FORMAT or data.get('raw_nor') is not False:
        raise RuntimeError('Unsupported filesystem backup.')
    if snapshot_tree(directory / 'data') != data.get('files'):
        raise RuntimeError('Filesystem backup checksum verification failed.')
    return data


def validate_storage_session(volume, serial, target, nonce):
    from nb4_maintenance import unique_object
    root=Path(volume)
    marker=root/'APEXTX-USB.json'
    if (root.is_symlink() or marker.is_symlink() or not marker.is_file() or marker.stat().st_size>4096 or
            not re.fullmatch('[0-9a-f]{32}',nonce or '')):
        raise RuntimeError('Select the radio storage volume prepared by this maintenance session.')
    value=json.loads(marker.read_text(encoding='utf-8'),object_pairs_hook=unique_object)
    if value != dict(format='apextx-storage-session-1',serial=serial,target=target,nonce=nonce):
        raise RuntimeError('This storage volume belongs to another radio or an older maintenance session.')
    return dict(serial=serial,target=target,nonce=nonce)


def backup_storage(volume, directory, *, device=None):
    volume, directory = Path(volume), Path(directory)
    source, destination = volume.resolve(), directory.resolve()
    if destination == source or source in destination.parents:
        raise RuntimeError('The backup directory must be on the computer.')
    before = snapshot_tree(volume)
    directory.mkdir(mode=0o700, parents=True, exist_ok=False)
    root = directory / 'data'
    root.mkdir()
    for name, info in before.items():
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        # Do not inherit read-only USB permissions: each new file is flushed
        # through its writable handle on Windows as well as Unix.
        with (volume / name).open('rb') as src, target.open('xb') as dst:
            shutil.copyfileobj(src, dst, 65536)
            dst.flush()
            os.fsync(dst.fileno())
        if file_hash(target) != info['sha256']:
            raise RuntimeError('Filesystem backup verification failed; device was not changed.')
    if snapshot_tree(volume) != before or snapshot_tree(root) != before:
        raise RuntimeError('Radio storage changed during backup. Retry before installing.')
    metadata = dict(format=FORMAT, raw_nor=False, files=before)
    if device is not None:
        expected=validate_storage_session(volume,device['serial'],device['target'],device['nonce'])
        if expected!=device: raise RuntimeError('Invalid storage backup identity.')
        metadata['device']=expected
    with (directory / 'storage.json').open('x', encoding='utf-8') as stream:
        json.dump(metadata, stream, indent=2, sort_keys=True)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    for path in sorted(root.rglob('*'), reverse=True):
        if path.is_dir():
            sync_directory(path)
    sync_directory(root)
    sync_directory(directory)
    sync_directory(directory.parent)
    verify_backup(directory)
    return metadata
