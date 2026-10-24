#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Smoke-test a packaged flasher with no USB operation or developer PATH."""
import hashlib
import json
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    mac = sys.platform == 'darwin'
    executable = ROOT / ('dist/ApexTX-Flasher.app/Contents/MacOS/ApexTX-Flasher'
                         if mac else 'dist/ApexTX-Flasher.exe')
    env = dict(os.environ)
    if mac:
        env['PATH'] = '/usr/bin:/bin'
        for key in list(env):
            if key.startswith(('PYTHON', 'DYLD_')):
                del env[key]
    with tempfile.TemporaryDirectory() as folder:
        report = Path(folder) / 'self-test.json'
        subprocess.run([str(executable), '--self-test', str(report)],
                       cwd=folder, env=env, check=True, timeout=60)
        result = json.loads(report.read_text())
    assert result['platform'] == sys.platform
    assert result['architecture'] == platform.machine().lower()
    assert result['usb_backend'] and result['dfu_util'] and result['shared_ui']
    for name, expected in result['sources'].items():
        assert hashlib.sha256((ROOT / 'tools' / name).read_bytes()).hexdigest() == expected, name
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
