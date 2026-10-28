# SPDX-License-Identifier: GPL-2.0-only
"""Persistent component plans shared by the desktop wizard and Linux CLI."""
import copy
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import secrets
import shutil
import struct
import time
import uuid
import io
import tarfile
import zipfile

from nb4_maintenance import connection, read_inventory, STORAGE_SESSION, CAPABILITIES, unique_object
from nb4_rf_transfer import RfTransfer, TransferClient, plan as rf_plan, save_json
from nb4_rf_catalog import verify_bundle, MAX_BUNDLE, IMAGES, image_name
from nb4_update_image import update_package, package_target, VARIANTS, HEADER, BOOT_SIZE, validate_full_image_auth
from nb4_usb_update import probe, run_update, UpdateError
from nb4_backup_agent import full_backup, verify as verify_backup, validate_agent
from nb4_storage_backup import validate_storage_session, file_hash
from nb4_releases import bootloader_required, image_bootloader_generation
import nb4_rom_flash

COMPONENTS=('application','module','receiver','resources','audio')
# Software support is distinct from demonstrating complete rollback on each
# transmitter. These gates change only after the physical acceptance tests.
FIRST_INSTALL_QUALIFIED=frozenset()


def unpack_package(data,directory):
    if not isinstance(data,bytes) or not 1<=len(data)<=128*1024*1024:
        raise UpdateError('Invalid complete-package archive size.')
    root=Path(directory); root.mkdir(parents=True,exist_ok=False)
    names=set(); total=0
    def extract(name,size,source):
        nonlocal total
        target=safe_path(root,name); key=name.casefold()
        if key in names or len(names)>=2048 or size<0: raise UpdateError('Duplicate or excessive package entries.')
        names.add(key); total+=size
        if total>128*1024*1024: raise UpdateError('The expanded component package exceeds its size limit.')
        target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as out:
            remaining=size
            while remaining:
                block=source.read(min(65536,remaining))
                if not block: raise UpdateError('Truncated component package.')
                out.write(block); remaining-=len(block)
    if data[:4]==b'PK\x03\x04':
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            for number,item in enumerate(archive.infolist()):
                if number>=4096: raise UpdateError('Excessive component-package entries.')
                if item.is_dir(): safe_path(root,item.filename.rstrip('/')); continue
                if item.flag_bits&1 or (item.external_attr>>16 & 0o170000) not in (0,0o100000):
                    raise UpdateError('Encrypted or special files are not supported in component packages.')
                with archive.open(item) as source: extract(item.filename,item.file_size,source)
    else:
        with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
            for number,item in enumerate(archive):
                if number>=4096: raise UpdateError('Excessive component-package entries.')
                if item.isdir(): safe_path(root,item.name.rstrip('/')); continue
                if not item.isfile(): raise UpdateError('Links and special files are not supported in component packages.')
                with archive.extractfile(item) as source: extract(item.name,item.size,source)
    manifests=list(root.glob('manifest.json'))+list(root.glob('*/manifest.json'))
    if len(manifests)!=1: raise UpdateError('Select one complete ApexTX component package.')
    return manifests[0].parent


def safe_path(root, name):
    relative=PurePosixPath(name)
    if (not isinstance(name,str) or not name or relative.is_absolute() or '..' in relative.parts or
            '\\' in name or ':' in name or relative.as_posix()!=name):
        raise UpdateError('Unsafe update-package path.')
    result=root.joinpath(*relative.parts)
    for path in (result,*result.parents):
        if path==root.parent: break
        if path.is_symlink(): raise UpdateError('Update packages cannot contain symbolic links.')
    return result


