# Third-party notices

ApexTX is derived from EdgeTX 2.12.4. EdgeTX is licensed under GNU GPL
version 2, so the inherited source and the project's own changes are
distributed under GNU GPL version 2 as described in `LICENSE` and in the
notices retained in individual files.

The embedded Barlow and Barlow Condensed fonts are distributed under the SIL
Open Font License 1.1. Their license texts and source metadata are in
`radio/src/fonts/Barlow/`.

The embedded Roboto font is distributed under the Apache License 2.0. Its
license text is in `radio/src/fonts/Roboto/LICENSE.txt`.

Small generated interface-font sources can combine Roboto, Barlow, EdgeTX icon
glyphs, and Font Awesome glyphs. Their SPDX expressions record every applicable
source license; the generation scripts retain those expressions on rebuild.

Additional inherited libraries retain their upstream notices and licenses in
their respective source directories. A release package includes the licenses
needed by the NB4 firmware and storage resources.

The separately published English and Spanish ApexTX audio packages are derived
from `EdgeTX/edgetx-sdcard-sounds`, licensed under GNU GPL version 2. The exact
upstream URL, tag, commit, checksums, and conversion format are recorded in each
package's `APEXTX-AUDIO.json`; the upstream license text is included alongside
the sounds. ApexTX replaces only the welcome prompt in each package with its
project-branded English or Spanish prompt.
