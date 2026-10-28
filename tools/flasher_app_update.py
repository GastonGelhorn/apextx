# SPDX-License-Identifier: GPL-2.0-only
"""Authenticated desktop updates. No USB imports or device operations."""
import base64
import hashlib
import json
import os
import platform
import re
import shutil
import ssl
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from nb4_releases import Version, _Redirects, _safe_url

REPOSITORY = 'GastonGelhorn/apextx'
API = 'https://api.github.com/repos/' + REPOSITORY
MANIFEST = 'apextx-flasher.update.json'
SIGNATURE = MANIFEST + '.sig'
MAX_MANIFEST = 128 * 1024
MAX_PACKAGE = 512 * 1024 * 1024
PROTOCOL = 1


class AppUpdateError(RuntimeError):
    def __init__(self, message, retry_seconds=3600):
        super().__init__(message)
        self.retry_seconds = retry_seconds


def json_object(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Duplicate JSON field')
            result[key] = value
        return result
    return json.loads(raw, object_pairs_hook=unique)


def app_version():
    folder = Path(__file__).resolve().parent
    if getattr(sys, 'frozen', False):
        return json_object((folder / 'apextx-flasher-build.json').read_bytes())['app_version']
    return (folder.parent / 'FLASHER_VERSION').read_text().strip()


def app_build():
    folder = Path(__file__).resolve().parent
    if getattr(sys, 'frozen', False):
        return json_object((folder / 'apextx-flasher-build.json').read_bytes())['app_build']
    return int((folder.parent / 'FLASHER_BUILD').read_text())


def trusted_keys():
    return json_object(Path(__file__).with_name('flasher-update-keys.json').read_bytes())


def verify_manifest(raw, signature, keys=None):
    """Verify exact bytes before parsing any publication-controlled content."""
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    if len(raw) > MAX_MANIFEST or len(signature) != 64:
        raise AppUpdateError('Invalid app update signature or manifest size.')
    for encoded in (trusted_keys() if keys is None else keys).values():
        try:
            Ed25519PublicKey.from_public_bytes(base64.b64decode(encoded, validate=True)).verify(signature, raw)
            break
        except (InvalidSignature, ValueError):
            continue
    else:
        raise AppUpdateError('The ApexTX Flasher update signature is not trusted.')
    try:
        doc = json_object(raw)
        version = Version(doc['version'])
        if (doc['schema'] != 1 or doc['product'] != 'org.apextx.flasher' or
                doc['channel'] != ('preview' if version.preview else 'stable') or
                type(doc['build']) is not int or doc['build'] < 1 or
                doc['updater_protocol'] != PROTOCOL or
                not isinstance(doc['notes'], str) or len(doc['notes']) > 16000 or
                not re.fullmatch(r'[0-9a-f]{40}', doc['revision']) or
                not isinstance(doc['packages'], dict) or not doc['packages']):
            raise ValueError()
        for target, item in doc['packages'].items():
            if target not in ('windows-x86_64', 'macos-arm64', 'macos-x86_64'):
                raise ValueError()
            suffix = '.exe' if target.startswith('windows-') else '.zip'
            if (item['name'] != f'ApexTX-Flasher-{version.text}-{target}{suffix}' or
                    type(item['size']) is not int or not 0 < item['size'] <= MAX_PACKAGE or
                    not re.fullmatch(r'[0-9a-f]{64}', item['sha256']) or
                    not re.fullmatch(r'[0-9]+(?:\.[0-9]+){0,2}', item['minimum_os'])):
                raise ValueError()
        return doc
    except (KeyError, TypeError, ValueError) as error:
        raise AppUpdateError('Invalid or unsupported ApexTX Flasher update metadata.') from error


def target_platform():
    # Follow the packaged runtime's architecture. An Intel build under Rosetta
    # stays Intel; migration to native ARM is deliberate, never guessed.
    arch = platform.machine().lower()
    arch = {'amd64': 'x86_64', 'aarch64': 'arm64'}.get(arch, arch)
    if sys.platform == 'win32':
        # Emulated x64 on Windows ARM has not been qualified with our USB tools.
        import ctypes
        from ctypes import wintypes
        process, native = wintypes.WORD(), wintypes.WORD()
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.GetCurrentProcess.restype = wintypes.HANDLE
        kernel.IsWow64Process2.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.WORD), ctypes.POINTER(wintypes.WORD)]
        if not kernel.IsWow64Process2(kernel.GetCurrentProcess(), ctypes.byref(process), ctypes.byref(native)):
            return None
        return 'windows-x86_64' if native.value == 0x8664 and arch == 'x86_64' else None
    if sys.platform == 'darwin' and arch in ('arm64', 'x86_64'):
        return 'macos-' + arch
    return None


