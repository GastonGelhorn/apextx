# Integrated maintenance: development status

The complete maintenance flow is still in development. Application updates
continue to use ApexTX Update; a successful application update does not mean
that the RF module, receivers or audio have been updated.

## Available now

- **System > Update** shows component versions, an explicit maintenance connection to
  ApexTX Flasher and the existing radio-firmware update action. Receiver & RF
  links to the same page.
- A stable module/receiver connection starts a bounded version check without
  requiring a visible editor. Binding disables queries; reconnecting or changing
  the car/RF configuration invalidates the previous readings.
- Windows/macOS Flasher and the Linux CLI can explicitly read an inventory
  over USB serial. Opening the app never opens this port or writes firmware.
  Desktop snapshots become stale after ten seconds.
- The RF catalog preparer validates exact package hashes, complete image sizes,
  image hashes and CRCs. The shipped catalogs under `tools/rf-catalog/`
  (`nb4.zip`, profile `nb4-rf-2`; `nb4plus.zip`, profile `nb4plus-rf-2`) are
  merged from the verified NB4 2.0.116 and NB4+ 1.0.31 source bundles
  (`apextx-rf-package.py <bundles> --merge <profile> --output <zip>`) and
  carry the latest verified image of every product for both radios: the NB4
  and NB4+ share their RF hardware, and FlySky no longer publishes NB4
  packages. They ship in every release package as `rf/catalog.zip` and are
  embedded in ApexTX Flasher.
- Audio installation verifies a full filesystem backup on the computer before
  replacing any file. It preserves personal recordings and omitted assignable
  tracks, retains storage headroom and journals interrupted installations.
- The desktop interface supports English, Spanish, German and Simplified Chinese.
- The read-only RAM agent has copied and independently verified the complete
  internal flash and raw NOR of an original NB4. This does not qualify NB4+
  startup or NOR restoration; first-installation formatting remains blocked.
- Module and FGr4S V2 image staging, on-radio hash verification and cancellation
  have been exercised on an original NB4 with RF writes disabled. Cancellation is recorded
  on both the transmitter and the computer; it does not install the RF image.

The radio now contains a single RF transfer/recovery engine, verified file
staging and a durable operation journal shared by local controls and the
maintenance CLI. Image bytes stay in external storage. The engine checks the
complete identity, every requested range, the image hash, transfer coverage
and a fresh version after restart. It checkpoints the boot peer before
sending image data and locks driving while an interrupted write needs recovery.

Production builds install, on both NB4 and NB4+, the RF module, the FGr4 V2
and FGr4S V2 images (catalog indices 0–2, whose transfer and recovery were
exercised on the bench, see [RF bench procedure](RF_BENCH.md)) and the latest
image of every other receiver (INr4-GYB 0x108, FGr8B, FGr4B and FGr12B
0x117, Mini-Z 0x108; indices 7–11). An image is selected the way the stock
firmware selects it: the connected component must report the image's product
ID, the image hash must match the catalog, and the component's bootloader
verifies its own product word before accepting data. Identity words
(hardware, RF hardware, bootloader, library) are read as diagnostics and
freeze the identity for one operation; they do not gate installation. The
older per-package variants (indices 3–6) have no rule and are never
selected. The flasher's one-click update only stores the images on the card
(`/FIRMWARE/RF/`): the radio hashes each file before it becomes visible, and
the computer never writes an RF component in that flow. A separate private
bench build remains bound to one transmitter serial number for images
without a rule.

The experimental desktop **Components** wizard and Linux component CLI share
a persistent dependency plan. They verify a complete offline package, pin the
transmitter, require its recovery backup, and keep disconnected receivers
pending. A separate RAM agent implements complete NOR restoration; an internal
firmware restoration command verifies the MCU identity before writing. Both
restoration paths still require physical qualification. Passing software tests
does not establish RF recovery or complete raw-NOR restoration.

## Complete component plan

1. Open **Components** in ApexTX Flasher. Download the complete compatible
   release package, or choose a local ZIP/tar.gz containing the firmware,
   recovery agents, resources and optional RF catalog. Preparation verifies
   sizes, checksums and hardware compatibility without writing to the radio.
   Older packages without both recovery agents cannot start this workflow;
   their ordinary application update remains separate.
2. Create a complete backup in **ROM DFU**, or select an existing verified
   backup for this exact transmitter. The app stores the plan and backups
   privately on the computer. The read-only RAM agent checks the MCU/NOR
   identity and independently reads both memories twice.
3. In **Maintenance**, read the application, module, receiver and voice-pack
   inventory. An unavailable receiver remains pending. In **ApexTX Update**,
   read versions again to check the bootloader requirements. These are
   different USB modes; the radio's three-option **Serial** connection is
   not Maintenance.
