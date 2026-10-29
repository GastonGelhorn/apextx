# SPDX-License-Identifier: GPL-2.0-only
"""Detached app-only replacement, rollback and startup handshake. Never uses USB."""
import ctypes
import json
import os
import plistlib
import secrets
import shutil
import stat
import subprocess
import sys
import time
import zipfile
from pathlib import Path, PurePosixPath

from flasher_app_update import (AppUpdateError, RelocationRequired, MANIFEST, SIGNATURE, atomic_json,
    data_directory, file_digest, json_object, target_platform, verify_manifest, InstanceLease)

EXECUTABLE = 'Contents/MacOS/ApexTX-Flasher'


def executable(app):
    return app / EXECUTABLE if app.suffix == '.app' else app


def installed_app():
    path = Path(sys.executable).resolve()
    if sys.platform == 'darwin':
        if path.parts[-3:] != ('Contents', 'MacOS', 'ApexTX-Flasher'):
            raise AppUpdateError('The running app bundle could not be identified.')
        return path.parents[2]
    return path


def clean_environment():
    env = {k: v for k, v in os.environ.items() if not k.startswith(('PYTHON', 'DYLD_', '_PYI_'))}
    env['PYINSTALLER_RESET_ENVIRONMENT'] = '1'
    return env


def extract_bundle(archive, directory):
    """Bounded ZIP extraction; permit only internal relative bundle symlinks."""
    with zipfile.ZipFile(archive) as source:
        entries = source.infolist()
        if len(entries) > 30000 or sum(e.file_size for e in entries) > 2 * 1024**3:
            raise AppUpdateError('App archive exceeds extraction limits.')
        seen, links = set(), []
        for entry in entries:
            # ZipInfo normalizes Windows separators and truncates NULs in
            # filename; inspect the original decoded name before using it.
            if '\\' in entry.orig_filename or '\x00' in entry.orig_filename:
                raise AppUpdateError('Unsafe original path in app archive.')
            path = PurePosixPath(entry.filename)
            # ditto adds resource metadata; it is not executable bundle content.
            if path.parts and path.parts[0] == '__MACOSX':
                continue
            if (not path.parts or path.parts[0] != 'ApexTX-Flasher.app' or path.is_absolute() or
                    any(p in ('..', '.') for p in path.parts) or '\\' in entry.filename or
                    '\x00' in entry.filename or str(path).casefold() in seen):
                raise AppUpdateError('Unsafe or duplicate path in app archive.')
            seen.add(str(path).casefold())
            mode = entry.external_attr >> 16
            destination = directory.joinpath(*path.parts)
            if stat.S_ISLNK(mode):
                target = source.read(entry).decode('utf-8')
                resolved = (destination.parent / target).resolve()
                bundle = directory / 'ApexTX-Flasher.app'
                if Path(target).is_absolute() or not resolved.is_relative_to(bundle):
                    raise AppUpdateError('App symlink escapes its bundle.')
                links.append((destination, target))
            elif entry.is_dir():
                destination.mkdir(parents=True, exist_ok=True)
            elif stat.S_IFMT(mode) in (0, stat.S_IFREG):
                destination.parent.mkdir(parents=True, exist_ok=True)
                with source.open(entry) as incoming, destination.open('xb') as outgoing:
                    shutil.copyfileobj(incoming, outgoing, 65536)
                destination.chmod(0o755 if mode & 0o111 else 0o644)
            else:
                raise AppUpdateError('Unsupported special file in app archive.')
        for destination, target in links:
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.exists():
                raise AppUpdateError('Conflicting app symlink.')
            destination.symlink_to(target)
    app = directory / 'ApexTX-Flasher.app'
    with (app / 'Contents/Info.plist').open('rb') as stream:
        if plistlib.load(stream).get('CFBundleIdentifier') != 'org.apextx.flasher':
            raise AppUpdateError('Unexpected macOS app identity.')
    if not executable(app).is_file():
        raise AppUpdateError('The app executable is missing.')
    subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(app)],
                   check=True, capture_output=True, timeout=60)
    return app


