#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Smoke-test a packaged flasher with no USB operation or developer PATH."""
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    mac = sys.platform == 'darwin'
    env = dict(os.environ)
    if mac:
        env['PATH'] = '/usr/bin:/bin'
        for key in list(env):
            if key.startswith(('PYTHON', 'DYLD_')):
                del env[key]
    with tempfile.TemporaryDirectory() as folder:
        report = Path(folder) / 'self-test.json'
        if mac:
            archive = ROOT / f'dist/ApexTX-Flasher-macOS-{platform.machine().lower()}.zip'
            subprocess.run(['ditto', '-x', '-k', str(archive), folder], check=True)
            executable = Path(folder) / 'ApexTX-Flasher.app/Contents/MacOS/ApexTX-Flasher'
            subprocess.run(['codesign', '--verify', '--deep', '--strict',
                            str(Path(folder) / 'ApexTX-Flasher.app')], check=True)
        else:
            executable = Path(folder) / 'ApexTX-Flasher.exe'
            shutil.copy2(ROOT / 'dist/ApexTX-Flasher.exe', executable)
        subprocess.run([str(executable), '--self-test', str(report)],
                       cwd=folder, env=env, check=True, timeout=60)
        result = json.loads(report.read_text())
    assert result['platform'] == sys.platform
    assert result['architecture'] == platform.machine().lower()
    assert result['revision'] == subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    assert result['usb_backend'] and result['dfu_util'] and result['shared_ui']
    assert result['release_client'] and result['app_updates']
    assert result['app_version'] == (ROOT / 'FLASHER_VERSION').read_text().strip()
    assert result['app_build'] == int((ROOT / 'FLASHER_BUILD').read_text())
    for name, expected in result['sources'].items():
        assert hashlib.sha256((ROOT / 'tools' / name).read_bytes()).hexdigest() == expected, name
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
