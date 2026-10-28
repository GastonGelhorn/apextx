# ApexTX Flasher — Windows and macOS

For desktop app updates, signing and release setup, see
[Flasher self-update](FLASHER_SELF_UPDATE.md). These updates are separate from
radio firmware updates.

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

Every tagged release includes these direct downloads, built from that same tag:

| Download | Purpose |
| --- | --- |
| `apextx-nb4-<version>.bin` | Complete firmware for the original NB4 |
| `apextx-nb4plus-<version>.bin` | Complete firmware for the NB4+ |
| `ApexTX-Flasher.exe` | Windows app |
| `ApexTX-Flasher-macOS-arm64.zip` | Apple Silicon `.app` |
| `ApexTX-Flasher-macOS-x86_64.zip` | Intel `.app` |
| `SHA256SUMS` | Checksums for firmware, apps and resource archives |

The existing complete firmware/resource packages and EN/ES/DE/CN audio packs remain
available alongside these files. Publication waits for both radio builds, all
three app packages and the audio packs to succeed; a missing download fails
publication. Downloaded binaries and application archives carry GitHub build
attestations in addition to checksums.

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
Homebrew, WinUSB or separate Python installation. The app finds and downloads
the compatible firmware; selecting a local image remains optional.

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

### Update
Application-only update through **Settings → System → Update** on the radio.
Keeps the installed bootloader. This is the normal path once ApexTX is installed.

1. Open the app. It immediately looks for a connected radio. Switch on the radio,
   open **Menu → System → Update**, and connect a USB **data** cable when prompted.
   A charging-only connection does not enumerate a USB device and cannot be detected.
2. The app identifies the NB4 or NB4+, its USB serial and, when supported, the
   installed **application** version. Standard USB descriptor reads do not claim
   interfaces, send DFU commands or expose internal flash, option bytes or OTP.
3. It checks published releases in `GastonGelhorn/apextx`, filters by hardware and
   compares semantic versions. **Include alpha / beta releases** is visible and
   enabled initially because ApexTX currently ships alpha releases. Disable it
   for stable releases only; an empty stable channel is stated explicitly.
4. Review the version and release notes, then click **Update to…**. Nothing is
   downloaded or written merely because a radio is connected. The app downloads
   the exact model's binary (or extracts `firmware.bin` in memory from an older
   official ZIP), verifies `SHA256SUMS`, GitHub's asset digest when present, the
   image's full checksum, application CRC/manifest, hardware and embedded version.
5. The USB serial and target are checked again after downloading and before any
   transfer. A swapped radio, ambiguous connection or invalid image stops the
   operation. Transfer read-back and the existing on-radio verification remain
   mandatory. Wait for the verified completion and restart instructions.

Brief USB status stalls during internal-flash writes are retried with a bounded
wait. Only status reads are repeated; the firmware is not resent or installed
again. A removed device or a persistent failure leaves the outcome unconfirmed
in the app. Keep the radio powered and check its display before restarting.

An installed version equal to or newer than the offered release never triggers
an automatic reinstall or downgrade. An older development build can upgrade to
a newer release after clicking **Update to…**. Replacing a development build
with the same or an older release requires **Choose a local firmware image**.
That option also works offline; release lookup failures do not disable local updates.
Complete installation remains explicit and always backs up the current firmware.
Local image selection and model confirmation remain available in Install / Recovery.

#### Releases requiring a new bootloader

When a release needs a bootloader newer than the one actually detected in Update
mode, the app replaces the normal Update action with **Prepare full installation**.
It downloads and validates the matching complete image first, then shows a guide:

1. Disconnect USB and switch the radio off completely.
2. Hold the recessed left grip button while switching on and connecting USB.
3. Release the grip button once **STM32 ROM DFU** is detected in the app.
4. Click **Back up & install on Noble NB4 / NB4+** to start the full installation.

