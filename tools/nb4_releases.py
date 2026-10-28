# SPDX-License-Identifier: GPL-2.0-only
"""Public ApexTX release discovery and bounded, verified firmware downloads."""
import hashlib
import io
import json
import re
import ssl
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass
from functools import total_ordering

from nb4_update_image import (BOOT_SIZE, FLASH_SIZE, TARGETS, VARIANTS,
                              package_target, update_package, validate_full_image_auth)

REPOSITORY = 'GastonGelhorn/apextx'
API = 'https://api.github.com/repos/' + REPOSITORY
SEMVER = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?')


class ReleaseError(RuntimeError):
    pass


@total_ordering
@dataclass(frozen=True)
class Version:
    text: str

    def __post_init__(self):
        match = SEMVER.fullmatch(self.text)
        if not match:
            raise ValueError('Invalid release version')
        pre = match[4]
        parts = pre.split('.') if pre else []
        if any(not p or (p.isdigit() and len(p) > 1 and p.startswith('0')) for p in parts):
            raise ValueError('Invalid release version')
        key = tuple(int(match[i]) for i in (1, 2, 3)) + (
            not bool(pre), tuple((0, int(p)) if p.isdigit() else (1, p) for p in parts))
        object.__setattr__(self, 'key', key)
        object.__setattr__(self, 'preview', bool(pre))

    def __lt__(self, other):
        return self.key < other.key


def firmware_identity(value):
    """Parse the application's stamp, never the bootloader or USB bcdDevice."""
    match = re.fullmatch(r'apextx-nb4-[0-9]+\.[0-9]+\.[0-9]+-apextx-'
                         r'([0-9A-Za-z.-]+) \(([0-9a-f]{7,40})\)', value)
    if not match:
        return dict(version=None, revision=None, development=False)
    version, revision = match.groups()
    development = version.endswith('-dev')
    if development:
        version = version[:-4]
    try:
        Version(version)
    except ValueError:
        return dict(version=None, revision=None, development=False)
    return dict(version=version, revision=revision, development=development)


def update_decision(device, release):
    if not release:
        return 'unavailable'
    if device.get('version') and Version(device['version']) > Version(release['version']):
        return 'development' if device.get('development') else 'ahead'
    if device.get('state', 'ready') == 'ready' and bootloader_required(device, release.get('bootloader', 0)):
        return 'bootloader'
    if not device.get('version'):
        return 'unknown'
    installed, offered = Version(device['version']), Version(release['version'])
    if installed < offered:
        return 'available'
    if device.get('development'):
        return 'development'
    return 'current' if installed == offered else 'ahead'


def bootloader_required(device, generation):
    # Only the descriptor served by the bootloader in Update mode establishes
    # its generation. An application's version or data-mode descriptor cannot.
    return generation > (device.get('bootloader') or 0)


def image_bootloader_generation(image):
    generations = set(re.findall(rb'APXINFO([1-9][0-9]{0,3});', image[:BOOT_SIZE]))
    if len(generations) > 1:
        raise ReleaseError('The image has conflicting bootloader identities. Nothing was written.')
    return int(next(iter(generations))) if generations else 0


def _safe_url(url):
    parsed = urllib.parse.urlsplit(url)
    return (parsed.scheme == 'https' and not parsed.username and not parsed.password
            and parsed.port in (None, 443) and parsed.hostname in
            ('api.github.com', 'github.com', 'release-assets.githubusercontent.com',
             'objects.githubusercontent.com'))


class _Redirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        if not _safe_url(newurl):
            raise ReleaseError('The release download redirected outside GitHub. Nothing was written.')
        return super().redirect_request(request, fp, code, message, headers, newurl)


