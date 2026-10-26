#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Build on the target Mac architecture; the installed app needs no Homebrew.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ "$(uname -s)" != Darwin ]]; then
  echo 'Build the macOS application on a Mac.' >&2
  exit 1
fi
python_bin="${APEXTX_BUILD_PYTHON:-python3.11}"
work="$PWD/build/flasher/macos-$(uname -m)"
mkdir -p "$work"
if [[ ! -x "$work/venv/bin/python" ]]; then
  "$python_bin" -m venv "$work/venv"
fi
py="$work/venv/bin/python"
"$py" -m pip install --disable-pip-version-check -r tools/requirements-flasher.txt

# These are build-host dependencies only. PyInstaller bundles and relocates
# dfu-util and its libusb dependency into the app for machines without Brew.
dfu_prefix="$(brew --prefix dfu-util)"
usb_prefix="$(brew --prefix libusb)"
"$py" tools/build-flasher.py --dfu-dir "$dfu_prefix/bin" \
  --libusb "$usb_prefix/lib/libusb-1.0.0.dylib"
