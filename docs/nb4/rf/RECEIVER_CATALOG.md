# Receiver candidates from reviewed NB4 and NB4+ packages

Choose the **latest verified, compatible component image**, independently of the
transmitter model from which its updater package came. Never install a complete
NB4+ transmitter image, bootloader or resources onto an original NB4. Source
provenance and component compatibility are separate decisions.

This increment prepares exact metadata pins and read-only assessment for the
active #23/#33 maintenance work. It does not enable RF writes or certify hardware.
It is based on committed #33 (`306b1dab5`) so that the dirty maintenance checkout
can continue without interference.

## What the four pinned packages actually contain

The STD/FCC pair has byte-identical component resources within each transmitter
family. The original module/FGr4 V2/FGr4S V2 images are identical across all four
sources. The other receiver payloads differ:

| Component | NB4 2.0.116 | NB4+ 1.0.31 EU | Read-only catalog decision |
| --- | --- | --- | --- |
| Main RF module | `0x10021` | `0x10021` | Same pinned image; no newer image here |
| FGr4 V2 | `0x0113` | `0x0113` | Same pinned image |
| FGr4S V2 / shared product ID | `0x0111` | `0x0111` | Same pinned image; full hardware tuple remains necessary |
| FGr4B | `0x0103` | `0x0117` | Both variants pass size/hash/CRC checks |
| FGr8B | `0x0114` | `0x0117` | Both variants pass; see [Semtex evidence](FGR8B.md) |
| FGr12B | `0x010c` | `0x0117` | Both variants pass size/hash/CRC checks |
| Mini-Z receiver entry | `0x0107` | `0x0108` | Both variants pass; update protocol still needs qualification |
| INr4-GYB | `0x0108` | `0x0109` | Accept `0x0108`; defer `0x0109` pending checksum-format investigation |

[Machine-readable evidence](receiver-catalog-evidence.json) records the exact
source/resource hashes, all twelve distinct receiver-image hashes/sizes and CRC
results. Eleven receiver variants are accepted; the registry contains those plus
the original main RF-module image. NB4 profiles export eight images and NB4+
profiles seven. Unreviewed extra RF-module images remain excluded.

Both NB4+ `0x0109` INr4-GYB payloads have the reported SHA-256
`74e5ae3b30e24b15078f138fc82b6f7ac96cc2bad1dcc75df4b034a46b2aa5a3` and size
71344, but their last word is `0x00000000`; CRC-32 of the preceding bytes is
`0xd7d0fb34`. This fails the current receiver CRC convention. It is **not** proof
that the manufacturer image is corrupt: a different checksum/layout is possible.
Do not remove the CRC gate, recalculate/patch vendor bytes, or present this
candidate as installable until its actual verification contract is understood.

The contributor extractor independently reproduces all seven NB4+ receiver
payloads, including this deferred gyro image. Its vector plausibility indicator
is not a replacement for source/image pins or checksum validation.

## Source-independent assessment

`assess_receiver_catalogs(descriptions, installed)` accepts descriptions obtained
from verified catalog ZIPs. It compares the receiver product and version across
those sources, deduplicates identical pins, and reports `available`, `current`,
`ahead` or `unknown`. Equal product/version with different payload hashes is
`ambiguous`, with no selected image. Disconnected, stale, malformed or forged
inputs do not become upgrade offers.

An original NB4 with an FGr8B at `0x0114` can therefore see the `0x0117` candidate
from the NB4+ source. No transmitter-model filter should hide that candidate.
The result still has `required=False`, `write_supported=False` and
`compatibility_verified=False`. Deferred newer payloads are reported separately
as `known_unverified`, never selected. A receiver already ahead is not downgraded.

Before enabling a real update, the maintenance engine must independently check
fresh full identity, hardware revision, bootloader/update protocol, RF/region and
library compatibility, transmitter/module support, complete backup and recoverable
interruption. The Mini-Z catalog entry is not assumed to share another receiver's
update protocol. Release age or a shared product ID is not sufficient evidence.

## Handoff after the active work reaches a safe checkpoint

- Carry over the new constants, per-source selections, assessment helper and tests
  without reverting the current catalog format 3, generic profile IDs, module
  `reported_product` mapping or maintenance protocol.
- Preserve existing image indices `0..2`. Regenerate the firmware header with
  `tools/nb4-rf-generate.py`; new indices must have no hardware qualification rules.
  Finish/cancel any active bench operation before replacing its catalog files.
- Rebuild catalog ZIPs. Wire candidate selection into the component planner;
  the current three-choice maintenance CLI is not extended by this metadata work.
  Do not automatically install every embedded component or silently reflash an
  equal version.
- Keep INr4-GYB `0x0109` outside accepted profiles. Investigate its verification
  format separately and add a specific tested validator before accepting it.
- Run catalog, transfer, generated-header and native RF tests after integration.

A copied snapshot of the active maintenance sources, with this change applied
and its header regenerated **only in the copy**, passes the RF host tests while
retaining the six original hardware rules and no new write qualification. No
radio was accessed and no source updater executable was run during the offline audit.
