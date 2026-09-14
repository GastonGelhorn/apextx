# Changelog

All notable ApexTX changes are recorded here. The project follows
[Semantic Versioning](https://semver.org/) while it is developed independently
on top of a separately identified EdgeTX base release.

## [Unreleased]

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

[Unreleased]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.1...HEAD
[0.1.0-alpha.1]: https://github.com/GastonGelhorn/apextx/releases/tag/v0.1.0-alpha.1
