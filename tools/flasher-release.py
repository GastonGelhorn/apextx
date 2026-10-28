#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Create app-only signed releases; private keys never enter the repository."""
import argparse
import base64
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from flasher_app_update import MANIFEST, SIGNATURE, Version, verify_manifest, trusted_keys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generate-key', type=Path, metavar='PRIVATE_PEM_OUTSIDE_REPOSITORY')
    parser.add_argument('--key-file', type=Path)
    parser.add_argument('--directory', type=Path, default=ROOT / 'dist')
    parser.add_argument('--notes', type=Path)
    parser.add_argument('--build', type=int)
    args = parser.parse_args()
    if args.generate_key:
        path = args.generate_key.expanduser().resolve()
        if path.is_relative_to(ROOT) or path.exists():
            parser.error('Create a new private key outside the repository; never overwrite an existing key.')
        key = Ed25519PrivateKey.generate()
        path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as stream:
            stream.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                          serialization.NoEncryption()))
        print(json.dumps({'apextx-2026': base64.b64encode(key.public_key().public_bytes_raw()).decode()}))
        return
    build = int((ROOT / 'FLASHER_BUILD').read_text())
    if args.build is not None and args.build != build:
        parser.error('--build must match FLASHER_BUILD.')
    args.build = build
    if not args.notes or build < 1:
        parser.error('--notes and a positive monotonically increasing --build are required.')
    encoded = args.key_file.read_bytes() if args.key_file else os.environ.get('APEXTX_FLASHER_SIGNING_KEY', '').encode()
    if not encoded:
        parser.error('A release signing key is required. Unsigned publication is forbidden.')
    key = serialization.load_pem_private_key(encoded, password=None)
    if not isinstance(key, Ed25519PrivateKey):
        parser.error('An Ed25519 signing key is required.')
    public = base64.b64encode(key.public_key().public_bytes_raw()).decode()
    if public not in trusted_keys().values():
        parser.error('The private key does not match a public key shipped in the app.')
    version = (ROOT / 'FLASHER_VERSION').read_text().strip()
    Version(version)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    layouts = {'windows-x86_64': ('ApexTX-Flasher.exe', '10.0.17763'),
               'macos-arm64': ('ApexTX-Flasher-macOS-arm64.zip', '15.0'),
               'macos-x86_64': ('ApexTX-Flasher-macOS-x86_64.zip', '15.0')}
    packages = {}
    for target, (original, minimum) in layouts.items():
        source = args.directory / original
        destination = args.directory / f'ApexTX-Flasher-{version}-{target}{source.suffix}'
        shutil.copy2(source, destination)
        packages[target] = dict(name=destination.name, size=destination.stat().st_size,
            sha256=hashlib.sha256(destination.read_bytes()).hexdigest(), minimum_os=minimum)
    doc = dict(schema=1, product='org.apextx.flasher', version=version, build=args.build,
        channel='preview' if Version(version).preview else 'stable', updater_protocol=1,
        revision=revision, notes=args.notes.read_text(encoding='utf-8'), packages=packages)
    raw = (json.dumps(doc, sort_keys=True, indent=2, ensure_ascii=False) + '\n').encode()
    signature = key.sign(raw)
    verify_manifest(raw, signature)
    (args.directory / MANIFEST).write_bytes(raw)
    (args.directory / SIGNATURE).write_bytes(signature)
    print(f'Signed ApexTX Flasher {version}, build {args.build}; all three packages verified.')


if __name__ == '__main__':
    main()
