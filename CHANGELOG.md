# Changelog

All notable ApexTX changes are recorded here. The project follows
[Semantic Versioning](https://semver.org/) while it is developed independently
on top of a separately identified EdgeTX base release.

## [Unreleased]

- Restored automatic exit from STM32 ROM DFU after a verified flash by using
  the NB4-compatible zero-length DfuSe manifestation request.
- Standardized built-in palette names in English and added transparent
  migration from the earlier development names.

## [0.1.0-alpha.1] - unreleased

- Added the car-focused portrait and landscape interface.
- Added live steering, throttle, brake, trim, race timing, telemetry, and audio.
- Added configurable physical-control assignments and navigation actions.
- Recovered and integrated the original NB4 AFHDS3 module route.
- Added persistent receiver binding, receiver voltage, link quality, and link
  loss handling.
- Added the expanded external-NOR filesystem and controlled USB transitions.
- Added verified MCU backup and single-write DFU flashing tools.
- Added build, validation, packaging, compatibility, and installation docs.

[Unreleased]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.1...HEAD
[0.1.0-alpha.1]: https://github.com/GastonGelhorn/apextx/releases/tag/v0.1.0-alpha.1
