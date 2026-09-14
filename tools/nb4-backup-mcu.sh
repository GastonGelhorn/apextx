#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

readonly USB_ID="0483:df11"
readonly INTERNAL_FLASH_SIZE=$((0x200000))
readonly OUTPUT_DIR="${1:-}"
SERIAL="${NB4_DFU_SERIAL:-}"

fail() {
  printf '\nError: %s\n' "$*" >&2
  exit 1
}

command -v dfu-util >/dev/null || fail "dfu-util is not installed"
[[ -n "$OUTPUT_DIR" ]] || fail "usage: tools/nb4-backup-mcu.sh PRIVATE_OUTPUT_DIRECTORY"
[[ ! -e "$OUTPUT_DIR" ]] || fail "output path already exists: $OUTPUT_DIR"

if command -v shasum >/dev/null; then
  sha256_write() { shasum -a 256 "$@"; }
  sha256_check() { shasum -a 256 -c "$1"; }
elif command -v sha256sum >/dev/null; then
  sha256_write() { sha256sum "$@"; }
  sha256_check() { sha256sum -c "$1"; }
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

for alt in 0 1 2; do
  printf '%s\n' "$dfu_list" | grep -q "alt=$alt" ||
    fail "the STM32 DFU descriptor does not expose alternate setting $alt"
done

mkdir -p "$OUTPUT_DIR"
printf '%s\n' "$dfu_list" > "$OUTPUT_DIR/dfu-layout.txt"
printf '%s\n' "$SERIAL" > "$OUTPUT_DIR/dfu-serial.txt"

printf 'Noble NB4 MCU backup\n'
printf '  output: %s\n' "$OUTPUT_DIR"
printf '  device: %s\n' "$SERIAL"

dfu-util -d "$USB_ID" -S "$SERIAL" -a 0 \
  -s "0x08000000:$INTERNAL_FLASH_SIZE" \
  -U "$OUTPUT_DIR/internal-flash-2MiB.bin"
dfu-util -d "$USB_ID" -S "$SERIAL" -a 1 \
  -U "$OUTPUT_DIR/option-bytes.bin"
dfu-util -d "$USB_ID" -S "$SERIAL" -a 2 \
  -U "$OUTPUT_DIR/otp.bin"

internal_size=$(wc -c < "$OUTPUT_DIR/internal-flash-2MiB.bin" | tr -d ' ')
(( internal_size == INTERNAL_FLASH_SIZE )) ||
  fail "internal flash upload has an unexpected size: $internal_size bytes"
[[ -s "$OUTPUT_DIR/option-bytes.bin" ]] || fail "option-byte upload is empty"
[[ -s "$OUTPUT_DIR/otp.bin" ]] || fail "OTP upload is empty"

(
  cd "$OUTPUT_DIR"
  sha256_write \
    internal-flash-2MiB.bin option-bytes.bin otp.bin \
    dfu-layout.txt dfu-serial.txt > SHA256SUMS
  sha256_check SHA256SUMS
)

printf '\nMCU backup complete and verified.\n'
printf 'WARNING: this does not contain the external 8 MiB SPI NOR.\n'
printf 'Keep this directory private; its contents are specific to this transmitter.\n'
