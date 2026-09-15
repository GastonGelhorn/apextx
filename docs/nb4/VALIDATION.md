# Validation

Run these checks before publishing a firmware revision:

```sh
cmake --build build/nb4-native --target gtests-radio -j8
./build/nb4-native/gtests-radio
python3 -m unittest discover -s tools/tests -p 'test_nb4_*.py'
cmake --build build/nb4-device --target firmware-size -j8
tools/nb4-generate-qualified-profile.py --check docs/nb4/rf/qualification.json
git diff --check
```

On the radio, verify:

- Steering and throttle remain responsive on the home and curve pages.
- The receiver reconnects after both devices are power-cycled.
- Receiver voltage and link quality expire cleanly when the receiver is off.
- Bind completes and survives a radio restart.
- Steering travel, reverse, center, throttle travel, brake, and failsafe affect
  the intended side and channel.
- Serial and storage USB modes connect and disconnect without freezing the UI.
- Race timing, lap input, assignments, audio, storage, and emergency shutdown
  work with and without an attached USB cable.

For the menu/Home revision, also verify:

- Every route in English and Spanish, portrait and landscape, using touch and
  grip-key navigation; Back restores the previous list position and focus.
- Home and every additional screen open their own editor regardless of the
  visible screen. Replace/remove widgets, select another layout, and restore
  Racing after confirmation without changing servo or other screen settings.
- Restart and switch cars: Home/widgets remain model-specific, while appearance,
  brightness, lights and quick access remain radio-wide.
- An old model is backed up before Home migration. Removing writable storage
  defers migration. Unknown/newer data does not get silently overwritten.
- Empty/invalid shortcut slots, duplicate template names, personal-template
  deletion, overwrite confirmation and storage failures are handled visibly.
- Shared curves identify affected inputs/mixes; the curve library is not a
  standalone menu or shortcut.
- Consecutive races, including zero laps, open their own saved result. Pending
  and failed saves never open a previous race; retry preserves the result.
- An incompatible model cannot edit layouts/top bar, but brightness, appearance,
  lights, update, help and recovery remain reachable.
- Car details has only name/labels; Startup checks descriptions remain below their
  controls, including long Spanish labels. Vehicle presets require confirmation
  and preserve race/channel/screen settings. Race setup does not open presets.
- Quick access keeps every canonical Menu entry and category visible. Adding or
  removing an entry preserves the Menu grid's focus and scroll. Old axis-tab
  shortcuts still migrate without duplicates; existing Receiver/USB IDs survive.
- Editor-header ? opens contextual help; navigation grids omit it. Both Got it and physical Back return to the
  same editor. Opening a setting from the help index does not leave help in the
  Back trail. Decorative icons and dialog-backdrop taps do not navigate back.

The automated menu checks are in `Nb4Routes`, `Nb4Ux`, `Nb4RacingUi`,
`Nb4Compatibility` and `Nb4History`. See [MENUS.md](MENUS.md) for the storage
versions and migration/rollback files. Hardware checks remain required before
release; simulator tests cannot qualify a transmitter or receiver.

## Display, touch and rendering revision — 2026-09-15

- Native suite: **360/360 passed**, 48 suites. New regressions cover one shared
  car-state snapshot per UI frame, unfiltered latency stalls and touch
  IRQ-to-present timing; the complete existing navigation, storage, history,
  compatibility and rendering suite remains green.
- Python NB4 tooling: **53/53 passed**. The checked RF profile remains
  development/NB4-original/USART6/addressless SLIP; this revision does not
  change RF timing or qualification.
- Original-NB4 ARM build and manifest validation pass. Application:
  **1,645,160 / 1,966,048 bytes (83.68%)**, leaving **320,888 bytes
  (313.4 KiB)**. Internal `.bss` is 66,144 bytes and SDRAM allocation is
  3,018,752 bytes. Padded image: 2,097,152 bytes.
- Image SHA-256:
  `44f86da164e5fc08380a4a3d24eea2af788f98405beefa3244b5bba6d078d7a9`.
- Touch accepts the controller's initial press event, checks every I2C result,
  retains stable coordinates on faults and polls at 20 ms. Diagnostics expose
  touch-to-present time and retain the raw maximum behind filtered control
  latency.
