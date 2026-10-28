# Experimental RF bench procedure

This procedure is for development qualification. It is not a public integrated
updater or a claim that interrupted RF writes have been recovered on hardware.
Normal builds cannot write RF firmware. Do not perform a deliberate power cut
until the component's independent recovery has been demonstrated.

## Identity and versions

The initial original-NB4 measurements are:

| Component | Product | Hardware | Application | Other identity words |
| --- | --- | --- | --- | --- |
| RF module | 0x00010003 | 0x00010003 | 0x00010021 | RF library 0x00030000 |
| FGr4 V2 | 0x12340004 | 0x0104 | 0x0113 | RF hardware 0x0104, boot 0x0103, library 0x0301 |
| FGr4S V2 | 0x12340006 | 0x0104 | 0x0111 | RF hardware 0x0104, boot 0x0107, library 0x0301 |

The FGr4S product ID is shared. Its full hardware tuple must match, not merely
its name or product ID. NB4+ readings still require independent confirmation.
These versions already match the prepared images, so a normal upgrade performs
no RF write. Bench reinstallation must be selected explicitly.

## Build for exactly one transmitter

Use the qualified compiler and normal target flags described in the repository
development instructions, with a separate build directory and:

    -DNB4_RF_UPDATE_BENCH=ON
    -DNB4_RF_UPDATE_BENCH_SERIAL=TWELVE_UPPERCASE_HEX_DIGITS
    -DAPEXTX_PUBLIC_RELEASE=OFF

Use NB4_PLUS_HW=OFF for original NB4 and ON only for NB4+. Never install an
image built for the other display. The build refuses an invalid serial or a
public bench image. On the radio, Connect to ApexTX Flasher explicitly arms
the bench session; arming does not survive a reboot.

## Create a private complete backup

The read-only RAM agent copies the internal 2 MiB and raw external 8 MiB NOR.
Its linker and verifier reject load segments outside SRAM and flash-write
drivers. Complete two-pass reads have been exercised on an original NB4
running ApexTX. NB4+ startup, first-installation rollback and interrupted
initialization still need physical qualification.

Build the nb4-backup-agent target with the qualified compiler. Verify the ELF,
binary and SHA using tools/nb4-verify-backup-agent.py and record the report.
The verifier also checks the complete C runtime startup and return sequences;
an SRAM-only link is insufficient if section removal breaks initialization.
Launching this agent requires STM32 ROM DFU, not ApexTX Update:

    python3 tools/apextx-full-backup.py \
      --agent /path/to/nb4-backup-agent.bin \
      --agent-sha256 VERIFIED_SHA256 \
      --serial TRANSMITTER_SERIAL \
      --output /path/to/new-private-backup
    python3 tools/apextx-full-backup.py --verify /path/to/new-private-backup

The host identifies the installed application before loading RAM, checks the
same unit after enumeration and reads both memories twice. It retains the
initial MCU read separately even if launching the agent fails. The agent
rejects erase and nonvolatile write requests.

Protocol 3 includes an explicit exit command, a new two-second Power press to
switch off and a two-minute idle timeout. The Power hold used to enter ROM is
ignored until it has been released once. A hardware watchdog covers stalled
initialization or processing; the timer interrupt cannot keep a hung main
loop alive.

After a complete backup the host requests shutdown. Disconnect USB and start
the radio normally. If USB exit cannot be confirmed, release Power and hold
it again for two seconds. With external power still connected, a subsequent
new long press restarts the installed firmware. These exits still need
physical qualification; passing host tests is not that qualification.

A filesystem backup is not a raw-NOR backup. Formatting remains blocked until
complete restoration has been demonstrated. The separate
`nb4-nor-restore-agent` target implements the NOR writer; it is never linked
into the read-only backup agent. Verify it with
`tools/nb4-verify-backup-agent.py --restore` before use.

The experimental restoration entry point is:

    python3 tools/apextx-restore-storage.py \
      --agent /path/to/nb4-nor-restore-agent.bin \
      --agent-sha256 VERIFIED_SHA256 --backup /path/to/complete-backup \
      --operation /path/to/new-private-restoration --serial TRANSMITTER_SERIAL \
      --confirm-unit TRANSMITTER_SERIAL

