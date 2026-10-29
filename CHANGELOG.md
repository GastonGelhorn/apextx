# Changelog

All notable ApexTX changes are recorded here. The project follows
[Semantic Versioning](https://semver.org/) while it is developed independently
on top of a separately identified EdgeTX base release.

## [Unreleased]

- ApexTX Flasher 1.4.0-alpha.4 (build 14004): **Check for updates** opens the
  update dialog immediately and shows the check in progress, then the new
  version, "You're up to date" or a failure with **Try again**. The footer
  sentence about firmware never leaving the computer was removed.
- ApexTX Flasher 1.4.0-alpha.3 (build 14003): the desktop app now checks for
  its own updates at every launch and presents a new version in a dialog with
  the release notes, **Update and restart**, **Remind me later** and **Skip
  this version**, plus a link to the GitHub release. **Check for updates** moved
  to the top bar next to the language switcher and turns into an **Update
  available** badge when a version is waiting; the collapsible "App updates"
  panel is gone. The "Install in my user folder" option was removed: when the
  app cannot replace itself in place (disk image, Downloads or a read-only
  folder) it installs the update in the user folder automatically and reports
  the new location.

## [0.1.0-alpha.8] - 2026-10-29

- Refreshed the ApexTX Flasher desktop interface: a calmer layout that
  follows the system light or dark appearance, consistent buttons and
  collapsible sections, and wording written for radio owners rather than
  developers in English, Spanish, German and Simplified Chinese. Every flow
  is unchanged: one-click update from the radio's ApexTX Flasher USB mode,
  update and recovery, the Components wizard, voice packs and app updates.
- Replaced the desktop app icon with the ApexTX mark on a dark tile so it is
  visible in the Dock and taskbar on light and dark desktops. The icon is
  rendered by `tools/nb4-flasher-icon.py` and packaged on both platforms.
- Published ApexTX Flasher 1.4.0-alpha.2 (build 14002) as the first signed
  desktop release for the app's own update check.

## [0.1.0-alpha.7] - 2026-10-29

- Added RF module and receiver firmware updates from the radio. Verified
  images from the official NB4 2.0.116 and NB4+ 1.0.31 packages ship in a
  merged catalog for both radios (RF module, FGr4 V2, FGr4S V2, FGr8B,
  FGr4B, FGr12B, Mini-Z and INr4-GYB); the radio installs an image only when
  the connected component reports its product ID and the file hashes to the
  catalog, as the stock firmware does. After binding an older receiver a
  notice offers the update; System > Update shows progress and offers
  Recover after an interrupted write. Identity words are shown as
  diagnostics.
- Added **ApexTX Flasher** to the radio's USB menu. From that mode the
  desktop flasher stores the RF catalog on the card, restarts the radio into
  update mode and flashes the firmware in one confirmed step; it never writes
  an RF component itself.
- Added a shared RF transfer/recovery engine with checked external-storage
  staging and persistent unit-bound operation journals; transfer and recovery
  of the module and FGr4 V2 / FGr4S V2 images were exercised on the bench.
- Added an SRAM-only, read-only internal-flash/NOR backup agent and host-side
  two-pass verification, exercised on an original NB4. Added a separate
  sector-verified NOR restoration agent and UID-checked internal-firmware
  restoration. Physical restoration qualification remains pending.
- Added the experimental Components wizard and offline Linux CLI with a
  persistent dependency plan, exact-unit recovery backups, bootloader checks,
  reconnect verification and separate preparation/installation actions.
  Interrupted work survives app restarts and blocks desktop self-update.
- Added a unit-bound Maintenance-to-Storage handover, verified resource
  installation, resumable audio rollback and voice-pack receipts. Kept
  installed audio unless explicitly changed and preserved personal files.
- Added a cross-platform, read-only RF package preparer for reviewed
  installation packages that verifies pinned hashes and CRCs, rejects
  malformed bundles and can merge verified source bundles into one catalog.
- Added a component page under System > Update, a separate read-only USB
  inventory connection shared by the desktop app and Linux CLI, and bounded
  version checks after stable receiver connections without a visible editor.
- Added on-demand RF module version, receiver firmware and receiver model
  readouts. Diagnostic queries never wait in the live-channel transaction
  queue, have bounded retries, discard stale or malformed results, and are
  translated into all four NB4 interface languages.
- Corrected module and receiver application-version decoding from live
  measurements.
- Made audio installation resumable with a verified full-filesystem backup,
  preserved personal recordings and omitted assignable tracks, per-file
  integrity checks and settings-save headroom. Added Simplified Chinese to the
  desktop flasher and kept audio/text languages independent.
- Fixed false UI-stall recovery when waiting on a responsive startup dialog.
  Added a confirmed Restart normally action that clears the retained health
  fault while preserving cars and calibration.
- Corrected telemetry timer teardown and made USB control-state recovery bounded
  without repeating a firmware-install command.

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

[Unreleased]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.7...HEAD
[0.1.0-alpha.7]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.6...v0.1.0-alpha.7
[0.1.0-alpha.6]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.5...v0.1.0-alpha.6
[0.1.0-alpha.5]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.4...v0.1.0-alpha.5
[0.1.0-alpha.4]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.3...v0.1.0-alpha.4
[0.1.0-alpha.3]: https://github.com/GastonGelhorn/apextx/compare/v0.1.0-alpha.1...v0.1.0-alpha.3
[0.1.0-alpha.1]: https://github.com/GastonGelhorn/apextx/releases/tag/v0.1.0-alpha.1
