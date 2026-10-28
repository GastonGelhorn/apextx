# Changelog

All notable ApexTX changes are recorded here. The project follows
[Semantic Versioning](https://semver.org/) while it is developed independently
on top of a separately identified EdgeTX base release.

## [Unreleased]

- Began integrated maintenance with a cross-platform, read-only RF package
  preparer for reviewed NB4/NB4+ source packages and the initial FGr4 V2/FGr4S
  V2 targets. It verifies pinned hashes and CRCs, rejects malformed bundles,
  and cannot enter a USB update path. RF writes and automatic post-bind
  notifications remain disabled pending protocol and hardware qualification.

- Added on-demand RF module version, receiver firmware and receiver model
  readouts. Diagnostic queries never wait in the live-channel transaction
  queue, have bounded retries, discard stale or malformed results, and are
  translated into all four NB4 interface languages. This does not enable RF
  module or receiver firmware updates.

## [0.1.0-alpha.6] - 2026-10-27

- Added complete Simplified Chinese interface coverage, compact CJK fonts and
  verified portrait/landscape captures for both display geometries.
- Published four independently selectable audio packs: English, Spanish,
  German and Mandarin Chinese, including the ApexTX welcome prompt. Interface
  and voice languages remain independent; install only one audio pack.
- Added guided bootloader upgrades through ROM DFU when a normal update needs
  a newer bootloader, retaining mandatory backup and image verification.
- Corrected battery indicators and live charging-status LED updates.
- Added per-model update metadata for compatible-release selection and made
  publication require both radio packages and all four audio packs.
- Retained experimental NB4+ status: LED flicker or incorrect colours remain
  under investigation in issues #19 and #20.

## [0.1.0-alpha.5] - 2026-10-24

- Updated the maintained EdgeTX base from 2.12.4 to 2.12.5, retaining the
  ApexTX NB4/NB4+ interface, AFHDS3 support and guarded update workflow.
- Refined the touch interface with consistent navigation, aligned icons,
  larger controls, readable input labels and improved Appearance dropdowns.
- Fixed USB host detection and session teardown while using the NB4 base pack.
- Added shared Windows and macOS flashers that detect the radio and installed
  version, find compatible releases, and download, verify and install updates.
  Local firmware loading remains available; newer releases can explicitly
  replace an older development build.
- Added complete firmware/bootloader recovery with mandatory backup, full-image
  verification, automatic startup and clear progress during the black-screen
  ROM installation. Fixed complete USB version reporting and transient status
  stalls during normal updates.
- Published direct NB4/NB4+ firmware binaries and Windows, Mac Apple Silicon
  and Mac Intel flasher downloads with checksums and build attestations.

## [0.1.0-alpha.4] - 2026-10-20

- Added separately named original-NB4 and experimental-NB4+ firmware packages
  to each release, with an explicit model table and cross-flash warning.
- Published the hardened Windows installer/recovery app with mandatory backup,
  exact model and DFU-layout checks, full-image read-back, and same-device
  verification immediately before writing.
- Corrected mismatched memory deallocation, a FlySky trace-buffer boundary,
  wrap-safe PWM detection timing, integer-size calculations and loop counters
  reported by CodeQL.
- Removed an unused legacy Python 2 CGI build endpoint inherited from OpenTX.
- Fixed inverted steering-trim direction and simplified adding a new car by
  bringing car creation, activation and binding into the same workflow.

## [0.1.0-alpha.3] - 2026-09-29

- Unified NB4 headers: every destination title now stays in the top bar with
  a fixed, legible font and the same icon as its Menu tile; long titles clip
  at the trailing edge. Navigation modals retain the ApexTX logo and preserve
  room for their vertically centred section title.
- Moved user voice-track assignments to a focused, radio-wide **Sound &
  alerts > Voice assignments** editor with trigger, track preview, repeat and
  active state. Changing cars no longer changes these assignments.
- Added separate, reproducible English and Spanish NB4 audio packages. Each
  contains every system prompt, the ApexTX welcome prompt, and as many useful
  assignable sounds as fit safely; an installer verifies, backs up, replaces,
  and verifies the selected single-language pack.
- Added a control-latency figure to System > Diagnostics: the time from
  sampling the wheel and trigger to handing the frame that carries them to the
  module, as minimum, average and maximum microseconds. docs/nb4/LATENCY.md
  says what it covers and what it leaves to an oscilloscope.

## [0.1.0-alpha.1] - 2026-09-15

First public build for the original FlySky Noble NB4.

- Car-focused interface in portrait and landscape, with live steering,
  throttle, brake and trim, race timing, telemetry and audio.
- Configurable physical-control assignments and navigation actions, and one
  route to each setting.
- AFHDS3 receiver support over the NB4's internal module: persistent binding,
  receiver voltage, link quality, and link-loss handling.
- Ten car widgets for user-built home screens.
- Boot warnings that explain what happened and open the setting that fixes it.
- Update mode: install a new build over USB from the radio's own menu, with a
  desktop updater, without opening the case.
- Expanded external-NOR filesystem, controlled USB transitions, and verified
  MCU backup and single-write DFU flashing tools.
- English and Spanish interface, switchable on the radio.
- Build, validation, packaging, compatibility and installation documentation.

[Unreleased]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.6...HEAD
[0.1.0-alpha.6]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.5...v0.1.0-alpha.6
[0.1.0-alpha.5]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.4...v0.1.0-alpha.5
[0.1.0-alpha.4]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.3...v0.1.0-alpha.4
[0.1.0-alpha.3]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.1...v0.1.0-alpha.3
[0.1.0-alpha.1]: https://github.com/GastonGelhorn/apextx/releases/tag/v0.1.0-alpha.1