Review the CLI's `--help` and the exact memory-replacement plan first. Loading
the agent is RAM-only. After the host and agent match the MCU UID, JEDEC ID,
capacity, nonce and complete backup checksum, each sector must be fully
received and hash-checked before any erase. A fresh run starts at sector zero,
skips matching sectors and verifies the whole image. Keep the restoration
journal and the original backup after any failure. This writer has software
tests but is not yet physically qualified.

## Prepare, review and explicitly install one component

Prepare and verify the local RF catalog as described in INTEGRATED_UPDATE.md.
Open the radio's maintenance connection, then stage the selected image:

    python3 tools/apextx-rf-maintenance.py --stage /path/to/rf.zip \
      --image fgr4-v2 --backup /path/to/new-private-backup \
      --operation /path/to/new-private-operation --serial TRANSMITTER_SERIAL

The choices are module, fgr4-v2 and fgr4s-v2. Staging checks the backup's unit,
the pinned image, the live component identity, storage space and the independent
radio-side hash. It does not enter the RF bootloader.

Before preparing or starting a receiver update, bind it to the current car and
wait for a live two-way connection and a fresh matching identity. A saved bind
or cached version alone is insufficient. If the receiver disconnects after
staging, START is rejected; reconnect it and review a fresh plan. Updating the
internal RF module does not require a connected receiver.

Review the printed current/proposed versions. Keep only the intended bound
receiver powered, disconnect the motor, secure the car on the bench, and use
a transmitter battery of at least 3.8 V with stable receiver power. Explicitly start:

    python3 tools/apextx-rf-maintenance.py --install --reinstall \
      --operation /path/to/new-private-operation --serial TRANSMITTER_SERIAL \
      --confirm-unit TRANSMITTER_SERIAL

Omit --reinstall for a genuine newer-image update. Downgrades are rejected.
The local radio action uses the same engine and requires confirmation.
The host persists intent before START and never repeats START after an
ambiguous disconnect. The radio alternates two checksum-protected journal
slots, preserving the previous verified phase if a write is interrupted.
A damaged record cannot silently release the driving lock.
Keep the private backup and operation directory.

## Status and recovery

    python3 tools/apextx-rf-maintenance.py --status \
      --operation /path/to/new-private-operation --serial TRANSMITTER_SERIAL

To cancel a prepared image without installing it:

    python3 tools/apextx-rf-maintenance.py --cancel \
      --operation /path/to/new-private-operation --serial TRANSMITTER_SERIAL

The computer marks it cancelled only after the radio confirms that no RF bytes
were transferred and driving is unlocked.

Cancellation is available only before bootloader entry. After entry, failure
locks driving and records Recovery required. Reconnect the same transmitter
and bound receiver, arm the same bench build, inspect the cause and explicitly
recover:

    python3 tools/apextx-rf-maintenance.py --recover \
      --operation /path/to/new-private-operation --serial TRANSMITTER_SERIAL \
      --confirm-unit TRANSMITTER_SERIAL

Recovery revalidates the unit, car/RF configuration, image hash and saved boot
peer response. It repeats the complete image; it does not guess a resume offset.
The opaque boot-response fields and actual recovery behavior still require
bench qualification. ROM DFU of the transmitter is not RF-component recovery.

There is one automatic check for an entry that received no boot-peer
checkpoint and could not have transferred image data. Driving remains locked
while the radio requests a fresh normal-application identity. Only an exact
match with the original hardware and software, followed by a verified
cancellation journal write, releases that lock. Damaged journals or any saved
boot-peer checkpoint prevent this exit. This does not mark the RF image as
installed.

**Update radio firmware** always updates the transmitter application. The
private bench **Reinstall RF / Recover RF** action is separate, so a failed RF
operation does not replace the route needed to install an application repair.

Completion requires every image byte to have been requested and a fresh
matching component version after restart. A peer's success byte alone is not
completion. Test module and each receiver separately, then persistent binding,
channels, telemetry and failsafe. Repeat on NB4+ before enabling public writes.