class ReleaseClient:
    def __init__(self, opener=None):
        if opener is None:
            # Desktop bundles carry a CA store; source installs can use Python's
            # system trust store when certifi is not installed.
            try:
                import certifi
                context = ssl.create_default_context(cafile=certifi.where())
            except ImportError:
                context = ssl.create_default_context()
            # A supplied SSL context bypasses urllib's default ALPN setup.
            # Advertise the HTTP version this client actually implements.
            context.set_alpn_protocols(['http/1.1'])
            opener = urllib.request.build_opener(_Redirects(), urllib.request.HTTPSHandler(context=context))
        self.opener = opener

    def fetch(self, url, limit, progress=None):
        if not _safe_url(url):
            raise ReleaseError('Invalid GitHub release URL.')
        request = urllib.request.Request(url, headers={
            'User-Agent': 'ApexTX-Flasher/1.2', 'Accept': 'application/vnd.github+json'
            if url.startswith(API) else 'application/octet-stream',
            'X-GitHub-Api-Version': '2022-11-28'})
        try:
            with self.opener.open(request, timeout=10) as response:
                length = int(response.headers.get('Content-Length', '0'))
                if length < 0 or length > limit:
                    raise ReleaseError('The release download exceeds the allowed size.')
                chunks, count, deadline = [], 0, time.monotonic() + 90
                while True:
                    chunk = response.read(min(65536, limit + 1 - count))
                    if not chunk:
                        break
                    count += len(chunk)
                    if count > limit or time.monotonic() > deadline:
                        raise ReleaseError('The release download exceeded its size or time limit.')
                    chunks.append(chunk)
                    if progress:
                        progress('Downloading firmware...', min(7, count * 7 // (length or limit)))
                if length and count != length:
                    raise ReleaseError('The release download is incomplete. Try again.')
                return b''.join(chunks)
        except urllib.error.HTTPError as error:
            if error.code in (403, 429):
                raise ReleaseError('GitHub temporarily limited release checks. Try again later or choose a local image.') from error
            raise ReleaseError(f'GitHub returned HTTP {error.code}. Try again or choose a local image.') from error
        except (OSError, urllib.error.URLError) as error:
            raise ReleaseError('Could not reach GitHub. Check your connection or choose a local image.') from error

    def latest(self, target, preview=True):
        if target not in VARIANTS:
            raise ReleaseError('The connected radio model is not supported.')
        candidates = []
        hardware = next(name for name, number in TARGETS.items() if number == target)
        for page in range(1, 6):
            try:
                rows = json.loads(self.fetch(f'{API}/releases?per_page=100&page={page}', 4 * 1024 * 1024))
                if not isinstance(rows, list):
                    raise ValueError()
                for row in rows:
                    if row.get('draft'):
                        continue
                    tag = row.get('tag_name', '')
                    try:
                        version = Version(tag.removeprefix('v'))
                    except ValueError:
                        continue
                    prerelease = version.preview or bool(row.get('prerelease'))
                    if prerelease and not preview:
                        continue
                    assets = row.get('assets', [])
                    by_name = {a['name']: a for a in assets if a.get('state') == 'uploaded'}
                    if len(by_name) != len([a for a in assets if a.get('state') == 'uploaded']):
                        continue
                    names = [f'apextx-{hardware}-{version.text}.bin', f'apextx-{hardware}-{version.text}.zip']
                    if target == 1:
                        names.append(f'apextx-{version.text}.zip')  # pre-NB4+ releases
                    asset = next((by_name[name] for name in names if name in by_name), None)
                    sums = by_name.get('SHA256SUMS')
                    if not asset or not sums:
                        continue
                    metadata = by_name.get(f'apextx-{hardware}-{version.text}.update.json')
                    package_asset=by_name.get(f'apextx-{hardware}-{version.text}.zip')
                    for item in (asset, sums, *([metadata] if metadata else []), *([package_asset] if package_asset else [])):
                        expected = ('https://github.com/' + REPOSITORY + '/releases/download/' +
                                    urllib.parse.quote(tag, safe='') + '/' + urllib.parse.quote(item['name'], safe=''))
                        if item.get('browser_download_url') != expected:
                            raise ReleaseError('The release has an unexpected download URL.')
                    if not 0 < asset.get('size', 0) <= 32 * 1024 * 1024:
                        continue
                    if asset['name'].endswith('.bin') and asset['size'] != FLASH_SIZE:
                        continue
                    candidates.append((version, dict(id=row['id'], version=version.text,
                        prerelease=prerelease, target=target, asset=asset, checksums=sums,
                        metadata=metadata,
                        package_asset=package_asset,
                        url=f'https://github.com/{REPOSITORY}/releases/tag/{urllib.parse.quote(tag, safe="")}')))
                if len(rows) < 100:
                    break
            except (ValueError, KeyError, TypeError) as error:
                raise ReleaseError('GitHub returned invalid release metadata.') from error
        else:
            raise ReleaseError('The release list is too long to check safely. Choose a local image.')
        if not candidates:
            return None
        offer = max(candidates, key=lambda item: item[0])[1]
        metadata = offer.pop('metadata')
        if metadata:
            raw = self.fetch(metadata['browser_download_url'], 16384)
            self._digest(raw, metadata.get('digest'))
            try:
                policy = json.loads(raw)
                generation = policy['bootloader']
                if (policy['schema'] != 1 or policy['version'] != offer['version'] or
                        policy['target'] != target or policy['firmware'] != offer['asset']['name'] or
                        type(generation) is not int or not 1 <= generation <= 9999 or
                        not re.fullmatch(r'[0-9a-f]{64}', policy['sha256'])):
                    raise ValueError()
                offer.update(bootloader=generation, image_sha256=policy['sha256'])
            except (ValueError, KeyError, TypeError) as error:
                raise ReleaseError('The release has invalid bootloader requirements. Nothing was written.') from error
        elif Version(offer['version']) <= Version('0.1.0-alpha.5'):
            # Published before compatibility assets existed. Only alpha.5 added
            # APXINFO1 to its bootloader. Never infer a future requirement.
            offer['bootloader'] = 1 if offer['version'] == '0.1.0-alpha.5' else 0
        else:
            raise ReleaseError('The release is missing bootloader requirements. Choose a local image or try again later.')
        return offer

    def download(self, release, progress):
        asset = release['asset']
        data=self.download_asset(release,asset,progress)
        image = self._unpack(data, asset['name'], release) if asset['name'].endswith('.zip') else data
        validate_full_image_auth(image)
        if (image_bootloader_generation(image) != release.get('bootloader', 0) or
                (release.get('image_sha256') and hashlib.sha256(image).hexdigest() != release['image_sha256'])):
            raise ReleaseError('The firmware does not match the release bootloader requirements. Nothing was written.')
        if package_target(update_package(image)) != release['target']:
            raise ReleaseError('The downloaded firmware is for another radio. Nothing was written.')
        stamps = re.findall(rb'apextx-nb4-[^\x00]{1,110}\x00', image[BOOT_SIZE:BOOT_SIZE + 1024])
        identities = [firmware_identity(s[:-1].decode('ascii', errors='replace')) for s in stamps]
        if not any(i['version'] == release['version'] and not i['development'] for i in identities):
            raise ReleaseError('The firmware version does not match the release. Nothing was written.')
        progress('Firmware download verified.', 8)
        return image

    def download_asset(self, release, asset, progress=lambda *_: None):
        if not isinstance(asset,dict) or not 0<asset.get('size',0)<=128*1024*1024:
            raise ReleaseError('The selected release asset exceeds its size limit.')
        sums = self.fetch(release['checksums']['browser_download_url'], 128 * 1024)
        self._digest(sums, release['checksums'].get('digest'))
        expected = []
        for line in sums.decode('utf-8').splitlines():
            match = re.fullmatch(r'([0-9a-fA-F]{64}) [ *](.+)', line)
            if match and match[2] == asset['name']:
                expected.append(match[1].lower())
        if len(expected) != 1:
            raise ReleaseError('The release has no unique checksum for this firmware.')
        data = self.fetch(asset['browser_download_url'], asset['size'], progress)
        if len(data) != asset['size'] or hashlib.sha256(data).hexdigest() != expected[0]:
            raise ReleaseError('The release checksum does not match. Nothing was written.')
        self._digest(data, asset.get('digest'))
        return data

    @staticmethod
    def _digest(data, digest):
        if digest is not None and digest != 'sha256:' + hashlib.sha256(data).hexdigest():
            raise ReleaseError('The GitHub asset checksum does not match. Nothing was written.')

    @staticmethod
    def _unpack(data, name, release):
        root = name[:-4] + '/'
        try:
            with zipfile.ZipFile(io.BytesIO(data)) as archive:
                images = [i for i in archive.infolist() if i.filename == root + 'firmware.bin']
                manifests = [i for i in archive.infolist() if i.filename == root + 'manifest.json']
                if len(images) != 1 or len(manifests) != 1 or images[0].file_size != FLASH_SIZE or manifests[0].file_size > 128 * 1024:
                    raise ReleaseError('The release archive has an invalid firmware layout.')
                image = archive.read(images[0])
                manifest = json.loads(archive.read(manifests[0]))
                expected = manifest['files']['firmware.bin']
                if (manifest['release'] != release['version'] or
                        manifest['target'] != VARIANTS[release['target']] or
                        expected['bytes'] != FLASH_SIZE or expected['sha256'] != hashlib.sha256(image).hexdigest()):
                    raise ReleaseError('The package manifest does not match the selected release.')
                return image
        except (zipfile.BadZipFile, KeyError, ValueError, RuntimeError) as error:
            raise ReleaseError('The firmware package is invalid. Nothing was written.') from error
