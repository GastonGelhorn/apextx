# Third-party notices

ApexTX is derived from EdgeTX 2.12.5. EdgeTX is licensed under GNU GPL
version 2, so the inherited source and the project's own changes are
distributed under GNU GPL version 2 as described in `LICENSE` and in the
notices retained in individual files.

The embedded Barlow and Barlow Condensed fonts are distributed under the SIL
Open Font License 1.1. Their license texts and source metadata are in
`radio/src/fonts/Barlow/`.

The embedded Roboto font is distributed under the Apache License 2.0. Its
license text is in `radio/src/fonts/Roboto/LICENSE.txt`.

The embedded Simplified Chinese font is derived from Noto Sans CJK SC,
distributed under the SIL Open Font License 1.1. Its license text is in
`radio/src/fonts/Noto/LICENSE_OFL.txt` and accompanies the firmware packages.

Small generated interface-font sources can combine Roboto, Barlow, EdgeTX icon
glyphs, and Font Awesome glyphs. Their SPDX expressions record every applicable
source license; the generation scripts retain those expressions on rebuild.

Additional inherited libraries retain their upstream notices and licenses in
their respective source directories. A release package includes the licenses
needed by the NB4 firmware and storage resources.

The desktop ApexTX Flasher packages Python, PyUSB, pywebview and PyInstaller's
bootloader under their upstream terms. The platform package includes libusb
(LGPL-2.1-or-later) and dfu-util (GPL-2.0-or-later). Their source and license
information are available from https://libusb.info/ and
https://dfu-util.sourceforge.net/. The macOS package includes both libraries'
COPYING files under its `licenses/` resources. These desktop packages use the
same ApexTX source and interface on Windows and macOS.
Release downloads use certifi's CA bundle (Mozilla Public License 2.0),
from https://github.com/certifi/python-certifi; its license and CA data are
included in the desktop package.

The separately published English, Spanish, German and Mandarin Chinese ApexTX
audio packages are derived
from `EdgeTX/edgetx-sdcard-sounds`, licensed under GNU GPL version 2. The exact
upstream URL, tag, commit, checksums, and conversion format are recorded in each
package's `APEXTX-AUDIO.json`; the upstream license text is included alongside
the sounds. ApexTX replaces only the welcome prompt in each package with its
project-branded prompt in the corresponding language.
