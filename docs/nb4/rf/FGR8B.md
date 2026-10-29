# FGr8B: reviewed images and integration handoff

This is an **offline catalog and read-only assessment** increment for #23/#33.
It does not qualify FGr8B RF writes, recovery, or NB4+ backup-agent operation.
It is prepared separately so the active integrated-update checkout is untouched.

## Verified package differences

[Semtexmagix's report](https://github.com/GastonGelhorn/apextx/pull/33#issuecomment-6472880983)
identifies a real FGr8B on NB4+ as product `0x00010107`, application `0x0117`.
Our offline comparison confirms that image in both already-pinned NB4+ packages,
and finds an older, different FGr8B image in the original-NB4 packages:

| Reviewed input | FGr8B application | Complete bytes | SHA-256 |
| --- | --- | --- | --- |
| NB4 2.0.116 STD and FCC | `0x0114` | 64464 | `a3f55ca1b90d0e82b1302ea3004600431d79b62fc9231ca54af4ebbd10aeb8b5` |
| NB4+ 1.0.31 EU STD and FCC | `0x0117` | 88320 | `f0f25179d309f90d2d33611cbbacae200457c058aee3d2255b558e5eaaa5a37a` |

All four source-file hashes and both resource hashes match the existing pinned
profiles. Both FGr8B variants pass their trailing CRC-32 as well as exact image
size/SHA-256. [Audit metadata](fgr8b-evidence.json) records each input separately;
no source executable, extracted firmware or private radio data is committed.
These numbers are receiver version words, not transmitter or desktop versions.

## Independent extractor comparison

The contributor's reference was inspected and pinned to commit
[`9a5c96da3f9d345697d5580219fe5a00af65b278`](https://github.com/Semtexmagix/apextx/tree/9a5c96da3f9d345697d5580219fe5a00af65b278/tools/rf-image-extractor),
Python blob `5150358e7e61fcee37be0b4dc2e7f0c6c4b7636c`.
Only its reviewed parsing functions were called, on local files whose source
hashes were checked first. Neither updater executables nor its GUI/destination
or write functions were run; no radio or FlySky service was accessed.

Its PE parser returns byte-identical resources for all four packages. Its image
parser reproduces the original three pins and FGr8B `0x0117` byte for byte in
both NB4+ packages (nine entries each). It rejects the NB4 resources because its
format check expects the NB4+ four-byte header value. This agrees with its README,
which explicitly says the original-NB4 packages were not tested. Keep the bounded
ApexTX parser and its source/resource/image pins; the reference is a useful
independent cross-check, not a drop-in importer for every source.

## Prepared catalog behavior

- Preserve the initial module/FGr4 V2/FGr4S V2 indices `0..2` unchanged.
- Append the explicit receiver variants after the initial three image indices,
  but put **only the matching, CRC-verified variants** in each source profile.
  See [the full receiver catalog](RECEIVER_CATALOG.md) for the other RX models
  and the deferred INr4-GYB candidate.
- A live FGr8B at `0x0117` is `current` with an NB4+ catalog and `ahead` with
  an NB4 catalog. The latter must never become a downgrade offer.
- Lower/current/higher versions remain informational `available/current/ahead`.
  Unknown, stale or disconnected readings remain `unknown`. No assessment marks
  an update required or authorizes writing.
- Rebuild development catalog ZIPs after integration. Their manifests are checked
  against exact profile contents; an older three-image catalog is not silently
  relabelled or extended. Finish/cancel any active bench operation first.

For local verification with privately obtained, already-pinned inputs:

```sh
python3 tools/apextx-rf-package.py /path/to/input.exe --output /path/to/new-catalog.zip
python3 tools/apextx-rf-package.py /path/to/new-catalog.zip --check
python3 -m unittest tools.tests.test_nb4_rf_catalog tools.tests.test_nb4_rf_fgr8b
```

## Integration into the active maintenance branch

This increment is based on the committed #33 head `306b1dab5`, not on its
uncommitted maintenance engine. Apply after that work reaches a safe checkpoint:

1. Carry over `COMMON_IMAGES`, both FGr8B pins and per-source image selection.
   Keep the branch's newer module `reported_product`, profile IDs, catalog format
   and maintenance protocol. Do not revert them to this increment's older base.
2. Regenerate/check `radio/src/nb4_rf_images.h` with `tools/nb4-rf-generate.py`.
   (Superseded: install rules are now product-based, `UPDATE_RULES` in the
   catalog, and cover the latest image of every receiver on both radios.)
3. Run RF catalog, maintenance-transfer, generated-header and native RF tests.
   The added tests also assert that none of the new receiver candidates has a hardware qualification rule.
4. Update the maintenance documentation's image counts and supported candidates;
   keep FGr8B write/recovery capability disabled until separately qualified.

## Newer component images from an NB4+ source on an original NB4

The original-NB4 package's older receiver payload is not a reason to cap ApexTX
on NB4 at that receiver version. Treat the updater package as a source container:
an NB4 may assess the isolated `0x0117` FGr8B image from the reviewed NB4+ package.
A test covers an NB4 inventory at `0x0114` with that candidate: `available`,
`required=False`, `write_supported=False`.

Keep **source provenance**, **transmitter target** and **component compatibility**
separate. For a future writer, choose by the receiver's complete hardware tuple,
its bootloader/update protocol, RF/region compatibility and the qualified
transmitter/module combination. Package radio model alone must neither certify
compatibility nor rule out a newer isolated component image. The full transmitter
firmware, bootloader, calibration and storage resources stay model-specific.

The three original pinned component images are byte-identical in all four
packages, including the main RF-module image. That proves image identity; it is
not a blanket proof that the transmitters or every FGr8B revision are identical.
The comment's receiver is already at `0x0117`, so it needs no automatic update.

As checked on 2026-10-28, the [NB4 download page](https://www.flysky-cn.com/nb4-xiazai)
lists 2.0.116 with an upload date of 2025-08-28, while the
[NB4+ page](https://www.flysky-cn.com/noble-nb4plus-downloads) lists 1.0.31 with an
update date of 2026-09-14. These listing dates and the payload differences do not
prove that support for the original NB4 has ended. Do not present that hypothesis
as an official manufacturer policy.

## Useful next contribution from the volunteer

Start with read-only evidence on his NB4+ and FGr8B: exact transmitter build,
receiver model/revision and the **complete five-word identity reply**, with the
transmitter serial kept private. The current report supplies product/application
only; hardware, RF hardware, bootloader and RF-library words are still missing.
Confirm repeat reads after reconnect/bind and that `0x0117` is shown as current.

Do not ask him to downgrade to `0x0114` or reinstall `0x0117` just to exercise an
unqualified write path. A later private bench requires explicit consent, verified
backups and independently demonstrated receiver recovery, separate from radio
ROM DFU. A receiver already at the offered version is useful for read-only tests
but is not an automatic update candidate.