def swap(old, new):
    """Same-volume replacement; never delete the working app first."""
    if sys.platform == 'darwin':
        libc = ctypes.CDLL(None, use_errno=True)
        rename_swap = libc.renameatx_np
        rename_swap.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        if rename_swap(-2, os.fsencode(old), -2, os.fsencode(new), 2) != 0:  # RENAME_SWAP
            raise OSError(ctypes.get_errno(), 'Atomic bundle exchange failed')
    elif sys.platform == 'win32':
        from ctypes import wintypes
        replace = ctypes.WinDLL('kernel32', use_last_error=True).ReplaceFileW
        replace.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.LPCWSTR,
                            wintypes.DWORD, wintypes.LPVOID, wintypes.LPVOID]
        if not replace(str(old), str(new), None, 0, None, None):
            raise ctypes.WinError(ctypes.get_last_error())
    else:
        raise AppUpdateError('App replacement is supported only on Windows and macOS.')


def prepare(folder, offer, relocate=False):
    doc = verify_manifest((folder / MANIFEST).read_bytes(), (folder / SIGNATURE).read_bytes())
    item = doc['packages'][target_platform()]
    archive = folder / item['name']
    if archive.stat().st_size != item['size'] or file_digest(archive) != item['sha256']:
        raise AppUpdateError('The downloaded app changed before installation.')
    current = installed_app()
    destination = current
    if relocate:
        destination = ((Path.home() / 'Applications/ApexTX-Flasher.app') if sys.platform == 'darwin'
                       else data_directory().parent / 'Programs/ApexTX Flasher/ApexTX-Flasher.exe')
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination != current and destination.exists():
            raise AppUpdateError('The user installation already exists. Open that copy to update it.')
    if (sys.platform == 'darwin' and (str(current).startswith('/Volumes/') or '/AppTranslocation/' in str(current))
            and not relocate):
        raise RelocationRequired('This copy runs from a disk image or download and is installed in the user folder instead.')
    # Staging in the destination's parent makes atomic replacement possible.
    try:
        stage = destination.parent / ('.apextx-update-' + secrets.token_hex(12))
        stage.mkdir(mode=0o700)
    except OSError as error:
        raise RelocationRequired('The app folder is not writable; the update is installed in the user folder instead.') from error
    if shutil.disk_usage(stage).free < item['size'] * 4 + 64 * 1024**2:
        raise AppUpdateError('Not enough space in the installation folder.')
    if sys.platform == 'darwin':
        payload = extract_bundle(archive, stage)
        runner = folder / 'Updater.app'
        # An entire copy is intentional: it survives replacement and does not
        # depend on libraries inside the bundle it is about to exchange.
        subprocess.run(['/usr/bin/ditto', str(current), str(runner)], check=True, timeout=120)
    else:
        payload = stage / 'ApexTX-Flasher.exe'
        shutil.copy2(archive, payload)
        runner = folder / 'Updater.exe'
        shutil.copy2(current, runner)
    # New code runs only after authentication, with no USB discovery.
    report = stage / 'self-test.json'
    subprocess.run([str(executable(payload)), '--self-test', str(report)],
                   check=True, timeout=90, env=clean_environment(), capture_output=True)
    result = json_object(report.read_bytes())
    if (result.get('app_version') != doc['version'] or result.get('revision') != doc['revision'] or
            result.get('architecture', '').lower().replace('amd64', 'x86_64') != target_platform().split('-', 1)[1]):
        raise AppUpdateError('The app does not match its signed build identity.')
    backup = stage / ('Previous.app' if sys.platform == 'darwin' else 'Previous.exe')
    if sys.platform == 'win32' and destination == current:
        shutil.copy2(current, backup)
    transaction = dict(schema=1, phase='prepared', nonce=secrets.token_hex(32),
        parent=os.getpid(), parent_identity=process_token(os.getpid()), current=str(current), destination=str(destination),
        payload=str(payload), backup=str(backup), runner=str(runner), version=doc['version'],
        revision=doc['revision'], platform=sys.platform, old_digest=file_digest(executable(current)),
        new_digest=file_digest(executable(payload)))
    atomic_json(folder / 'transaction.json', transaction)
    return folder / 'transaction.json'


