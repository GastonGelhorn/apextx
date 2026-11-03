# SPDX-License-Identifier: GPL-2.0-only
"""Recoverable per-file replacements. The journal never promises FAT-wide atomicity."""
import json
import shutil
import uuid
from pathlib import Path
from nb4_editor_files import EditorFileError, digest, safe_path, write_durable, WRITE_LOCK
from nb4_storage_backup import sync_directory


def commit(root, changes, expected, backups, identity, check=None):
    root = safe_path(root).resolve()
    with WRITE_LOCK:
        paths = {}
        for name, data in changes.items():
            relative = Path(name)
            if relative.is_absolute() or '..' in relative.parts or '\\' in name:
                raise EditorFileError('Invalid editor path.')
            path = safe_path(root / relative)
            before = path.read_bytes() if path.exists() else None
            if name not in expected or (digest(before) if before is not None else None) != expected[name]:
                raise EditorFileError('A file changed since it was loaded. Reload it before saving.')
            paths[name] = (path, before, data)
        needed = max((len(d) for _, _, d in paths.values() if d is not None), default=0)
        growth = sum(max(0, len(d or b'') - len(b or b'')) for _, b, d in paths.values())
        if shutil.disk_usage(root).free < needed + growth + 262144:
            raise EditorFileError('Not enough radio storage; keep 256 KiB free for settings.')
        folder = Path(backups) / uuid.uuid4().hex
        folder.mkdir(parents=True, exist_ok=False)
        sync_directory(folder.parent)
        entries = {}
        for i, (name, (_, before, data)) in enumerate(paths.items()):
            for suffix, value in [('before', before), ('after', data)]:
                if value is not None:
                    write_durable(folder / f'{i}.{suffix}', value)
            entries[name] = dict(index=i, before=digest(before) if before is not None else None,
                                 after=digest(data) if data is not None else None)
        record = dict(format='apextx-editor-transaction-1', identity=identity, files=entries, complete=False)
        write_durable(folder / 'transaction.json', json.dumps(record).encode())
        try:
            recover(root, folder, identity, check=check)
        except Exception as error:
            raise EditorFileError(f'Save interrupted. Recovery backup: {folder}. {error}') from error
        return dict(backup=str(folder), hashes={n:e['after'] for n,e in entries.items()})


def recover(root, folder, identity, restore=False, check=None):
    with WRITE_LOCK:
        root, folder = safe_path(root).resolve(), safe_path(folder).resolve()
        journal = folder / 'transaction.json'
        record = json.loads(journal.read_bytes())
        if record.get('format') != 'apextx-editor-transaction-1' or record.get('identity') != identity:
            raise EditorFileError('Recovery belongs to another radio.')
        for name, entry in record['files'].items():
            relative = Path(name)
            if relative.is_absolute() or '..' in relative.parts or '\\' in name:
                raise EditorFileError('Invalid recovery path.')
            target = safe_path(root / name)
            current = digest(target.read_bytes()) if target.exists() else None
            if current not in (entry['before'], entry['after']):
                raise EditorFileError('Resolve external changes before recovery.')
            version = 'before' if restore else 'after'
            desired = entry[version]
            source = safe_path(folder / f"{entry['index']}.{version}")
            if desired is not None and digest(source.read_bytes()) != desired:
                raise EditorFileError('Backup verification failed.')
        for name, entry in record['files'].items():
            current = digest((root/name).read_bytes()) if (root/name).exists() else None
            if current not in (entry['before'],entry['after']):raise EditorFileError('A file changed during recovery.')
            if check is not None and check() != identity:
                raise EditorFileError('The connected radio changed during saving.')
            target = safe_path(root / name)
            version = 'before' if restore else 'after'
            if entry[version] is None:
                target.unlink(missing_ok=True)
                if target.parent.exists(): sync_directory(target.parent)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                write_durable(target, (folder / f"{entry['index']}.{version}").read_bytes(), expected_hash=current)
        record.update(complete=True, restored=restore)
        write_durable(journal, json.dumps(record).encode())