def os_compatible(minimum):
    current = platform.mac_ver()[0] if sys.platform == 'darwin' else platform.win32_ver()[1]
    def parts(value):
        return tuple((list(map(int, value.split('.'))) + [0, 0, 0])[:3])
    return bool(current) and parts(current) >= parts(minimum)


def data_directory():
    if sys.platform == 'win32':
        base = Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local'))
    elif sys.platform == 'darwin':
        base = Path.home() / 'Library/Application Support'
    else:
        base = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    return base / 'ApexTX Flasher'


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd, temporary = tempfile.mkstemp(prefix='.' + path.name, dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as stream:
            json.dump(value, stream, ensure_ascii=False)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


class InstanceLease:
    """One radio-capable process per user, including CLI and alternate copies."""
    def __init__(self, directory=None, update_nonce=None):
        folder = directory or data_directory()
        folder.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.stream = (folder / 'radio-session.lock').open('a+b')
        try:
            if sys.platform == 'win32':
                import msvcrt
                self.stream.seek(0)
                self.stream.write(b'0')
                self.stream.flush()
                self.stream.seek(0)
                msvcrt.locking(self.stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            self.stream.close()
            raise AppUpdateError('Another ApexTX Flasher is open. Close it before continuing.') from error

        try:
            active = json_object((folder / 'active-update.json').read_bytes())
            from flasher_app_installer import parent_alive
            if parent_alive(active['helper']) and active.get('nonce') != update_nonce:
                self.close()
                raise AppUpdateError('An ApexTX Flasher replacement is in progress. Wait for it to finish.')
        except FileNotFoundError:
            pass
        except (ValueError, KeyError, TypeError) as error:
            self.close()
            raise AppUpdateError('The app replacement journal needs recovery.') from error

    def close(self):
        self.stream.close()


class AppReleaseClient:
    def __init__(self, opener=None):
        if opener is None:
            try:
                import certifi
                context = ssl.create_default_context(cafile=certifi.where())
            except ImportError:
                context = ssl.create_default_context()
            context.set_alpn_protocols(['http/1.1'])
            opener = urllib.request.build_opener(_Redirects(), urllib.request.HTTPSHandler(context=context))
        self.opener = opener
        self.cache = {}

    def fetch(self, url, limit, destination=None, progress=None):
        if not _safe_url(url):
            raise AppUpdateError('Invalid app release URL.')
        request = urllib.request.Request(url, headers={'User-Agent': 'ApexTX-Flasher/' + app_version(),
            'Accept': 'application/vnd.github+json' if url.startswith(API + '/') else 'application/octet-stream'})
        if url in self.cache:
            request.add_header('If-None-Match', self.cache[url][0])
        try:
            with self.opener.open(request, timeout=10) as response:
                length = int(response.headers.get('Content-Length', '0'))
                if not 0 <= length <= limit:
                    raise AppUpdateError('App download is too large.')
                deadline, count, chunks = time.monotonic() + (900 if destination else 90), 0, []
                while True:
                    chunk = response.read(min(65536, limit + 1 - count))
                    if not chunk:
                        break
                    count += len(chunk)
                    if count > limit or time.monotonic() > deadline:
                        raise AppUpdateError('App download exceeded its size or time limit.')
                    if destination:
                        destination.write(chunk)
                    else:
                        chunks.append(chunk)
                    if progress:
                        progress(count * 100 // limit)
                if length and count != length:
                    raise AppUpdateError('Incomplete app download. Try again.')
                result = count if destination else b''.join(chunks)
                if not destination and url.startswith(API + '/') and response.headers.get('ETag'):
                    self.cache[url] = (response.headers['ETag'], result)
                return result
        except urllib.error.HTTPError as error:
            error.close()
            if error.code == 304 and url in self.cache:
                return self.cache[url][1]
            if error.code in (403, 429):
                try:
                    retry = max(int(error.headers.get('Retry-After', '3600')), int(error.headers.get('X-RateLimit-Reset', '0')) - int(time.time()))
                except ValueError:
                    retry = 3600
                raise AppUpdateError('GitHub limited app checks. Try again later.', max(60, min(retry, 86400))) from error
            raise AppUpdateError('App release is unavailable. Try again later.') from error
        except (OSError, ValueError) as error:
            raise AppUpdateError('Could not reach GitHub. Local radio operations are still available.') from error

    def latest(self, target, preview, installed, highest_build=0):
        candidates = []
        for page in range(1, 11):
            rows = json_object(self.fetch(f'{API}/releases?per_page=100&page={page}', 4 * 1024 * 1024))
            if not isinstance(rows, list):
                raise AppUpdateError('Invalid app release list.')
            for row in rows:
                if row.get('draft') or not str(row.get('tag_name', '')).startswith('flasher-v'):
                    continue
                try:
                    version = Version(row['tag_name'][9:])
                except ValueError:
                    continue
                if version <= Version(installed) or ((version.preview or row.get('prerelease')) and not preview):
                    continue
                assets = {a['name']: a for a in row.get('assets', []) if a.get('state') == 'uploaded'}
                if len(assets) != len([a for a in row.get('assets', []) if a.get('state') == 'uploaded']):
                    raise AppUpdateError('Duplicate app release assets.')
                def asset_url(name):
                    item = assets.get(name)
                    url = f'https://github.com/{REPOSITORY}/releases/download/{row["tag_name"]}/{name}'
                    if not item or item.get('browser_download_url') != url:
                        raise AppUpdateError('Incomplete app release or unexpected asset URL.')
                    return url
                raw = self.fetch(asset_url(MANIFEST), MAX_MANIFEST)
                signature = self.fetch(asset_url(SIGNATURE), 64)
                doc = verify_manifest(raw, signature)
                if doc['version'] != version.text:
                    raise AppUpdateError('App release identity or anti-rollback check failed.')
                package = doc['packages'].get(target)
                if target and not package:
                    raise AppUpdateError('This app release has no package for this platform.')
                if package:
                    url = asset_url(package['name'])
                    if assets[package['name']]['size'] != package['size']:
                        raise AppUpdateError('App asset size does not match its signed metadata.')
                else:
                    url = None
                candidates.append((version, dict(document=doc, raw=raw, signature=signature,
                    package=package, url=url, compatible=bool(package and os_compatible(package['minimum_os'])))))
            if len(rows) < 100:
                break
        else:
            raise AppUpdateError('App release list is incomplete. Try again later.')
        offer = max(candidates, key=lambda entry: entry[0])[1] if candidates else None
        if offer and offer['document']['build'] < highest_build:
            raise AppUpdateError('The app release failed the anti-rollback check.')
        return offer

    def download(self, offer, folder, progress):
        item = offer['package']
        if shutil.disk_usage(folder).free < item['size'] * 4 + 64 * 1024 * 1024:
            raise AppUpdateError('Not enough free space to update ApexTX Flasher safely.')
        partial = folder / 'download.part'
        try:
            with partial.open('xb') as stream:
                count = self.fetch(offer['url'], item['size'], stream, progress)
                stream.flush()
                os.fsync(stream.fileno())
            if count != item['size'] or file_digest(partial) != item['sha256']:
                raise AppUpdateError('App checksum verification failed. The installed app was preserved.')
            complete = folder / item['name']
            partial.rename(complete)
            (folder / MANIFEST).write_bytes(offer['raw'])
            (folder / SIGNATURE).write_bytes(offer['signature'])
            return complete
        finally:
            partial.unlink(missing_ok=True)


def file_digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


class AppUpdateController:
    def __init__(self, session, restart, directory=None, client=None):
        self.session, self.restart = session, restart
        self.directory = directory or data_directory()
        self.preferences_path = self.directory / 'updates.json'
        try:
            self.preferences = json_object(self.preferences_path.read_bytes())
            if not isinstance(self.preferences, dict):
                raise ValueError()
        except (OSError, ValueError):
            self.preferences = {}
        self.preview = bool(self.preferences.get('preview', Version(app_version()).preview))
        self.offer = None
        self.state = dict(state='idle', version=app_version(), progress=0, error=None,
                          last_check=self.preferences.get('last_check'), preview=self.preview)
        self.stopped = threading.Event()
        self.client = client or AppReleaseClient()
        self.checking = False
        self.next_check = self.preferences.get('last_check', 0) + 86400
        cached = self.preferences.get('offer')
        if cached:
            try:
                raw = base64.b64decode(cached['raw'], validate=True)
                signature = base64.b64decode(cached['signature'], validate=True)
                doc = verify_manifest(raw, signature)
                package = doc['packages'].get(target_platform())
                if (Version(doc['version']) > Version(app_version()) and
                        (self.preview or not Version(doc['version']).preview) and doc['build'] >= app_build()):
                    self.offer = dict(document=doc, raw=raw, signature=signature, package=package,
                        url=(f'https://github.com/{REPOSITORY}/releases/download/flasher-v{doc["version"]}/'+package['name']) if package else None,
                        compatible=bool(package and os_compatible(package['minimum_os'])))
                    self.state['state'] = 'available'
            except (ValueError, KeyError, TypeError, AppUpdateError):
                self.preferences.pop('offer', None)
        try:
            self.state['last_result'] = json_object((self.directory / 'last-update.json').read_bytes())
        except (OSError, ValueError):
            pass

    def snapshot(self):
        with self.session.lock:
            result = dict(self.state, preview=self.preview, supported=bool(target_platform() and getattr(sys, 'frozen', False)),
                          blocked=self.session.app_restart_blocked())
            if self.offer:
                doc = self.offer['document']
                result.update(available=doc['version'], notes=doc['notes'], compatible=self.offer['compatible'],
                    hidden=(self.preferences.get('skip') == doc['version'] or time.time() < self.preferences.get('snooze', 0)))
            return result

    def save(self):
        atomic_json(self.preferences_path, self.preferences)

    def monitor(self):
        while not self.stopped.wait(2):
            with self.session.lock:
                due = (time.time() >= self.next_check and not self.session.app_restart_blocked()
                       and not self.session.app_updating and not self.checking)
            if due:
                self.check()

    def check(self, preview=None, manual=False):
        with self.session.lock:
            if self.session.app_updating or self.checking:
                raise AppUpdateError('An app check or update is already in progress.')
            if preview is not None:
                if not isinstance(preview, bool):
                    raise AppUpdateError('Choose an app release channel.')
                if preview != self.preview:
                    self.offer = None
                    self.preferences.pop('offer', None)
                self.preview = preview
                self.preferences['preview'] = preview
                self.save()
            self.checking = True
            self.state.update(state='checking', error=None)
            self.next_check = time.time() + 3600
        def work():
            try:
                offer = self.client.latest(target_platform(), self.preview, app_version(),
                                           max(app_build(), self.preferences.get('highest_preview_build' if self.preview else 'highest_stable_build', 0)))
                with self.session.lock:
                    self.offer = offer
                    self.preferences['last_check'] = time.time()
                    if offer:
                        self.preferences['offer'] = dict(raw=base64.b64encode(offer['raw']).decode(), signature=base64.b64encode(offer['signature']).decode())
                        self.preferences['highest_preview_build' if self.preview else 'highest_stable_build'] = max(app_build(), self.preferences.get('highest_preview_build' if self.preview else 'highest_stable_build', 0), offer['document']['build'])
                    else:
                        self.preferences.pop('offer', None)
                    if manual:
                        self.preferences.pop('skip', None)
                        self.preferences.pop('snooze', None)
                    self.save()
                    self.next_check = time.time() + 86400
                    self.state.update(state='available' if offer else 'current', last_check=self.preferences['last_check'])
            except Exception as error:
                with self.session.lock:
                    self.next_check = time.time() + getattr(error, 'retry_seconds', 3600)
                    self.state.update(state='error', error=str(error))
            finally:
                with self.session.lock:
                    self.checking = False
        threading.Thread(target=work, daemon=True).start()

    def defer(self, skip=False):
        with self.session.lock:
            if self.session.app_updating:
                raise AppUpdateError('An app update is in progress.')
            if skip and self.offer:
                self.preferences['skip'] = self.offer['document']['version']
            else:
                self.preferences['snooze'] = time.time() + 86400
            self.save()

    def install(self, version, relocate=False):
        with self.session.lock:
            if (self.checking or not self.offer or self.offer['document']['version'] != version or
                    not self.offer['compatible'] or not getattr(sys, 'frozen', False)):
                raise AppUpdateError('Check the compatible ApexTX Flasher release again.')
            self.directory.mkdir(parents=True, exist_ok=True, mode=0o700)
            self.session.reserve_app_update()
            offer = self.offer
            self.state.update(state='downloading', error=None, progress=0)
        def work():
            try:
                from flasher_app_installer import prepare, start_helper
                # A signed, fully downloaded package remains usable offline
                # after a permission/prepare failure or a previous app restart.
                folder = None
                for candidate in self.directory.glob('update-*'):
                    try:
                        package = candidate / offer['package']['name']
                        if (package.stat().st_size == offer['package']['size'] and
                                (candidate / MANIFEST).read_bytes() == offer['raw'] and
                                (candidate / SIGNATURE).read_bytes() == offer['signature'] and
                                file_digest(package) == offer['package']['sha256']):
                            folder = candidate
                            break
                    except OSError:
                        continue
                if folder is None:
                    folder = Path(tempfile.mkdtemp(prefix='update-', dir=self.directory))
                    self.client.download(offer, folder, self.progress)
                self.state['state'] = 'verifying'
                transaction = prepare(folder, offer, relocate)
                start_helper(transaction)
                with self.session.lock:
                    # Reservation prevents every device operation until exit.
                    self.state['state'] = 'restarting'
                self.restart()
            except Exception as error:
                with self.session.lock:
                    self.session.app_updating = False
                    self.state.update(state='error', error=str(error))
        threading.Thread(target=work, daemon=False).start()

    def progress(self, value):
        with self.session.lock:
            self.state['progress'] = value
