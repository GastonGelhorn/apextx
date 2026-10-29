# SPDX-License-Identifier: GPL-2.0-only
"""Install managed themes/tools by file, preserving user changes and a backup."""
import hashlib
import json
from pathlib import Path
import os
import shutil
from nb4_storage_backup import backup_storage, verify_backup, validate_storage_session, file_hash, sync_directory
from nb4_rf_transfer import save_json
from nb4_rf_catalog import IMAGES

JOURNAL='APEXTX-RESOURCES-INSTALL.json'
MANIFEST='APEXTX-RESOURCES.json'


def install(package, files, volume, backup, device, *, confirmed=False, progress=lambda *_: None):
    if not confirmed: raise RuntimeError('Review the resource plan before installing.')
    package,volume,backup=Path(package),Path(volume),Path(backup)
    validate_storage_session(volume,**device)
    previous={}
    marker=volume/MANIFEST
    if marker.exists():
        if marker.is_symlink() or marker.stat().st_size>65536: raise RuntimeError('Invalid installed resource manifest.')
        previous=json.loads(marker.read_text(encoding='utf-8')).get('files',{})
        if not isinstance(previous,dict): raise RuntimeError('Invalid installed resource file list.')
    writes={}; preserved=[]; managed={}
    for name, expected in files.items():
        relative=Path(name)
        rf=any(name==f"FIRMWARE/RF/{image['product']:08X}.bin" and expected==image['sha256'] for image in IMAGES)
        if (relative.is_absolute() or '..' in relative.parts or '\\' in name or ':' in name or
                (name!='APEXTX-VERSION.json' and
                 (len(relative.parts)<2 or (relative.parts[0] not in ('THEMES','SCRIPTS') and not rf)))):
            raise RuntimeError('Unsafe resource path.')
        source=package/'resources'/relative; target=volume/relative
        if (source.is_symlink() or not source.is_file() or file_hash(source)!=expected):
            raise RuntimeError('A prepared resource changed.')
        for parent in (target,*target.parents):
            if parent==volume.parent: break
            if parent.is_symlink(): raise RuntimeError('A resource path contains a symbolic link.')
        current=file_hash(target) if target.is_file() else None
        if current and current not in (expected,previous.get(name)):
            preserved.append(name); continue
        managed[name]=expected
        if current!=expected: writes[name]=dict(before=current,sha256=expected,bytes=source.stat().st_size)
    pending=volume/JOURNAL
    if pending.exists():
        if pending.is_symlink() or pending.stat().st_size>131072: raise RuntimeError('Invalid resource recovery record.')
        record=json.loads(pending.read_text(encoding='utf-8'))
        if record.get('device')!=device or record.get('files')!=files or record.get('backup')!=str(backup.resolve()):
            raise RuntimeError('Resume resources with the original package, radio and computer backup.')
        verify_backup(backup)
        host=backup/'resources.json'
        if host.is_symlink() or not host.is_file() or host.stat().st_size>131072 or json.loads(host.read_text(encoding='utf-8'))!=record:
            raise RuntimeError('The resource recovery plan does not match the private computer record.')
        writes=record['writes']; preserved=record['preserved']; managed=record['managed']
    else:
        progress('Backing up radio storage before resources',5)
        backup_storage(volume,backup,device=device)
        record=dict(format='apextx-resource-install-1',device=device,files=files,writes=writes,
                    preserved=preserved,managed=managed,backup=str(backup.resolve()),complete=False)
        save_json(backup/'resources.json',record); save_json(pending,record)
    if (not isinstance(writes,dict) or not isinstance(managed,dict) or not set(writes)<=set(files) or
            not set(managed)<=set(files) or any(expected!=files[name] for name,expected in managed.items()) or
            any(entry.get('sha256')!=files[name] or entry.get('bytes')!=(package/'resources'/name).stat().st_size
                for name,entry in writes.items())):
        raise RuntimeError('The resource recovery plan contains unreviewed files.')
    # Only one replacement file needs temporary space; do not assume two full
    # copies fit in the NOR. Keep settings/history headroom throughout.
    required=max((entry['bytes'] for entry in writes.values()),default=0)
    growth=sum(max(0,entry['bytes']-(volume/name).stat().st_size if (volume/name).is_file() else entry['bytes']) for name,entry in writes.items())
    if shutil.disk_usage(volume).free < required+growth+262144:
        raise RuntimeError('Not enough storage for resources while keeping settings/history reserve. Defer resources or free space first.')
    for number,(name,entry) in enumerate(writes.items()):
        validate_storage_session(volume,**device)
        source=package/'resources'/name; target=volume/name
        current=file_hash(target) if target.is_file() else None
        if current==entry['sha256']: continue
        if current!=entry['before']: raise RuntimeError('A resource was changed after the backup. Resolve the conflict before resuming.')
        target.parent.mkdir(parents=True,exist_ok=True)
        temporary=target.with_name(target.name+'.apxtmp')
        if temporary.is_symlink(): raise RuntimeError('Invalid temporary resource path.')
        with source.open('rb') as src,temporary.open('wb') as dst:
            shutil.copyfileobj(src,dst); dst.flush(); os.fsync(dst.fileno())
        if file_hash(temporary)!=entry['sha256']: raise RuntimeError('Resource transfer verification failed.')
        validate_storage_session(volume,**device)
        os.replace(temporary,target); sync_directory(target.parent)
        progress('Installing verified resources',10+(number+1)*85//max(1,len(writes)))
    for name,expected in managed.items():
        if file_hash(volume/name)!=expected: raise RuntimeError('Installed resource verification failed.')
    validate_storage_session(volume,**device)
    save_json(marker,dict(format='apextx-resources-1',files=managed))
    record['complete']=True; save_json(backup/'resources.json',record)
    pending.unlink(); sync_directory(volume)
    progress('Resources independently verified',100)
    return dict(state='complete',installed=len(writes),preserved=preserved,backup=str(backup))