def read_transaction(path):
    path = Path(path)
    if path.is_symlink():
        raise AppUpdateError('The app transaction cannot be a symlink.')
    path = path.resolve()
    root = data_directory().resolve()
    if not path.parent.is_relative_to(root) or path.name != 'transaction.json' or path.is_symlink():
        raise AppUpdateError('Invalid app transaction location.')
    doc = json_object(path.read_bytes())
    if doc.get('schema') != 1 or doc.get('platform') != sys.platform:
        raise AppUpdateError('Unsupported app transaction.')
    current, destination, payload = (Path(doc[key]) for key in ('current', 'destination', 'payload'))
    if (not all(p.is_absolute() and p == p.resolve() for p in (current, destination, payload)) or
            not payload.parent.name.startswith('.apextx-update-') or
            payload.parent.parent != destination.parent or
            destination.suffix != current.suffix or not isinstance(doc.get('nonce'), str) or len(doc['nonce']) != 64):
        raise AppUpdateError('Unsafe app replacement paths.')
    backup = Path(doc['backup'])
    if backup.parent != payload.parent or Path(doc['runner']).parent != path.parent:
        raise AppUpdateError('Unsafe app recovery paths.')
    return doc


def start_helper(transaction):
    doc = read_transaction(transaction)
    process = subprocess.Popen([str(executable(Path(doc['runner']))), '--apply-app-update', str(transaction), doc['nonce']],
        env=clean_environment(), cwd=transaction.parent,
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        start_new_session=(sys.platform != 'win32'))
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        ready = transaction.parent / 'helper-ready.json'
        if ready.exists() and json_object(ready.read_bytes()).get('nonce') == doc['nonce']:
            atomic_json(transaction.parent / 'shutdown-authorized.json', dict(nonce=doc['nonce']))
            return
        if process.poll() is not None:
            break
        time.sleep(.1)
    doc['phase'] = 'cancelled'
    atomic_json(transaction, doc)
    raise AppUpdateError('The update helper could not start. The installed app was preserved.')


def process_token(pid):
    """Bind a PID to its creation time, so reboot/PID reuse cannot block recovery."""
    try:
        if sys.platform == 'win32':
            from ctypes import wintypes
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            kernel.OpenProcess.restype = wintypes.HANDLE
            kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
            kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            handle = kernel.OpenProcess(0x1000, False, pid)
            if not handle:
                return None
            try:
                values = [wintypes.FILETIME() for _ in range(4)]
                if not kernel.GetProcessTimes(handle, *(ctypes.byref(value) for value in values)):
                    return None
                return str(values[0].dwHighDateTime << 32 | values[0].dwLowDateTime)
            finally:
                kernel.CloseHandle(handle)
        if sys.platform.startswith('linux'):
            boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
            fields = Path(f'/proc/{pid}/stat').read_text().rpartition(') ')[2].split()
            return boot + ':' + fields[19]
        result = subprocess.run(['/bin/ps', '-p', str(pid), '-o', 'lstart='],
            capture_output=True, text=True, timeout=5,
            env=dict(clean_environment(), LC_ALL='C', TZ='UTC'))
        return result.stdout.strip() or None
    except (OSError, ValueError, IndexError, subprocess.TimeoutExpired):
        return None


def parent_alive(pid, identity=None):
    if identity is not None:
        token = process_token(pid)
        if token is not None and token != identity:
            return False
    if sys.platform == 'win32':
        from ctypes import wintypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.OpenProcess(0x1000, False, pid)
        if not handle:
            return False
        try:
            code = wintypes.DWORD()
            return not kernel.GetExitCodeProcess(handle, ctypes.byref(code)) or code.value == 259
        finally:
            kernel.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False


