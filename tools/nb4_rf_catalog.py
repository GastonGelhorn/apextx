# SPDX-License-Identifier: GPL-2.0-only
"""Prepare verified, local RF assets. This module has no USB or flash writer.

Only explicitly reviewed installation packages and image digests are accepted. CRCs
detect damage; the pinned SHA-256 values, not a user-editable manifest, define
which image is accepted. Input packages are parsed as data, never executed.
"""
import binascii
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import zipfile
import zlib

MAX_SOURCE = 32 * 1024 * 1024
MAX_BUNDLE = 2 * 1024 * 1024
MAX_IMAGE = 512 * 1024
FORMAT = 'apextx-rf-catalog-3'

# Exact image identities accepted by this catalog. Engine indices 0..2 are
# the module and the two FGr4 V2 variants; keep them stable.
COMMON_IMAGES = (
    dict(kind='module', product=0x12340003, reported_product=0x00010003, version=0x10021, bytes=38954,
         sha256='9f91794947ca8c24b6e3b242d373b9d02c17d794e4b794f2584341231bbe9b25',
         label='RF module (PN 0x12340003)'),
    dict(kind='receiver', product=0x12340004, version=0x113, bytes=51432,
         sha256='cbb5021a4a8824d246886d3745b3742ede7f9e0a4bdac89311c699042f753859',
         label='FGr4 V2'),
    dict(kind='receiver', product=0x12340006, version=0x111, bytes=50596,
         sha256='adaed3edf380bfce83b04342cffb2e12ab8b87311975bb5ea720261421cc6a84',
         label='FGr4S V2 (shared product ID)'),
)
# Package provenance and receiver compatibility are independent. These pins
# authorize offline extraction/assessment only; hardware/write qualification
# remains separate. Keep the original engine indices 0..2 unchanged.
RECEIVERS_FROM_NB4 = (
    dict(kind='receiver', product=0x00010107, version=0x114, bytes=64464,
         sha256='a3f55ca1b90d0e82b1302ea3004600431d79b62fc9231ca54af4ebbd10aeb8b5', label='FGr8B'),
    dict(kind='receiver', product=0x00010104, version=0x107, bytes=52376,
         sha256='6fb87ea3e8206eb7398046dd5618c6c2f7c4e80201b3f85f074c96b27a9a2e2d', label='Mini-Z (receiver entry)'),
    dict(kind='receiver', product=0x0001010e, version=0x103, bytes=61956,
         sha256='a3f155303c50e5b960e74b8298c1470ab90ba4873faa2d4d49246f0eaed16159', label='FGr4B'),
    dict(kind='receiver', product=0x00010108, version=0x10c, bytes=65720,
         sha256='8f1e6fffcdc74b1ef7f880d5aa3c106808b85c8df874257802883278be937dd6', label='FGr12B'),
    dict(kind='receiver', product=0x0001010b, version=0x108, bytes=71344,
         sha256='cc0780c06c4d5737ba407363c20866f983c8892c4ac96aa2a773016cf3d76f72', label='INr4-GYB'),
)
RECEIVERS_FROM_NB4PLUS = (
    dict(kind='receiver', product=0x00010107, version=0x117, bytes=88320,
         sha256='f0f25179d309f90d2d33611cbbacae200457c058aee3d2255b558e5eaaa5a37a', label='FGr8B'),
    dict(kind='receiver', product=0x00010104, version=0x108, bytes=52056,
         sha256='9e9411c68a76be97b0a429c69e5f7d8abb11492530f8fad74bf187d893ef628e', label='Mini-Z (receiver entry)'),
    dict(kind='receiver', product=0x0001010e, version=0x117, bytes=86780,
         sha256='9d5c95fa810e97c424a2898463964cae2e2f9239afeab6c25252d120c58f570d', label='FGr4B'),
    dict(kind='receiver', product=0x00010108, version=0x117, bytes=88916,
         sha256='0e9f277a34fb8a6b1432fa665e2f5616344477867668243c0bd38e0cc83a9ed1', label='FGr12B'),
)
FGR8B_NB4, FGR8B_NB4PLUS = RECEIVERS_FROM_NB4[0], RECEIVERS_FROM_NB4PLUS[0]
IMAGES = COMMON_IMAGES + RECEIVERS_FROM_NB4 + RECEIVERS_FROM_NB4PLUS

