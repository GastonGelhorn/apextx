# ApexTX Flasher — Windows and macOS

The ApexTX Flasher packages the existing local-UI updater
([`tools/apextx-updater.py`](../../tools/apextx-updater.py)) as a single,
double-clickable Windows or macOS app, and adds a full-image **install / recovery** mode
over STM32 ROM DFU. It keeps every check the terminal updater already does
(image validation, the NB4 / NB4+ target check, and the read-back before
installing), and needs no Python or terminal.

The executable includes the libusb user-mode runtime, but Windows must still
associate each USB mode with a compatible kernel driver. If the radio is not
accessible, install the WinUSB driver for the displayed ApexTX Update or STM32
BOOTLOADER device (for example with Zadig), disconnect the radio and reconnect
it in the intended mode. Installing STM32CubeProgrammer can also provide a DFU
driver, but CubeProgrammer itself is not required by ApexTX Flasher.

Build it with [`tools/build-windows-flasher.ps1`](../../tools/build-windows-flasher.ps1)
(fetches dfu-util over HTTPS and verifies its pinned SHA-256; no binaries are
committed). CI builds Windows, macOS Apple Silicon and macOS Intel packages,
and tagged releases publish and attest them alongside the firmware packages. The same
`apextx-updater.py` still runs from a terminal exactly as before.

## One application, platform-specific packages

Both platforms use `tools/apextx-updater.py`, the same HTML interface and the
same image-validation, backup, USB-update and recovery modules. There is no
separate Mac UI or copy of the flashing core. `tools/build-flasher.py` packages
those sources, with dependencies pinned in `tools/requirements-flasher.txt`.
The small Windows/macOS scripts prepare only their platform's build tools.

Every PR builds all three packages through the same reusable workflow. Each
packaged application is checked with `--self-test REPORT.json`: it loads its
own libusb, starts bundled `dfu-util --version` and verifies the shared UI
resources without discovering or writing a radio. The report records the
source revision and hashes of the common UI/core files. A window cannot close
while an update, backup or recovery is running.

## macOS

Download `ApexTX-Flasher-macOS-arm64.zip` for Apple Silicon or
`ApexTX-Flasher-macOS-x86_64.zip` for Intel. Extract the archive, copy
`ApexTX-Flasher.app` to Applications and open it. Both modes below are available.
The app includes Python, libusb and dfu-util; end users need no Terminal,
Homebrew, WinUSB or separate Python installation. The firmware image is chosen
separately and must match the radio.

The initial packages have an ad-hoc signature for bundle integrity, not an Apple
Developer ID signature or notarization. macOS may block downloaded copies until
the user explicitly permits that app in Privacy & Security. This is not a
notarized distribution. GitHub build attestations are separate from Apple signing.

For developers, build on each target architecture from the repository root:

```sh
brew install dfu-util libusb
# Python 3.11 is needed only on the build machine.
bash tools/build-macos-flasher.sh
python3.11 tools/test-flasher-package.py
```

The build creates the `.app`, a zip and a SHA-256 sidecar in `dist/`. It bundles
the native Homebrew dependencies and relocates their dylib references; the
packaged smoke test extracts that zip to a fresh directory, checks its signature
and removes developer Python/DYLD variables and Homebrew from PATH. CI performs
the same build and test separately on Apple Silicon and Intel.

Local Apple Silicon validation covers packaging, signature integrity, bundled
USB tools and opening both UI modes in Spanish. A complete radio write through
the packaged app is a separate hardware test; packaging checks do not claim it.

The executable is not currently Authenticode-signed, so Windows SmartScreen may
show an unknown-publisher warning. Download it only from the ApexTX release and
verify the release checksum/provenance; do not use copies from third parties.

## Two modes

### Update (unchanged)
Application-only update through **Settings → System → Update** on the radio.
Keeps the installed bootloader. This is the normal path once ApexTX is installed.

### Install / Recovery (new)
Writes the **complete 2 MiB image** (bootloader + application) over **STM32 ROM
DFU**, for first installation (from FlySky) and for recovery. Driven by a bundled
`dfu-util`, the same operation as [`tools/nb4-flash.sh`](../../tools/nb4-flash.sh).

Safeguards, for people who have never flashed before:
- **Image validated before any write** (`nb4_rom_flash.validate_full_image`):
  only an exact 2 MiB ApexTX image with valid bootloader vectors, application
  vectors, target markers, application manifest/CRC and complete-image SHA-256
  is accepted. Partial images and unmarked stock backups are never accepted.
- **Strict device selection**: the generic STM32 USB ID is accepted only when
  alternate zero advertises 2 MiB of internal flash at `0x08000000`. More than
  one matching device is refused.
- **Explicit model confirmation**: the user must choose NB4 or NB4+, and the
  choice must match the target recorded in the image before a backup or write.
- **Mandatory backup first** (`backup_internal`): the radio's current 2 MiB
  internal flash is read out and saved (`~/ApexTX-Backups/…`) before writing.
  A JSON sidecar records its SHA-256, byte count, USB serial and confirmed radio
  model. Keep both files private and together.
- **Current-firmware detection** (`inspect_flash` on the backup): ApexTX images
  carry markers that name the firmware and variant, so for recovery the radio's
  model (NB4 / NB4+) is known and the chosen image is cross-checked against it.
  Stock firmware has no marker, so the model is confirmed by the user (the
  image's own marker is shown).
- **Model cross-check**: an NB4 image on a detected ApexTX-NB4+ (or the reverse)
  is refused.
- **Read-back verify**: after writing, the whole image is read back and compared;
  on mismatch the radio is not started and stays in DFU.
- **No unverified boot**: any write or read-back failure stops before the leave
  request. Keep the radio powered, reconnect it in ROM DFU if necessary, and
  retry with a known-good ApexTX image. The STM32 ROM bootloader normally makes
  this recovery possible even when the installed flash does not boot; this is
  not a guarantee against hardware, power or option-byte damage.
- After a ROM DFU install the radio is switched off and on once to start it (the
  app says so; it does not claim an automatic restart).

## Honest limitation
ROM DFU only reaches the STM32 internal flash. The external **8 MiB SPI NOR**
(filesystem + factory calibration) **cannot be read or written** this way, so the
mandatory backup contains the *firmware*, not the NOR. The app intentionally
does not accept raw stock backups for restoration: they do not carry the ApexTX
manifest, checksum and NB4/NB4+ target marker needed for an automatic safety
check. Restore such a private backup only with the expert procedure in
[`FLASH.md`](FLASH.md), after verifying its JSON SHA-256 and target radio. A factory-exact backup needs
a separate method for the external NOR (an SPI reader / programmer).

## UI
Native window (Edge WebView2 on Windows, WKWebView on macOS, with a default-browser
fallback), the ApexTX brand logo and colours, and English / Spanish / German,
matching the firmware's languages.