The selected image and radio identity survive the mode change. DFU detection
alone never starts a write. Both the UI and server require the original radio's
USB serial; discovery checks it again before backup. A different device, multiple
devices, failed backup or failed verification stops the operation. Allow
**5–10 minutes** with a black radio screen; progress is shown in the app. The
radio starts automatically only after the complete image passes read-back.
Cancel discards the prepared image without writing. Windows and macOS use this
same flow. No separate firmware download or manual file selection is necessary.

Normal Update and local application-only updates cannot bypass a bootloader
requirement. Once the required bootloader is installed, later compatible
releases use normal Update again.

Release builds publish an `apextx-<model>-<version>.update.json` compatibility
asset for each radio, alongside the firmware, checksums and provenance. Schema 1
records the release version, numeric target, complete binary name/SHA-256 and
required `bootloader` generation. `tools/nb4-release-metadata.py` extracts that
generation from the **built bootloader**, rather than the application version.
The downloader checks the metadata against the verified firmware image. Invalid
or missing metadata on releases newer than alpha.5 blocks automatic updates.
Earlier releases have explicit compatibility defaults: alpha.5 requires generation
1; older releases require generation 0.

When a bootloader change must be installed by users, bump the numeric generation
in `NB4_USB_INFO_PREFIX` (`radio/src/bootloader/nb4_update_id.h`), retaining the
three-field descriptor layout. Future release builds then publish the matching
requirement automatically for both models. Do not use the application's release
version or USB `bcdDevice` to decide whether a bootloader is installed.

#### Version-query compatibility

New firmware publishes a read-only `APXINFO1;<hardware>;<application stamp>` USB
configuration string in its data modes. New bootloaders cache the installed
application's stamp before starting USB Update, after validating the application;
they do **not** report their own build version as the installed firmware. Product,
serial and the exact Update interface remain authoritative for selecting a write
target. The USB `bcdDevice` value is not an ApexTX release number.
The numeric APXINFO generation identifies the bootloader compatibility level
**only when read in Update mode**. A normal data-mode descriptor describes the
application and cannot prove which bootloader is installed. The query uses
standard string slot 4 (`USBD_IDX_CONFIG_STR`) explicitly:
inherited ST class descriptors can advertise configuration index 0 or 2,
which does not point to this callback. No third-party class descriptor or
interface/driver identity is changed to perform the query.

Older bootloaders do not provide this descriptor. The UI says **Not available**
for their application version and treats their bootloader as generation 0.
If the release requires a newer generation, it offers the guided full install
above. A normal update preserves the bootloader and cannot add its missing
Update-mode version reporting. A complete installation requires an explicit click.
**Not available** is a version-query limitation, not an installation failure.
Installation is verified separately against the exact application size and CRC
reported by the bootloader before the app requests a restart.
The new application's normal data modes can report its version, but the user
still enters Update on the radio before writing. Older NB4+ applications used the
NB4 product name in normal data modes; the app waits for Update to establish the
model instead of guessing. On Windows, native HID/storage drivers may prevent
libusb descriptor access in normal modes; Update with WinUSB is the supported
cross-platform discovery/update path. Do not replace a joystick/storage driver
just to enable discovery.

The app makes unauthenticated HTTPS requests to the public GitHub release API
and asset endpoints. It uses a bundled CA store, bounded responses/downloads,
timeouts, a five-minute release cache and serialised USB access while writing.
No radio contents, USB serials or firmware are uploaded. The app supports one
connected radio per operation. ROM DFU is detected separately and cannot be used
by the automatic Update path.

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
- **Automatic startup after verification**: current images contain a dedicated
  `APXROM1` return vector in the bootloader. Only after the complete read-back
  matches does the app use ST's Leave DFU command with that vector. Its entry
  enables CCM RAM before using the stack (the STM32 ROM disables its clock),
  then requests a real MCU reset and one automatic start through the usual
  bootloader/application reset path. Ordinary cold boots and shutdowns keep
  their power-button behavior.
  Older images without the entry, or a radio that stays in ROM DFU, receive
  manual power-cycle instructions instead.

Allow **5–10 minutes** for a complete install. The NB4 screen stays black while
the STM32 ROM handles USB; the app shows backup, erase, write and read-back
verification as separate stages, then starts a compatible image automatically.

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
