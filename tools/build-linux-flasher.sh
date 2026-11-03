#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Run from the repository root. GTK/WebKit and USB drivers remain system dependencies.
set -euo pipefail
[[ "$(uname -s)" == Linux ]]
work="$PWD/build/flasher/linux-$(uname -m)"
python3 -m venv "$work/venv"
py="$work/venv/bin/python"
"$py" -m pip install -r tools/requirements-flasher.txt
usb_library="$(ldconfig -p | awk '/libusb-1.0.so.0 / {print $NF; exit}')"
"$py" tools/build-flasher.py --dfu-dir /usr/bin --libusb "$usb_library"