- Backlight level 100 reaches a true 100% duty cycle at the unchanged 10 kHz
  drive. Framebuffer synchronisation copies exact dirty row extents, and the
  dashboard builds its complete car state once per UI frame.
- Menu and quick-access grids adapt to the current width, use route-specific
  icons, and branded modal headers identify the open view. The high-contrast
  `ApexTX Sun` palette remains the outdoor option; no duplicate palette was
  introduced.
- The release resource set now includes a 16 kHz, 16-bit mono English
  `hello.wav` saying “Welcome to ApexTX”; the TTS source mapping and package
  manifest use the same prompt, and the resource-format regression passes.
- The host updater found no connected **ApexTX NB4 Update** interface at the
  final check, so this image was not installed. Confirm the USB cable/update
  screen, then verify touch, maximum brightness, orientation changes and
  System > Diagnostics on the transmitter after installation.

## Earlier stable Menu revision — 2026-09-15

- Native suite: **358/358 passed**, 48 suites. Includes category order and stable
  route IDs, permanent Menu entries, EN/ES and both orientations, scope strips
  across two cars, navigation-only button choices, timer-list Back, confirmed
  session resets, manual-backup/reset separation and per-car variable enable.
  Menu/Quick access/category grids have no ? button, including Advanced opened
  through a route; editor help and System > Help remain available.
- Tile regression checks first-frame focused contrast (at least 4.5:1), label
  bounds and actual two-column submenu placement. Updated native captures are
  in [GALLERY.md](GALLERY.md). Existing history/layout/storage/compatibility and
  physical-Back tests remain green.
- Python NB4 tooling: **52/52 passed**. RF profile validation passes and remains
  development/NB4-original/USART6/addressless SLIP; no new RF qualification.
- NB4 firmware build passes: **1,642,908 / 1,966,048 bytes (83.56%)**;
  **323,140 bytes (315.6 KiB)** free. Padded image: 2,097,152 bytes.
- Manifest, target vectors and CRC validated using the updater package checker.
  Image SHA-256: `692265f9ff36b84d0a40a125263cf330880f070bb03a814e8c571fe872c4c494`.
- The existing YAML edits were preserved. Shortcut storage remains version 2;
  no Home/model schema migration was added by this revision.
- This image has **not** been installed on the radio in this revision. Touch,
  physical controls, restart/car switching and RF still require a hardware pass.

## Earlier Menu/Home implementation and polish check — 2026-09-15

- `build/verify/native/gtests-radio`: **352/352 passed**, including both
  orientations and EN/ES route rendering, widget-zone actions, transparent
  editor visibility and physical Back through the simulator key driver.
  The polish regressions cover quick-access migration/live Settings filtering,
  one editor per axis, contextual-help Back, decorative-icon/backdrop hit tests,
  wrapped Safety labels and preset confirmation/preservation.
- NB4 Python tooling tests: **52/52 passed**.
- `cmake --build build/verify --target firmware-size -j8`: successful original
  NB4 build. Application: **1,630,896 / 1,966,048 bytes (82.95%)**, leaving
  **335,152 bytes (327.3 KiB)**. The complete image is padded to 2,097,152 bytes.
- Application manifest, target vectors and CRC: valid. RF profile check:
  `development`, original NB4, USART6/addressless SLIP. This remains a
  laboratory image, not a new RF qualification.
- Updated [gallery](GALLERY.md) reviewed against the test-generated framebuffers.
  `git diff --check`: clean.

The preceding Menu/Home build (1,625,640-byte application) was installed through
ApexTX Update with successful readback, and the user confirmed it reached Home.
The subsequent polish build (1,630,896-byte application) was also installed
through ApexTX Update after the user placed the radio in update mode. Transfer
verification and firmware verification completed successfully (100%); reset
was requested and the radio left the update interface. Its SHA-256 is
`59d39f59da72225ff6053fffc8816fe6564b72e91798fbca980da12125e6c42d`.
User confirmation of Home and the remaining real-radio checks above are still
pending; simulator checks and a successful update are not hardware acceptance.

The AFHDS3 settings this firmware ships carry no vendor specification or
approval, which is why the checked-in RF descriptor is still marked
`development`. `rf/BENCH_ACCEPTANCE.md` lists what to check on a real radio
before tagging a version. Nothing in the build enforces it.