class Package:
    def __init__(self,directory):
        self.root=Path(directory).resolve()
        path=self.root/'manifest.json'
        if not path.is_file() or path.is_symlink() or path.stat().st_size>1024*1024:
            raise UpdateError('Select an extracted ApexTX package containing manifest.json.')
        self.manifest=json.loads(path.read_text(encoding='utf-8'),object_pairs_hook=unique_object)
        entries=self.manifest.get('files')
        if (self.manifest.get('formatVersion')!=3 or not isinstance(entries,dict) or 'firmware.bin' not in entries or
                not 1<=len(entries)<=2048): raise UpdateError('Unsupported component-package manifest.')
        total=0
        for name,entry in entries.items():
            source=safe_path(self.root,name)
            if (not isinstance(entry,dict) or type(entry.get('bytes')) is not int or entry['bytes']<0 or
                    not source.is_file() or source.stat().st_size!=entry['bytes'] or
                    file_hash(source)!=entry.get('sha256')):
                raise UpdateError('A component-package file is missing or changed.')
            total+=entry['bytes']
            if total>128*1024*1024: raise UpdateError('The component package exceeds its size limit.')
        self.image=(self.root/'firmware.bin').read_bytes()
        validate_full_image_auth(self.image)
        self.target=package_target(update_package(self.image))
        self.hardware={1:'nb4',2:'nb4plus'}[self.target]
        if (self.manifest.get('hardware')!=self.hardware or
                self.manifest.get('target')!=VARIANTS[self.target]):
            raise UpdateError('The package and transmitter image identify different hardware.')
        self.resources={name[len('resources/'):]:entry['sha256'] for name,entry in entries.items() if name.startswith('resources/')}
        self.catalog=None; self.images={}
        if 'rf/catalog.zip' in entries:
            data=(self.root/'rf/catalog.zip').read_bytes()
            self.catalog,self.images=verify_bundle(data)
        for name,kind in (('backup','apextx-backup-agent-1'),('nor-restore','apextx-nor-restore-agent-1')):
            report=f'recovery/{name}-agent.json'; binary=f'recovery/nb4-{name}-agent.bin'
            if report not in entries or binary not in entries:
                raise UpdateError('Rebuild the component package with both verified recovery agents.')
            value=json.loads((self.root/report).read_text(encoding='utf-8'),object_pairs_hook=unique_object)
            if value.get('format')!=kind or value.get('sha256')!=entries[binary]['sha256']:
                raise UpdateError('The recovery agent report does not match its image.')
            validate_agent((self.root/binary).read_bytes(),value['sha256'])

    def copy(self,directory):
        directory.mkdir(parents=True,exist_ok=False)
        manifest=copy.deepcopy(self.manifest)
        manifest['files']={name:entry for name,entry in self.manifest['files'].items() if
            name=='firmware.bin' or name.startswith(('resources/','recovery/','rf/'))}
        for name,entry in manifest['files'].items():
            source=safe_path(self.root,name); target=safe_path(directory,name)
            target.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(source,target)
            if file_hash(target)!=entry['sha256']: raise UpdateError('The prepared component copy failed verification.')
        for image in IMAGES:
            data=self.images.get(image_name(image))
            if data is None: continue
            name=f"resources/FIRMWARE/RF/{image['product']:08X}.bin"
            target=safe_path(directory,name); target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
            manifest['files'][name]=dict(bytes=len(data),sha256=image['sha256'])
        save_json(directory/'manifest.json',manifest)
        return Package(directory)


