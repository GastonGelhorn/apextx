#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

readonly USB_ID="0483:df11"
SERIAL="${NB4_DFU_SERIAL:-}"
readonly FLASH_SIZE=$((0x200000))
readonly BOOT_ADDRESS="0x08000000"
readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly SOURCE_IMAGE="${1:-$ROOT/build/nb4-device/arm-none-eabi/firmware.bin}"
readonly WORK_DIR="$(mktemp -d)"
readonly IMAGE="$WORK_DIR/firmware.bin"
readonly READBACK="$WORK_DIR/readback.bin"
trap 'rm -rf "$WORK_DIR"' EXIT

fail() {
  printf '\nError: %s\n' "$*" >&2
  exit 1
}

command -v dfu-util >/dev/null || fail "dfu-util is not installed"
[[ -f "$SOURCE_IMAGE" ]] || fail "firmware image does not exist: $SOURCE_IMAGE"

dfu_version=$(dfu-util --version 2>&1 | sed -n '1s/^dfu-util //p')
dfu_major=${dfu_version%%.*}
dfu_minor=${dfu_version#*.}
dfu_minor=${dfu_minor%%[^0-9]*}
[[ "$dfu_major" =~ ^[0-9]+$ && "$dfu_minor" =~ ^[0-9]+$ ]] ||
  fail "could not determine the installed dfu-util version"
(( dfu_major > 0 || dfu_minor >= 10 )) ||
  fail "dfu-util 0.10 or newer is required for a standalone DfuSe leave request"

if command -v shasum >/dev/null; then
  sha256_file() { shasum -a 256 "$1" | cut -d' ' -f1; }
elif command -v sha256sum >/dev/null; then
  sha256_file() { sha256sum "$1" | cut -d' ' -f1; }
else
  fail "shasum or sha256sum is required"
fi

dfu_list=$(dfu-util -l 2>/dev/null || true)
if [[ -z "$SERIAL" ]]; then
  serials=$(printf '%s\n' "$dfu_list" |
    grep "\[$USB_ID\]" |
    sed -n 's/.*serial="\([^"]*\)".*/\1/p' |
    sort -u)
  serial_count=$(printf '%s\n' "$serials" | sed '/^$/d' | wc -l | tr -d ' ')
  (( serial_count == 1 )) ||
    fail "expected one STM32 DFU device; set NB4_DFU_SERIAL when more than one is connected"
  SERIAL="$serials"
fi
readonly SERIAL

modified=$(stat -f %m "$SOURCE_IMAGE" 2>/dev/null || stat -c %Y "$SOURCE_IMAGE")
age=$(( $(date +%s) - modified ))
(( age >= 10 )) || fail "firmware image is only $age seconds old; wait for the build to finish"

cp "$SOURCE_IMAGE" "$IMAGE"
size=$(wc -c < "$IMAGE" | tr -d ' ')
(( size == FLASH_SIZE )) || fail "image must be the exact 2 MiB ApexTX firmware.bin"

python3 - "$ROOT" "$IMAGE" <<'PY'
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(sys.argv[1]) / "tools"))
from nb4_rom_flash import validate_full_image

variant = validate_full_image(pathlib.Path(sys.argv[2]).read_bytes())
print(f"  validated complete ApexTX image for Noble {variant}")
PY

interfaces=$(printf '%s\n' "$dfu_list" | grep -c "serial=\"$SERIAL\"" || true)
(( interfaces > 0 )) || fail "no STM32 DFU device found with serial $SERIAL"

printf 'Noble NB4 DFU flash\n'
printf '  image:  %s\n' "$SOURCE_IMAGE"
printf '  size:   %s bytes\n' "$size"
printf '  sha256: %s\n' "$(sha256_file "$IMAGE")"
printf '  device: %s DFU interfaces, serial %s\n' "$interfaces" "$SERIAL"

dfu-util -d "$USB_ID" -S "$SERIAL" -a 0 -s "$BOOT_ADDRESS" -D "$IMAGE"
dfu-util -d "$USB_ID" -S "$SERIAL" -a 0 -s "$BOOT_ADDRESS:$size" -U "$READBACK"
cmp -s "$IMAGE" "$READBACK" || fail "readback differs from the firmware image; the radio remains in DFU"

printf '  readback verified; leaving DFU through the ApexTX bootloader\n'
# The STM32 ROM in the NB4 rejects a generic USB DFU DETACH request. A
# standalone DfuSe leave sets the bootloader address and submits a zero-length
# download internally, without erasing or rewriting the verified flash image.
dfu-util -d "$USB_ID" -S "$SERIAL" -a 0 \
  -s "$BOOT_ADDRESS:leave"

for _ in 1 2 3 4 5 6 7 8 9 10; do
  sleep 1
  if ! dfu-util -l 2>/dev/null | grep -q "serial=\"$SERIAL\""; then
    printf '  complete: the radio left DFU and is starting\n'
    exit 0
  fi
done

fail "firmware readback passed, but the radio did not leave DFU"
