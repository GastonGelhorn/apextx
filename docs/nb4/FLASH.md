# Installing ApexTX from the FlySky firmware

This procedure installs ApexTX on an **original FlySky Noble NB4** from a
terminal. It does not apply to the NB4+, NB4 Pro, or another PL18-family radio.
Read the backup and recovery sections before writing anything.

## What the installation changes

The firmware image replaces the complete 2 MiB STM32 internal flash, including
the FlySky bootloader and application. On first setup, ApexTX also offers
to erase the complete 8 MiB external SPI NOR and create its own filesystem.

The STM32 ROM DFU interface can back up the internal MCU flash, option bytes,
and OTP, but it cannot read the external SPI NOR. An exact return to the stock
firmware after formatting requires both the original internal-flash image and a
unit-specific raw dump of the external NOR. Copying the USB storage volume does
not replace the raw NOR dump because factory calibration and configuration can
live outside the visible filesystem.

> **Stop before formatting if an exact stock rollback matters and no raw NOR
> dump exists.** Obtain it with a known-good recovery image or an SPI programmer.
> Never restore a NOR image, option bytes, or calibration from another radio.

The custom firmware does not depend on the FlySky calibration records; it uses
the calibration performed in its own System > Calibration page. The factory
records still matter if the radio may later be returned to FlySky software.

## Requirements

- An original Noble NB4 with a well-charged internal battery.
- A reliable data-capable USB cable connected directly to the computer.
- `git`, CMake, Python 3.11 or newer, an ARM GNU toolchain, and `dfu-util`
  0.10 or newer.
- SDL2 when the native test suite will also be built.
- No receiver or powered vehicle connected during installation.

Close FlySky Assistant, serial terminals, and any program that could claim the
USB device. Do not perform the write through an unstable hub. Losing USB during
the write normally leaves the STM32 ROM DFU available, but the radio will not
have a bootable application until a complete image is written successfully.

## 1. Clone and prepare the source tree

```sh
git clone --recursive https://github.com/GastonGelhorn/apextx.git
cd apextx
git checkout main
git submodule update --init --recursive
```

On Ubuntu 24.04, the inherited EdgeTX setup script can now install the complete
build environment:

```sh
./tools/setup_buildenv_ubuntu24.04.sh
```

Review that script before running it: it installs system packages, a toolchain,
Node.js tools, and Qt and asks for `sudo` when needed. Qt is not required for the
NB4-only build, so an existing EdgeTX development environment can be used
instead. Create the project Python environment in either case:

```sh
python3 -m venv .venv
./.venv/bin/python -m pip install -r tools/requirements-nb4.txt
```

Do not build or flash from a dirty working tree unless the changes are deliberate
and reviewed:

```sh
git status --short
git submodule status
```

## 2. Archive the visible FlySky storage

Start the original FlySky firmware and select its USB storage mode. Copy the
complete mounted volume to a private directory. Set the path for the operating
system before running `rsync`:

```sh
export NB4_VOLUME="/Volumes/NAME_SHOWN_BY_THE_RADIO"
export NB4_BACKUP_ROOT="$HOME/EdgeTX-Noble-backups/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$NB4_BACKUP_ROOT/visible-storage"
rsync -a --exclude='._*' "$NB4_VOLUME/" "$NB4_BACKUP_ROOT/visible-storage/"
```

On Linux, the volume is commonly below `/media/$USER/` or `/run/media/$USER/`.
Confirm that files exist in the backup before continuing. This archive is for
recovery and reference; FlySky model files are not EdgeTX YAML files and must
not be copied blindly into the new `RADIO` or `MODELS` directories.

Eject the volume cleanly, leave USB storage mode, turn the radio off, and wait
until the screen and LEDs are off.

## 3. Enter STM32 ROM DFU and identify the radio

With the radio off, hold the hidden left grip button while turning the radio on.
Keep it held until the STM32 DFU device appears, then release it. Connect USB and
verify the device from the terminal:

```sh
dfu-util -l
```

The output must contain USB ID `0483:df11` and alternate settings for internal
flash, option bytes, and OTP. If more than one STM32 DFU device is connected,
disconnect the others or record the NB4 serial and use `NB4_DFU_SERIAL` in the
commands below.

If `dfu-util` reports no device, do not start flashing. Try another data cable or
USB port, repeat the power/button sequence, and check host USB permissions. A
charging-only cable can power the radio while exposing no DFU interface.

