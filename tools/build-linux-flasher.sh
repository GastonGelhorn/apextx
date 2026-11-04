#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Run from the repository root. GTK/WebKit and USB drivers remain system dependencies.
set -euo pipefail
[[ "$(uname -s)" == Linux ]]
work="$PWD/build/flasher/linux-$(uname -m)"
python3 -m venv "$work/venv"
py="$work/venv/bin/python"
"$py" -m pip install -r tools/requirements-flasher.txt

# Ubuntu 22.04 ships dfu-util 0.9. Package the same qualified 0.11 release as
# Windows/macOS instead of silently taking whichever version is on PATH.
dfu_archive="$work/dfu-util-0.11.tar.gz"
dfu_sha256=b4b53ba21a82ef7e3d4c47df2952adf5fa494f499b6b0b57c58c5d04ae8ff19e
if [[ ! -f "$dfu_archive" ]]; then
  curl --fail --location --retry 3 \
    https://dfu-util.sourceforge.net/releases/dfu-util-0.11.tar.gz \
    --output "$dfu_archive.part"
  mv "$dfu_archive.part" "$dfu_archive"
fi
printf '%s  %s\n' "$dfu_sha256" "$dfu_archive" | sha256sum --check --status
tar -xzf "$dfu_archive" -C "$work"
dfu_source="$work/dfu-util-0.11"
dfu_prefix="$work/dfu"
(
  cd "$dfu_source"
  ./configure --prefix="$dfu_prefix"
  make -j"$(getconf _NPROCESSORS_ONLN)"
  make install
)
cp "$dfu_source/COPYING" "$dfu_prefix/COPYING"
"$dfu_prefix/bin/dfu-util" --version

# Read the full cache so pipefail cannot turn an early awk exit into SIGPIPE.
usb_library="$(ldconfig -p | awk '/libusb-1.0.so.0 / && !library {library=$NF} END {print library}')"
if [[ ! -f "$usb_library" ]]; then
  echo 'Install libusb-1.0-0-dev before packaging the Linux application.' >&2
  exit 1
fi
"$py" tools/build-flasher.py --dfu-dir "$dfu_prefix/bin" --libusb "$usb_library"