class IntegratedUpdate:
    @classmethod
    def create(cls,package,serial,target,directory):
        if not re.fullmatch('[0-9A-F]{12}',serial or '') or target not in (1,2):
            raise UpdateError('Select one identified NB4 or NB4+ first.')
        source=Package(package)
        if source.target!=target: raise UpdateError('This complete firmware package is for another transmitter.')
        directory=Path(directory); directory.mkdir(mode=0o700,parents=True,exist_ok=False)
        source.copy(directory/'assets')
        value=cls.__new__(cls); value.root=directory; value.package=Package(directory/'assets')
        value.record=dict(format='apextx-integrated-update-1',id=uuid.uuid4().hex,serial=serial,target=target,
            approved=False,backup=None,storage_nonce=None,inventory=None,experimental=True,rf_assets=bool(value.package.catalog),
            bootloader=dict(state='pending',proposed=image_bootloader_generation(value.package.image)),
            selected={name:name!='audio' for name in COMPONENTS},
            steps={name:dict(state='pending') for name in COMPONENTS},error=None)
        if not value.package.resources: value.record['selected']['resources']=False
        value.record['steps']['audio']=dict(state='deferred',reason='Keep the installed voice language unless another pack is explicitly selected.')
        value.save(); return value

    @classmethod
    def load(cls,directory):
        value=cls.__new__(cls); value.root=Path(directory)
        path=value.root/'session.json'
        if path.is_symlink() or not path.is_file() or path.stat().st_size>131072:
            raise UpdateError('Invalid component-session record.')
        value.record=json.loads(path.read_text(encoding='utf-8'),object_pairs_hook=unique_object)
        r=value.record
        if (r.get('format')!='apextx-integrated-update-1' or not re.fullmatch('[0-9a-f]{32}',r.get('id','')) or
                not re.fullmatch('[0-9A-F]{12}',r.get('serial','')) or r.get('target') not in (1,2) or
                set(r.get('selected',{}))!=set(COMPONENTS) or set(r.get('steps',{}))!=set(COMPONENTS) or
                any(type(r['selected'][key]) is not bool for key in COMPONENTS) or type(r.get('approved')) is not bool):
            raise UpdateError('The component session is incomplete or invalid.')
        value.package=Package(value.root/'assets')
        if value.package.target!=r['target']: raise UpdateError('The prepared session targets another transmitter.')
        # An interrupted invocation is never treated as a reason to replay a
        # destructive request. Reconnect and verify or use explicit recovery.
        for key,step in r['steps'].items():
            if step['state']=='running':
                state='failed' if key in ('resources','audio') else 'recovery_required'
                if key in ('module','receiver'):
                    transfer=RfTransfer(None,r['serial'],value.package.hardware,value.root/(key+'-operation'))
                    operation=transfer.load()
                    if not operation['write_started']: state='staged' if operation['state']=='staged' else 'failed'
                step.update(state=state,reason='Interrupted operation; inspect before continuing.')
        value.save(); return value

    def save(self): save_json(self.root/'session.json',self.record)
    def snapshot(self):
        result=copy.deepcopy(self.record)
        result.update(directory=str(self.root.resolve()),package_version=self.package.manifest.get('release'),outcome=self.outcome())
        return result
    def outcome(self):
        if any(self.record.get(key,{}).get('state') in ('running','recovery_required') for key in ('nor_restoration','internal_restoration')):
            return 'recovery_required'
        if any(step['state']=='recovery_required' for step in self.record['steps'].values()): return 'recovery_required'
        if self.record['selected']['application'] and self.record.get('bootloader',{}).get('state') in ('pending','available') and self.record['bootloader'].get('proposed'):
            return 'partial'
        if any(step['state']=='failed' for step in self.record['steps'].values()): return 'partial'
        states=[step['state'] for key,step in self.record['steps'].items() if self.record['selected'][key]]
        if 'recovery_required' in states: return 'recovery_required'
        return 'complete' if states and all(state in ('current','complete') for state in states) else 'partial'
    def device(self,mode):
        value=probe()
        if (value.get('serial')!=self.record['serial'] or
                value.get('target') not in (None,self.record['target']) or
                value.get('state')!=('ready' if mode=='update' else 'recovery' if mode=='rom' else 'normal') or
                (mode=='maintenance' and value.get('maintenance') is not True) or
                (mode=='storage' and value.get('mode')!='storage')):
            raise UpdateError('Reconnect the original transmitter in '+mode+' mode. No next component was started.')
        return value
    def inventory(self):
        if probe().get('state')=='ready':
            device=self.device('update')
            boot=dict(current=device.get('bootloader',0),proposed=image_bootloader_generation(self.package.image))
            boot['state']='available' if bootloader_required(device,boot['proposed']) else 'current'
            if self.record.get('bootloader')!=boot: self.record['approved']=False
            self.record['bootloader']=boot; self.save(); return dict(bootloader=boot)
        self.device('maintenance')
        result=read_inventory(self.record['serial'],self.package.hardware,verified=True)
        old=self.record.get('inventory')
        self.record['inventory']=result
        if old!=result: self.record['approved']=False
        app=result.get('application',{})
        self.record['installed_audio']=result.get('audio',dict(state='unknown'))
        fields=HEADER.unpack(self.package.image[-32:])
        if app.get('valid') and (app['bytes'],app['crc32'])==(fields[3],fields[4]):
            self.record['steps']['application']=dict(state='current',bytes=app['bytes'],crc32=app['crc32'])
        elif self.record['steps']['application']['state'] in ('current','complete','verifying'):
            self.record['steps']['application']=dict(state='ready' if app.get('valid') else 'unknown',reason='The installed application does not match this prepared package.')
        for key in ('module','receiver'):
            candidates=[rf_plan(result,i) for i,image in enumerate(IMAGES) if image['kind']==key]
            candidate=next((entry for entry in candidates if entry['state'] in ('current','available','ahead')),None)
            if candidate:
                previous=self.record['steps'][key]
                if previous['state'] not in ('staged','running','recovery_required'):
                    self.record['steps'][key]=dict(candidate,state='current' if candidate['state']=='current' else
                        'ready' if candidate['state']=='available' else 'blocked')
            elif self.record['steps'][key]['state'] not in ('staged','running','recovery_required'):
                self.record['steps'][key]=dict(state='pending' if key=='receiver' and not result['connected'] else 'unknown',
                    reason='Connect a supported receiver and read versions again.' if key=='receiver' else 'A compatible module identity is required.')
        for key in ('module','receiver'):
            step=self.record['steps'][key]; directory=self.root/(key+'-operation')
            if step['state'] in ('staged','running','recovery_required') and directory.is_dir():
                with connection(self.record['serial'],self.package.hardware,verified=True) as port:
                    transfer=RfTransfer(port,self.record['serial'],self.package.hardware,directory)
                    transfer.load(); status=transfer.reconcile()
                    state=transfer.record['state']
                    step.update(state='failed' if state=='failed_before_write' else state,result=status)
        self.save(); return result
    def select(self,selected):
        if set(selected)!=set(COMPONENTS) or any(type(v) is not bool for v in selected.values()):
            raise UpdateError('Review the complete component selection.')
        if any(step['state'] in ('running','recovery_required','staged') for step in self.record['steps'].values()):
            raise UpdateError('Finish, cancel or recover the existing RF operation before changing the plan.')
        if selected['audio'] and not (self.root/'audio.zip').is_file(): raise UpdateError('Select and verify one audio pack first.')
        self.record.update(selected=selected,approved=False); self.save()
    def approve(self):
        self.record['approved']=True; self.record['error']=None; self.save()
    def require_approval(self):
        if not self.record['approved']: raise UpdateError('Review and confirm this component plan first.')
        if not self.record.get('backup'): raise UpdateError('Create or attach the complete recovery backup before installing a component.')
        metadata=verify_backup(self.record['backup'])
        if metadata['identity']['serial']!=self.record['serial'] or metadata['target']!=VARIANTS[self.record['target']]:
            raise UpdateError('The recovery backup belongs to another transmitter.')
        return metadata
    def attach_backup(self,path):
        metadata=verify_backup(path)
        if (metadata['identity']['serial'],metadata['target'])!=(self.record['serial'],VARIANTS[self.record['target']]):
            raise UpdateError('The complete backup belongs to another transmitter.')
        first=False
        try: update_package((Path(path)/'internal.bin').read_bytes())
        except ValueError: first=True
        self.record.update(backup=str(Path(path).resolve()),first_installation=first,approved=False); self.save()
    def backup(self,progress):
        self.device('rom')
        path=self.package.root/'recovery/nb4-backup-agent.bin'
        digest=self.package.manifest['files']['recovery/nb4-backup-agent.bin']['sha256']
        output=self.root/('backup-'+uuid.uuid4().hex[:8])
        full_backup(path,digest,output,self.record['serial'],progress)
        self.attach_backup(output)
    def add_audio(self,path):
        from nb4_audio_install import safe_extract,read_and_verify_pack,MAX_ARCHIVE_BYTES
        import tempfile
        for operation in (self.root/'audio-backups').glob('*/install.json'):
            progress=operation.with_name('progress.json'); restored=operation.with_name('restore.json')
            if (not progress.is_file() or not json.loads(progress.read_text(encoding='utf-8')).get('complete')) and (
                    not restored.is_file() or json.loads(restored.read_text(encoding='utf-8')).get('state')!='complete'):
                raise UpdateError('Resume or restore the interrupted audio install before replacing its original package.')
        path=Path(path)
        if path.stat().st_size>MAX_ARCHIVE_BYTES: raise UpdateError('The audio package is too large.')
        with tempfile.TemporaryDirectory(prefix='apextx-audio-review-') as folder:
            safe_extract(path,Path(folder)); manifest,_=read_and_verify_pack(Path(folder))
        shutil.copyfile(path,self.root/'audio.zip')
        self.record['steps']['audio']=dict(state='ready',language=manifest['language'],release=manifest['release'],sha256=file_hash(self.root/'audio.zip'))
        self.record['selected']['audio']=True; self.record['approved']=False; self.save()
    def prepare_storage(self):
        self.require_approval(); self.device('maintenance')
        if any(self.record['steps'][key]['state']=='recovery_required' for key in ('module','receiver')):
            raise UpdateError('Recover the interrupted RF component before handing storage to USB.')
        nonce=bytes.fromhex(self.record['storage_nonce']) if self.record.get('storage_nonce') else secrets.token_bytes(16)
        with connection(self.record['serial'],self.package.hardware,verified=True) as port:
            client=TransferClient(port)
            if client.request(CAPABILITIES).get('storage_session') is not True:
                raise UpdateError('Install the maintenance build with storage-session support first.')
            result=client.request(STORAGE_SESSION,nonce)
            if result.get('nonce')!=nonce.hex() or result.get('storage_session') is not True:
                raise UpdateError('The radio did not verify the storage handover.')
        self.record['storage_nonce']=nonce.hex(); self.save()
    def volume(self,path):
        self.device('storage')
        return validate_storage_session(path,self.record['serial'],self.package.hardware,self.record.get('storage_nonce'))
    def dependencies(self,key):
        self.require_approval()
        if not self.record['selected'][key]: raise UpdateError('This component is not selected.')
        for parent in ('application','module'):
            if parent==key: break
            if self.record['selected'][parent] and self.record['steps'][parent]['state'] not in ('current','complete'):
                raise UpdateError('Finish or explicitly defer '+parent+' before '+key+'.')
    def application(self,progress):
        metadata=self.require_approval(); self.dependencies('application')
        device=probe()
        normal=False
        try: update_package((Path(self.record['backup'])/'internal.bin').read_bytes()); normal=True
        except ValueError: pass
        if not normal and self.record['target'] not in FIRST_INSTALL_QUALIFIED:
            raise UpdateError('First installation remains gated until complete rollback is demonstrated on this transmitter model.')
        if device.get('state')=='recovery':
            self.device('rom')
            if normal and self.record['steps']['application']['state']!='recovery_required' and self.record.get('bootloader',{}).get('state')!='available':
                raise UpdateError('Use ApexTX Update for a normal application update. ROM DFU is for a required bootloader change or recovery.')
        else:
            device=self.device('update')
            if self.record.get('bootloader',{}).get('state')=='pending':
                raise UpdateError('Read bootloader requirements in Update mode and review the component plan before installing.')
            if bootloader_required(device,image_bootloader_generation(self.package.image)):
                raise UpdateError('This application needs a newer bootloader. Use ROM DFU with the verified full backup.')
        self.record['steps']['application']=dict(state='running'); self.save()
        try:
            if device['state']=='recovery':
                nb4_rom_flash.run_install(self.package.image,progress,self.record['serial'])
                self.record['bootloader']=dict(state='current',current=image_bootloader_generation(self.package.image),proposed=image_bootloader_generation(self.package.image),verification='complete_readback')
            else: run_update(self.package.image,progress,serial=self.record['serial'])
            self.record['steps']['application']=dict(state='verifying',reason='Start normally, reconnect maintenance, then read versions to verify the installed application.')
        except Exception:
            self.record['steps']['application']=dict(state='recovery_required',reason='Inspect Update/ROM DFU and verify the application before retrying.'); raise
        finally: self.save()
    def rf(self,key,action,progress,*,reinstall=False):
        if key not in ('module','receiver'): raise UpdateError('Select a module or receiver operation.')
        self.dependencies(key); self.device('maintenance')
        step=self.record['steps'][key]
        with connection(self.record['serial'],self.package.hardware,verified=True) as port:
            transfer=RfTransfer(port,self.record['serial'],self.package.hardware,self.root/(key+'-operation'))
            if action in ('cancel','recover'):
                transfer.load()
                result=transfer.cancel() if action=='cancel' else transfer.recover(confirmed=True,progress=progress)
                step.update(state='ready' if action=='cancel' else 'complete',result=result)
                if action=='cancel': shutil.move(transfer.directory,self.root/(key+'-cancelled-'+uuid.uuid4().hex[:8]))
            else:
                inventory=transfer.inventory()
                index=step.get('image')
                if index is None: raise UpdateError('Read a compatible component identity and review the plan first.')
                candidate=rf_plan(inventory,index)
                previous=self.record.get('inventory')
                peer='receiver' if key=='receiver' else 'module'
                if (not previous or previous['car_key']!=inventory['car_key'] or previous[peer]!=inventory[peer]):
                    raise UpdateError('The component identity changed. Read versions and review the plan again.')
                if candidate['state']=='current' and not reinstall and action=='install':
                    step.update(state='current'); self.save(); return
                if candidate['state'] not in ('current','available'): raise UpdateError('No compatible upgrade is available.')
                if action=='install' and not candidate['write_supported']:
                    raise UpdateError('RF transfer/recovery is not qualified for this combination. Use the unit-bound bench build only for the documented test.')
                if step['state']!='staged':
                    if not self.package.catalog: raise UpdateError('This package does not contain verified RF images.')
                    if transfer.directory.exists():
                        transfer.load(); transfer.reconcile()
                        if transfer.record['state'] not in ('complete','cancelled'):
                            raise UpdateError('Inspect or cancel the previous component operation before preparing another.')
                        shutil.move(transfer.directory,self.root/(key+'-'+transfer.record['state']+'-'+uuid.uuid4().hex[:8]))
                    self.record['steps'][key]=dict(step,state='running'); self.save()
                    try:
                        result=transfer.stage(index,self.package.images[image_name(IMAGES[index])],self.record['backup'],progress=progress)
                    except Exception:
                        self.record['steps'][key].update(state='failed',reason='Preparation failed before entering the RF bootloader. Cancel the staged operation before retrying.'); self.save(); raise
                    step=self.record['steps'][key]; step.update(state='staged',result=result); self.save()
                else: transfer.load()
                if action=='install':
                    step['state']='running'; self.save()
                    try: result=transfer.start(confirmed=True,reinstall=reinstall,progress=progress)
                    except Exception:
                        step.update(state='recovery_required' if transfer.record.get('write_started') else 'failed'); self.save(); raise
                    step.update(state='complete',result=result)
        self.save()
    def resources(self,volume,progress=lambda *_:None):
        from nb4_resource_install import install
        self.dependencies('resources'); device=self.volume(volume)
        self.record['steps']['resources']['state']='running'; self.save()
        try: result=install(self.package.root,self.package.resources,volume,self.root/'resource-backup',device,confirmed=True,progress=progress)
        except Exception:
            self.record['steps']['resources'].update(state='failed',reason='Resume with the original resource package and backup.'); self.save(); raise
        self.record['steps']['resources']=result; self.save(); return result
    def audio(self,volume,progress=lambda *_:None):
        from nb4_audio_install import install,JOURNAL
        self.dependencies('audio'); device=self.volume(volume)
        step=self.record['steps']['audio']; archive=self.root/'audio.zip'
        if file_hash(archive)!=step.get('sha256'): raise UpdateError('The reviewed audio package changed.')
        step['state']='running'; self.save()
        try: result=install(archive,Path(volume),self.root/'audio-backups',True,resume=(Path(volume)/JOURNAL).exists(),device=device,progress=progress)
        except Exception:
            step.update(state='failed',reason='Resume the original audio package or restore its computer backup.'); self.save(); raise
        step.update(state='complete',backup=str(result.backup),preserved=result.preserved); self.save()
    def restore_nor(self,progress):
        from nb4_nor_restore import run
        self.require_approval(); self.device('rom')
        path=self.package.root/'recovery/nb4-nor-restore-agent.bin'
        operation=self.root/('nor-restore-'+uuid.uuid4().hex[:8])
        self.record['nor_restoration']=dict(state='running',operation=str(operation)); self.save()
        try:
            result=run(path,self.package.manifest['files']['recovery/nb4-nor-restore-agent.bin']['sha256'],
                       self.record['backup'],operation,self.record['serial'],confirmed=True,progress=progress)
        except Exception:
            receipt=operation/'restore.json'
            started=receipt.is_file() and json.loads(receipt.read_text()).get('write_started')
            self.record['nor_restoration']['state']='recovery_required' if started else 'failed_before_write'; self.save(); raise
        self.record['nor_restoration']=result; self.save()
    def restore_audio(self,volume,progress=lambda *_:None):
        from nb4_audio_install import restore_backup,JOURNAL
        self.require_approval(); device=self.volume(volume)
        path=self.record['steps']['audio'].get('backup')
        if not path:
            marker=Path(volume)/JOURNAL
            if not marker.is_file() or marker.is_symlink() or marker.stat().st_size>65536:
                raise UpdateError('There is no interrupted audio installation for this plan.')
            identifier=json.loads(marker.read_text(encoding='utf-8'),object_pairs_hook=unique_object).get('id','')
            if not re.fullmatch('apextx-audio-[0-9a-f]{32}',identifier): raise UpdateError('Invalid interrupted audio identity.')
            path=self.root/'audio-backups'/identifier
        self.record['steps']['audio']['state']='recovery_required'; self.save()
        result=restore_backup(Path(volume),Path(path),device,confirm=True,progress=progress)
        self.record['steps']['audio']=dict(state='deferred',restoration=result)
        self.record['selected']['audio']=False; self.record['approved']=False; self.save()
    def restore_application(self,progress):
        self.require_approval(); self.device('rom')
        self.record['internal_restoration']=dict(state='running'); self.save()
        try: nb4_rom_flash.restore_internal_backup(self.record['backup'],progress,self.record['serial'],confirmed=True)
        except Exception:
            self.record['internal_restoration']['state']='recovery_required'; self.save(); raise
        self.record['internal_restoration']=dict(state='complete',readback_verified=True); self.save()