## 4. Back up the MCU before the first write

Run the read-only backup helper. Choose a directory outside the Git repository:

```sh
tools/nb4-backup-mcu.sh "$NB4_BACKUP_ROOT/mcu"
```

With multiple DFU devices connected, select the radio explicitly:

```sh
NB4_DFU_SERIAL=YOUR_SERIAL \
  tools/nb4-backup-mcu.sh "$NB4_BACKUP_ROOT/mcu"
```

The helper uploads and checksums:

- the complete 2 MiB internal flash;
- the STM32 option-byte alternate setting;
- the STM32 OTP alternate setting;
- the DFU layout and device serial used for the capture.

Verify the backup again and keep a second private copy on different storage:

```sh
(cd "$NB4_BACKUP_ROOT/mcu" && shasum -a 256 -c SHA256SUMS)
```

These files can identify or configure one physical transmitter. Do not commit,
publish, edit, or restore them to another unit. The installation never needs to
write option bytes or OTP.

This helper does **not** back up the external 8 MiB NOR. Record the location and
SHA-256 of a raw NOR dump separately if one was made. If no raw dump exists,
acknowledge that an exact factory rollback will not be available after step 7.

## 5. Build and test the firmware

Configure the original NB4 target and build its complete image:

```sh
cmake -S . -B build/nb4-device \
  -DPCB=PL18 -DPCBREV=NB4 -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python" \
  -DNB4_RF_PROFILE=RECOVERED_USART6 \
  -DTRANSLATIONS=EN -DDISABLE_COMPANION=ON

cmake --build build/nb4-device --target firmware-size -j8
```

Run the repository checks before installing a self-built image:

```sh
python3 -m unittest discover -s tools/tests -p 'test_nb4_*.py'
python3 tools/nb4-generate-qualified-profile.py \
  --check docs/nb4/rf/qualification.json

cmake --build build/nb4-device --target native-configure -j8
cmake --build build/nb4-device/native --target tests-radio -j8
```

The image must be at:

```text
build/nb4-device/arm-none-eabi/firmware.bin
```

Record the exact source revision and image checksum:

```sh
git rev-parse HEAD
wc -c build/nb4-device/arm-none-eabi/firmware.bin
shasum -a 256 build/nb4-device/arm-none-eabi/firmware.bin
```

Use `sha256sum` in place of `shasum -a 256` on hosts that provide only the GNU
utility.

Do not flash an image built for `NB4P`, a partial `firmware.elf`, or an image whose
build or validation failed. A wrong target can initialize power, display, touch,
storage, or RF pins incorrectly.

## 6. Flash and verify the complete image

Return the radio to STM32 ROM DFU if it is no longer there, then run:

```sh
tools/nb4-flash.sh \
  build/nb4-device/arm-none-eabi/firmware.bin
```

The script:

1. selects exactly one `0483:df11` device;
2. checks the bootloader and application vectors and image size;
3. writes the image at `0x08000000`;
4. uploads the same number of bytes and compares them byte for byte;
5. sends a standalone DfuSe `:leave` request to the ApexTX bootloader at
   `0x08000000`, without erasing or rewriting the verified image;
6. requires the STM32 ROM DFU device to disappear before reporting success.

Do not disconnect USB or remove power while the write or readback is active. If
the script reports a readback mismatch, leave the radio in DFU and repeat the
write with a known-good image and cable. Do not proceed to storage creation.

The STM32 ROM in the NB4 rejects the standard USB DFU `DETACH` request. The
script therefore uses the DfuSe manifestation sequence to enter the ApexTX
bootloader, which performs the same hardware preparation as a normal start and
then launches the application. If the device remains in DFU, the script reports
an error; do not flash the image again because its readback has already passed.

## 7. Create the ApexTX filesystem

The first ApexTX boot cannot mount the FlySky NOR layout. Open:

```text
Settings > System > Storage > Create filesystem
```

Read the confirmation and start creation only after the backups above have been
checked. This operation erases and verifies the complete external NOR before it
creates FAT over the 7.875 MiB logical FrFTL volume. It destroys the FlySky
filesystem and the unit-specific factory region in that NOR.

Keep the radio powered and do not connect or disconnect USB while the erase and
format are running. The erase can take tens of seconds. Interrupting it cannot
damage the STM32 ROM DFU or the verified internal firmware, but the NOR will be
in an incomplete state and must be formatted again before settings can be saved.