4. Review the selected components and confirm the plan. Audio is initially
   unselected: keep the installed voice pack unless a new pack is explicitly
   chosen. Changing a component, image or live identity invalidates approval.
   When the same release has published voice packs, choose one language and
   **Download and prepare voice pack** directly in the app. A local audio ZIP
   also works. Downloads verify the release checksum, pack language and version;
   they prepare files only and still require plan approval before installation.
5. Install the application in Update. ROM DFU is used only for a required
   bootloader change or recovery. After restarting, reconnect Maintenance and
   read versions to verify the exact application size/CRC. A completed USB
   transfer alone does not mark this component complete.
6. Prepare the module and then the connected receiver. Preparation transfers
   a verified file to external storage; **Install** is a separate action.
   Current versions require no write. A failed dependency stops subsequent
   components. Production builds write only images with an install rule
   (catalog indices 0–2 and 7–11); use the private qualification procedure
   below to bench-test any other image.
7. Prepare the storage connection from Maintenance. Reconnect USB and select
   **Storage**, then choose the mounted radio volume in the app. A fresh
   unit-bound token links that volume to this plan; capacity or a volume name
   alone is insufficient. Install resources and optional audio by file after
   verifying a filesystem backup. Personal and modified files are preserved;
   insufficient space is a conflict that can be deferred.
8. Safely eject Storage, restart, reconnect Maintenance and read versions.
   The result distinguishes complete, partial and recovery-required. Resume
   the saved plan after a disconnect or app restart; no component starts
   automatically. Desktop self-update cannot restart the app during an
   unresolved installation.

First installation remains disabled until complete rollback is physically
demonstrated for the selected transmitter model. Cars from the previous system
remain in the private backup and are not automatically converted. After an
initial installation, complete **Menu > System > Calibration** before setting
up a car, binding, and checking channels and failsafe.

### Linux / offline CLI

Use the same private session directory for every step. The example below uses
an extracted complete package; replace the serial and paths with the selected
transmitter and its files. Preparing the plan does not access USB.

```sh
python3 tools/apextx-components.py --session /private/plan \
  --create /path/to/extracted-package --serial TRANSMITTER_SERIAL --target nb4
# ROM DFU: create a complete copy, or attach an already verified one.
python3 tools/apextx-components.py --session /private/plan --action backup
python3 tools/apextx-components.py --session /private/plan \
  --action attach-backup --path /private/complete-backup
# Read in Maintenance; repeat in Update for bootloader requirements.
python3 tools/apextx-components.py --session /private/plan --action read
# Optional: replace the default selection, explicitly deferring omitted items.
python3 tools/apextx-components.py --session /private/plan \
  --select application module receiver resources
python3 tools/apextx-components.py --session /private/plan \
  --action approve --confirm-unit TRANSMITTER_SERIAL
python3 tools/apextx-components.py --session /private/plan \
  --action install --component application --confirm-unit TRANSMITTER_SERIAL
# Restart, reconnect Maintenance, read and review the updated plan again.
python3 tools/apextx-components.py --session /private/plan --action read
python3 tools/apextx-components.py --session /private/plan --action status
```

For a reviewed RF candidate use `--action stage --component module` or
`receiver`. `--action cancel` cancels preparation; `install` and `recover`
require `--confirm-unit`. A current component is skipped; `--reinstall` is
reserved for an explicitly selected bench test. See [RF bench](RF_BENCH.md).

For resources/audio first use `--action storage` in Maintenance. In Storage
use `--action install --component resources --path /mounted/radio
--confirm-unit TRANSMITTER_SERIAL`. To change voice packs, first select one
with `--action audio-pack --path /path/to/audio.zip`, review and approve again,
then install the `audio` component with the mounted volume path. Failed
operations retain their original package and backup for resume or restoration.

### Recovery choices

- **Recover RF** uses the single radio RF engine, its saved peer response and
  verified image. It repeats the complete image through the component's own
  bootloader. Transmitter ROM DFU cannot repair a receiver or RF module.
- **Restore previous audio** uses the verified per-file computer backup in
  Storage mode. It preserves later unrelated personal recordings and refuses
  conflicting edits. An interrupted restoration stays marked as incomplete.
- **Restore complete external storage** loads a separate NOR restoration
  agent into RAM from ROM DFU. It checks the exact MCU UID/NOR identity,
  validates each complete sector before erase, verifies each written sector
  and independently reads the full 8 MiB back. Only the enabled NOR chip is
  accepted. An interrupted restore restarts from sector zero and skips sectors
  already matching the backup; it never guesses a partially written offset.
- **Restore internal firmware** writes the verified private 2 MiB backup in
  ROM DFU after checking the actual MCU UID. It independently reads back the
  entire image. Neither restoration writes OTP, option bytes, security
  settings or a component's calibration/identity areas.

