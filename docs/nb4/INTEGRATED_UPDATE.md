# Integrated update: implementation status

The intended maintenance flow covers the application/bootloader, internal RF
module, receiver and selected storage/audio resources. It must preserve cars,
calibration and radio preferences and verify each component separately. A
disconnected receiver remains pending, not "updated".

## Implemented in this first increment

- Inspect four reviewed official packages: original NB4 2.0.116 STD/FCC and
  NB4+ 1.0.31 EU STD/FCC. Exact source files are pinned by SHA-256. Unknown or
  modified files are rejected; there is no override switch.
- Extract only three reviewed complete images: RF-module candidate with
  product ID `0x12340003`, FGr4 V2 candidate and FGr4S V2 candidate. These image bytes are identical across
  the four sources. Other embedded receivers/modules are excluded.
- Verify source/resource hashes, image sizes, pinned image hashes and CRCs.
  Modules use their CRC-16; receivers use their CRC-32.
- Prepare and re-verify a small local RF catalog ZIP without overwriting any
  existing file. The ZIP excludes the stock transmitter firmware, display
  assets, calibration and FlySky executable.
- Assess a supplied receiver inventory as unknown, current, available or
  ahead. Available is not automatically required, and no assessment authorizes
  writing or downgrading.
- Expose preparation through the shared Windows/macOS flasher CLI and a
  standard-library-only Python CLI for Linux. These commands return before
  USB setup/discovery, including on errors.

**RF module/receiver writing is not implemented or enabled by this increment.**
Neither is automatic post-bind notification or live inventory export to the
PC. The radio's existing readout remains on demand. This catalog is not an
ApexTX radio image: normal Update and ROM recovery refuse it.

## Prepare locally, without connecting a radio

Supply your own supported package from [NB4 downloads](https://www.flysky-cn.com/nb4-xiazai)
or [NB4+ downloads](https://www.flysky-cn.com/noble-nb4plus-downloads). An `.exe`
on macOS/Linux is parsed as data and never executed. No factory files are
downloaded automatically, bundled with ApexTX or committed to this repository.

Inspect, prepare a new local file, then independently verify it:

```sh
python3 tools/apextx-rf-package.py /path/to/official-updater.exe
python3 tools/apextx-rf-package.py /path/to/official-updater.exe \
  --output /path/to/new-apextx-rf-catalog.zip
python3 tools/apextx-rf-package.py /path/to/new-apextx-rf-catalog.zip --check
```

The same backend is included in ApexTX Flasher:

```sh
python3 tools/apextx-updater.py --rf-source /path/to/official-updater.exe \
  --rf-output /path/to/new-apextx-rf-catalog.zip
python3 tools/apextx-updater.py --rf-bundle /path/to/new-apextx-rf-catalog.zip
```

These commands prepare/verify host files only. The normal graphical update and
recovery operations are unchanged. JSON explicitly reports `device_writes:
false` and `rf_write_supported: false`. Filenames are not trusted: a renamed
file is accepted only if its content hash matches. After an interrupted host
write, re-verification is mandatory; an incomplete file is not a usable
catalog. Choose a new output filename for another attempt.

## Post-bind behavior to implement next

FlySky's [official FAQ](https://www.flysky-cn.com/0enfaq) describes prompting
for receiver updates and transferring them wirelessly. Its [receiver update
guide](https://www.flysky-cn.com/journal/2022/3/11/operation-instructions-for-updating-afhds3-rf-library-firmware)
distinguishes connected updates from forced-update/recovery procedures.

In ApexTX a successful bind should initiate one bounded version check after
the module/link settle, using the non-blocking diagnostic transport. Results
must belong to that car/receiver connection, not a previous bind.

| State | Intended behavior |
| --- | --- |
| Current | No update warning |
| Available, compatible image present | Offer an update with confirmation |
| Known incompatible RF generation | Explain the required update/recovery path |
| Receiver ahead of imported package | Do not offer an automatic downgrade |
| One-way, disconnected, stale or unknown hardware | Not checked; do not claim an update is required |

`assess_receiver()` currently implements only the read-only comparison. For
developer tests, `--check --assess inventory.json` accepts an object containing
`connected`, `valid`, integer `product` and integer `firmware`. This is not live
device identification and cannot authorize a write. Versions remain raw vendor
values, not guessed semantic versions.

## Gates before enabling RF writes

1. Confirm bootloader protocols, image selection, chunk limits,
   acknowledgements, final integrity checks and reset behavior.
2. Establish recovery for interrupted module and receiver writes. Transmitter
   ROM DFU must not be assumed to recover these separate components.
3. Pin live identity and hardware revision throughout the operation. Shared
   product IDs alone must not authorize an image for another device.
4. Check power, motor-safe bench conditions, compatible RF generation and
   storage. Back up mutable settings before changing firmware.
5. Test success, rejection, interruption and resumption in emulation and on
   FGr4 V2/FGr4S V2, then qualify NB4+ separately.
6. Re-read versions and verify operation after reboot before reporting success.
   Track each component separately: application success does not imply RF or
   audio success.

This is an end-user installation preparer, not a memory reader/disassembler.
Private analysis scripts, original-code addresses, dumps and proprietary
images remain outside Git and the issue tracker.