After creation succeeds, restart the radio once. If the storage page still says
that no filesystem exists, repeat creation. If it reports an erase verification
failure, stop; repeated formatting can hide a failing NOR chip or power problem.

## 8. Install storage resources

Start USB Storage mode in ApexTX and set `NB4_VOLUME` to the newly mounted
volume. Install the resources shipped by this repository:

```sh
rsync -a --exclude='._*' sdcard/ "$NB4_VOLUME/"
```

Install compatible EdgeTX sound packs separately under `SOUNDS/en` and, if
wanted, `SOUNDS/es`. System prompts belong below each language's `SYSTEM`
directory. The firmware works without WAV files, but voice prompts will be
silent or missing.

On macOS, remove AppleDouble metadata before ejecting; every `._*.wav` consumes
directory entries and at least one filesystem cluster:

```sh
find "$NB4_VOLUME" -name '._*' -delete
sync
diskutil unmount "$NB4_VOLUME"
```

On Linux, run `sync` and unmount the volume through the desktop or `udisksctl`.
Never unplug the cable while files are still being copied. Leaving USB Storage
mode causes a controlled restart so ownership of the NOR returns to the radio.

## 9. First-start checks

Complete these checks before controlling a vehicle:

1. Open System > Calibration and calibrate steering, throttle, and auxiliary
   controls through their full physical travel.
2. Create a new car model. Do not reuse unconverted FlySky model data.
3. Check steering direction, left/right travel, throttle, brake, trims, channel
   endpoints, and failsafe with the driven wheels clear of the ground.
4. Put the receiver in bind mode, bind once, remove any bind plug or cable, then
   power-cycle both transmitter and receiver and verify automatic reconnection.
5. Confirm that RF state, receiver voltage, transmitter voltage, and loss-of-link
   behavior are credible before driving.
6. Verify that USB Serial and USB Storage both exit cleanly after disconnecting
   the cable.

Treat an unexpected channel direction, missing failsafe, intermittent input, RF
module error, or storage error as a failed installation check. Do not drive until
the cause is understood.

## Recovery and rollback

### Interrupted or unbootable internal-flash write

The STM32 ROM DFU is independent of the EdgeTX application. Re-enter ROM DFU and
run `tools/nb4-flash.sh` again with a verified ApexTX image. If the radio
does not enumerate as `0483:df11`, solve the cable, host-permission, battery, or
button-entry problem before attempting another write.

### Restore the backed-up FlySky internal flash

The following restores only the MCU image. It does not restore the external NOR:

```sh
export FACTORY_MCU="$NB4_BACKUP_ROOT/mcu/internal-flash-2MiB.bin"
dfu-util -d 0483:df11 -a 0 -s 0x08000000 -D "$FACTORY_MCU"
dfu-util -d 0483:df11 -a 0 -s 0x08000000:0x200000 \
  -U "$NB4_BACKUP_ROOT/mcu/factory-readback.bin"
cmp "$FACTORY_MCU" "$NB4_BACKUP_ROOT/mcu/factory-readback.bin"
dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave
```

Only request the DfuSe leave after `cmp` succeeds. It transfers control to the
restored FlySky bootloader at `0x08000000`; a generic `dfu-util -e` does not exit
the STM32 ROM DFU on this radio. If the device remains in DFU, disconnect USB and
perform a normal power cycle. Restoring the FlySky application after step 7 is
incomplete unless the original raw external NOR is also restored with a
compatible recovery image or SPI programmer. STM32 ROM DFU and
`tools/nb4-flash.sh` cannot write that SPI NOR.

Do not write saved option bytes as a routine rollback step. Never try to write
OTP; it is one-time-programmable memory. Those captures are diagnostic evidence
for a targeted hardware recovery.

### Storage does not mount

If the radio boots but storage does not mount, use System > Storage > Create
filesystem only when no data needs to be recovered. Formatting is not a repair
for a radio that resets, loses power, or disconnects during NOR access; solve the
power or USB problem first.

### USB disconnect leaves a black screen

Wait for file copying to finish, eject the volume on the host, and leave the USB
mode from the radio before removing the cable. If the screen remains black,
perform one normal power cycle. Repeated black-screen behavior is a failed check;
preserve the storage contents and return to a verified firmware image rather
than repeatedly formatting the NOR.