def apply_update(transaction, nonce):
    """Called only by the copied helper; never terminates any running app."""
    transaction = Path(transaction)
    doc = read_transaction(transaction)
    if not secrets.compare_digest(nonce, doc['nonce']) or doc['phase'] != 'prepared':
        raise AppUpdateError('Invalid app replacement authorization.')
    raw, signature = ((transaction.parent / name).read_bytes() for name in (MANIFEST, SIGNATURE))
    manifest = verify_manifest(raw, signature)
    if manifest['version'] != doc['version'] or manifest['revision'] != doc['revision']:
        raise AppUpdateError('The app transaction identity changed.')
    staged = Path(doc['payload'])
    if file_digest(executable(staged)) != doc['new_digest']:
        raise AppUpdateError('The staged app changed before helper authorization.')
    if sys.platform == 'darwin':
        subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(staged)], check=True, capture_output=True, timeout=60)
    doc['helper'] = os.getpid()
    doc['helper_identity'] = process_token(os.getpid())
    atomic_json(transaction, doc)
    atomic_json(data_directory() / 'active-update.json', dict(helper=os.getpid(), identity=doc['helper_identity'], nonce=nonce))
    atomic_json(transaction.parent / 'helper-ready.json', dict(nonce=nonce))
    deadline = time.monotonic() + 120
    while parent_alive(doc['parent'], doc.get('parent_identity')):
        if time.monotonic() > deadline:
            raise AppUpdateError('The flasher is still running. No files were replaced.')
        time.sleep(.2)
    try:
        authorized = json_object((transaction.parent / 'shutdown-authorized.json').read_bytes())
    except (OSError, ValueError) as error:
        raise AppUpdateError('The original app did not authorize shutdown. No files were replaced.') from error
    if authorized.get('nonce') != nonce:
        raise AppUpdateError('The original app shutdown authorization does not match.')
    lease = InstanceLease(update_nonce=nonce)
    current, destination, payload = (Path(doc[key]) for key in ('current', 'destination', 'payload'))
    try:
        # Recheck the exact staged code and whole download after the parent exits.
        package = manifest['packages'][target_platform()]
        archive = transaction.parent / package['name']
        if (file_digest(archive) != package['sha256'] or file_digest(executable(payload)) != doc['new_digest'] or
                file_digest(executable(current)) != doc['old_digest']):
            raise AppUpdateError('App files changed. Replacement was cancelled.')
        doc['phase'] = 'replacing'
        atomic_json(transaction, doc)
        if destination == current:
            # Antivirus may hold the Windows executable after process exit.
            for attempt in range(50):
                try:
                    swap(destination, payload)
                    break
                except OSError:
                    if attempt == 49:
                        raise
                    time.sleep(.2)
        else:
            payload.rename(destination)
        doc['phase'] = 'starting'
        atomic_json(transaction, doc)
        lease.close()
        process = subprocess.Popen([str(executable(destination)), '--app-update-start', str(transaction), nonce],
            env=clean_environment(), cwd=str(Path.home()))
        deadline = time.monotonic() + 90
        health = transaction.parent / 'healthy.json'
        while time.monotonic() < deadline:
            if health.exists() and json_object(health.read_bytes()).get('nonce') == nonce:
                atomic_json(data_directory() / 'last-update.json', dict(version=doc['version'], result='updated', location=str(destination)))
                doc['phase'] = 'confirmed'
                atomic_json(transaction, doc)
                return
            if process.poll() is not None:
                raise AppUpdateError('The new app could not start.')
            time.sleep(.2)
        # Do not kill it: it might have become interactive. Recovery occurs on
        # the next launch after the process has exited.
        doc['phase'] = 'unconfirmed'
        atomic_json(transaction, doc)
        return
    except Exception as error:
        lease.close()
        # Never roll back under an independently opened radio session.
        recovery_lease = InstanceLease(update_nonce=nonce)
        recovery_lease.close()
        rollback(transaction, doc, str(error))
    finally:
        lease.close()
        active = data_directory() / 'active-update.json'
        if active.exists() and json_object(active.read_bytes()).get('nonce') == nonce:
            active.unlink()