# Observed in an exact reviewed source, but rejected by our current CRC
# gate. Do not export or select this payload until its format is qualified.
UNVERIFIED_RECEIVER_IMAGES = (
    dict(kind='receiver', product=0x0001010b, version=0x109, bytes=71344,
         sha256='74e5ae3b30e24b15078f138fc82b6f7ac96cc2bad1dcc75df4b034a46b2aa5a3',
         label='INr4-GYB', source_radio='nb4plus', reason='crc32-mismatch'),
)

PROFILES = (
    dict(id='nb4-rf-1a', radio='nb4',
         sha256='ddd46193f0abaeb8d11eb5f3e7998fb97d7b868c27ae7a44a9ce7cc22d619c02',
         resource_sha256='37780d8512809d8fc3ec43764b1672d28f2758e6f9ca26a850bf8888b848a665',
         images=COMMON_IMAGES + RECEIVERS_FROM_NB4),
    dict(id='nb4plus-rf-1a', radio='nb4plus',
         sha256='5f5475055b7d4e19fc647f4180b30e478e72a68ef0987aefdbfe1d6f9fd6c248',
         resource_sha256='71a09fb94b656de17028ee35441dcbd0ba1b37735330eea4ea8f3f1911dab825',
         images=COMMON_IMAGES + RECEIVERS_FROM_NB4PLUS),
)
PROFILES += (
    dict(PROFILES[0], id='nb4-rf-1b',
         sha256='662955adfa643dde4ec22a5bc57c01869e98aec38f21e4b580b5aa480b5a219c'),
    dict(PROFILES[1], id='nb4plus-rf-1b',
         sha256='1b119de1e2a0efa9787f3db3fd9abe01cd16b4a7f4069d254dcf8b9e46b90b72'),
)
SOURCE_PROFILES = PROFILES

# The latest verified image per receiver product across both packages. The
# NB4 and NB4+ share their RF hardware and FlySky no longer publishes NB4
# packages, so both radios ship the NB4+ receiver images. INr4-GYB keeps the
# NB4 package's 0x108: the NB4+ 0x109 fails the CRC gate (UNVERIFIED above).
LATEST_RECEIVER_IMAGES = (RECEIVERS_FROM_NB4[4],) + RECEIVERS_FROM_NB4PLUS
# Merged profiles are built from verified source bundles, never from a package.
MERGED_PROFILES = tuple(
    dict(id=f'{radio}-rf-2', radio=radio, sources=('nb4-rf-1b', 'nb4plus-rf-1b'),
         images=COMMON_IMAGES + LATEST_RECEIVER_IMAGES) for radio in ('nb4', 'nb4plus'))
PROFILES += MERGED_PROFILES

# Images the radio installs by itself, per target (1 = NB4, 2 = NB4+). An
# image is authorized by the product ID the component reports and by its
# pinned hash, the way the stock firmware updates components: the receiver
# bootloader verifies its own product word, and FlySky ships one image per
# product. Identity words are read and shown as diagnostics and freeze the
# identity for the duration of one operation; they do not gate installation.
# The older per-package variants (indices 3-6) are not installed: the merged
# catalogs carry the latest verified image of each product.
INSTALLABLE_IMAGES = (0, 1, 2) + tuple(IMAGES.index(image) for image in LATEST_RECEIVER_IMAGES)
UPDATE_RULES = tuple(dict(target=target, image=image) for target in (1, 2) for image in INSTALLABLE_IMAGES)


class CatalogError(ValueError):
    pass


