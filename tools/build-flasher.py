#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Package the same ApexTX UI and USB core for Windows, macOS or Linux."""
import argparse
import hashlib
import importlib.metadata
import json
import platform
import plistlib
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def stage_linux_libusb(source, work):
    """Keep a stable runtime name even when the system SONAME is a symlink."""
    destination = work / 'libusb-1.0.so.0'
    if source.resolve() != destination.resolve():
        shutil.copy2(source, destination)
    return destination


def app_icon(destination):
    """Write the platform icon from the rendered master (tools/nb4-flasher-icon.py)."""
    from PIL import Image
    master = Image.open(ROOT / 'tools/apextx-flasher-icon.png').convert('RGBA')
    if destination.suffix == '.icns':
        master.save(destination)
    else:
        sizes = [(size, size) for size in (16, 24, 32, 48, 64, 128, 256)]
        master.resize((256, 256), Image.Resampling.LANCZOS).save(destination, sizes=sizes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dfu-dir', type=Path, required=True)
    parser.add_argument('--libusb', type=Path, required=True)
    parser.add_argument('--preview-dir', type=Path, default=ROOT / 'build/nb4-customization/native')
    parser.add_argument('--dist-dir', type=Path, default=ROOT / 'dist', help='Destination for the app and archive.')
    args = parser.parse_args()
    dist = args.dist_dir.resolve()
    dist.mkdir(parents=True, exist_ok=True)
    if sys.platform not in ('win32', 'darwin', 'linux'):
        parser.error('Build on Windows, macOS or Linux for that operating system.')
    mac = sys.platform == 'darwin'
    linux = sys.platform == 'linux'
    arch = platform.machine().lower()
    work = ROOT / 'build/flasher' / ('macos-' + arch if mac else 'linux-' + arch if linux else 'windows')
    work.mkdir(parents=True, exist_ok=True)
    dfu = args.dfu_dir.resolve() / ('dfu-util' if mac or linux else 'dfu-util.exe')
    libusb = args.libusb.resolve()
    if not dfu.is_file() or not libusb.is_file():
        parser.error('Both dfu-util and libusb must exist before packaging.')
    if linux:
        libusb = stage_linux_libusb(libusb, work)
    icon = work / ('ApexTX.icns' if mac else 'ApexTX.ico')
    app_icon(icon)

    version = (ROOT / 'FLASHER_VERSION').read_text().strip()
    sources = ['apextx-theme-editor.py', 'apextx-theme-editor.html', 'apextx-theme-editor-catalog.json', 'nb4_editor_files.py', 'nb4_editor_transaction.py', 'nb4_preview.py','flasher_app_update.py', 'flasher_app_installer.py', 'flasher-update-keys.json', 'apextx-updater.py', 'apextx-updater.html', 'nb4_usb_update.py',
               'nb4_update_image.py', 'nb4_rom_flash.py', 'nb4_releases.py',
               'nb4_flasher_session.py', 'nb4_rf_catalog.py',
               'nb4_maintenance.py', 'nb4_audio_install.py', 'nb4_storage_backup.py',
               'nb4_rf_transfer.py', 'nb4_backup_agent.py', 'nb4_nor_restore.py',
               'nb4_integrated_update.py', 'nb4_resource_install.py',
               'rf-catalog/nb4.zip', 'rf-catalog/nb4plus.zip']
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    metadata = work / 'apextx-flasher-build.json'
    metadata.write_text(json.dumps({
        'revision': revision, 'platform': sys.platform, 'architecture': arch,
        'app_version': version, 'app_build': int((ROOT / 'FLASHER_BUILD').read_text()), 'updater_protocol': 1,
        'sources': {name: hashlib.sha256((ROOT / 'tools' / name).read_bytes()).hexdigest()
                    for name in sources},
    }, indent=2) + '\n', encoding='utf-8')

    # One entry point, one HTML UI, one dependency list, on every platform.
    options = [
        '--onedir' if mac or linux else '--onefile', '--windowed', '--name', 'ApexTX-Flasher',
        '--icon', str(icon), '--paths', str(ROOT / 'tools'),
        '--add-data', str(ROOT / 'tools/apextx-updater.html') + ':.',
        '--add-data', str(ROOT / 'tools/apextx-updater-logo.png') + ':.',
        # Verified RF catalogs for the one-click update without a release package.
        '--add-data', str(ROOT / 'tools/rf-catalog') + ':rf-catalog',
        '--add-data', str(ROOT / 'LICENSE') + ':licenses',
        '--add-data', str(ROOT / 'tools/flasher-update-keys.json') + ':.',
        '--collect-all', 'cryptography',
        '--add-data', str(metadata) + ':.',
        '--add-binary', str(libusb) + ':.',
        '--add-binary', str(dfu) + ':dfu',
        '--hidden-import', 'usb.backend.libusb1', '--hidden-import', 'serial',
        '--hidden-import', 'nb4_rf_transfer', '--hidden-import', 'nb4_backup_agent',
        '--hidden-import', 'nb4_integrated_update', '--hidden-import', 'nb4_nor_restore', '--hidden-import', 'nb4_resource_install',
        '--collect-all', 'webview',
        '--workpath', str(work / 'pyinstaller'), '--specpath', str(work),
        '--distpath', str(dist), '--noconfirm', '--clean',
    ]
    for name in ('apextx-theme-editor.py', 'apextx-theme-editor.html', 'apextx-theme-editor-catalog.json'):
        options += ['--add-data', str(ROOT / 'tools' / name) + ':.']
    if linux:
        options += ['--hidden-import', 'webview.platforms.gtk']
    options += ['--add-data', str(ROOT / 'tools/nb4-preview-defaults') + ':nb4-preview-defaults',
                '--hidden-import', 'nb4_editor_files', '--hidden-import', 'nb4_editor_transaction', '--hidden-import', 'nb4_preview']
    extension = '.dylib' if mac else '.so' if linux else '.dll'
    preview = args.preview_dir / ('nb4-preview' + extension)
    if not preview.is_file(): parser.error('Build the nb4-preview target for this platform before packaging.')
    options += ['--add-binary', str(preview.resolve()) + ':.']
    plus_source = ROOT / 'build/nb4plus-preview/native' / ('nb4-preview' + extension)
    if not plus_source.is_file(): parser.error('Build the NB4+ preview in build/nb4plus-preview/native first.')
    plus = work / ('nb4plus-preview'+extension)
    shutil.copy2(plus_source,plus)
    options += ['--add-binary', str(plus.resolve())+':.']
    certifi = importlib.metadata.distribution('certifi')
    ca_license = next((certifi.locate_file(f) for f in certifi.files
                       if str(f).endswith('/LICENSE')), None)
    if ca_license is None or not ca_license.is_file():
        parser.error('The bundled certifi CA store must include its license.')
    options += ['--add-data', str(ca_license) + ':licenses/certifi']
    for dependency in ('cryptography', 'cffi'):
        distribution = importlib.metadata.distribution(dependency)
        licenses = [f for f in distribution.files if '/licenses/' in str(f)]
        if not licenses:
            parser.error(f'Missing {dependency} distribution licenses.')
        for license_path in licenses:
            options += ['--add-data', str(distribution.locate_file(license_path)) + ':licenses/' + dependency]
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
    elif linux:
        for license_file, name in [
                (args.dfu_dir.resolve().parent / 'COPYING', 'dfu-util'),
                (Path('/usr/share/doc/libusb-1.0-0/copyright'), 'libusb')]:
            if not license_file.is_file():
                parser.error(f'Missing {name} license: {license_file}')
            options += ['--add-data', str(license_file) + ':licenses/' + name]
    else:
        options += ['--add-binary', str(args.dfu_dir.resolve() / 'libusb-1.0.dll') + ':dfu']
    if not mac and not linux:
        numbers = tuple(map(int, version.split('-')[0].split('.'))) + (int((ROOT / 'FLASHER_BUILD').read_text()),)
        version_file = work / 'version.txt'
        version_file.write_text("VSVersionInfo(ffi=FixedFileInfo(filevers=" + repr(numbers) +
            ", prodvers=" + repr(numbers) + ", mask=0x3f, flags=0, OS=0x40004, fileType=1, subtype=0, date=(0,0)), "
            "kids=[StringFileInfo([StringTable('040904B0',[StringStruct('FileVersion','" + version +
            "'),StringStruct('ProductName','ApexTX Flasher')])]),VarFileInfo([VarStruct('Translation',[1033,1200])])])", encoding='utf-8')
        options += ['--version-file', str(version_file)]
    options.append(str(ROOT / 'tools/apextx-updater.py'))
    subprocess.run([sys.executable, '-m', 'PyInstaller', *options], check=True, cwd=ROOT)

    if mac:
        app = dist / 'ApexTX-Flasher.app'
        info = app / 'Contents/Info.plist'
        with info.open('rb') as stream:
            properties = plistlib.load(stream)
        properties.update(CFBundleShortVersionString=version.split('-')[0],
                          CFBundleVersion=(ROOT / 'FLASHER_BUILD').read_text().strip())
        with info.open('wb') as stream:
            plistlib.dump(properties, stream)
        # Changing Info.plist invalidates only the enclosing bundle signature.
        subprocess.run(['codesign', '--force', '--sign', '-', str(app)], check=True)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
        archive = dist / f'ApexTX-Flasher-macOS-{arch}.zip'
        subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent',
                        str(app), str(archive)], check=True)
    elif linux:
        archive = dist / f'ApexTX-Flasher-Linux-{arch}.tar.gz'
        with tarfile.open(archive, 'w:gz') as bundle:
            bundle.add(dist / 'ApexTX-Flasher', arcname='ApexTX-Flasher')
    else:
        archive = dist / 'ApexTX-Flasher.exe'
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(archive.suffix + '.sha256').write_text(
        f'{checksum}  {archive.name}\n', encoding='utf-8')
    print(json.dumps({'artifact': str(archive), 'sha256': checksum, 'architecture': arch}))


if __name__ == '__main__':
    main()