def rollback(transaction, doc, reason, relaunch=True):
    destination, payload = Path(doc['destination']), Path(doc['payload'])
    current, backup = Path(doc['current']), Path(doc['backup'])
    if destination != current:
        # The original was never changed during relocation.
        pass
    elif not destination.exists() and backup.is_file():
        backup.rename(destination)
    elif executable(destination).exists() and file_digest(executable(destination)) == doc['new_digest']:
        if sys.platform == 'darwin':
            swap(destination, payload)
        else:
            swap(destination, backup)
    doc.update(phase='rolled-back', error=reason)
    atomic_json(transaction, doc)
    atomic_json(data_directory() / 'last-update.json', dict(version=doc['version'], result='rolled-back', error=reason))
    active = data_directory() / 'active-update.json'
    if active.exists() and json_object(active.read_bytes()).get('nonce') == doc['nonce']:
        active.unlink()
    if relaunch:
        subprocess.Popen([str(executable(current))], env=clean_environment(), cwd=str(Path.home()))


def startup_handshake(transaction, nonce, version):
    path = Path(transaction)
    doc = read_transaction(path)
    if (not secrets.compare_digest(nonce, doc['nonce']) or doc['phase'] != 'starting' or
            doc['version'] != version or installed_app() != Path(doc['destination'])):
        raise AppUpdateError('Invalid updated-app startup handshake.')
    def confirm():
        atomic_json(path.parent / 'healthy.json', dict(nonce=nonce))
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if json_object(path.read_bytes()).get('phase') == 'confirmed':
                return
            time.sleep(.1)
        raise AppUpdateError('The helper did not confirm app startup. Radio operations remain blocked.')
    return confirm


def recover_interrupted():
    """Run under the instance lease before USB discovery on ordinary startup."""
    root = data_directory()
    if not root.exists():
        return False
    for path in root.glob('update-*/transaction.json'):
        doc = read_transaction(path)
        if doc['phase'] in ('replacing', 'starting', 'unconfirmed') and not parent_alive(doc.get('helper', doc['parent']), doc.get('helper_identity')):
            if Path(doc['destination']) == installed_app():
                rollback(path, doc, 'The previous app update was interrupted before startup confirmation.', relaunch=False)
                return True
    return False


def run_helper(transaction, nonce):
    """Keep launch failures visible even when the windowed helper has no console."""
    try:
        apply_update(transaction, nonce)
    except Exception as error:
        doc = read_transaction(transaction)
        atomic_json(data_directory() / 'last-update.json', dict(version=doc['version'], result='failed', error=str(error)))
        current = Path(doc['current'])
        active = data_directory() / 'active-update.json'
        if active.exists() and json_object(active.read_bytes()).get('nonce') == nonce:
            active.unlink()
        if not parent_alive(doc['parent'], doc.get('parent_identity')) and file_digest(executable(current)) == doc['old_digest']:
            lease = InstanceLease()
            lease.close()
            subprocess.Popen([str(executable(current))], env=clean_environment(), cwd=str(Path.home()))
        # The original app's panel displays the recorded failure after relaunch.
        # If it cannot relaunch, retain the journal and show a native message.
        elif not parent_alive(doc['parent'], doc.get('parent_identity')):
            message = 'ApexTX Flasher could not finish its app update. Reopen it to recover. ' + str(error)
            if sys.platform == 'win32':
                ctypes.windll.user32.MessageBoxW(None, message, 'ApexTX Flasher', 0x10)
            else:
                subprocess.run(['/usr/bin/osascript', '-e', 'on run argv\ndisplay alert "ApexTX Flasher" message (item 1 of argv)\nend run', message], check=False)
