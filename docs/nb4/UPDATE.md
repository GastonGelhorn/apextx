# ApexTX Updater for the original Noble NB4

Normal updates follow this flow:

1. On the radio, open **Settings > System > Update** and confirm.
2. The radio keeps its display and power latch active and shows **ApexTX Update**.
3. Connect USB, open the computer updater, and select the new `firmware.bin`.
4. The updater transfers and verifies the image, requests installation, waits
   for flash verification, and requests a restart.

Menu entry, USB transfer, flash verification, and automatic restart were tested
on an original NB4 from macOS on 2026-09-14. The final hardware run completed on
its first attempt. The bootloader clears pending flash status before starting
an erase and explicitly requests application resume at restart. The host also
permits one complete retry of an initial erase failure. Other hosts and broader
hardware acceptance remain untested. This does not target NB4+, NB4P, or FlySky
firmware packages.

## First installation

If the radio still runs the official FlySky firmware, start with
[Installing ApexTX from the FlySky firmware](FLASH.md). The desktop ApexTX
Updater requires the ApexTX bootloader to be installed already; it cannot
perform that first installation through the official firmware's Update mode.
FlySky Assistant is not an installer for the ApexTX image.

The first-installation guide covers these steps:

1. Back up the original storage and MCU contents. Review the separate raw NOR
   backup requirement before formatting if a complete factory rollback matters.
2. Enter **STM32 ROM DFU** and confirm its internal-flash, option-byte, and OTP
   interfaces. A black display or USB ID alone does not identify this mode.
3. Build and validate the complete 2 MiB `firmware.bin`, then install it with
   `tools/nb4-flash.sh`. This writes and verifies the ApexTX bootloader and
   application together.
4. Complete storage setup, resource installation, calibration, and receiver
   checks as described in that guide.
5. For subsequent application updates, use **Settings > System > Update** and
   this updater.

An older ApexTX image that jumps to the STM32 ROM from its Update menu also
needs a complete image with the new bootloader installed through ROM DFU once.
The current build includes the application manifest in that complete image.

After that installation, normal updates use the menu and the ApexTX bootloader.
They preserve the first 128 KiB containing that bootloader. A future bootloader
upgrade remains a separate complete-image installation through ROM DFU.

## Start the computer updater

From the repository root, use the project's Python environment:

```sh
.venv/bin/python -m pip install -r tools/requirements-updater.txt
.venv/bin/python tools/apextx-updater.py
```

The updater opens a local browser window. It requires a local libusb backend
(`brew install libusb` on macOS, `libusb-1.0` on Linux). On Windows, PyUSB needs
libusb and a compatible USB driver for the update interface. Windows packaging
and driver installation have not been validated in this implementation.

| Platform | Current status | Requirements |
| --- | --- | --- |
| macOS | Complete update tested with an original NB4 | Python, PyUSB, and libusb 1.0; no additional USB driver was needed on the tested Mac |
| Windows | Supported by the USB libraries; this updater still needs a hardware test on Windows | Python, PyUSB, a loadable libusb 1.0 DLL matching Python's architecture, and a compatible USB driver such as WinUSB |
| Linux | Not tested with this updater | Python, PyUSB, libusb 1.0, and permission to access the USB interface |

Standalone packages for Windows and macOS use this same updater and local
interface. See [ApexTX Flasher](FLASHER.md) for the `.exe`, Apple Silicon and
Intel `.app` downloads, build instructions and install/recovery mode. Running
the Python script directly still requires the dependencies described above.

On Windows, create and use the environment from PowerShell:

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools\requirements-updater.txt
.\.venv\Scripts\python.exe tools\apextx-updater.py
```

The Python dependency installation does not install the native libusb DLL or
the Windows USB driver. Follow the official
[PyUSB backend setup instructions](https://github.com/pyusb/pyusb#requirements-and-platform-support)
for the DLL and the
[libusb Windows driver instructions](https://github.com/libusb/libusb/wiki/Windows#driver-installation)
for the update interface. The current updater discovers a system-provided
libusb DLL; installing the optional `libusb-package` Python module alone does
not configure this updater to load it. Select **ApexTX NB4 Update** when setting
up a driver, rather than the radio's joystick or storage interface. These setup
instructions do not constitute a completed Windows hardware validation.

The browser talks only to a server bound to `127.0.0.1`. Firmware is not sent to
an external service. Keep the terminal open while using the interface. Closing
the browser does not cancel an installation already accepted by the server.

The same updater can run from the terminal:

```sh
# Check the built image without accessing USB:
.venv/bin/python tools/apextx-updater.py \
  build/nb4-device/arm-none-eabi/firmware.bin --check

