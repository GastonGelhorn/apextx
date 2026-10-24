#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Package the same ApexTX UI and USB core for Windows or macOS."""
import argparse
import hashlib
import json
import platform
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def app_icon(destination):
    from PIL import Image
    source = Image.open(ROOT / 'docs/branding/apextx-logo.png').convert('RGBA')
    pixels = source.load()
    for y in range(source.height):
        for x in range(source.width):
            r, g, b, _ = pixels[x, y]
            level = max(r, g, b)
            alpha = 0 if level <= 26 else 255 if level >= 80 else int((level - 26) / 54 * 255)
            pixels[x, y] = (r, g, b, alpha)
    source = source.crop(source.getbbox())
    size = 1024 if destination.suffix == '.icns' else 256
    canvas = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    source.thumbnail((size * 9 // 10, size * 9 // 10), Image.Resampling.LANCZOS)
    canvas.paste(source, ((size - source.width) // 2, (size - source.height) // 2))
    canvas.save(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dfu-dir', type=Path, required=True)
    parser.add_argument('--libusb', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform not in ('win32', 'darwin'):
        parser.error('Build on Windows or macOS for that operating system.')
    mac = sys.platform == 'darwin'
    arch = platform.machine().lower()
    work = ROOT / 'build/flasher' / ('macos-' + arch if mac else 'windows')
    work.mkdir(parents=True, exist_ok=True)
    dfu = args.dfu_dir.resolve() / ('dfu-util' if mac else 'dfu-util.exe')
    libusb = args.libusb.resolve()
    if not dfu.is_file() or not libusb.is_file():
        parser.error('Both dfu-util and libusb must exist before packaging.')
    icon = work / ('ApexTX.icns' if mac else 'ApexTX.ico')
    app_icon(icon)

    sources = ['apextx-updater.py', 'apextx-updater.html', 'nb4_usb_update.py',
               'nb4_update_image.py', 'nb4_rom_flash.py']
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    metadata = work / 'apextx-flasher-build.json'
    metadata.write_text(json.dumps({
        'revision': revision, 'platform': sys.platform, 'architecture': arch,
        'sources': {name: hashlib.sha256((ROOT / 'tools' / name).read_bytes()).hexdigest()
                    for name in sources},
    }, indent=2) + '\n', encoding='utf-8')

    # One entry point, one HTML UI, one dependency list, on both platforms.
    options = [
        '--onedir' if mac else '--onefile', '--windowed', '--name', 'ApexTX-Flasher',
        '--icon', str(icon), '--paths', str(ROOT / 'tools'),
        '--add-data', str(ROOT / 'tools/apextx-updater.html') + ':.',
        '--add-data', str(ROOT / 'tools/apextx-updater-logo.png') + ':.',
        '--add-data', str(ROOT / 'LICENSE') + ':licenses',
        '--add-data', str(metadata) + ':.',
        '--add-binary', str(libusb) + ':.',
        '--add-binary', str(dfu) + ':dfu',
        '--hidden-import', 'usb.backend.libusb1', '--collect-all', 'webview',
        '--workpath', str(work / 'pyinstaller'), '--specpath', str(work),
        '--distpath', str(ROOT / 'dist'), '--noconfirm', '--clean',
    ]
    if mac:
        options += ['--osx-bundle-identifier', 'org.apextx.flasher',
                    '--target-architecture', arch,
                    '--hidden-import', 'webview.platforms.cocoa']
        for prefix, name in [(args.dfu_dir.resolve().parent, 'dfu-util'),
                             (libusb.parent.parent, 'libusb')]:
            license_file = prefix / 'COPYING'
            if not license_file.is_file():
                parser.error(f'Missing {name} license: {license_file}')
            options += ['--add-data', str(license_file) + ':licenses/' + name]
    else:
        options += ['--add-binary', str(args.dfu_dir.resolve() / 'libusb-1.0.dll') + ':dfu']
    options.append(str(ROOT / 'tools/apextx-updater.py'))
    subprocess.run([sys.executable, '-m', 'PyInstaller', *options], check=True, cwd=ROOT)

    if mac:
        app = ROOT / 'dist/ApexTX-Flasher.app'
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
        archive = ROOT / f'dist/ApexTX-Flasher-macOS-{arch}.zip'
        subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent',
                        str(app), str(archive)], check=True)
    else:
        archive = ROOT / 'dist/ApexTX-Flasher.exe'
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(archive.suffix + '.sha256').write_text(
        f'{checksum}  {archive.name}\n', encoding='utf-8')
    print(json.dumps({'artifact': str(archive), 'sha256': checksum, 'architecture': arch}))


if __name__ == '__main__':
    main()
