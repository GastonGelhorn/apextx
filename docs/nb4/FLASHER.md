# ApexTX Flasher — a no-terminal flashing app

The ApexTX Flasher packages the existing local-UI updater
([`tools/apextx-updater.py`](../../tools/apextx-updater.py)) as a single,
double-clickable Windows app, and adds a full-image **install / recovery** mode
over STM32 ROM DFU. It keeps every check the terminal updater already does
(image validation, the NB4 / NB4+ target check, and the read-back before
installing), and needs no Python, no manual libusb/driver setup and no terminal.

Build it with [`tools/build-windows-flasher.ps1`](../../tools/build-windows-flasher.ps1)
(fetches dfu-util and libusb, no binaries are committed). The same
`apextx-updater.py` still runs from a terminal exactly as before.

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
  only a complete image with valid bootloader *and* application vectors is
  accepted.
- **Mandatory backup first** (`backup_internal`): the radio's current 2 MiB
  internal flash is read out and saved (`~/ApexTX-Backups/…`) before writing, so
  the previous firmware can be restored with this same mode.
- **Current-firmware detection** (`inspect_flash` on the backup): ApexTX images
  carry markers that name the firmware and variant, so for recovery the radio's
  model (NB4 / NB4+) is known and the chosen image is cross-checked against it.
  Stock firmware has no marker, so the model is confirmed by the user (the
  image's own marker is shown).
- **Model cross-check**: an NB4 image on a detected ApexTX-NB4+ (or the reverse)
  is refused.
- **Read-back verify**: after writing, the whole image is read back and compared;
  on mismatch the radio is not started and stays in DFU.
- **Cannot be permanently bricked**: the STM32 ROM bootloader is in silicon, so
  the radio can always re-enter ROM DFU and be re-flashed. Any failure leaves it
  in DFU, nothing half-started.
- After a ROM DFU install the radio is switched off and on once to start it (the
  app says so; it does not claim an automatic restart).

## Honest limitation
ROM DFU only reaches the STM32 internal flash. The external **8 MiB SPI NOR**
(filesystem + factory calibration) **cannot be read or written** this way, so the
mandatory backup restores the *firmware*, not the NOR. A factory-exact backup
needs a separate method (an SPI reader / programmer).

## UI
Native window (Edge WebView2, falls back to the default browser so it runs on any
Windows), the ApexTX brand logo and colours, and English / Spanish / German,
matching the firmware's languages.
