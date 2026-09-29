# Changelog

All notable ApexTX changes are recorded here. The project follows
[Semantic Versioning](https://semver.org/) while it is developed independently
on top of a separately identified EdgeTX base release.

## [Unreleased]

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

[Unreleased]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.3...HEAD
[0.1.0-alpha.3]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.1...v0.1.0-alpha.3
[0.1.0-alpha.1]: https://github.com/GastonGelhorn/apextx/releases/tag/v0.1.0-alpha.1