def firmware_catalog_header():
    """Render the radio's exact image allowlist from this same catalog."""
    lines = ['// Generated by tools/nb4-rf-generate.py; do not edit.',
             '// SPDX-License-Identifier: GPL-2.0-only', '#pragma once',
             '#include "nb4_rf_update_core.h"', 'namespace nb4 {']
    for index, image in enumerate(IMAGES):
        values = ','.join(f'0x{byte:02x}' for byte in bytes.fromhex(image['sha256']))
        lines.append(f'constexpr uint8_t rfImageHash{index}[32] = {{{values}}};')
    lines.append('constexpr RfUpdateImage rfImages[] = {')
    for index, image in enumerate(IMAGES):
        lines.append('  {0x%x, 0x%x, %d, rfImageHash%d, %s, 0x%x},' %
                     (image['product'], image['version'], image['bytes'], index,
                      'true' if image['kind'] == 'receiver' else 'false',
                      image.get('reported_product', image['product'])))
    lines += ['};', '// Images the radio installs on a target; the component must report the image product.',
              'struct RfUpdateRule { uint32_t target, image; };',
              'constexpr RfUpdateRule rfUpdateRules[] = {']
    for rule in UPDATE_RULES:
        lines.append('  {%d, %d},' % (rule['target'], rule['image']))
    lines += ['};', '}', '']
    return '\n'.join(lines)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise CatalogError('Duplicate RF manifest key.')
        result[key] = value
    return result


def canonical(value):
    return json.dumps(value, sort_keys=True, allow_nan=False)