These destructive restoration actions require a separate confirmation naming
the transmitter. They replace the selected memory with its private backup;
they do not restore changes made after that backup. Do not deliberately cut
power until the corresponding independent recovery has passed on hardware.

## Read a live inventory

1. Choose **ApexTX Flasher** on the radio's USB menu when plugging in, or open
   **Menu > System > Update > Connect to ApexTX Flasher** and confirm. An
   existing ordinary Serial connection can be switched directly.
2. Connect USB if needed. Close USB Storage or Joystick before switching.
3. In the desktop app, open **Components > Read component versions**.

The radio remains on the Update page. **Flasher mode active** confirms that the
maintenance mode is selected; it does not mean a firmware installation started.
Disconnecting USB makes the connection action available again.
The desktop app identifies this as **Maintenance connected**. **Update radio
firmware** is a separate mode for installing the transmitter application; it
is not needed for reading or preparing RF components.

The same explicitly selected connection works from Linux:

    python3 tools/apextx-updater.py --inventory

The versioned APXM protocol uses bounded frames, CRC-32 and request IDs. It is
separate from debug/CLI serial sessions and the APXINFO bootloader descriptor.
Capabilities separately report inventory, verified staging, RF write, RF
recovery, asset and restart support. Staging a file does not authorize or start
a write. `AssetBegin`/`AssetData` (22/23) store a catalog image in 512-byte
chunks and the radio verifies its SHA-256 before keeping it; `RebootUpdate`
(24) restarts into ApexTX Update after the reply. Unsupported commands are
rejected. The ordinary inventory client cannot send RF commands, and the
flasher's asset client cannot prepare, start or recover one.
A disconnected or one-way receiver is not considered checked or updated.

## Prepare and verify an RF catalog offline

The preparer accepts a supported local installation package, parses it as data,
and never executes it or accesses USB:

    python3 tools/apextx-rf-package.py /path/to/package.exe --output /path/to/new-rf.zip
    python3 tools/apextx-rf-package.py /path/to/new-rf.zip --check
    python3 tools/apextx-updater.py --rf-bundle /path/to/new-rf.zip

Catalog format 3 contains component identities, versions, sizes, hashes and
install rules. The module's reported product ID is mapped explicitly to its
image ID. Receiver versions preserve all five identity words and use the fourth
word as the application version. Rebuild older development catalogs.
The catalog is not a transmitter image; normal Update and ROM recovery reject it.

## Back up ApexTX storage

With the radio in USB Storage mode, select the mounted radio volume explicitly:

    python3 tools/apextx-updater.py --backup-storage /path/to/radio-volume --backup-output /path/to/new-private-backup
    python3 tools/apextx-updater.py --verify-backup /path/to/new-private-backup

This copies and independently verifies settings, cars, history, themes and
sounds. The destination must be outside the radio and must not exist. Backups
remain private on the computer. A filesystem backup is **not a raw NOR image**
and does not satisfy the first-installation rollback requirement.

## Development captures

The development view is rendered in both orientations. These captures show
the native renderer, not hardware update qualification.

| Language | Portrait | Landscape |
| --- | --- | --- |
| English | [View](images/update-components-en-portrait.png) | [View](images/update-components-en-landscape.png) |
| Spanish | [View](images/update-components-es-portrait.png) | [View](images/update-components-es-landscape.png) |
| German | [View](images/update-components-de-portrait.png) | [View](images/update-components-de-landscape.png) |
| Simplified Chinese | [View](images/update-components-cn-portrait.png) | [View](images/update-components-cn-landscape.png) |

Active maintenance mode: [portrait](images/update-connected-en-portrait.png),
[landscape](images/update-connected-en-landscape.png). The same state is covered
in all four languages and both orientations.

RF notice: [Spanish portrait](images/rf-update-notice-es-portrait.png),
[German landscape](images/rf-update-notice-de-landscape.png).
The [desktop component plan](images/flasher-components-es.png) uses a synthetic
radio and private paths for UI verification; it is not a completed hardware run.
The [voice-pack selector](images/flasher-voice-es.png) prepares one explicitly
chosen language and retains the installed pack until installation is confirmed.

## Remaining release gates

1. Qualify live hardware/revision selection, module/receiver bootloader entry,
   image transfer, acknowledgements, final integrity checks and restart.
2. Prove interrupted-write recovery independently for the radio, RF module and
   both receiver models. Do not assume the radio's ROM DFU repairs other chips.
3. Qualify the read-only RAM backup agent on NB4+ and complete NOR/internal
   restoration on both transmitters before enabling first installation.
4. Exercise the component wizard, storage handover, voice selection, resource
   preservation and interruption/resume using actual mounted radio storage.
5. Exercise normal updates, first installation and controlled interruption on
   **both NB4 and NB4+**, each with FGr4 V2 and FGr4S V2.
6. Publish integral-update support only after both hardware checklists pass.
   An experimental software build or passing simulation is not that qualification.