# Identify an ApexTX update interface, without writing:
.venv/bin/python tools/apextx-updater.py --check

# Update; waits up to 90 seconds for the radio:
.venv/bin/python tools/apextx-updater.py \
  build/nb4-device/arm-none-eabi/firmware.bin
```

Connect one radio at a time, or use `--serial` with the terminal updater.
The host requires both an ApexTX update product name and its exact interface
descriptor. A matching STM32 VID/PID alone is insufficient; a ROM DFU device is
never claimed for a normal update.

## NB4 and NB4+ images

An image is built for one radio, and only that radio's bootloader installs it.
The original NB4 bootloader announces itself as `ApexTX NB4 Update`; one built
with `-DNB4_PLUS_HW=ON` announces itself as `ApexTX NB4+ Update`. The updater
compares that name with the image before sending anything, and refuses a
mismatch with a message naming both radios. The bootloader refuses it as well,
so an image for the other radio cannot be installed even without this updater.

Every NB4 bootloader released so far accepts only NB4 images, so they already
refuse NB4+ images. An NB4+ that was set up with an earlier experimental build
has an NB4 bootloader and needs one installation through ROM DFU, following
[FLASH.md](FLASH.md), before it accepts NB4+ updates.

NB4+ builds are experimental and are published only in the separately named
`apextx-nb4plus-*` package. Full recovery and normal update have worked on real
NB4+ hardware, but that does not replace the remaining charging, RF/failsafe
under load and peripheral checks. The first status LED can still flicker red
(issue #19).

Firmware updates do not modify the external audio files. When a release changes
voice resources, install one matching English, Spanish, German or Mandarin Chinese package
separately in USB Storage mode as described in [AUDIO.md](AUDIO.md).

## Failure and recovery

Before installation, firmware resides in SDRAM and the current application is
unchanged. The complete transfer is read back and compared by the computer.
The radio independently validates its manifest, length, vectors, and CRC before
erasing flash. A failed or incomplete transfer can be retried from the beginning.

During installation, only the application region and its manifest are erased.
The bootloader is preserved, and no external NOR, model, calibration, OTP, or
option-byte write is exposed. Application vectors are written last, after the
body and manifest have been verified. Every normal boot validates the complete
application CRC, so even a torn erase that happens to leave plausible vectors
returns to Update instead of jumping into an incomplete application.

On exit, the bootloader passes a resume request to the application before the
software reset. This avoids waiting for a new power-button press and preserves
the normal startup path. It does not enable the watchdog on ordinary cold boot,
where the radio may legitimately wait for the power button.

Keep the radio powered while installing. If installation fails, the radio
remains in Update and accepts another complete transfer. If power was lost,
turn it on and retry when the Update screen appears. Before installation, or
after a successful installation, hold the power button to restart a valid
application. The updater reports flash verification separately from leaving
Update; disappearance from USB alone is not proof that the main UI booted.

## Protocol and artifacts

The ApexTX bootloader uses the project's ST USB DFU class as a transport for a
custom RAM-staged protocol. It neither jumps into the ROM nor implements the
FlySky updater protocol. The virtual staging address is `0xC0200000`; it is
translated into a linker-allocated SDRAM buffer, not dereferenced directly.
Virtual `0xC03F0000` accepts only `APXSTART` and `APXRESET`, and returns a 32-byte
installation status on upload. Raw internal flash addresses are inaccessible.

`tools/nb4_update_image.py` adds a 32-byte manifest at `0x081FFFE0` after the
firmware binary is built. The application begins at `0x08020000`. The manifest
contains `APXNB4U1`, the target (1 for the NB4, 2 for the NB4+), application
address, length, CRC32, protocol version 1, and a CRC32 over the first 28 header
bytes (little endian integers). The target is not a build argument: it is read
from the `APXHW:<id>` marker the firmware carries in its own image, so a manifest
can only describe the binary it is attached to.
The USB package contains this header followed by the application bytes; the
host strips the bootloader and unused padding from the complete binary.
CRC checks detect corruption; this format does not authenticate a publisher.

Tests run the actual C++ flash engine with partial erase/program failures at
every operation, retries, invalid vectors, corruption, and bounds checks. Host
tests cover target validation, ROM-device exclusion, transfer readback, and
requiring successful flash status before restart:

```sh
.venv/bin/python -m unittest tools.tests.test_nb4_updater -v
```