def cut(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise CatalogError('A package range is truncated or outside the file.')
    return data[offset:offset + size]


def unpack(fmt, data, offset):
    return struct.unpack(fmt, cut(data, offset, struct.calcsize(fmt)))


def resource_from_pe(data):
    """Read the numeric RCDATA resource through bounded PE directory entries."""
    if not 64 <= len(data) <= MAX_SOURCE or data[:2] != b'MZ':
        raise CatalogError('Expected a bounded installation package.')
    pe, = unpack('<I', data, 60)
    if cut(data, pe, 4) != b'PE\0\0':
        raise CatalogError('Invalid PE header.')
    machine, count = unpack('<HH', data, pe + 4)
    optional_size, = unpack('<H', data, pe + 20)
    optional = pe + 24
    magic, = unpack('<H', data, optional)
    if machine not in (0x14c, 0x8664) or not 1 <= count <= 96 or magic not in (0x10b, 0x20b):
        raise CatalogError('Unsupported PE layout.')
    directory_offset = 96 if magic == 0x10b else 112
    if optional_size < directory_offset + 24:
        raise CatalogError('Missing PE resource directory.')
    cut(data, optional, optional_size)
    directories, = unpack('<I', data, optional + directory_offset - 4)
    if directories < 3:
        raise CatalogError('Missing PE resource data directory.')
    rva, resource_size = unpack('<II', data, optional + directory_offset + 16)
    if not 16 <= resource_size <= MAX_SOURCE:
        raise CatalogError('Invalid PE resource size.')
    sections = []
    for index in range(count):
        section = optional + optional_size + index * 40
        cut(data, section, 40)
        virtual_size, address, raw_size, raw_offset = unpack('<4I', data, section + 8)
        cut(data, raw_offset, raw_size)
        sections.append((address, raw_size, raw_offset))

    def mapped(address, size):
        matches = [offset + address - start for start, length, offset in sections
                   if start <= address and address - start <= length and
                   size <= length - (address - start)]
        if len(matches) != 1:
            raise CatalogError('Ambiguous or invalid PE resource mapping.')
        return cut(data, matches[0], size)

    resource = mapped(rva, resource_size)

    def entries(offset):
        cut(resource, offset, 16)
        named, ids = unpack('<HH', resource, offset + 12)
        if named + ids > 512:
            raise CatalogError('Too many PE resource entries.')
        result = {}
        for index in range(named + ids):
            name, child = unpack('<II', resource, offset + 16 + index * 8)
            if name in result:
                raise CatalogError('Duplicate PE resource identifier.')
            result[name] = child
        return result

    directory = 0
    for identifier in (10, 901):
        child = entries(directory).get(identifier)
        if child is None or not child & 0x80000000:
            raise CatalogError('Missing RF resource in the update package.')
        directory = child & 0x7fffffff
    languages = entries(directory)
    if len(languages) != 1:
        raise CatalogError('Ambiguous RF resource language.')
    child = next(iter(languages.values()))
    if child & 0x80000000:
        raise CatalogError('Unexpected nested RF resource.')
    address, size, _, reserved = unpack('<4I', resource, child)
    if reserved or not 64 <= size <= MAX_SOURCE:
        raise CatalogError('Invalid RF resource data entry.')
    return mapped(address, size)


def validate_image(data, entry):
    if len(data) != entry['bytes'] or digest(data) != entry['sha256']:
        raise CatalogError('RF image size or pinned SHA-256 does not match.')
    if entry['kind'] == 'module':
        valid = len(data) >= 2 and binascii.crc_hqx(data[:-2], 0xffff) == int.from_bytes(data[-2:], 'little')
    else:
        valid = len(data) >= 4 and zlib.crc32(data[:-4]) == int.from_bytes(data[-4:], 'little')
    if not valid:
        raise CatalogError('RF image checksum does not match.')


def catalog_images(resource, profile):
    """Decode the supplied installation resource, never scan for vector tables."""
    if digest(resource) != profile['resource_sha256']:
        raise CatalogError('RF resource does not match the reviewed package.')
    magic, _, _, sections = unpack('<4H', resource, 0)
    if magic != 0x5a5a or not 1 <= sections <= 16:
        raise CatalogError('Unsupported installation resource header.')
    # Resolve the installation container's relocation from its final, named
    # section. No target-memory addresses or disassembly offsets are embedded
    # in the importer. Ambiguous names/layouts are rejected, not guessed.
    name_offset = resource.find(b'Firmware\0')
    if name_offset < 0 or resource.find(b'Firmware\0', name_offset + 1) >= 0:
        raise CatalogError('Ambiguous firmware section name.')
    name_pointer, _, _ = unpack('<3I', resource, 20 + (sections - 1) * 12)
    base = name_pointer - name_offset
    if base < 0:
        raise CatalogError('Invalid resource relocation.')
    firmware_tables = []
    for index in range(sections):
        name, count, table = unpack('<3I', resource, 20 + index * 12)
        label = cut(resource, name - base, 16).split(b'\0', 1)[0]
        if label == b'Firmware':
            if count != 1:
                raise CatalogError('Ambiguous firmware resource group.')
            firmware_tables.append(table - base)
    if len(firmware_tables) != 1:
        raise CatalogError('Missing or duplicate firmware resource group.')
    count, address = unpack('<2I', resource, firmware_tables[0])
    if not 1 <= count <= 64:
        raise CatalogError('Invalid firmware catalog length.')
    table = address - base
    cut(resource, table, count * 20)
    catalog, ranges = {}, []
    for index in range(count):
        tag, product, version, size, address = unpack('<5I', resource, table + index * 20)
        kind = {0: 'module', 1: 'receiver'}.get(tag & 0xffff)
        if tag >> 16 != 1 or not kind or not 32 <= size <= MAX_IMAGE:
            raise CatalogError('Unsupported firmware catalog entry.')
        key = kind, product
        if key in catalog:
            raise CatalogError('Ambiguous firmware product identifier.')
        start = address - base
        payload = cut(resource, start, size)
        if start < 20 + sections * 12 or start + size > table:
            raise CatalogError('Firmware payload overlaps resource metadata.')
        if any(start < end and begin < start + size for begin, end in ranges):
            raise CatalogError('Firmware payload ranges overlap.')
        ranges.append((start, start + size))
        catalog[key] = (version, payload)
    images = {}
    for expected in profile['images']:
        key = expected['kind'], expected['product']
        if key not in catalog or catalog[key][0] != expected['version']:
            raise CatalogError('Expected firmware product/version is missing.')
        payload = catalog[key][1]
        validate_image(payload, expected)
        images[image_name(expected)] = payload
    return images


def image_name(entry):
    return f"images/{entry['kind']}-{entry['product']:08x}-{entry['version']:08x}.bin"


def source_profile(identifier):
    return next(p for p in SOURCE_PROFILES if p['id'] == identifier)


def manifest(profile):
    sources = profile.get('sources')
    return dict(format=FORMAT, profile=profile['id'], radio=profile['radio'],
                input_sha256=([source_profile(s)['sha256'] for s in sources] if sources
                              else profile['sha256']),
                rf_write_supported=False,
                images=[dict(entry, file=image_name(entry)) for entry in profile['images']])


def import_source(data):
    if not 64 <= len(data) <= MAX_SOURCE:
        raise CatalogError('Installation package exceeds the permitted size.')
    source_hash = digest(data)
    matches = [p for p in PROFILES if p.get('sha256') == source_hash]
    if len(matches) != 1:
        raise CatalogError('This exact installation package has not been reviewed; nothing was prepared.')
    profile = matches[0]
    images = catalog_images(resource_from_pe(data), profile)
    return manifest(profile), images


def bundle_bytes(description, images):
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, payload in [('APEXTX-RF.json', (json.dumps(description, indent=2) + '\n').encode()),
                              *sorted(images.items())]:
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100600 << 16
            archive.writestr(info, payload)
    raw = output.getvalue()
    verify_bundle(raw)  # Never export a file that this installer cannot verify.
    return raw


def verify_bundle(raw):
    if not 1 <= len(raw) <= MAX_BUNDLE:
        raise CatalogError('RF bundle exceeds the permitted size.')
    try:
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            entries = archive.infolist()
            names = [entry.filename for entry in entries]
            if (len(entries) > 16 or len(set(names)) != len(names) or
                    sum(entry.file_size for entry in entries) > MAX_BUNDLE or
                    any(entry.flag_bits & 1 or entry.file_size > MAX_IMAGE or
                        (entry.external_attr >> 16 & 0o170000) not in (0, 0o100000)
                        for entry in entries)):
                raise CatalogError('Invalid, duplicate or oversized RF archive entries.')
            description = json.loads(archive.read('APEXTX-RF.json'), object_pairs_hook=unique_object)
            if not isinstance(description, dict):
                raise CatalogError('Invalid RF bundle manifest.')
            profile = next((p for p in PROFILES if p['id'] == description.get('profile')), None)
            if not profile or canonical(description) != canonical(manifest(profile)):
                raise CatalogError('RF bundle metadata does not match a reviewed profile.')
            expected = {image_name(entry): entry for entry in profile['images']}
            if set(names) != {'APEXTX-RF.json', *expected}:
                raise CatalogError('Missing, extra or unsafe RF archive paths.')
            images = {}
            for name, entry in expected.items():
                images[name] = archive.read(name)
                validate_image(images[name], entry)
            return description, images
    except (zipfile.BadZipFile, KeyError, UnicodeError, ValueError, RuntimeError,
            NotImplementedError, OSError) as error:
        raise CatalogError('RF bundle is damaged or incomplete.') from error


def merge_bundles(raws, profile_id):
    """Combine verified source bundles into a merged profile's image set."""
    profile = next((p for p in PROFILES if p['id'] == profile_id and p.get('sources')), None)
    if not profile:
        raise CatalogError('Not a merged catalog profile.')
    images, seen = {}, []
    for raw in raws:
        description, files = verify_bundle(raw)
        if description['profile'] not in profile['sources'] or description['profile'] in seen:
            raise CatalogError('A merged catalog takes each reviewed source bundle once.')
        seen.append(description['profile'])
        for name, data in files.items():
            if images.get(name, data) != data:
                raise CatalogError('Source bundles disagree on an image.')
            images[name] = data
    if set(seen) != set(profile['sources']):
        raise CatalogError('A reviewed source bundle is missing.')
    selected = {}
    for entry in profile['images']:
        name = image_name(entry)
        if name not in images:
            raise CatalogError('Expected firmware product/version is missing.')
        validate_image(images[name], entry)
        selected[name] = images[name]
    return manifest(profile), selected


def merge(sources, destination, profile_id):
    raws = []
    for source in sources:
        source = Path(source)
        if source.stat().st_size > MAX_BUNDLE:
            raise CatalogError('RF bundle exceeds the permitted size.')
        with source.open('rb') as stream:
            raws.append(stream.read(MAX_BUNDLE + 1))
    description, images = merge_bundles(raws, profile_id)
    raw = bundle_bytes(description, images)
    with Path(destination).open('xb') as stream:  # never replace an existing file
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    return description


def inspect_source_file(source):
    source = Path(source)
    if source.stat().st_size > MAX_SOURCE:
        raise CatalogError('Installation package exceeds the permitted size.')
    with source.open('rb') as stream:
        return import_source(stream.read(MAX_SOURCE + 1))


def prepare(source, destination):
    description, images = inspect_source_file(source)
    destination = Path(destination)
    raw = bundle_bytes(description, images)
    # Exclusive creation: never replace an existing file, backup or source.
    with destination.open('xb') as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    return description


def assess_receiver(description, installed):
    """Read-only update assessment, suitable for a post-bind inventory.

    Available does NOT mean required or safe to write. Until image selection,
    hardware identification, transfer and recovery are qualified, no assessment
    produced here authorizes a write, nor an automatic downgrade.
    """
    try:
        profile = next((p for p in PROFILES if canonical(manifest(p)) == canonical(description)), None)
    except (ValueError, TypeError):
        profile = None
    result = dict(state='unknown', required=False, write_supported=False)
    if (not profile or not isinstance(installed, dict) or
            installed.get('connected') is not True or installed.get('valid') is not True):
        return result
    product, version = installed.get('product'), installed.get('firmware')
    if type(product) is not int or type(version) is not int or not 0 <= version <= 0xffff:
        return result
    match = next((e for e in profile['images'] if e['kind'] == 'receiver' and e['product'] == product), None)
    if match:
        result.update(state='current' if version == match['version'] else
                            'available' if version < match['version'] else 'ahead',
                      installed=version, available=match['version'], product=product)
    return result


def assess_receiver_catalogs(descriptions, installed):
    """Latest verified receiver candidate across source packages, read-only.

    A package's radio model is provenance, not a receiver compatibility rule.
    This result never authorizes transfer or certifies a hardware/bootloader/RF
    combination. Callers must obtain descriptions through verify_bundle() and
    invalidate disconnected or stale inventory before presenting any candidate.
    """
    unknown = dict(state='unknown', required=False, write_supported=False,
                   compatibility_verified=False)
    if (not isinstance(descriptions, (list, tuple)) or
            not 1 <= len(descriptions) <= len(PROFILES) or
            not isinstance(installed, dict) or installed.get('fresh') is False):
        return unknown
    candidates = []
    for description in descriptions:
        try:
            profile = next((p for p in PROFILES if canonical(manifest(p)) == canonical(description)), None)
        except (TypeError, ValueError):
            return unknown
        if profile is None:
            return unknown
        assessment = assess_receiver(description, installed)
        if assessment['state'] != 'unknown':
            image = next(entry for entry in profile['images'] if entry['kind'] == 'receiver' and
                         entry['product'] == installed['product'])
            candidates.append((image, profile, assessment))
    if not candidates:
        return unknown
    latest_version = max(item[0]['version'] for item in candidates)
    latest = [item for item in candidates if item[0]['version'] == latest_version]
    if len({item[0]['sha256'] for item in latest}) != 1:
        return dict(unknown, state='ambiguous', product=installed['product'], available=latest_version)
    image, _, assessment = latest[0]
    sources = sorted({(profile['id'], profile['radio']) for candidate, profile, _ in candidates
                      if (candidate['version'], candidate['sha256']) == (image['version'], image['sha256'])})
    pending = [dict(version=entry['version'], source_radio=entry['source_radio'], reason=entry['reason'])
               for entry in UNVERIFIED_RECEIVER_IMAGES if entry['product'] == image['product'] and
               entry['version'] > image['version']]
    return dict(assessment, compatibility_verified=False, image=dict(image),
                source_profiles=[dict(profile=identifier, radio=radio) for identifier, radio in sources],
                known_unverified=pending)
